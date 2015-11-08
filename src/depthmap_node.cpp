// This file is part of REMODE - REgularized MOnocular Depth Estimation.
//
// Copyright (C) 2014 Matia Pizzoli <matia dot pizzoli at gmail dot com>
// Robotics and Perception Group, University of Zurich, Switzerland
// http://rpg.ifi.uzh.ch
//
// REMODE is free software: you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or any later version.
//
// REMODE is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include <ros/ros.h>
#include <opencv2/opencv.hpp>
#include <cv_bridge/cv_bridge.h>
#include <sensor_msgs/image_encodings.h>
#include <vikit/params_helper.h>

#include <rmd/depthmap_node.h>
#include <rmd/se3.cuh>

rmd::DepthmapNode::DepthmapNode()
{
  state_ = rmd::State::TAKE_REFERENCE_FRAME;
}

bool rmd::DepthmapNode::init()
{
  if(!vk::hasParam("remode/cam_width"))
    return false;
  if(!vk::hasParam("remode/cam_height"))
    return false;
  if(!vk::hasParam("remode/cam_fx"))
    return false;
  if(!vk::hasParam("remode/cam_fy"))
    return false;
  if(!vk::hasParam("remode/cam_cx"))
    return false;
  if(!vk::hasParam("remode/cam_cy"))
    return false;

  const size_t cam_width  = vk::getParam<int>("remode/cam_width");
  const size_t cam_height = vk::getParam<int>("remode/cam_height");
  const float  cam_fx     = vk::getParam<float>("remode/cam_fx");
  const float  cam_fy     = vk::getParam<float>("remode/cam_fy");
  const float  cam_cx     = vk::getParam<float>("remode/cam_cx");
  const float  cam_cy     = vk::getParam<float>("remode/cam_cy");

  depthmap_.reset(new rmd::Depthmap(cam_width,
                                    cam_height,
                                    cam_fx,
                                    cam_cx,
                                    cam_fy,
                                    cam_cy));

  if(vk::hasParam("remode/cam_d0") &&
     vk::hasParam("remode/cam_d1") &&
     vk::hasParam("remode/cam_d2") &&
     vk::hasParam("remode/cam_d3") )
  {
    depthmap_->initUndistortionMap(
          vk::getParam<float>("remode/cam_d0"),
          vk::getParam<float>("remode/cam_d1"),
          vk::getParam<float>("remode/cam_d2"),
          vk::getParam<float>("remode/cam_d3"),
          vk::getParam<float>("remode/cam_d4", 0.0f));
  }

  ref_compl_perc_ = vk::getParam<float>("remode/ref_compl_perc", 10.0f);

  return true;
}

void rmd::DepthmapNode::denseInputCallback(
    const svo_msgs::DenseInputConstPtr &dense_input)
{
  if(!depthmap_)
  {
    ROS_ERROR("depthmap not initialized. Call the DepthmapNode::init() method");
    return;
  }

  cv::Mat img_8uC1;
  try
  {
    img_8uC1 = cv_bridge::toCvCopy(
          dense_input->image,
          sensor_msgs::image_encodings::MONO8)->image;
  }
  catch (cv_bridge::Exception& e)
  {
    ROS_ERROR("cv_bridge exception: %s", e.what());
  }
  rmd::SE3<float> T_world_curr(
        dense_input->pose.orientation.w,
        dense_input->pose.orientation.x,
        dense_input->pose.orientation.y,
        dense_input->pose.orientation.z,
        dense_input->pose.position.x,
        dense_input->pose.position.y,
        dense_input->pose.position.z);

  std::cout << "DEPTHMAP NODE: received image "
            << img_8uC1.cols << "x" << img_8uC1.rows
            <<  std::endl;
  std::cout << "T_world_curr:" << std::endl;
  std::cout << T_world_curr << std::endl;

  switch (state_) {
  case rmd::State::TAKE_REFERENCE_FRAME:
  {
    if(depthmap_->setReferenceImage(
         img_8uC1,
         T_world_curr.inv(),
         dense_input->min_depth,
         dense_input->max_depth))
    {
      state_ = State::UPDATE;
    }
    else
    {
      std::cerr << "ERROR: could not set reference image" << std::endl;
    }
    break;
  }
  case rmd::State::UPDATE:
  {
    depthmap_->update(img_8uC1, T_world_curr.inv());
    float perc_conv = depthmap_->getConvergedPercentage();
    std::cout << "INFO: percentage of converged measurements: " << perc_conv << "%" << std::endl;
    if(perc_conv > ref_compl_perc_)
    {
      state_ = rmd::State::TAKE_REFERENCE_FRAME;
#if 1
      cv::Mat curr_depth;
      depthmap_->outputDenoisedDepthmap(curr_depth, 0.4f, 200);
      cv::imshow("curr_depth", rmd::Depthmap::scaleMat(curr_depth));
      cv::waitKey(2);
#endif
    }
    break;
  }
  default:
    break;
  }

}

