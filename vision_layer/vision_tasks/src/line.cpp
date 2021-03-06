#include <line.h>

Line::Line(){
	this->low_h_ = 31;
	this->high_h_ = 47;
	this->low_s_ = 0;
	this->high_s_ = 255;
	this->low_v_ = 0;
	this->high_v_ = 255;
	this->opening_mat_point_ = 1;
	this->opening_iter_ = 0;
	this->closing_mat_point_ = 2;
	this->closing_iter_ = 1;
	this->camera_frame_ = "auv-iitk";
	image_transport::ImageTransport it(nh);
	this->thresholded_pub = it.advertise("/line_task/thresholded", 1);
	this->marked_pub = it.advertise("/line_task/marked", 1);
	this->x_coordinates_pub = nh.advertise<std_msgs::Float32>("/anahita/x_coordinate", 1);
	this->y_coordinates_pub = nh.advertise<std_msgs::Float32>("/anahita/y_coordinate", 1);
	this->z_coordinates_pub = nh.advertise<std_msgs::Float32>("/anahita/z_coordinate", 1);
	this->coordinates_pub = nh.advertise<geometry_msgs::Pose2D>("/line_task/line_coordinates", 1000);
	this->image_raw_sub = it.subscribe("/anahita/bottom_camera/image_raw", 1, &Line::imageCallback, this);
}

void Line::callback(vision_tasks::lineRangeConfig &config, double level)
{
	Line::low_h_ = config.low_h;
	Line::low_s_ = config.low_s;
	Line::low_v_ = config.low_v;
	Line::high_h_ = config.high_h;
	Line::high_s_ = config.high_s;
	Line::high_v_ = config.high_v;
	Line::opening_mat_point_ = config.opening_mat_point;
	Line::opening_iter_ = config.opening_iter;
	Line::closing_mat_point_ = config.closing_mat_point;
	Line::closing_iter_ = config.closing_iter;
};

double Line::computeMean(std::vector<double> &newAngles)
{
	double sum = 0;
	for(int  i =0; i < newAngles.size(); i++)
		sum += newAngles[i];
	return (double) sum/newAngles.size();
}

void Line::imageCallback(const sensor_msgs::Image::ConstPtr &msg)
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
}

void Line::TaskHandling()
{  
	dynamic_reconfigure::Server<vision_tasks::lineRangeConfig> server;
	dynamic_reconfigure::Server<vision_tasks::lineRangeConfig>::CallbackType f;
	f = boost::bind(&Line::callback, this, _1, _2);
	server.setCallback(f);

	cv::Scalar line_center_color(255, 255, 255);
	cv::Scalar image_center_color(0, 0, 0);
	cv::Scalar edge_color(255, 255, 255);
	cv::Scalar hough_line_color(0, 164, 253);
	cv::Scalar contour_color(255, 0, 0);

	cv::Mat image_hsv;
	cv::Mat image_thresholded;
  	std::vector<std::vector<cv::Point> > contours;
	cv::RotatedRect bounding_rectangle;
	std::vector<cv::Vec4i> lines;
	geometry_msgs::Pose2D line_point_message;
	cv::Mat image_marked;

	while (1)
	{
		if (!image_.empty())
		{
			image_.copyTo(image_marked);
			if (high_h_ > low_h_ && high_s_ > low_s_ && high_v_ > low_v_)
			{
				cv::cvtColor(image_, image_hsv, CV_BGR2HSV);
				image_thresholded = vision_commons::Threshold::threshold(image_hsv, low_h_, high_h_, low_s_, high_s_, low_v_, high_v_);
				image_thresholded = vision_commons::Morph::open(image_thresholded, 2 * opening_mat_point_ + 1, opening_mat_point_, opening_mat_point_, opening_iter_);
				image_thresholded = vision_commons::Morph::close(image_thresholded, 2 * closing_mat_point_ + 1, closing_mat_point_, closing_mat_point_, closing_iter_);
				contours = vision_commons::Contour::getBestX(image_thresholded, 1);
				cv::Mat edges(image_thresholded.rows, image_thresholded.cols, CV_8UC1, cv::Scalar::all(0));
				std::vector<double> angles;

				cv::drawContours(edges, contours, 0, edge_color, 1, 8);
				if (contours.size() != 0)
				{
					bounding_rectangle = cv::minAreaRect(cv::Mat(contours[0]));
					cv::HoughLinesP(edges, lines, 1, CV_PI / 180, 60, 70, 10);
					ROS_INFO("HOugh lines number = %d", lines.size());
					for (int i = 0; i < lines.size(); i++)
					{
						if ((lines[i][2] == lines[i][0]) || (lines[i][1] == lines[i][3]))
							continue;
						angles.push_back(atan(static_cast<double>(lines[i][2] - lines[i][0]) / (lines[i][1] - lines[i][3])) * 180.0 / 3.14159);
						cv::line(image_marked, cv::Point(lines[i][0], lines[i][1]), cv::Point(lines[i][2], lines[i][3]), hough_line_color, 1, CV_AA);
					}
					ROS_INFO("Loop OVer");
					x_coordinate.data = (image_.size().height) / 2 - bounding_rectangle.center.y;
					y_coordinate.data = bounding_rectangle.center.x - (image_.size().width) / 2;
					if (angles.size() > 0)
					{
						double angle = computeMean(angles);
						if (angle > 90.0)
							line_point_message.theta = angle - 90.0;
						else
							line_point_message.theta = angle;
					}
					else
						line_point_message.theta = 0.0;
					ROS_INFO("Line (x, y, theta) = (%.2f, %.2f, %.2f)", line_point_message.x, line_point_message.y, line_point_message.theta);
					cv::circle(image_marked, cv::Point(bounding_rectangle.center.x, bounding_rectangle.center.y), 1, line_center_color, 8, 0);
					cv::circle(image_marked, cv::Point(image_.size().width / 2, image_.size().height / 2), 1, image_center_color, 8, 0);
					for (int i = 0; i < contours.size(); i++)
					{
						cv::drawContours(image_marked, contours, i, contour_color, 1, 8);
					}
				}
			}
			x_coordinates_pub.publish(x_coordinate);
			y_coordinates_pub.publish(y_coordinate);
			z_coordinates_pub.publish(z_coordinate);
			thresholded_pub.publish(cv_bridge::CvImage(std_msgs::Header(), "mono8", image_thresholded).toImageMsg());
			coordinates_pub.publish(line_point_message);
			marked_pub.publish(cv_bridge::CvImage(std_msgs::Header(), "bgr8", image_marked).toImageMsg());
		}
		else
			ROS_INFO("Image empty");
		ros::spinOnce();
	}
}
