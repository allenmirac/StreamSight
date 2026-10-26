#ifndef STREAMSIGHT_AI_VECTOR_MATH_H
#define STREAMSIGHT_AI_VECTOR_MATH_H

#include <cmath>
#include <cstddef>
#include <vector>

namespace streamsight::ai {

// Both inputs are expected to be L2-normalized, so the dot product is the
// cosine similarity. Returns -1 for empty or mismatched dimensions.
inline float CosineSimilarity(const std::vector<float>& a,
                              const std::vector<float>& b) {
  if (a.size() != b.size() || a.empty()) return -1.0f;
  float dot = 0.0f;
  for (size_t i = 0; i < a.size(); ++i) dot += a[i] * b[i];
  return dot;
}

// In-place L2 normalization. A near-zero vector is left unchanged.
inline void L2Normalize(std::vector<float>& value) {
  float norm = 0.0f;
  for (float x : value) norm += x * x;
  norm = std::sqrt(norm);
  if (norm < 1e-10f) return;
  for (float& x : value) x /= norm;
}

}  // namespace streamsight::ai

#endif  // STREAMSIGHT_AI_VECTOR_MATH_H
