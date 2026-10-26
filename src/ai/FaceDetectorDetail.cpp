#include "FaceDetectorDetail.h"

#include <algorithm>
#include <cmath>

namespace streamsight::ai::detail {
namespace {

struct LevelHeads {
  int cls = -1;
  int obj = -1;
  int bbox = -1;
  int kps = -1;
};

float PriorForStride(int stride) {
  switch (stride) {
  case 8:
    return 1.0f / 3.0f;
  case 16:
    return 0.5f;
  case 32:
    return 3.0f / 4.0f;
  default:
    return 0.5f;
  }
}

}  // namespace

std::vector<FaceBox> DecodeYuNet(const std::vector<cv::Mat>& outputs,
                                 const std::vector<std::string>& out_names,
                                 cv::Size input_size, cv::Size frame_size,
                                 float score_thresh) {
  std::vector<FaceBox> candidates;

  auto find = [&out_names](const std::string& name) -> int {
    for (size_t i = 0; i < out_names.size(); ++i) {
      if (out_names[i] == name) return static_cast<int>(i);
    }
    return -1;
  };

  const float sx =
      static_cast<float>(frame_size.width) / input_size.width;
  const float sy =
      static_cast<float>(frame_size.height) / input_size.height;

  for (int stride : {8, 16, 32}) {
    const std::string s = std::to_string(stride);
    const LevelHeads h{find("cls_" + s), find("obj_" + s),
                       find("bbox_" + s), find("kps_" + s)};
    if (h.cls < 0 || h.obj < 0 || h.bbox < 0) continue;
    if (h.cls >= static_cast<int>(outputs.size()) ||
        h.obj >= static_cast<int>(outputs.size()) ||
        h.bbox >= static_cast<int>(outputs.size())) {
      continue;
    }

    const int fw = input_size.width / stride;
    const int fh = input_size.height / stride;
    const int n = fw * fh;

    const cv::Mat cls = outputs[h.cls].reshape(1, n);
    const cv::Mat obj = outputs[h.obj].reshape(1, n);
    const cv::Mat bbox = outputs[h.bbox].reshape(1, n);
    if (bbox.cols < 4) continue;

    const float prior = PriorForStride(stride);

    for (int i = 0; i < n; ++i) {
      const float score =
          std::sqrt(cls.ptr<float>(i)[0] * obj.ptr<float>(i)[0]);
      if (score < score_thresh) continue;

      const float cx_prior = (i % fw + 0.5f) / fw;
      const float cy_prior = (i / fw + 0.5f) / fh;
      const float* b = bbox.ptr<float>(i);

      const float cx =
          (cx_prior + b[0] * 0.1f * prior) * input_size.width;
      const float cy =
          (cy_prior + b[1] * 0.1f * prior) * input_size.height;
      const float w = prior * std::exp(b[2] * 0.1f) * input_size.width;
      const float h = prior * std::exp(b[3] * 0.2f) * input_size.height;

      FaceBox box;
      box.rect = cv::Rect2f((cx - w / 2) * sx, (cy - h / 2) * sy, w * sx,
                            h * sy);
      box.confidence = score;
      candidates.push_back(box);
    }
  }

  return candidates;
}

std::vector<FaceBox> Nms(const std::vector<FaceBox>& boxes, float iou_thresh) {
  std::vector<FaceBox> sorted = boxes;
  std::sort(sorted.begin(), sorted.end(),
            [](const FaceBox& a, const FaceBox& b) {
              return a.confidence > b.confidence;
            });

  std::vector<FaceBox> keep;
  for (const auto& candidate : sorted) {
    bool suppressed = false;
    for (const auto& kept : keep) {
      const float inter = (candidate.rect & kept.rect).area();
      const float uni = candidate.rect.area() + kept.rect.area() - inter;
      if (uni > 0 && inter / uni > iou_thresh) {
        suppressed = true;
        break;
      }
    }
    if (!suppressed) keep.push_back(candidate);
  }
  return keep;
}

}  // namespace streamsight::ai::detail
