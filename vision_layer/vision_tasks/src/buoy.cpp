#include <buoy.h>

Buoy::Buoy(){
	this->clahe_clip_ = 0.15;
	this->clahe_grid_size_ = 3;
	this->clahe_bilateral_iter_ = 2;
	this->balanced_bilateral_iter_ = 4;
	this->denoise_h_ = 10.0;
	this->low_h_ = 0;
	this->high_h_ = 25;
	this->low_s_ = 245;
	this->high_s_ = 255;
	this->low_v_ = 78;
	this->high_v_ = 115;
	this->opening_mat_point_ = 1;
	this->opening_iter_ = 3;
	this->closing_mat_point_ = 1;
	this->closing_iter_ = 0;
	this->camera_frame_ = "auv-iitk";
	this->current_color = 0;
	image_transport::ImageTransport it(nh);
	this->blue_filtered_pub = it.advertise("/buoy_task/blue_filtered", 1);
	this->thresholded_pub = it.advertise("/buoy_task/thresholded", 1);
	this->marked_pub = it.advertise("/buoy_task/marked", 1);
	this->coordinates_pub = nh.advertise<geometry_msgs::PointStamped>("/buoy_task/buoy_coordinates", 1000);
	this->x_coordinates_pub = nh.advertise<std_msgs::Float32>("/anahita/x_coordinate", 1000);
	this->y_coordinates_pub = nh.advertise<std_msgs::Float32>("/anahita/y_coordinate", 1000);
	this->z_coordinates_pub = nh.advertise<std_msgs::Float32>("/anahita/z_coordinate", 1000);
	this->detection_pub = nh.advertise<std_msgs::Bool>("/detected", 1000);
	this->image_raw_sub = it.subscribe("/anahita/front_camera/image_raw", 1, &Buoy::imageCallback, this);
}

void Buoy::switchColor(int color)
{
	if(color > 2)
		std::cerr << "Changing to wrong buoy color, use 0-2 for the the different colors" << std::endl;
	else
	{
		current_color = color;		
		Buoy::low_h_ = Buoy::data_low_h[current_color];
		Buoy::high_h_ = Buoy::data_high_h[current_color];
		Buoy::low_s_ = Buoy::data_low_s[current_color];
		Buoy::high_s_ = Buoy::data_high_s[current_color];
		Buoy::low_v_ = Buoy::data_low_v[current_color];
		Buoy::high_v_ = Buoy::data_high_v[current_color];
	}
	std::cout << "Colour changed successfully" << std::endl;
}

void Buoy::callback(vision_tasks::buoyRangeConfig &config, double level)
{
	if(Buoy::current_color != config.color)
	{
		config.color = current_color;
		config.low_h = Buoy::data_low_h[current_color];
		config.high_h = Buoy::data_high_h[current_color];
		config.low_s = Buoy::data_low_s[current_color];
		config.high_s = Buoy::data_high_s[current_color];
		config.low_v = Buoy::data_low_v[current_color];
		config.high_v = Buoy::data_high_v[current_color];
	}
	Buoy::clahe_clip_ = config.clahe_clip;
	Buoy::clahe_grid_size_ = config.clahe_grid_size;
	Buoy::clahe_bilateral_iter_ = config.clahe_bilateral_iter;
	Buoy::balanced_bilateral_iter_ = config.balanced_bilateral_iter;
	Buoy::denoise_h_ = config.denoise_h;
	Buoy::low_h_ = config.low_h;
	Buoy::high_h_ = config.high_h;
	Buoy::low_s_ = config.low_s;
	Buoy::high_s_ = config.high_s;
	Buoy::low_v_ = config.low_v;
	Buoy::high_v_ = config.high_v;
	Buoy::opening_mat_point_ = config.opening_mat_point;
	Buoy::opening_iter_ = config.opening_iter;
	Buoy::closing_mat_point_ = config.closing_mat_point;
	Buoy::closing_iter_ = config.closing_iter;
};

void Buoy::imageCallback(const sensor_msgs::Image::ConstPtr &msg)
{
	try
	{
		image_ = cv_bridge::toCvCopy(msg, sensor_msgs::image_encodings::BGR8)->image;
	}
	catch (cv_bridge::Exception &e)
	{
		ROS_ERROR("cv_bridge exception: %s", e.what());
	}
	catch (cv::Exception &e)
	{
		ROS_ERROR("cv exception: %s", e.what());
	}
};

void Buoy::TaskHandling(bool status){
	if(status)
	{
		spin_thread = new boost::thread(boost::bind(&Buoy::spinThread, this)); 
	}
	else 
	{
        spin_thread->join();
	}
	std::cout << "Task Handling function over" << std::endl;	
}


