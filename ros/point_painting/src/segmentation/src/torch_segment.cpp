#include <ros/ros.h>
#include <torch_segment/torch_segment.h>


TorchEngine::TorchEngine(const ros::NodeHandle& nh, const ros::NodeHandle& nh_private) :
    nh_(nh),
    nh_private_(nh_private),
    it(ros::NodeHandle())
{
    nh_private_.getParam("serialized_engine", torch_engine_);
    nh_private_.getParam("mean", mean_);
    nh_private_.getParam("std", std_);
    nh_private_.getParam("use_cuda", use_cuda_);
    nh_private_.getParam("camera_topic", camera_topic_);
    
    rgb_mean = cv::Scalar(mean_[0], mean_[1], mean_[2]);
    rgb_std = cv::Scalar(std_[0] ,std_[1], std_[2]);
    
    inference_pub_ = it.advertise("d435/color/inference_image", 1);
    rgb_sub_ = it.subscribe(camera_topic_, 100, &TorchEngine::image_callback, this);
}

void TorchEngine::image_callback(const sensor_msgs::ImageConstPtr &image) 
{   
    // save header
    image_header = image->header;
    rgb_image = cv_bridge::toCvCopy(image, sensor_msgs::image_encodings::BGR8)->image;
    run_inference(); 
}

void TorchEngine::run_inference() 
{
    // ROS_INFO_STREAM(torch_engine_);
    torch::jit::script::Module module;
    try {
        // Deserialize the ScriptModule from a file using torch::jit::load().
        module = torch::jit::load(torch_engine_);
    } catch (const c10::Error &e) {
        ROS_ERROR("Error Loading Model\n");
        return;
    }

    // cv::Mat input_image;
    // ROS_INFO("hi");
    cv::cvtColor(rgb_image, rgb_image, cv::COLOR_BGR2RGB);
    rgb_image.convertTo(rgb_image, CV_32FC3);
    cv::resize(rgb_image, rgb_image, cv::Size(512, 256));
    // Normalize 
    cv::divide(rgb_image, cv::Scalar(255, 255, 255), rgb_image);
    cv::subtract(rgb_image, rgb_mean, rgb_image);
    cv::divide(rgb_image, rgb_std, rgb_image);

    //conversion of cv::Mat to at::Tensor
    auto input = ((torch::from_blob(rgb_image.data, {1, 256, 512, 3}).permute({0, 3, 1, 2})));
    // input = input.reshape({input.size(0), input.size(3), input.size(1), input.size(2)});
    // Execution
    /* TODO add cuda device transfer */ 
    at::Tensor output = module.forward({input}).toTensor();
    // ROS_INFO("JESUS");
    // ROS_INFO_STREAM(output.size(0) << " " << output.size(1) << " " << output.size(2));
    output = output.argmax(1); 
    output = output.permute({1, 2, 0}).to(torch::kU8);
    cv::Mat result_image(256, 512, CV_8UC1);
    cv::Mat inference_image(256, 512, CV_8UC3);
    std::memcpy((void *) result_image.data, output.data_ptr(), sizeof(torch::kU8) * output.numel());
    
    // colormapping
    result_image.convertTo(inference_image, CV_8UC3, (255.0/12.0));
    cv::applyColorMap(inference_image, inference_image, cv::COLORMAP_RAINBOW);
    // cv::imshow("infer", inference_image);
    // cv::waitKey(0);
    cv::resize(inference_image, inference_image, cv::Size(640, 480), cv::INTER_NEAREST);
    sensor_msgs::ImagePtr image = cv_bridge::CvImage(image_header, "bgr8", inference_image).toImageMsg();
    inference_pub_.publish(image);
}