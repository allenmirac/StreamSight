# StreamSight

> 基于 C++11 的实时 RTSP 流媒体服务器，集成 AI 人脸检测与识别能力。

[English](README.md)

---

## 项目简介

StreamSight 在高性能 RTSP/RTP 推流服务器的基础上，扩展了 AI 视频分析流水线。摄像头、本地视频文件或远程 RTSP 拉流的原始画面，经过实时人脸检测与识别后，将标注结果叠加到 H.264 码流中推送给 RTSP 客户端，并通过 REST API 提供实时查询接口。

```
视频源 → AI 分析 → 画面叠加 → H.264 编码 → RTSP/RTP → 客户端
             ↓
       REST API  +  JSON 事件日志
```

---

## 功能特性

**流媒体基础**
- 基于 Reactor 模式（Linux epoll / Windows select）的 RTSP 服务器与推流器
- 支持 H.264、H.265、G711A、AAC、VP8 音视频格式
- 音视频混合推流（ffmpeg_streamer 集成 AAC 音频）
- 支持单播（RTP over TCP、RTP over UDP）和组播
- 支持摘要认证（Digest Authentication，RFC 2617）
- RTCP Sender Report (SR) — 周期性 NTP/RTP 时间戳映射，支持延迟测量

**AI 分析** *(需要 OpenCV 4 + FFmpeg)*
- 人脸检测：OpenCV DNN 加载 ONNX 模型（YuFaceDetectNet / RetinaFace）
- 人脸识别：ArcFace ONNX，512 维特征向量，余弦相似度比对
- 可配置分析帧率（默认 5 fps），在精度与 CPU 占用之间灵活调节
- 实时在 RTSP 推流画面中叠加人脸框和姓名标签

**结果输出**
- 带标注的 RTSP 实时流
- RTMP 推流（通过 FFmpeg C API 管线）
- REST HTTP API（查询当前人脸、历史事件、人脸库管理）
- JSON Lines 格式事件日志（`events.jsonl`）

---

## 快速开始

### 安装依赖

```bash
# 运行时 + AI
sudo apt install libopencv-dev ffmpeg
# FFmpeg C API 管线（ffmpeg_streamer 目标需要）
sudo apt install libavformat-dev libavcodec-dev libavutil-dev libswscale-dev
```

### 编译

```bash
# Make（主要方式）
make -j$(nproc)

# CMake（可选，ffmpeg_streamer 需要 CMake）
mkdir -p build && cd build
cmake .. && make -j$(nproc)
```

编译产物输出到 `bin/`（Make）或 `build/bin/`（CMake）。

### 编译目标

| 目标 | 说明 |
|--------|-------------|
| `rtsp_server` | 基础 RTSP 服务器 |
| `rtsp_pusher` | RTSP 推流器（推送到上游服务器） |
| `rtsp_h264_file` | H.264 文件 RTSP 推流 |
| `rtsp_analysis_server` | 完整管线：视频源 + AI + 叠加 + RTSP |
| `rtsp_edge_analysis_server` | CDN 边缘调度原型 |
| `ffmpeg_streamer` | FFmpeg C API 管线（解封装→解码→AI→编码→RTSP/RTMP，音视频） |

### 下载 AI 模型

```bash
mkdir -p models
# 人脸检测模型（YuNet）
wget -O models/face_detection.onnx \
  https://github.com/opencv/opencv_zoo/raw/main/models/face_detection_yunet/face_detection_yunet_2023mar.onnx
# 人脸识别模型（SFace / ArcFace）
wget -O models/face_recognition.onnx \
  https://github.com/opencv/opencv_zoo/raw/main/models/face_recognition_sface/face_recognition_sface_2021dec.onnx
```

### 运行

```bash
# 本地视频文件模式（含 AI 分析）
./rtsp_analysis_server --source file --input ../pic/test.mp4 --port 8554 --http-port 8080

# USB 摄像头模式
./rtsp_analysis_server --source camera --device 0 --port 8554 --http-port 8080

# RTSP 拉流转发模式
./rtsp_analysis_server --source rtsp --input rtsp://192.168.1.100:554/stream --port 8554

# 仅编码推流，跳过 AI
./rtsp_analysis_server --source file --input test.h264 --no-ai --port 8554

# FFmpeg C API 管线（音视频 + RTMP 支持）
./ffmpeg_streamer --input test.h264 --port 8554
./ffmpeg_streamer --input test.h264 --rtmp rtmp://localhost/live/test --port 8554
```

