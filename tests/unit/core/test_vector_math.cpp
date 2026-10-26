#include <gtest/gtest.h>

#include <vector>

#include "ai/VectorMath.h"

using streamsight::ai::CosineSimilarity;
using streamsight::ai::L2Normalize;

TEST(VectorMath, ComputesDotProductForNormalizedVectors) {
  EXPECT_FLOAT_EQ(CosineSimilarity({1.0f, 0.0f}, {1.0f, 0.0f}), 1.0f);
  EXPECT_FLOAT_EQ(CosineSimilarity({1.0f, 0.0f}, {0.0f, 1.0f}), 0.0f);
  EXPECT_FLOAT_EQ(CosineSimilarity({0.6f, 0.8f}, {0.6f, 0.8f}), 1.0f);
}

TEST(VectorMath, ReturnsMinusOneForInvalidInput) {
  EXPECT_FLOAT_EQ(CosineSimilarity({}, {}), -1.0f);
  EXPECT_FLOAT_EQ(CosineSimilarity({1.0f}, {1.0f, 2.0f}), -1.0f);
}

TEST(VectorMath, NormalizesVector) {
  std::vector<float> value = {3.0f, 4.0f};
  L2Normalize(value);
  EXPECT_NEAR(value[0], 0.6f, 1e-6);
  EXPECT_NEAR(value[1], 0.8f, 1e-6);
}

TEST(VectorMath, LeavesZeroVectorUnchanged) {
  std::vector<float> value = {0.0f, 0.0f};
  L2Normalize(value);
  EXPECT_FLOAT_EQ(value[0], 0.0f);
  EXPECT_FLOAT_EQ(value[1], 0.0f);
}
