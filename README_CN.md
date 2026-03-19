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
- 支持 H.264、H.265、G711A、AAC 音视频格式
- 支持单播（RTP over TCP、RTP over UDP）和组播
- 支持摘要认证（Digest Authentication，RFC 2617）

**AI 分析** *(需要 OpenCV 4 + FFmpeg)*
- 人脸检测：OpenCV DNN 加载 ONNX 模型（YuFaceDetectNet / RetinaFace）
- 人脸识别：ArcFace ONNX，512 维特征向量，余弦相似度比对
- 可配置分析帧率（默认 5 fps），在精度与 CPU 占用之间灵活调节
- 实时在 RTSP 推流画面中叠加人脸框和姓名标签

**结果输出**
- 带标注的 RTSP 实时流
- REST HTTP API（查询当前人脸、历史事件、人脸库管理）
- JSON Lines 格式事件日志（`events.jsonl`）

---

## 快速开始

### 安装依赖

```bash
sudo apt install libopencv-dev ffmpeg
```

### 编译

```bash
make -j$(nproc)          # 编译所有目标，含 rtsp_analysis_server
```

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
./rtsp_analysis_server --source file --input test.h264 --port 8554 --http-port 8080

# USB 摄像头模式
./rtsp_analysis_server --source camera --device 0 --port 8554 --http-port 8080

# RTSP 拉流转发模式
./rtsp_analysis_server --source rtsp --input rtsp://192.168.1.100:554/stream --port 8554

# 仅编码推流，跳过 AI
./rtsp_analysis_server --source file --input test.h264 --no-ai --port 8554
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
├── xop/      RTSP/RTP 协议层（RtspServer、MediaSession、H264Source）
└── ai/       AI 分析层（VideoSource、FaceDetector、H264Encoder 等）
example/
├── rtsp_server.cpp             基础 RTSP 服务器示例
├── rtsp_h264_file.cpp          本地文件推流示例
└── rtsp_analysis_server.cpp    完整 AI 分析流水线示例
docs/
├── architecture.md             系统架构图与数据流说明
├── api.md                      REST API 接口文档
├── setup.md                    安装与运行指南
└── interview.md                技术深挖 Q&A（面试总结）
models/                         ONNX 模型文件（需单独下载）
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
| FFmpeg | 4.x（`ffmpeg` 命令在 PATH 中） |
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
