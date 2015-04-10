/*
* Copyright (c) 2014, Autonomous Systems Lab
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
* * Redistributions of source code must retain the above copyright
* notice, this list of conditions and the following disclaimer.
* * Redistributions in binary form must reproduce the above copyright
* notice, this list of conditions and the following disclaimer in the
* documentation and/or other materials provided with the distribution.
* * Neither the name of the Autonomous Systems Lab, ETH Zurich nor the
* names of its contributors may be used to endorse or promote products
* derived from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
* ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
* SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
* CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
* OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
*/

#ifndef FEATURE_TRACKER_HPP_
#define FEATURE_TRACKER_HPP_

#include <ros/ros.h>
#include <image_transport/image_transport.h>
#include <cv_bridge/cv_bridge.h>
#include <sensor_msgs/image_encodings.h>
#include <sensor_msgs/Imu.h>
#include "rovio/common_vision2.hpp"

namespace rovio{


class FeatureTrackerNode{
 public:
  ros::NodeHandle nh_;
  ros::Subscriber subImu_;
  ros::Subscriber subImg_;
  static constexpr int nMax_ = 100;
  static constexpr int patchSize_ = 8;
  static constexpr int nLevels_ = 4;
  cv::Mat draw_image_, img_, draw_patches_;
  unsigned int min_feature_count_, max_feature_count_;
  ImagePyramid<nLevels_> pyr_;
  class MultilevelPatchSet<nLevels_,patchSize_,nMax_> mlps_;
  static constexpr int nDetectionBuckets_ = 100;
  static constexpr double scoreDetectionExponent_ = 0.5;
  static constexpr double penaltyDistance_ = 20;
  static constexpr double zeroDistancePenalty_ = nDetectionBuckets_*1.0;
  static constexpr int l1 = 1;
  static constexpr int l2 = 3;
  static constexpr int detectionThreshold = 10;

