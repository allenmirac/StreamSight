#ifndef STREAMSIGHT_AI_FACE_DETECTOR_DETAIL_H
#define STREAMSIGHT_AI_FACE_DETECTOR_DETAIL_H

#include <opencv2/core.hpp>

#include <string>
#include <vector>

#include "FaceDetector.h"

namespace streamsight::ai::detail {

// Decodes YuNet raw heads into candidate boxes (no NMS).
std::vector<FaceBox> DecodeYuNet(const std::vector<cv::Mat>& outputs,
                                 const std::vector<std::string>& out_names,
                                 cv::Size input_size, cv::Size frame_size,
                                 float score_thresh);

// Greedy NMS over confidence-sorted candidates. Suppresses when
// IoU > iou_thresh (strictly greater).
std::vector<FaceBox> Nms(const std::vector<FaceBox>& boxes, float iou_thresh);

}  // namespace streamsight::ai::detail

#endif  // STREAMSIGHT_AI_FACE_DETECTOR_DETAIL_H
