#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "api/StreamApiServer.h"

using streamsight::api::StreamApiServer;
using streamsight::ffmpeg::StreamSessionConfig;

TEST(StreamApiServer, CreatesAndListsSessions) {
  StreamApiServer server(19999);
  StreamSessionConfig cfg;
  cfg.input_url = "test.mp4";
  cfg.enable_ai = false;
  cfg.enable_audio = false;

  const std::string id1 = server.CreateSession(cfg);
  EXPECT_FALSE(id1.empty());
  EXPECT_EQ(server.ListSessions().size(), 1u);

  const std::string id2 = server.CreateSession(cfg);
  EXPECT_FALSE(id2.empty());
  EXPECT_NE(id1, id2);
  EXPECT_EQ(server.ListSessions().size(), 2u);
}

TEST(StreamApiServer, RemovesSessions) {
  StreamApiServer server(19998);
  StreamSessionConfig cfg;
  cfg.enable_ai = false;
  cfg.enable_audio = false;

  const std::string id = server.CreateSession(cfg);
  EXPECT_EQ(server.ListSessions().size(), 1u);
  EXPECT_TRUE(server.RemoveSession(id));
  EXPECT_EQ(server.ListSessions().size(), 0u);
  EXPECT_FALSE(server.RemoveSession("nonexistent"));
}

TEST(StreamApiServer, GetsSessionStatus) {
  StreamApiServer server(19997);
  StreamSessionConfig cfg;
  cfg.enable_ai = false;
  cfg.enable_audio = false;

  const std::string id = server.CreateSession(cfg);
  const auto status = server.GetSessionStatus(id);
  EXPECT_EQ(status.stream_id, id);
  EXPECT_FALSE(status.running);
}

TEST(StreamApiServer, RegistersExternalSession) {
  StreamApiServer server(19993);
  StreamSessionConfig cfg;
  cfg.input_url = "external.mp4";
  cfg.enable_ai = false;
  cfg.enable_audio = false;

  auto session = std::make_shared<streamsight::ffmpeg::StreamSession>(cfg);
  const std::string id = server.RegisterSession(session);

  EXPECT_FALSE(id.empty());
  EXPECT_EQ(server.ListSessions().size(), 1u);
  EXPECT_EQ(server.GetSession(id), session);
}

// Characterization test: UpdateResult/AddEvent expose no public getter, so
// this only pins down that accepting a result and an event does not crash.
TEST(StreamApiServer, AcceptingResultsAndEventsDoesNotCrash) {
  StreamApiServer server(19996);
  streamsight::ai::AnalysisResult result;
  result.frame_id = 42;
  result.timestamp_ms = 1000;

  server.UpdateResult(result);
  server.AddEvent(result);
}

TEST(StreamApiServer, AcceptsNullDatabaseAndRecognizer) {
  StreamApiServer server(19995, nullptr, nullptr);
  EXPECT_FALSE(server.IsRunning());
}

TEST(StreamApiServer, MissingSessionReturnsNull) {
  StreamApiServer server(19994);
  EXPECT_EQ(server.GetSession("nonexistent"), nullptr);
}