### 播放与查询

```bash
ffplay rtsp://localhost:8554/live            # 播放带标注的 RTSP 流

curl http://localhost:8080/api/current       # 当前帧人脸识别结果
curl http://localhost:8080/api/status        # 服务运行状态

# 注册人脸
curl -X POST http://localhost:8080/api/faces \
  -F "name=张三" -F "image=@photo.jpg"
```

---

## 目录结构

```
src/
├── net/      网络层（EventLoop、epoll、Channel、TcpServer）
├── xop/      RTSP/RTP 协议层（RtspServer、MediaSession、H264Source、H265Source、
│             AACSource、G711ASource、VP8Source、RtcpMessage、SeiLatencyMarker）
├── ai/       AI 分析层（VideoSource、FaceDetector、FaceRecognizer、
│             FaceDatabase、FrameAnalyzer、FrameOverlay、H264Encoder、HttpApiServer）
├── ffmpeg/   FFmpeg C API 管线（FFmpegStreamer、RtspOutputAdapter、
│             RtmpOutputAdapter、MultiOutputAdapter、AudioOutputAdapter）
├── control/  控制层（Classifier、Scheduler、PipelineRunner、StreamManager）
├── cdn_sim/  CDN 边缘模拟（EdgeNode、EdgeNodePool、ThreadPool）
├── observe/  MetricsRegistry、LatencyTracer
└── 3rdpart/  cpp-httplib（HTTP）、md5
example/
├── rtsp_server.cpp               基础 RTSP 服务器
├── rtsp_pusher.cpp               RTSP 推流器
├── rtsp_h264_file.cpp            本地文件推流
├── rtsp_analysis_server.cpp      完整 AI 分析管线
├── rtsp_edge_analysis_server.cpp CDN 边缘调度原型
└── ffmpeg_streamer.cpp           FFmpeg C API 管线（音视频 + RTMP）
docs/
├── StreamSight项目架构说明文档.md   架构说明（中文）
├── StreamSight项目深度分析与优化方案.md  优化分析
├── StreamSight现有系统延迟测试方案.md    延迟测试方案
├── cdn_sim_design.md               CDN 模拟调度设计
├── api.md                          REST API 接口文档
├── setup.md                        安装与运行指南
├── interview.md                    技术深挖 Q&A
└── latency_testing_implementation.md  延迟追踪实现
models/                             ONNX 模型文件（需单独下载）
```

---

## REST API 接口

| 方法 | 路径 | 描述 |
|------|------|------|
| GET | `/api/status` | 服务运行状态与端口 |
| GET | `/api/current` | 当前帧分析结果（人脸列表） |
| GET | `/api/events?limit=N` | 最近 N 条检测事件 |
| GET | `/api/faces` | 已注册的人脸姓名列表 |
| POST | `/api/faces` | 注册人脸（multipart：`name` + `image`） |
| DELETE | `/api/faces/{name}` | 删除人脸 |

完整文档见 [docs/api.md](docs/api.md)

---

## 环境要求

| 组件 | 版本 |
|------|------|
| 编译器 | GCC 4.8+ / VS2015+（C++11） |
| OpenCV | 4.x，含 DNN 模块 |
| FFmpeg | 4.x（`ffmpeg` 命令；ffmpeg_streamer 需要 libavformat/libavcodec/libavutil/libswscale-dev） |
| CMake | 3.10+（CMake 构建 / ffmpeg_streamer 目标需要） |
| 操作系统 | Linux（epoll）/ Windows（select） |

---

## 架构说明

完整系统架构图和线程模型见 [docs/architecture.md](docs/architecture.md)。

---

## 整体框架（原始网络层）

![image](https://github.com/PHZ76/RtspServer/blob/master/pic/1.pic.JPG)

---

## 许可证

[MIT License](LICENSE)