void Buoy::spinThread(){

	dynamic_reconfigure::Server<vision_tasks::buoyRangeConfig> server;
	dynamic_reconfigure::Server<vision_tasks::buoyRangeConfig>::CallbackType f;
	f = boost::bind(&Buoy::callback, this, _1, _2);
	server.setCallback(f);

	cv::Scalar buoy_center_color(255, 255, 255);
	cv::Scalar image_center_color(0, 0, 0);
	cv::Scalar enclosing_circle_color(149, 255, 23);
	cv::Scalar contour_color(255, 0, 0);

	cv::Mat blue_filtered;
	cv::Mat image_hsv;
	cv::Mat image_thresholded;
	cv::Mat image_marked;
	std::vector<std::vector<cv::Point> > contours;
	cv::Rect bounding_rectangle;
	std::vector<cv::Point2f> center(1);
	std::vector<float> radius(1);
	geometry_msgs::PointStamped buoy_point_message;
	buoy_point_message.header.frame_id = camera_frame_.c_str();
	cv::RotatedRect min_ellipse;
	ros::Rate loop_rate(10);

	std_msgs::Bool detection_bool;

	while (1)
	{
		if (!image_.empty())
		{
			image_.copyTo(image_marked);
			// blue_filtered = vision_commons::Filter::blue_filter(image_, clahe_clip_, clahe_grid_size_, clahe_bilateral_iter_, balanced_bilateral_iter_, denoise_h_);
			blue_filtered = image_;
			if (high_h_ > low_h_ && high_s_ > low_s_ && high_v_ > low_v_)
			{	
				cv::cvtColor(blue_filtered, image_hsv, CV_BGR2HSV);
				image_thresholded = vision_commons::Threshold::threshold(image_hsv, low_h_, high_h_, low_s_, high_s_, low_v_, high_v_);
				image_thresholded = vision_commons::Morph::open(image_thresholded, 2 * opening_mat_point_ + 1, opening_mat_point_, opening_mat_point_, opening_iter_);
				image_thresholded = vision_commons::Morph::close(image_thresholded, 2 * closing_mat_point_ + 1, closing_mat_point_, closing_mat_point_, closing_iter_);
				contours = vision_commons::Contour::getBestX(image_thresholded, 2);
				if (contours.size() != 0)
				{
					int index = 0;
					bounding_rectangle = cv::boundingRect(cv::Mat(contours[0]));
					if (contours.size() >= 2)
					{
						cv::Rect bounding_rectangle2 = cv::boundingRect(cv::Mat(contours[1]));
						if ((bounding_rectangle2.br().y + bounding_rectangle2.tl().y) > (bounding_rectangle.br().y + bounding_rectangle.tl().y))
						{
							index = 1;
							bounding_rectangle = bounding_rectangle2;
						}
					}
					cv::minEnclosingCircle(contours[index], center[0], radius[0]);
					buoy_point_message.header.stamp = ros::Time();
					x_coordinate.data = pow(radius[0] / 7526.5, -.92678);
 					y_coordinate.data = center[0].x - ((float)image_.size().width) / 2;
					z_coordinate.data = ((float)image_.size().height) / 2 - center[0].y;
					ROS_INFO("Buoy Location (x, y, z) = (%.2f, %.2f, %.2f)", x_coordinate.data, y_coordinate.data, z_coordinate.data);

					if(contourArea(contours[index]) > 20 && abs(y_coordinate.data) < 220 && abs(z_coordinate.data) < 300) 
						detection_bool.data = true;
					else
						detection_bool.data = false;						
					detection_pub.publish(detection_bool);

					cv::circle(image_marked, cv::Point((bounding_rectangle.br().x + bounding_rectangle.tl().x) / 2, (bounding_rectangle.br().y + bounding_rectangle.tl().y) / 2), 1, buoy_center_color, 8, 0);
					cv::circle(image_marked, cv::Point(image_.size().width / 2, image_.size().height / 2), 1, image_center_color, 8, 0);
					cv::circle(image_marked, center[0], (int)radius[0], enclosing_circle_color, 2, 8, 0);
					for (int i = 0; i < contours.size(); i++)
					{
						cv::drawContours(image_marked, contours, i, contour_color, 1);
					}
				}
				else
				{
					ROS_INFO("Object not being detected");
					detection_bool.data = false;
					detection_pub.publish(detection_bool);
				}	
			}
			blue_filtered_pub.publish(cv_bridge::CvImage(buoy_point_message.header, "bgr8", blue_filtered).toImageMsg());
			thresholded_pub.publish(cv_bridge::CvImage(buoy_point_message.header, "mono8", image_thresholded).toImageMsg());
//			coordinates_pub.publish(buoy_point_message);

			x_coordinates_pub.publish(x_coordinate);
			y_coordinates_pub.publish(y_coordinate);
			z_coordinates_pub.publish(z_coordinate);
			marked_pub.publish(cv_bridge::CvImage(buoy_point_message.header, "bgr8", image_marked).toImageMsg());
		}
		else
		{
			ROS_INFO("Image empty");
		}
		loop_rate.sleep();
		ros::spinOnce();
	}
}
