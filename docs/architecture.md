# 系统架构说明

## 总体架构

```
┌─────────────────────────────────────────────────────────────────┐
│                     rtsp_analysis_server                        │
│                                                                 │
│  ┌──────────────┐   ┌─────────────────┐   ┌─────────────────┐  │
│  │  VideoSource │   │   FrameAnalyzer  │   │  HttpApiServer  │  │
│  │  (采集线程)  │──▶│   (分析线程)    │──▶│  (HTTP 线程池)  │  │
│  └──────────────┘   └─────────────────┘   └─────────────────┘  │
│         │                   │                       │           │
│         │            ┌──────┴──────┐                │           │
│         │            │FaceDetector │                │           │
│         │            │FaceRecognizer│               │           │
│         │            │FaceDatabase │                │           │
│         │            └─────────────┘                │           │
│         │                   │                       │           │
│         ▼                   ▼                       │           │
│  ┌──────────────┐   ┌─────────────────┐             │           │
│  │ FrameOverlay │◀──│  AnalysisResult │             │           │
│  └──────────────┘   └─────────────────┘             │           │
│         │                   │                       │           │
│         ▼                   ▼                       │           │
│  ┌──────────────┐   ┌─────────────────┐             │           │
│  │ H264Encoder  │   │  EventLogger    │             │           │
│  │  (子进程     │   │  (events.jsonl) │             │           │
│  │   ffmpeg)    │   └─────────────────┘             │           │
│  └──────────────┘                                   │           │
│         │                                           │           │
│         ▼                                           │           │
│  ┌──────────────────────────────────────────────┐  │           │
│  │              xop::RtspServer                 │  │           │
│  │         (现有 RTSP/RTP 推流层)               │◀─┘           │
│  └──────────────────────────────────────────────┘              │
└─────────────────────────────────────────────────────────────────┘
```

## 目录结构

```
RtspServer/
├── src/
│   ├── net/          # 网络层 (Reactor, EventLoop, TcpServer) — 不修改
│   ├── xop/          # RTSP/RTP 协议层 — 不修改
│   └── ai/           # 新增 AI 分析层
│       ├── VideoSource.h         # 视频源抽象接口
│       ├── CameraSource.*        # USB/V4L2 摄像头
│       ├── FileSource.*          # 本地视频文件
│       ├── RtspPullSource.*      # RTSP 拉流
│       ├── H264Encoder.*         # H.264 软编码 (FFmpeg 子进程)
│       ├── FaceDetector.*        # 人脸检测 (OpenCV DNN)
│       ├── FaceRecognizer.*      # 人脸识别 (ArcFace ONNX)
│       ├── FaceDatabase.*        # 人脸特征库 (JSON 持久化)
│       ├── FrameAnalyzer.*       # 分析调度器 (频率控制 + 结果缓存)
│       ├── FrameOverlay.*        # 帧标注 (bounding box + 文字)
│       ├── HttpApiServer.*       # REST API (cpp-httplib)
│       └── EventLogger.*         # 事件 JSON 日志
├── example/
│   └── rtsp_analysis_server.cpp  # 完整流水线示例
├── models/
│   ├── face_detection.onnx       # 人脸检测模型
│   └── face_recognition.onnx     # 人脸识别模型 (ArcFace)
└── docs/
    ├── architecture.md           # 本文件
    ├── api.md                    # REST API 文档
    └── setup.md                  # 安装与运行指南
```

## 数据流

### 主流水线（帧级）

```
VideoSource::GrabFrame()
    │  cv::Mat (BGR)
    ▼
FrameAnalyzer::Analyze()
    ├── FaceDetector::Detect()      → vector<FaceBox>
    └── FaceRecognizer::Extract()
        └── FaceDatabase::Query()   → vector<FaceResult>
    │  AnalysisResult
    ▼
FrameOverlay::Draw()               → cv::Mat (标注后)
    │
    ▼
H264Encoder::EncodeFrame()         → H.264 NAL (via FFmpeg)
    │  callback(data, len, key)
    ▼
xop::RtspServer::PushFrame()       → RTSP/RTP 客户端
```

### 并行输出

```
AnalysisResult ──▶ EventLogger::Log()          → events.jsonl
             ──▶ HttpApiServer::UpdateResult()  → GET /api/current
             ──▶ HttpApiServer::AddEvent()      → GET /api/events
```

## 线程模型

| 线程 | 职责 |
|------|------|
| 主线程 | 初始化、信号处理、等待退出 |
| 流水线线程 | GrabFrame → Analyze → Overlay → Encode (串行) |
| FFmpeg 子进程 | H.264 编码，通过 pipe 与父进程通信 |
| FFmpeg 读取线程 | 读取 FFmpeg stdout，解析 NAL 单元并回调 |
| xop EventLoop 线程池 | RTSP 连接管理、RTP 分发 |
| HTTP 线程池 | cpp-httplib 内置，处理 REST 请求 |

## H264Encoder 实现原理

```
父进程写线程:                    子进程 (ffmpeg):
  EncodeFrame(mat)
  │ write(pipe_in, BGR_data)    stdin (pipe_in[0])
  ▼                                 │
  pipe_in[1] ────────────────────▶ ffmpeg -f rawvideo ...
                                    │ -f h264 pipe:1
  pipe_out[0] ◀──────────────────── stdout (pipe_out[1])
  │
  ▼
  ReadLoop() (后台线程)
  │  ParseAndDeliver(NAL)
  ▼
  callback(nal_data, len, is_key)
  │
  ▼
  RtspServer::PushFrame()
```