  FeatureTrackerNode(ros::NodeHandle& nh): nh_(nh){
    static_assert(l2>=l1, "l2 must be larger than l1");
    subImu_ = nh_.subscribe("imuMeas", 1000, &FeatureTrackerNode::imuCallback,this);
    subImg_ = nh_.subscribe("/cam0/image_raw", 1000, &FeatureTrackerNode::imgCallback,this);
    min_feature_count_ = 50;
    max_feature_count_ = 20; // Maximal number of feature which is added at a time (not total)
    cv::namedWindow("Tracker");
  };
  ~FeatureTrackerNode(){
    cv::destroyWindow("Tracker");
  }
  void imuCallback(const sensor_msgs::Imu::ConstPtr& imu_msg){
  }
  void imgCallback(const sensor_msgs::ImageConstPtr & img){
    // Get image from msg
    cv_bridge::CvImagePtr cv_ptr;
    try {
      cv_ptr = cv_bridge::toCvCopy(img, sensor_msgs::image_encodings::TYPE_8UC1);
    } catch (cv_bridge::Exception& e) {
      ROS_ERROR("cv_bridge exception: %s", e.what());
      return;
    }
    cv_ptr->image.copyTo(img_);

    // Timing
    static double last_time = 0.0;
    double current_time = img->header.stamp.toSec();

    // Pyramid
    pyr_.computeFromImage(img_,true);

    // Drawing
    cvtColor(img_, draw_image_, CV_GRAY2RGB);
    const int numPatchesPlot = 10;
    draw_patches_ = cv::Mat::zeros(numPatchesPlot*(patchSize_*pow(2,nLevels_-1)+4),3*(patchSize_*pow(2,nLevels_-1)+4),CV_8UC1);

    // Prediction
    cv::Point2f dc;
    for(unsigned int i=0;i<nMax_;i++){
      if(mlps_.isValid_[i]){
        dc = 0.75*(mlps_.features_[i].c_ - mlps_.features_[i].log_previous_.c_);
        mlps_.features_[i].log_previous_.c_ = mlps_.features_[i].c_;
        mlps_.features_[i].c_ = mlps_.features_[i].c_ + dc;
        if(!isMultilevelPatchInFrame(mlps_.features_[i],pyr_,nLevels_-1,false)){
          mlps_.features_[i].c_ = mlps_.features_[i].log_previous_.c_;
        }
        mlps_.features_[i].increaseStatistics(current_time);
        mlps_.features_[i].status_.inFrame_ = true;
      }
    }

    // Track valid features
    const double t1 = (double) cv::getTickCount();
    cv::Point2f c_new;
    for(unsigned int i=0;i<nMax_;i++){
      if(mlps_.isValid_[i]){
        mlps_.features_[i].log_prediction_.c_ = mlps_.features_[i].c_;
        align2DComposed(mlps_.features_[i],pyr_,l2,l1,l2-l1,false);
      }
    }
    const double t2 = (double) cv::getTickCount();
    ROS_INFO_STREAM(" Matching " << mlps_.getValidCount() << " patches (" << (t2-t1)/cv::getTickFrequency()*1000 << " ms)");
    for(unsigned int i=0;i<nMax_;i++){
      if(mlps_.isValid_[i]){
        if(mlps_.features_[i].status_.matchingStatus_ == FOUND){
          mlps_.features_[i].status_.trackingStatus_ = TRACKED;
          mlps_.features_[i].log_meas_.c_ = mlps_.features_[i].c_;
          mlps_.features_[i].log_meas_.draw(draw_image_,cv::Scalar(0,255,255));
          mlps_.features_[i].log_meas_.drawLine(draw_image_,mlps_.features_[i].log_prediction_,cv::Scalar(0,255,255));
          mlps_.features_[i].log_meas_.drawText(draw_image_,std::to_string(mlps_.features_[i].idx_),cv::Scalar(0,255,255));
        } else {
          mlps_.features_[i].status_.trackingStatus_ = FAILED;
          mlps_.features_[i].log_prediction_.draw(draw_image_,cv::Scalar(0,0,255));
          mlps_.features_[i].log_prediction_.drawText(draw_image_,std::to_string(mlps_.features_[i].idx_),cv::Scalar(0,0,255));
        }
      }
    }
    MultilevelPatchFeature2<nLevels_,patchSize_> mlp;
    for(unsigned int i=0;i<numPatchesPlot;i++){
      if(mlps_.isValid_[i]){
        mlps_.features_[i].drawMultilevelPatch(draw_patches_,cv::Point2i(2,2+i*(patchSize_*pow(2,nLevels_-1)+4)),1,false);
        mlp.c_ = mlps_.features_[i].log_prediction_.c_;
        extractMultilevelPatchFromImage(mlp,pyr_,nLevels_-1,false);
        mlp.drawMultilevelPatch(draw_patches_,cv::Point2i(patchSize_*pow(2,nLevels_-1)+6,2+i*(patchSize_*pow(2,nLevels_-1)+4)),1,false);
        if(mlps_.features_[i].status_.matchingStatus_ == FOUND){
          mlp.c_ = mlps_.features_[i].c_;
          extractMultilevelPatchFromImage(mlp,pyr_,nLevels_-1,false);
          mlp.drawMultilevelPatch(draw_patches_,cv::Point2i(2*patchSize_*pow(2,nLevels_-1)+10,2+i*(patchSize_*pow(2,nLevels_-1)+4)),1,false);
          cv::rectangle(draw_patches_,cv::Point2i(0,i*(patchSize_*pow(2,nLevels_-1)+4)),cv::Point2i(patchSize_*pow(2,nLevels_-1)+3,(i+1)*(patchSize_*pow(2,nLevels_-1)+4)-1),cv::Scalar(255),2,8,0);
          cv::rectangle(draw_patches_,cv::Point2i(patchSize_*pow(2,nLevels_-1)+4,i*(patchSize_*pow(2,nLevels_-1)+4)),cv::Point2i(2*patchSize_*pow(2,nLevels_-1)+7,(i+1)*(patchSize_*pow(2,nLevels_-1)+4)-1),cv::Scalar(255),2,8,0);
        } else {
          cv::rectangle(draw_patches_,cv::Point2i(0,i*(patchSize_*pow(2,nLevels_-1)+4)),cv::Point2i(patchSize_*pow(2,nLevels_-1)+3,(i+1)*(patchSize_*pow(2,nLevels_-1)+4)-1),cv::Scalar(0),2,8,0);
          cv::rectangle(draw_patches_,cv::Point2i(patchSize_*pow(2,nLevels_-1)+4,i*(patchSize_*pow(2,nLevels_-1)+4)),cv::Point2i(2*patchSize_*pow(2,nLevels_-1)+7,(i+1)*(patchSize_*pow(2,nLevels_-1)+4)-1),cv::Scalar(0),2,8,0);
        }
        cv::putText(draw_patches_,std::to_string(mlps_.features_[i].idx_),cv::Point2i(2,2+i*(patchSize_*pow(2,nLevels_-1)+4)+10),cv::FONT_HERSHEY_SIMPLEX, 0.4, cv::Scalar(255));
      }
    }

    // Prune
    int prune_count = 0;
    for(unsigned int i=0;i<nMax_;i++){
      if(mlps_.isValid_[i]){
        if(!mlps_.features_[i].isGoodFeature(6,1,0.5,0.5)){
          mlps_.isValid_[i] = false;
          prune_count++;
        }
      }
    }
    ROS_INFO_STREAM(" Pruned " << prune_count << " features");

    // Extract feature patches
    for(unsigned int i=0;i<nMax_;i++){
      if(mlps_.isValid_[i]){
        if(mlps_.features_[i].status_.matchingStatus_ == FOUND && isMultilevelPatchInFrame(mlps_.features_[i],pyr_,nLevels_-1,true)){
          extractMultilevelPatchFromImage(mlps_.features_[i],pyr_,nLevels_-1,true);
        }
      }
    }

    // Get new features
    if(mlps_.getValidCount() < min_feature_count_){
      std::list<cv::Point2f> candidates;
      ROS_INFO_STREAM(" Adding keypoints");
      const double t1 = (double) cv::getTickCount();
      for(int l=l1;l<=l2;l++){
        detectFastCorners(pyr_,candidates,l,detectionThreshold);
      }
      const double t2 = (double) cv::getTickCount();
      ROS_INFO_STREAM(" == Detected " << candidates.size() << " on levels " << l1 << "-" << l2 << " (" << (t2-t1)/cv::getTickFrequency()*1000 << " ms)");
      pruneCandidates(mlps_,candidates);
      const double t3 = (double) cv::getTickCount();
      ROS_INFO_STREAM(" == Selected " << candidates.size() << " candidates (" << (t3-t2)/cv::getTickFrequency()*1000 << " ms)");
      std::unordered_set<unsigned int> newSet = addBestCandidates(mlps_,candidates,pyr_,current_time,
                                                                  l1,l2,max_feature_count_,nDetectionBuckets_, scoreDetectionExponent_,
                                                                  penaltyDistance_, zeroDistancePenalty_,true,0.0);
      const double t4 = (double) cv::getTickCount();
      ROS_INFO_STREAM(" == Got " << mlps_.getValidCount() << " after adding " << newSet.size() << " features (" << (t4-t3)/cv::getTickFrequency()*1000 << " ms)");
      for(auto it = newSet.begin();it != newSet.end();++it){
        mlps_.features_[*it].log_previous_.c_ = mlps_.features_[*it].c_;
        mlps_.features_[*it].status_.inFrame_ = true;
        mlps_.features_[*it].status_.matchingStatus_ = FOUND;
        mlps_.features_[*it].status_.trackingStatus_ = TRACKED;
      }
    }
    cv::imshow("Tracker", draw_image_);
    cv::imshow("Patches", draw_patches_);
    cv::waitKey(30);
    last_time = current_time;
  }
};
}


#endif /* FEATURE_TRACKER_HPP_ */
