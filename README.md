# StreamSight

> AI-Augmented Live Stream Processing Platform — Custom RTSP/RTP protocol stack + FFmpeg C API pipeline,
> integrating frame-level AI processing and live effects into real-time video streams.

[中文介绍](README_CN.md)

---

## Overview

StreamSight is an AI-augmented live stream processing platform. It handles the full pipeline
"ingest → decode → AI processing/effects → encode → multi-protocol output" in-process.

**Key Features:**
- **Custom RTSP/RTP stack (xop)**: Reactor-based (epoll), supports H.264/H.265/AAC.
  Processed video is accessible to any RTSP client over the network.
- **FFmpeg C API in-process pipeline**: 3-stage pipeline (Demux+Decode → AI Process → Encode),
  replacing fork+pipe subprocess approach. RingBuffer backpressure + FrameDropPolicy adaptive frame dropping.
- **Extensible EffectPlugin system**: Face detection/recognition (YuNet + ArcFace ONNX) as the first plugin demo.
  Extensible to watermarking, blurring, safety detection, beautification, etc.
- **RTMP live distribution**: Built-in RTMP Push Client for external SRS/nginx-rtmp distribution.
- **EffectFactory**: JSON-configurable plugin creation.
- **EventBus**: Thread-safe structured event pub/sub.
- **StreamSession**: Single-session abstraction (Start/Stop/GetStatus).
- **StreamApiServer**: Merged HTTP API with session CRUD + legacy routes.

```
Ingest → StreamPipeline (Demux+Decode → AI Process → Encode) → Output Adapters
              │                                      │
              │  Stage2: AI Process                  │
              │  ├── EffectChain (IEffectPlugin[])    │
              │  └── Content Understanding (future)   │
              │                                      │
              ▼                                      ▼
    RTSP live output (custom stack)   RTMP Push (external SRS)
    LAN/cross-segment client pull     Large-scale live distribution
```

---

## Quick Start

### Dependencies

```bash
# Runtime + AI
sudo apt install libopencv-dev ffmpeg
# FFmpeg C API pipeline
sudo apt install libavformat-dev libavcodec-dev libavutil-dev libswscale-dev
```

### Build

```bash
mkdir -p build && cd build
cmake .. && make -j$(nproc)
```

### Download AI Models

```bash
mkdir -p models
wget -O models/face_detection.onnx \
  https://github.com/opencv/opencv_zoo/raw/main/models/face_detection_yunet/face_detection_yunet_2023mar.onnx
wget -O models/face_recognition.onnx \
  https://github.com/opencv/opencv_zoo/raw/main/models/face_recognition_sface/face_recognition_sface_2021dec.onnx
```

### Run

```bash
# Primary demo: file input with AI analysis + RTSP output
./build/bin/ffmpeg_streamer --input test.mp4 --port 8554

# Camera input
./build/bin/ffmpeg_streamer --source camera --input 0 --port 8554

# Skip AI processing
./build/bin/ffmpeg_streamer --input test.mp4 --no-ai --port 8554

# RTMP output (requires SRS running, see below)
./build/bin/ffmpeg_streamer --input test.mp4 --rtmp rtmp://localhost:1935/live/stream --port 8554
```

### Watch & Query

```bash
# RTSP (direct, no external server needed)
ffplay rtsp://localhost:8554/live

# From another machine on the LAN
ffplay rtsp://192.168.1.x:8554/live

# HTTP API
curl http://localhost:8080/api/current       # current frame detections
curl http://localhost:8080/api/status        # server status

# Register a face
curl -X POST http://localhost:8080/api/faces \
  -F "name=Alice" -F "image=@alice.jpg"
```

---

## RTMP Distribution

StreamSight's `RtmpOutputAdapter` is an RTMP Push Client that pushes processed streams to an external RTMP server.

**rtmp://localhost:8888/live/test → Connection refused**
This means there is no RTMP server listening on port 8888. Start an external RTMP server first.

