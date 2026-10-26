#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "rtsp/H264Parser.h"

using streamsight::rtsp::H264Parser;
using streamsight::rtsp::Nal;

namespace {

Nal Find(const std::vector<uint8_t>& data) {
  return H264Parser::findNal(data.data(),
                             static_cast<uint32_t>(data.size()));
}

}  // namespace

TEST(H264Parser, FindsNalAfterFourByteStartCode) {
  const std::vector<uint8_t> data = {0x00, 0x00, 0x00, 0x01, 0x65, 0x88};
  const Nal nal = Find(data);

  ASSERT_NE(nal.first, nullptr);
  EXPECT_EQ(nal.first, data.data() + 4);
  EXPECT_EQ(nal.second, data.data() + 5);
}

TEST(H264Parser, FindsNalAfterThreeByteStartCode) {
  const std::vector<uint8_t> data = {0x00, 0x00, 0x01, 0x65, 0xAA};
  const Nal nal = Find(data);

  ASSERT_NE(nal.first, nullptr);
  EXPECT_EQ(nal.first, data.data() + 3);
  EXPECT_EQ(nal.second, data.data() + 4);
}

TEST(H264Parser, IncludesTrailingZerosInNal) {
  const std::vector<uint8_t> data = {0x00, 0x00, 0x00, 0x01,
                                     0x65, 0x00, 0x00};
  const Nal nal = Find(data);

  ASSERT_NE(nal.first, nullptr);
  EXPECT_EQ(nal.first, data.data() + 4);
  EXPECT_EQ(nal.second, data.data() + 6);
}

TEST(H264Parser, ReturnsFirstOfTwoNals) {
  const std::vector<uint8_t> data = {0x00, 0x00, 0x01, 0x65, 0xAA,
                                     0x00, 0x00, 0x01, 0x41, 0xBB};
  const Nal nal = Find(data);

  ASSERT_NE(nal.first, nullptr);
  EXPECT_EQ(nal.first, data.data() + 3);
  EXPECT_EQ(nal.second, data.data() + 4);
}

TEST(H264Parser, ReturnsNullWhenNoStartCode) {
  const std::vector<uint8_t> data = {0x11, 0x22, 0x33, 0x44, 0x55};
  const Nal nal = Find(data);

  EXPECT_EQ(nal.first, nullptr);
  EXPECT_EQ(nal.second, nullptr);
}

TEST(H264Parser, ReturnsNullWhenTooShort) {
  const std::vector<uint8_t> data = {0x00, 0x00, 0x01, 0x65};
  const Nal nal = Find(data);

  EXPECT_EQ(nal.first, nullptr);
  EXPECT_EQ(nal.second, nullptr);
}
