# 安装与运行指南

## 1. 依赖安装

### 系统要求

- Linux (Ubuntu 20.04+ / Debian 11+)
- GCC 7+ (支持 C++11)
- FFmpeg（用于 H.264 编码）
- OpenCV 4.x（含 DNN 模块）
- CMake 3.10+（CMake 构建可选）

### 安装依赖

```bash
# 基础构建工具
sudo apt update
sudo apt install -y build-essential pkg-config cmake

# FFmpeg（H.264 编码必需）
sudo apt install -y ffmpeg

# FFmpeg 开发库（ffmpeg_streamer 目标需要）
sudo apt install -y libavformat-dev libavcodec-dev libavutil-dev libswscale-dev

# OpenCV 4（含 DNN 模块）
sudo apt install -y libopencv-dev

# 验证 OpenCV 版本
pkg-config --modversion opencv4
```

### 验证 FFmpeg 与 OpenCV

```bash
ffmpeg -version | head -1
# 应输出: ffmpeg version 4.x.x ...

pkg-config --libs opencv4
# 应输出: -lopencv_core -lopencv_dnn ... 等
```

---

## 2. 获取 AI 模型

### 人脸检测模型（YuFaceDetectNet / ONNX）

```bash
mkdir -p models

# 下载 YuFaceDetectNet (320×320 输入)
wget -O models/face_detection.onnx \
  https://github.com/opencv/opencv_zoo/raw/main/models/face_detection_yunet/face_detection_yunet_2023mar.onnx
```

> **备选**：也可使用 RetinaFace-MobileNet ONNX 模型。
> 若模型输出格式不同，需调整 `FaceDetector::PostProcess()` 中的解析逻辑。

### 人脸识别模型（ArcFace / ONNX）

```bash
# 下载 ArcFace (InsightFace MobileNet 版本)
wget -O models/face_recognition.onnx \
  https://github.com/opencv/opencv_zoo/raw/main/models/face_recognition_sface/face_recognition_sface_2021dec.onnx
```

> **注意**：若使用 InsightFace 的 ArcFace（512-D 输出），需确认模型输入为 112×112 RGB，
> 与 `FaceRecognizer.cpp` 的预处理一致（`/127.5 - 1`）。

---

## 3. 编译

### CMake 构建（主构建方式）

```bash
mkdir -p build && cd build
cmake .. && make -j$(nproc)
# 编译产物在 build/bin/
```

CMake 会自动检测 FFmpeg 和 OpenCV 依赖。若 FFmpeg 开发库未安装，`ffmpeg_streamer`、`test_*` 和 `streamsight-stress` 目标将被跳过。

### 构建选项

```bash
# 启用测试目标（test_smoke 统一测试入口）
cmake .. -DBUILD_TESTS=ON && make -j$(nproc)
```

### 编译产物

| 目标 | 说明 |
|------|------|
| `ffmpeg_streamer` | ★ 主入口：StreamSession + API server（音视频 + RTMP） |
| `streamsight-stress` | 压力测试工具（多流并发、性能基线） |
| `test_event_bus` | EventBus 单元测试 |
| `test_effect_factory` | EffectFactory 单元测试 |
| `test_stream_session` | StreamSession 单元测试 |
| `test_api_server` | StreamApiServer 单元测试 |
| `test_smoke` | 统一测试入口（需 BUILD_TESTS=ON） |

---

## 4. 运行

### 模式一：本地视频文件（ffmpeg_streamer，推荐）

```bash
# 基础 RTSP 推流（含音频）
./build/bin/ffmpeg_streamer --input pic/test.mp4 --port 8554

# RTSP + RTMP 双输出
./build/bin/ffmpeg_streamer --input pic/test.mp4 --rtmp rtmp://localhost:1935/live/test --port 8554

# USB 摄像头（含 AI）
./build/bin/ffmpeg_streamer --source camera --input 0 --port 8554

# 跳过 AI
./build/bin/ffmpeg_streamer --input pic/test.mp4 --no-ai --port 8554
```

### 禁用 AI（仅转码推流）

```bash
./build/bin/ffmpeg_streamer --input pic/test.mp4 --no-ai --port 8554
```

---

## 5. 验证

### 播放 RTSP 流

```bash
# ffplay
ffplay rtsp://localhost:8554/live

# VLC
vlc rtsp://localhost:8554/live
```

### 查询 REST API

