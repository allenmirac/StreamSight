#include <gtest/gtest.h>

#include "ffmpeg/StreamSession.h"

using streamsight::ffmpeg::StreamSession;
using streamsight::ffmpeg::StreamSessionConfig;

TEST(StreamSession, StartsStopped) {
  StreamSessionConfig cfg;
  cfg.input_url = "test.mp4";
  cfg.http_port = 8888;
  cfg.enable_ai = false;
  cfg.enable_audio = false;

  StreamSession session(cfg);
  EXPECT_FALSE(session.IsRunning());
  EXPECT_TRUE(session.GetEffectNames().empty());
}

TEST(StreamSession, DefaultConfigValues) {
  StreamSessionConfig cfg;
  EXPECT_EQ(cfg.width, 640);
  EXPECT_EQ(cfg.height, 480);
  EXPECT_EQ(cfg.fps, 25);
  EXPECT_EQ(cfg.pipeline_mode, "serial");
}

TEST(StreamSession, EventBusWorksBeforeStart) {
  StreamSessionConfig cfg;
  StreamSession session(cfg);

  int events = 0;
  auto handle = session.GetEventBus().Subscribe(
      [&](const streamsight::ffmpeg::FrameProcessedEvent&) { ++events; });
  session.GetEventBus().Publish(streamsight::ffmpeg::FrameProcessedEvent());

  EXPECT_EQ(events, 1);
  session.GetEventBus().Unsubscribe(handle);
}

TEST(StreamSession, StatusOnStoppedSession) {
  StreamSessionConfig cfg;
  cfg.rtsp_suffix = "test";
  StreamSession session(cfg);

  auto status = session.GetStatus();
  EXPECT_FALSE(status.running);
  EXPECT_EQ(status.stream_id, "test");
  EXPECT_EQ(status.frames_processed, 0);
}

TEST(StreamSession, ConfigAccessor) {
  StreamSessionConfig cfg;
  cfg.input_url = "rtsp://example.com/stream";
  cfg.rtsp_suffix = "stream1";
  StreamSession session(cfg);

  EXPECT_EQ(session.Config().input_url, "rtsp://example.com/stream");
  EXPECT_EQ(session.Config().rtsp_suffix, "stream1");
}

TEST(StreamSession, NoRtspServerBeforeStart) {
  StreamSessionConfig cfg;
  StreamSession session(cfg);
  EXPECT_EQ(session.GetRtspServer(), nullptr);
}

TEST(StreamSession, NoFacePluginWhenAiDisabled) {
  StreamSessionConfig cfg;
  cfg.enable_ai = false;
  StreamSession session(cfg);
  EXPECT_EQ(session.GetFacePlugin(), nullptr);
}

TEST(StreamSession, SessionIdZeroBeforeStart) {
  StreamSessionConfig cfg;
  StreamSession session(cfg);
  EXPECT_EQ(session.GetSessionId(), 0u);
}

TEST(StreamSession, UpdateEffectsWithMissingModelsFails) {
  StreamSessionConfig cfg;
  cfg.enable_ai = false;
  StreamSession session(cfg);

  const bool ok = session.UpdateEffects(
      R"({"detect_model":"/nonexistent/model.onnx",)"
      R"("recog_model":"/nonexistent/recog.onnx",)"
      R"("face_db_path":"/tmp/streamsight_session_faces.json",)"
      R"("analyze_fps":5})");

  EXPECT_FALSE(ok);
  EXPECT_TRUE(session.GetEffectNames().empty());
}
