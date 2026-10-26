#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "rtsp/RtspMessage.h"
#include "support/socket_reader.h"

using streamsight::rtsp::RtspRequest;
using streamsight::rtsp::RTP_OVER_MULTICAST;
using streamsight::rtsp::RTP_OVER_TCP;
using streamsight::rtsp::RTP_OVER_UDP;
using streamsight::test::SocketReader;

namespace {

bool ParseAll(const std::string& text, RtspRequest& request) {
  SocketReader sr;
  sr.Feed(text);
  const bool ok = request.ParseRequest(&sr.reader());
  return ok && request.GotAll();
}

const char* kOptions =
    "OPTIONS rtsp://127.0.0.1:8554/live RTSP/1.0\r\n"
    "CSeq: 1\r\n"
    "\r\n";

const char* kDescribe =
    "DESCRIBE rtsp://127.0.0.1/live RTSP/1.0\r\n"
    "CSeq: 2\r\n"
    "Accept: application/sdp\r\n"
    "\r\n";

const char* kSetupTcp =
    "SETUP rtsp://127.0.0.1:8554/live/track1 RTSP/1.0\r\n"
    "CSeq: 3\r\n"
    "Transport: RTP/AVP/TCP;unicast;interleaved=0-1\r\n"
    "\r\n";

const char* kSetupUdp =
    "SETUP rtsp://127.0.0.1:8554/live/track0 RTSP/1.0\r\n"
    "CSeq: 4\r\n"
    "Transport: RTP/AVP;unicast;client_port=8000-8001\r\n"
    "\r\n";

const char* kSetupMulticast =
    "SETUP rtsp://127.0.0.1:8554/live/track0 RTSP/1.0\r\n"
    "CSeq: 5\r\n"
    "Transport: RTP/AVP;multicast\r\n"
    "\r\n";

const char* kPlay =
    "PLAY rtsp://127.0.0.1:8554/live RTSP/1.0\r\n"
    "CSeq: 6\r\n"
    "Session: 12345678\r\n"
    "\r\n";

}  // namespace

TEST(RtspRequest, ParsesOptions) {
  RtspRequest request;
  ASSERT_TRUE(ParseAll(kOptions, request));
  EXPECT_EQ(request.GetMethod(), RtspRequest::OPTIONS);
  EXPECT_EQ(request.GetCSeq(), 1u);
  EXPECT_EQ(request.GetIp(), "127.0.0.1");
  EXPECT_EQ(request.GetRtspUrlSuffix(), "live");
}

TEST(RtspRequest, ParsesDescribeWithDefaultPort) {
  RtspRequest request;
  ASSERT_TRUE(ParseAll(kDescribe, request));
  EXPECT_EQ(request.GetMethod(), RtspRequest::DESCRIBE);
  EXPECT_EQ(request.GetCSeq(), 2u);
  EXPECT_EQ(request.GetIp(), "127.0.0.1");
  EXPECT_EQ(request.GetRtspUrlSuffix(), "live");
}

TEST(RtspRequest, ParsesSetupOverTcp) {
  RtspRequest request;
  ASSERT_TRUE(ParseAll(kSetupTcp, request));
  EXPECT_EQ(request.GetMethod(), RtspRequest::SETUP);
  EXPECT_EQ(request.GetTransportMode(), RTP_OVER_TCP);
  EXPECT_EQ(request.GetRtpChannel(), 0);
  EXPECT_EQ(request.GetRtcpChannel(), 1);
  EXPECT_EQ(request.GetChannelId(), streamsight::rtsp::channel_1);
  EXPECT_EQ(request.GetRtspUrlSuffix(), "live/track1");
}

TEST(RtspRequest, ParsesSetupOverUdp) {
  RtspRequest request;
  ASSERT_TRUE(ParseAll(kSetupUdp, request));
  EXPECT_EQ(request.GetTransportMode(), RTP_OVER_UDP);
  EXPECT_EQ(request.GetRtpPort(), 8000);
  EXPECT_EQ(request.GetRtcpPort(), 8001);
  EXPECT_EQ(request.GetChannelId(), streamsight::rtsp::channel_0);
}

TEST(RtspRequest, ParsesSetupMulticast) {
  RtspRequest request;
  ASSERT_TRUE(ParseAll(kSetupMulticast, request));
  EXPECT_EQ(request.GetTransportMode(), RTP_OVER_MULTICAST);
  EXPECT_EQ(request.GetRtpPort(), 0);
  EXPECT_EQ(request.GetRtcpPort(), 0);
}

TEST(RtspRequest, ParsesPlayAndTeardown) {
  RtspRequest play;
  ASSERT_TRUE(ParseAll(kPlay, play));
  EXPECT_EQ(play.GetMethod(), RtspRequest::PLAY);

  RtspRequest teardown;
  ASSERT_TRUE(ParseAll(
      "TEARDOWN rtsp://127.0.0.1:8554/live RTSP/1.0\r\nCSeq: 7\r\n\r\n",
      teardown));
  EXPECT_EQ(teardown.GetMethod(), RtspRequest::TEARDOWN);
  EXPECT_EQ(teardown.GetCSeq(), 7u);
}

TEST(RtspRequest, ParsesGetParameter) {
  RtspRequest request;
  ASSERT_TRUE(ParseAll(
      "GET_PARAMETER rtsp://127.0.0.1:8554/live RTSP/1.0\r\n"
      "CSeq: 8\r\n\r\n",
      request));
  EXPECT_EQ(request.GetMethod(), RtspRequest::GET_PARAMETER);
}