```bash
# Start SRS
docker-compose up -d srs

# StreamSight pushes processed stream to SRS
./build/bin/ffmpeg_streamer --input test.mp4 --rtmp rtmp://localhost:1935/live/stream --port 8554

# Watch via RTMP
ffplay rtmp://localhost:1935/live/stream
```

See [docs/rtmp-distribution.md](docs/rtmp-distribution.md) for details.

---

## Effect Plugin Architecture

```cpp
// IEffectPlugin: unified interface for all effect/analysis plugins
class IEffectPlugin {
    virtual std::string Name() const = 0;
    virtual bool Process(uint8_t* bgr, int w, int h, int linesize,
                         EffectResult* result) = 0;
};

// EffectChain: ordered execution of multiple plugins
EffectChain chain;
chain.AddPlugin(std::make_shared<FaceRecognitionPlugin>(...));
chain.AddPlugin(std::make_shared<WatermarkPlugin>(...));  // future
chain.ProcessFrame(bgr_data, width, height, linesize, results);
```

Built-in: `FaceRecognitionPlugin` (face detection + recognition + bounding box overlay)

---

## Project Structure

```
src/
├── net/       Reactor network framework (EventLoop, epoll, TcpServer, RingBuffer)
├── xop/       RTSP/RTP protocol (RtspServer, MediaSession, H264Source...)
├── ffmpeg/    FFmpeg C API pipeline (StreamPipeline, StreamSession, IOutputAdapter...)
├── effect/    EffectPlugin system (IEffectPlugin, EffectChain, FaceRecognitionPlugin, EffectFactory)
├── api/       HTTP REST API (StreamApiServer — session CRUD, effect config, metrics)
├── ai/        AI model loading (FaceDetector, FaceRecognizer, FaceDatabase, FrameAnalyzer)
├── control/   Stream management + scheduling (LEGACY)
├── observe/   Observability (MetricsRegistry, LatencyTracer, EventBus)
└── cdn_sim/   CDN edge simulation

example/
├── ffmpeg_streamer.cpp        ★ Main entry: StreamSession + API server
├── rtsp_analysis_server.cpp     LEGACY (BUILD_LEGACY_TARGETS=ON)
└── ...

tests/
├── test_event_bus.cpp
├── test_effect_factory.cpp
├── test_stream_session.cpp
└── test_api_server.cpp

docs/
├── rtmp-distribution.md     RTMP distribution architecture
├── api.md                   REST API documentation
└── ...
```

---

## REST API

All routes served on a single port (default 8080) by StreamApiServer:

| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/status` | Server status + uptime |
| GET | `/api/current` | Latest frame detection result |
| GET | `/api/events?limit=N` | Recent detection events |
| GET | `/api/faces` | Registered faces |
| POST | `/api/faces` | Register face (multipart: name + image) |
| DELETE | `/api/faces/{name}` | Remove face |
| GET | `/api/latency/stats` | Pipeline latency percentiles |
| GET | `/api/v1/sessions` | List all sessions |
| POST | `/api/v1/sessions` | Create session (JSON body) |
| GET | `/api/v1/sessions/:id` | Session status + metrics |
| DELETE | `/api/v1/sessions/:id` | Stop and remove session |
| PUT | `/api/v1/sessions/:id/effects` | Update effect config (JSON body) |
| GET | `/api/v1/sessions/:id/results` | Detection results per session |

---

## Roadmap

- **Phase 1**: Architecture correction + EffectPlugin interface + code cleanup
- **Phase 2**: StreamSession abstraction + HTTP API platformization + dynamic effect config
- **Phase 3**: Video summarization + Content Understanding + Agent tool interface

---

## Requirements

| Component | Version |
|-----------|---------|
| Compiler | GCC 4.8+ / Clang (C++11) |
| OpenCV | 4.x with DNN module |
| FFmpeg | 4.x+ (libavformat/libavcodec/libavutil/libswscale) |
| CMake | 3.10+ |
| OS | Linux (epoll) / Windows (select) |

---

## License

[MIT License](LICENSE)