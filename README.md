# StreamSight

> Real-time RTSP streaming server with AI-powered face detection and recognition — built in C++11.

[中文介绍](README_CN.md)

---

## Overview

StreamSight extends a high-performance RTSP/RTP server with an AI analysis pipeline. Raw frames from a camera, video file, or remote RTSP source are analyzed for faces in real time, annotated with bounding boxes and names, encoded to H.264, and pushed to RTSP clients — all with a REST API for live result queries.

```
VideoSource → AI Analysis → Frame Overlay → H.264 Encode → RTSP/RTP → Clients
                  ↓
            REST API  +  JSON Event Log
```

---

## Features

**Streaming**
- RTSP server and pusher based on Reactor (epoll on Linux, select on Windows)
- H.264 / H.265 / G711A / AAC / VP8 codec support
- Audio+video streaming (AAC audio integrated in ffmpeg_streamer)
- Unicast (RTP over TCP, RTP over UDP) and multicast
- Digest authentication (RFC 2617)
- RTCP Sender Report (SR) — periodic NTP/RTP timestamp mapping for latency measurement

**AI Analysis** *(requires OpenCV 4 + FFmpeg)*
- Face detection via OpenCV DNN (YuFaceDetectNet / RetinaFace ONNX)
- Face recognition via ArcFace ONNX — 512-D embeddings, cosine similarity
- Configurable analysis rate (default 5 fps) to balance accuracy vs. CPU load
- Live bounding box + name overlay on the outgoing RTSP stream

**Output**
- RTSP stream with real-time annotations
- RTMP push (via FFmpeg C API pipeline)
- REST HTTP API for querying current detections and event history
- JSON Lines event log (`events.jsonl`)

---

## Quick Start

### Dependencies

```bash
# Runtime + AI
sudo apt install libopencv-dev ffmpeg
# FFmpeg C API pipeline (for ffmpeg_streamer target)
sudo apt install libavformat-dev libavcodec-dev libavutil-dev libswscale-dev
```

### Build

```bash
# Make (primary)
make -j$(nproc)

# CMake (alternative, required for ffmpeg_streamer)
mkdir -p build && cd build
cmake .. && make -j$(nproc)
```

Build outputs go to `bin/` (Make) or `build/bin/` (CMake).

### Targets

| Target | Description |
|--------|-------------|
| `rtsp_server` | Basic RTSP server |
| `rtsp_pusher` | RTSP pusher (push to upstream server) |
| `rtsp_h264_file` | Serve H.264 file over RTSP |
| `rtsp_analysis_server` | Full pipeline: video source + AI + overlay + RTSP |
| `rtsp_edge_analysis_server` | CDN edge-scheduling prototype |
| `ffmpeg_streamer` | FFmpeg C API pipeline (demux→decode→AI→encode→RTSP/RTMP, audio+video) |

### Download AI Models

```bash
mkdir -p models
# Face detection (YuNet)
wget -O models/face_detection.onnx \
  https://github.com/opencv/opencv_zoo/raw/main/models/face_detection_yunet/face_detection_yunet_2023mar.onnx
# Face recognition (SFace / ArcFace)
wget -O models/face_recognition.onnx \
  https://github.com/opencv/opencv_zoo/raw/main/models/face_recognition_sface/face_recognition_sface_2021dec.onnx
```

### Run

```bash
# Stream a local H.264 file with AI analysis
./rtsp_analysis_server --source file --input test.h264 --port 8554 --http-port 8080

# USB camera
./rtsp_analysis_server --source camera --device 0 --port 8554 --http-port 8080

# Pull and re-stream an RTSP source
./rtsp_analysis_server --source rtsp --input rtsp://192.168.1.100:554/stream --port 8554

# Encode only, skip AI
./rtsp_analysis_server --source file --input test.h264 --no-ai --port 8554

# FFmpeg C API pipeline (audio+video, RTMP support)
./ffmpeg_streamer --input test.h264 --port 8554
./ffmpeg_streamer --input test.h264 --rtmp rtmp://localhost/live/test --port 8554
```

### Watch & Query

```bash
ffplay rtsp://localhost:8554/live            # play annotated stream

curl http://localhost:8080/api/current       # current frame faces
curl http://localhost:8080/api/status        # server uptime

# Register a face
curl -X POST http://localhost:8080/api/faces \
  -F "name=Alice" -F "image=@alice.jpg"
```

---

## Project Structure

```
src/
├── net/      Reactor networking (EventLoop, epoll, Channel, TcpServer)
├── xop/      RTSP/RTP protocol (RtspServer, MediaSession, H264Source, H265Source,
│             AACSource, G711ASource, VP8Source, RtcpMessage, SeiLatencyMarker)
├── ai/       AI analysis pipeline (VideoSource, FaceDetector, FaceRecognizer,
│             FaceDatabase, FrameAnalyzer, FrameOverlay, H264Encoder, HttpApiServer)
├── ffmpeg/   FFmpeg C API pipeline (FFmpegStreamer, StreamPipeline,
│             PipelineManager, RtspOutputAdapter, RtmpOutputAdapter,
│             MultiOutputAdapter, AudioOutputAdapter, FrameDropPolicy)
├── control/  Control plane (Classifier, Scheduler, PipelineRunner, StreamManager)
├── cdn_sim/  Edge node simulation (EdgeNode, EdgeNodePool, ThreadPool)
├── observe/  MetricsRegistry, LatencyTracer, PithyPrint
└── 3rdpart/  cpp-httplib (HTTP), md5
example/
├── rtsp_server.cpp                Basic RTSP server
├── rtsp_pusher.cpp                RTSP pusher
├── rtsp_h264_file.cpp             File streaming example
├── rtsp_analysis_server.cpp       Full AI analysis pipeline
├── rtsp_edge_analysis_server.cpp  CDN edge-scheduling prototype
└── ffmpeg_streamer.cpp            FFmpeg C API pipeline (audio+video, RTMP)
docs/
├── StreamSight项目架构说明文档.md   Architecture (Chinese)
├── StreamSight项目深度分析与优化方案.md  Optimization analysis
├── StreamSight现有系统延迟测试方案.md    Latency testing plan
├── cdn_sim_design.md               CDN simulation design
├── api.md                          REST API reference
├── setup.md                        Installation and run guide
├── interview.md                    Technical deep-dive Q&A
└── latency_testing_implementation.md  Latency tracer implementation
models/                             ONNX model files (download separately)
scripts/
└── tune_kernel.sh                  Kernel tuning for low-latency streaming
```

---

## REST API

| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/status` | Server uptime and port |
| GET | `/api/current` | Latest frame analysis result |
| GET | `/api/events?limit=N` | Recent detection events |
| GET | `/api/faces` | Registered face names |
| POST | `/api/faces` | Register face (multipart: `name` + `image`) |
| DELETE | `/api/faces/{name}` | Remove face |

Full documentation: [docs/api.md](docs/api.md)

---

## Requirements

| Component | Version |
|-----------|---------|
| Compiler | GCC 4.8+ / VS2015+ (C++11) |
| OpenCV | 4.x with DNN module |
| FFmpeg | 4.x+ (`ffmpeg` binary; libavformat/libavcodec/libavutil/libswscale dev for ffmpeg_streamer) |
| CMake | 3.10+ (for CMake build / ffmpeg_streamer target) |
| OS | Linux (epoll) / Windows (select) |

---

## Architecture

See [docs/StreamSight项目架构说明文档.md](docs/StreamSight项目架构说明文档.md) for the full system diagram and thread model.

---

## License

[MIT License](LICENSE)
