# StreamSight

> AI 增强型直播流处理平台 — 自研 RTSP/RTP 协议栈 + FFmpeg C API 管线，
> 在实时音视频链路中集成帧级 AI 处理与直播特效能力。

[English](README.md)

---

## 项目简介

StreamSight 是一个自研的 AI 增强型直播流处理平台。它在进程内完成
"视频接入 → 解码 → AI 处理/特效叠加 → 编码 → 多协议输出"的全链路闭环。

**核心特色:**
- **自研 RTSP/RTP 协议栈（xop）**: 基于 Reactor 模式（epoll），支持 H.264/H.265/AAC，
  处理后视频可通过网络被任意 RTSP 客户端拉流播放
- **FFmpeg C API 进程内管线**: 3-stage 流水线（Demux+Decode → AI Process → Encode），
  替代 fork+pipe 子进程方案，RingBuffer 背压 + FrameDropPolicy 自适应丢帧
- **可扩展 EffectPlugin 体系**: 人脸检测识别（YuNet + ArcFace ONNX）作为首个插件 demo，
  后续可扩展水印、马赛克、安全检测、美颜等
- **RTMP 直播分发**: 内置 RTMP Push Client，对接外部 SRS/nginx-rtmp 实现大规模分发

```
Ingest → StreamPipeline (Demux+Decode → AI Process → Encode) → Output Adapters
              │                                      │
              │  Stage2: AI Process                   │
              │  ├── EffectChain (IEffectPlugin[])     │
              │  └── Content Understanding (future)    │
              │                                      │
              ▼                                      ▼
    RTSP 实时流输出 (自研协议栈)    RTMP Push (外部 SRS)
    局域网/跨网段客户端直接拉流     大规模直播分发
```

---

## 快速开始

### 安装依赖

```bash
# 运行时 + AI
sudo apt install libopencv-dev ffmpeg
# FFmpeg C API 管线
sudo apt install libavformat-dev libavcodec-dev libavutil-dev libswscale-dev
```

### 编译

```bash
mkdir -p build && cd build
cmake .. && make -j$(nproc)
```

### 下载 AI 模型

```bash
mkdir -p models
wget -O models/face_detection.onnx \
  https://github.com/opencv/opencv_zoo/raw/main/models/face_detection_yunet/face_detection_yunet_2023mar.onnx
wget -O models/face_recognition.onnx \
  https://github.com/opencv/opencv_zoo/raw/main/models/face_recognition_sface/face_recognition_sface_2021dec.onnx
```

### 运行

```bash
# 主入口: 文件输入 + AI 分析 + RTSP 输出
./build/bin/ffmpeg_streamer --input ../pic/test.mp4 --port 8554

# 摄像头输入
./build/bin/ffmpeg_streamer --source camera --input 0 --port 8554

# 跳过 AI 处理
./build/bin/ffmpeg_streamer --input ../pic/test.mp4 --no-ai --port 8554

# RTMP 输出 (需要先启动 SRS，见下方说明)
./build/bin/ffmpeg_streamer --input ../pic/test.mp4 --rtmp rtmp://localhost:1935/live/stream --port 8554
```

### 播放与查询

```bash
# RTSP 播放（直连，无需外部服务器）
ffplay rtsp://localhost:8554/live

# 局域网内其他机器播放
ffplay rtsp://192.168.1.x:8554/live

# HTTP API 查询
curl http://localhost:8080/api/current       # 当前帧检测结果
curl http://localhost:8080/api/status        # 服务状态

# 注册人脸
curl -X POST http://localhost:8080/api/faces \
  -F "name=张三" -F "image=@photo.jpg"
```

---

## RTMP 分发说明

StreamSight 的 `RtmpOutputAdapter` 是 RTMP Push Client，用于将处理后流推送到外部 RTMP Server。

**rtmp://localhost:8888/live/test → Connection refused**
这是因为本机没有 RTMP Server 监听 8888 端口。需要先启动外部 RTMP Server。

```bash
# 启动 SRS
docker-compose up -d srs

# StreamSight 推送处理后流到 SRS
./build/bin/ffmpeg_streamer --input ../pic/test.mp4 --rtmp rtmp://localhost:1935/live/stream --port 8554

# RTMP 播放
ffplay rtmp://localhost:1935/live/stream
```

详细说明见 [docs/rtmp-distribution.md](docs/rtmp-distribution.md).

---

## EffectPlugin 插件架构

```cpp
// IEffectPlugin: 所有特效/分析插件的统一接口
class IEffectPlugin {
    virtual std::string Name() const = 0;
    virtual bool Process(uint8_t* bgr, int w, int h, int linesize,
                         EffectResult* result) = 0;
};

// EffectChain: 有序执行多个插件
EffectChain chain;
chain.AddPlugin(std::make_shared<FaceRecognitionPlugin>(...));
chain.ProcessFrame(bgr_data, width, height, linesize, results);
```

当前内置: `FaceRecognitionPlugin` (人脸检测 + 识别 + 框选叠加)

---

## 目录结构

```
src/
├── net/       Reactor 网络框架 (EventLoop, epoll, TcpServer, RingBuffer)
├── xop/       RTSP/RTP 协议实现 (RtspServer, MediaSession, H264Source...)
├── ffmpeg/    FFmpeg C API 管线 (StreamPipeline, IOutputAdapter...)
├── effect/    EffectPlugin 插件体系 (IEffectPlugin, EffectChain, FaceRecognitionPlugin)
├── ai/        AI 模型加载 (FaceDetector, FaceRecognizer, FaceDatabase)
├── control/   流管理 + 调度
├── observe/   可观测性 (MetricsRegistry, LatencyTracer)
└── cdn_sim/   CDN 边缘模拟

example/
├── ffmpeg_streamer.cpp     ★ 主入口: 完整 AI 管线 + RTSP/RTMP 输出
├── rtsp_analysis_server.cpp  LEGACY: 旧 fork+pipe 路径
└── ...

docs/
├── rtmp-distribution.md     RTMP 分发架构说明
├── api.md                   REST API 文档
└── ...
```

---

## REST API 接口

| 方法 | 路径 | 描述 |
|------|------|------|
| GET | `/api/status` | 服务运行状态 |
| GET | `/api/current` | 当前帧分析结果 |
| GET | `/api/events?limit=N` | 最近检测事件 |
| GET | `/api/faces` | 已注册人脸列表 |
| POST | `/api/faces` | 注册人脸 (multipart: name + image) |
| DELETE | `/api/faces/{name}` | 删除人脸 |

---

## 开发路线

- **Phase 1** (当前): 定位修正 + EffectPlugin 接口 + 代码债务清理
- **Phase 2**: StreamSession 抽象 + HTTP API 平台化 + Effect 动态配置
- **Phase 3**: 视频摘要 + Content Understanding + Agent 工具接口预留

---

## 环境要求

| 组件 | 版本 |
|------|------|
| 编译器 | GCC 4.8+ / Clang (C++11) |
| OpenCV | 4.x，含 DNN 模块 |
| FFmpeg | 4.x+ (libavformat/libavcodec/libavutil/libswscale) |
| CMake | 3.10+ |
| 操作系统 | Linux (epoll) / Windows (select) |

---

## 许可证

[MIT License](LICENSE)