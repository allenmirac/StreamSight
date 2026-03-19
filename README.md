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
- H.264 / H.265 / G711A / AAC codec support
- Unicast (RTP over TCP, RTP over UDP) and multicast
- Digest authentication (RFC 2617)

**AI Analysis** *(requires OpenCV 4 + FFmpeg)*
- Face detection via OpenCV DNN (YuFaceDetectNet / RetinaFace ONNX)
- Face recognition via ArcFace ONNX — 512-D embeddings, cosine similarity
- Configurable analysis rate (default 5 fps) to balance accuracy vs. CPU load
- Live bounding box + name overlay on the outgoing RTSP stream

**Output**
- RTSP stream with real-time annotations
- REST HTTP API for querying current detections and event history
- JSON Lines event log (`events.jsonl`)

---

## Quick Start

### Dependencies

```bash
sudo apt install libopencv-dev ffmpeg
```

### Build

```bash
make -j$(nproc)          # builds all targets including rtsp_analysis_server
```

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
├── xop/      RTSP/RTP protocol layer (RtspServer, MediaSession, H264Source)
└── ai/       AI analysis pipeline (VideoSource, FaceDetector, H264Encoder, …)
example/
├── rtsp_server.cpp             Basic RTSP server
├── rtsp_h264_file.cpp          File streaming example
└── rtsp_analysis_server.cpp    Full AI analysis pipeline
docs/
├── architecture.md             System diagram and data flow
├── api.md                      REST API reference
├── setup.md                    Installation and run guide
└── interview.md                Technical deep-dive Q&A
models/                         ONNX model files (download separately)
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
| FFmpeg | 4.x (`ffmpeg` binary in PATH) |
| OS | Linux (epoll) / Windows (select) |

---

## Architecture

See [docs/architecture.md](docs/architecture.md) for the full system diagram and thread model.

---

## License

[MIT License](LICENSE)
