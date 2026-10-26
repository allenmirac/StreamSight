#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "rtsp/H264Source.h"
#include "rtsp/MediaSession.h"

using streamsight::rtsp::channel_0;
using streamsight::rtsp::channel_1;
using streamsight::rtsp::H264Source;
using streamsight::rtsp::MediaSession;

TEST(MediaSession, AddsGetsAndRemovesSources) {
  std::unique_ptr<MediaSession> session(MediaSession::CreateNew("live"));

  auto* source = H264Source::CreateNew(25);
  ASSERT_TRUE(session->AddSource(channel_0, source));
  EXPECT_EQ(session->GetMediaSource(channel_0), source);
  EXPECT_EQ(session->GetMediaSource(channel_1), nullptr);

  EXPECT_TRUE(session->RemoveSource(channel_0));
  EXPECT_EQ(session->GetMediaSource(channel_0), nullptr);
}

TEST(MediaSession, AddingToSameChannelReplacesSource) {
  std::unique_ptr<MediaSession> session(MediaSession::CreateNew("live"));

  auto* first = H264Source::CreateNew(25);
  auto* second = H264Source::CreateNew(30);
  ASSERT_TRUE(session->AddSource(channel_0, first));
  ASSERT_TRUE(session->AddSource(channel_0, second));
  EXPECT_EQ(session->GetMediaSource(channel_0), second);
  auto* current = static_cast<H264Source*>(session->GetMediaSource(channel_0));
  ASSERT_NE(current, nullptr);
  EXPECT_EQ(current->GetFramerate(), 30u);
  EXPECT_EQ(session->GetMediaSource(channel_1), nullptr);
}

TEST(MediaSession, BuildsSdpForRegisteredSource) {
  std::unique_ptr<MediaSession> session(MediaSession::CreateNew("live"));
  ASSERT_TRUE(session->AddSource(channel_0, H264Source::CreateNew(25)));

  const std::string sdp = session->GetSdpMessage("127.0.0.1", "test");
  EXPECT_NE(sdp.find("s=test\r\n"), std::string::npos);
  EXPECT_NE(sdp.find("m=video 0 RTP/AVP 96\r\n"), std::string::npos);
  EXPECT_NE(sdp.find("a=rtpmap:96 H264/90000\r\n"), std::string::npos);
  EXPECT_NE(sdp.find("a=control:track0\r\n"), std::string::npos);
}

TEST(MediaSession, SessionIdsAreUniqueAndIncrease) {
  std::unique_ptr<MediaSession> first(MediaSession::CreateNew("a"));
  std::unique_ptr<MediaSession> second(MediaSession::CreateNew("b"));

  EXPECT_GT(first->GetMediaSessionId(), 0u);
  EXPECT_GT(second->GetMediaSessionId(), first->GetMediaSessionId());
}

TEST(MediaSession, SuffixCanBeReadAndWritten) {
  std::unique_ptr<MediaSession> session(MediaSession::CreateNew("live"));
  EXPECT_EQ(session->GetRtspUrlSuffix(), "live");

  std::string suffix = "camera1";
  session->SetRtspUrlSuffix(suffix);
  EXPECT_EQ(session->GetRtspUrlSuffix(), "camera1");
}
