#include <gtest/gtest.h>

#include <cctype>
#include <string>

#include "rtsp/DigestAuthentication.h"

using streamsight::rtsp::DigestAuthentication;

TEST(DigestAuthentication, MatchesKnownVector) {
  DigestAuthentication auth("streamsight", "user", "pass");
  EXPECT_EQ(
      auth.GetResponse("abc123", "DESCRIBE", "rtsp://127.0.0.1/live"),
      "f71c28d14f01072969356b080aed539c");
}

TEST(DigestAuthentication, ResponseChangesWithCommandOrUrl) {
  DigestAuthentication auth("streamsight", "user", "pass");
  const std::string base =
      auth.GetResponse("abc123", "DESCRIBE", "rtsp://127.0.0.1/live");

  EXPECT_NE(base,
            auth.GetResponse("abc123", "SETUP", "rtsp://127.0.0.1/live"));
  EXPECT_NE(base,
            auth.GetResponse("abc123", "DESCRIBE", "rtsp://127.0.0.1/other"));
  EXPECT_NE(base,
            auth.GetResponse("other", "DESCRIBE", "rtsp://127.0.0.1/live"));
}

TEST(DigestAuthentication, NonceIsMd5Hex) {
  DigestAuthentication auth("streamsight", "user", "pass");
  const std::string nonce = auth.GetNonce();

  ASSERT_EQ(nonce.size(), 32u);
  for (char c : nonce) {
    EXPECT_TRUE(std::isxdigit(static_cast<unsigned char>(c)) != 0);
  }
}