```bash
# 服务状态
curl http://localhost:8080/api/status

# 当前帧分析结果
curl http://localhost:8080/api/current | python3 -m json.tool

# 最近 10 条事件
curl "http://localhost:8080/api/events?limit=10"

# 列出所有 session
curl http://localhost:8080/api/v1/sessions

# session 详情
curl http://localhost:8080/api/v1/sessions/session_1
```

### 运行压力测试

```bash
# 使用 stress_config.yaml 配置
./build/bin/streamsight-stress --config scripts/stress_config.yaml

# 或使用 Python 脚本
python3 scripts/stress_test.py --streams 4 --duration 60
```

### 注册人脸

```bash
# 准备一张清晰正面人脸照片
curl -X POST http://localhost:8080/api/faces \
  -F "name=Alice" \
  -F "image=@/path/to/alice.jpg"

# 确认注册
curl http://localhost:8080/api/faces
```

### 查看事件日志

```bash
tail -f events.jsonl | python3 -c "
import sys, json
for line in sys.stdin:
    obj = json.loads(line)
    for f in obj['faces']:
        print(f'[{obj[\"ts\"]}] {f[\"name\"]} conf={f[\"conf\"]:.2f}')
"
```

---

## 6. 配置选项速查

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `--source` | `file` | 输入类型：`file`（文件/URL）或 `camera`（v4l2） |
| `--input` | `test.h264` | 输入 URL（文件路径 / 摄像头索引 / `rtsp://...`） |
| `--width` | `640` | 输出宽度（像素） |
| `--height` | `480` | 输出高度（像素） |
| `--fps` | `25` | 帧率 |
| `--port` | `8554` | RTSP 端口 |
| `--http-port` | `8080` | HTTP API 端口 |
| `--suffix` | `live` | RTSP 路径后缀 |
| `--detect-model` | `models/face_detection.onnx` | 检测模型路径 |
| `--recog-model` | `models/face_recognition.onnx` | 识别模型路径 |
| `--db` | `faces.json` | 人脸数据库路径 |
| `--log` | `events.jsonl` | 事件日志路径 |
| `--analyze-fps` | `5` | AI 分析帧率（降低此值可减少 CPU 占用） |
| `--no-ai` | — | 禁用 AI，仅编码推流 |
| `--no-audio` | — | 禁用音频 |
| `--rtmp` | — | RTMP 推流地址（如 rtmp://localhost:1935/live/stream） |
| `--bitrate` | `2000000` | 编码码率（bps） |
| `--threads` | `2` | 编码线程数 |
| `--pipeline-mode` | `serial` | 管线模式：`serial`（单线程）或 `parallel`（3-stage） |
| `--ringbuf-size` | `4` | RingBuffer 大小（parallel 模式） |
| `--max-frame-age-ms` | `500` | 帧最大存活时间（背压修剪） |
| `--time-window-ms` | `0` | 滑动时间窗口宽度（背压修剪） |
| `--eventloop-threads` | `2` | 进程级 EventLoop 线程数 |

### 延迟追踪环境变量

| 变量 | 默认值 | 说明 |
|------|--------|------|
| `STREAMSIGHT_LATENCY_ENABLE` | (空) | 设置为 `1` 开启延迟追踪 |
| `STREAMSIGHT_LATENCY_LOG` | `runtime/latency_events.jsonl` | JSONL 日志输出路径 |
| `STREAMSIGHT_LATENCY_BUFFER_SIZE` | `10000` | 内存环形缓冲区大小 |

---

## 7. 常见问题

**Q: 编译时找不到 OpenCV**

```
pkg-config --exists opencv4 || echo "OpenCV 4 not found"
# 若未找到，尝试：
sudo apt install libopencv-dev
# 或从源码编译 OpenCV 4
```

**Q: 端口 554 权限被拒**

```bash
# 使用非特权端口
./build/bin/ffmpeg_streamer --input pic/test.mp4 --port 8554

# 或给二进制添加 CAP_NET_BIND_SERVICE
sudo setcap 'cap_net_bind_service=+eip' ./build/bin/ffmpeg_streamer
```

**Q: AI 模型加载失败**

程序会打印警告并继续运行，但不做 AI 分析。请确认：
1. `models/` 目录下存在对应 `.onnx` 文件
2. OpenCV 编译时包含 DNN 模块：`pkg-config --libs opencv4 | grep dnn`

**Q: ffplay 播放花屏**

H.264 流可能在 P 帧前缺少 SPS/PPS。检查 FFmpeg GOP 设置（`-g` 参数），确保关键帧间隔不超过 2 秒。