TEST(RtspRequest, HandlesSplitRequest) {
  SocketReader sr;
  RtspRequest request;

  sr.Feed("SETUP rtsp://127.0.0.1:8554/live/track0 RTSP/1.0\r\n");
  EXPECT_TRUE(request.ParseRequest(&sr.reader()));
  EXPECT_FALSE(request.GotAll());

  sr.Feed("CSeq: 9\r\nTransport: RTP/AVP/TCP;unicast;interleaved=0-1\r\n\r\n");
  EXPECT_TRUE(request.ParseRequest(&sr.reader()));
  EXPECT_TRUE(request.GotAll());
  EXPECT_EQ(request.GetCSeq(), 9u);
}

TEST(RtspRequest, RejectsUnknownMethod) {
  RtspRequest request;
  EXPECT_FALSE(ParseAll(
      "FOO rtsp://127.0.0.1:8554/live RTSP/1.0\r\nCSeq: 1\r\n\r\n", request));
  EXPECT_FALSE(request.GotAll());
}

TEST(RtspRequest, RejectsMissingCSeq) {
  RtspRequest request;
  EXPECT_FALSE(ParseAll(
      "OPTIONS rtsp://127.0.0.1:8554/live RTSP/1.0\r\n\r\n", request));
  EXPECT_FALSE(request.GotAll());
}

TEST(RtspRequest, RejectsSetupWithoutTransport) {
  RtspRequest request;
  EXPECT_FALSE(ParseAll(
      "SETUP rtsp://127.0.0.1:8554/live/track0 RTSP/1.0\r\n"
      "CSeq: 1\r\n\r\n",
      request));
  EXPECT_FALSE(request.GotAll());
}

TEST(RtspRequest, RejectsCrlfOnly) {
  RtspRequest request;
  EXPECT_FALSE(ParseAll("\r\n", request));
  EXPECT_FALSE(request.GotAll());
}

TEST(RtspRequest, RejectsNonRtspUrl) {
  RtspRequest request;
  EXPECT_FALSE(ParseAll(
      "OPTIONS http://127.0.0.1:8554/live RTSP/1.0\r\nCSeq: 1\r\n\r\n",
      request));
}

TEST(RtspRequest, RejectsUnparseableUrl) {
  RtspRequest request;
  EXPECT_FALSE(ParseAll(
      "OPTIONS rtsp:// RTSP/1.0\r\nCSeq: 1\r\n\r\n", request));
}

TEST(RtspRequest, BuildsOptionResponse) {
  RtspRequest request;
  ASSERT_TRUE(ParseAll(kOptions, request));

  char buf[1024] = {0};
  const int n = request.BuildOptionRes(buf, sizeof(buf));
  EXPECT_EQ(n, static_cast<int>(std::strlen(buf)));
  EXPECT_NE(std::string(buf).find("RTSP/1.0 200 OK\r\n"), std::string::npos);
  EXPECT_NE(std::string(buf).find("CSeq: 1\r\n"), std::string::npos);
  EXPECT_NE(std::string(buf).find("Public: OPTIONS"), std::string::npos);
}

TEST(RtspRequest, BuildsDescribeResponseWithContentLength) {
  RtspRequest request;
  ASSERT_TRUE(ParseAll(kDescribe, request));

  const std::string sdp = "v=0\r\ns=test\r\n";
  char buf[1024] = {0};
  const int n = request.BuildDescribeRes(buf, sizeof(buf), sdp.c_str());
  EXPECT_EQ(n, static_cast<int>(std::strlen(buf)));
  EXPECT_NE(std::string(buf).find("Content-Length: " +
                                  std::to_string(sdp.size()) + "\r\n"),
            std::string::npos);
  EXPECT_NE(std::string(buf).find(sdp), std::string::npos);
}

TEST(RtspRequest, BuildsSetupResponses) {
  {
    RtspRequest request;
    ASSERT_TRUE(ParseAll(kSetupTcp, request));
    char buf[1024] = {0};
    request.BuildSetupTcpRes(buf, sizeof(buf), 0, 1, 42);
    EXPECT_NE(std::string(buf).find("interleaved=0-1"), std::string::npos);
    EXPECT_NE(std::string(buf).find("Session: 42"), std::string::npos);
  }
  {
    RtspRequest request;
    ASSERT_TRUE(ParseAll(kSetupUdp, request));
    char buf[1024] = {0};
    request.BuildSetupUdpRes(buf, sizeof(buf), 9000, 9001, 43);
    EXPECT_NE(std::string(buf).find("client_port=8000-8001;server_port=9000-9001"),
              std::string::npos);
    EXPECT_NE(std::string(buf).find("Session: 43"), std::string::npos);
  }
}

TEST(RtspRequest, BuildsPlayResponse) {
  RtspRequest request;
  ASSERT_TRUE(ParseAll(kPlay, request));

  char buf[1024] = {0};
  request.BuildPlayRes(buf, sizeof(buf), "RTP-Info: url=track0;seq=1", 42);
  EXPECT_NE(std::string(buf).find("Range: npt=0.000-"), std::string::npos);
  EXPECT_NE(std::string(buf).find("Session: 42; timeout=60"),
            std::string::npos);
  EXPECT_NE(std::string(buf).find("RTP-Info: url=track0;seq=1"),
            std::string::npos);
}

TEST(RtspRequest, BuildTruncatesWithoutOverflow) {
  RtspRequest request;
  ASSERT_TRUE(ParseAll(kOptions, request));

  char tiny[10] = {0};
  const int n = request.BuildOptionRes(tiny, sizeof(tiny));
  EXPECT_LT(n, static_cast<int>(sizeof(tiny)));
  EXPECT_EQ(n, static_cast<int>(std::strlen(tiny)));
}
