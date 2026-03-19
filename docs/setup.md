# 安装与运行指南

## 1. 依赖安装

### 系统要求

- Linux (Ubuntu 20.04+ / Debian 11+)
- GCC 7+ (支持 C++11)
- FFmpeg（用于 H.264 编码）
- OpenCV 4.x（含 DNN 模块）

### 安装依赖

```bash
# 基础构建工具
sudo apt update
sudo apt install -y build-essential pkg-config

# FFmpeg（H.264 编码必需）
sudo apt install -y ffmpeg

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

```bash
# 克隆（若尚未克隆）
git clone <repo_url>
cd RtspServer

# 编译所有目标（包含 rtsp_analysis_server）
make -j$(nproc)

# 仅编译 AI 分析服务器
make rtsp_analysis_server -j$(nproc)

# 调试模式
make V=1 -j$(nproc)
```

编译产物：

```
rtsp_server            # 原有基础服务器
rtsp_pusher            # 原有推流工具
rtsp_h264_file         # 原有文件推流
rtsp_analysis_server   # 新增 AI 分析服务器
```

---

## 4. 运行

### 模式一：本地视频文件

```bash
# 需要 root 或 CAP_NET_BIND_SERVICE 绑定 554 端口
# 或修改 --port 为 8554 等非特权端口

./rtsp_analysis_server \
  --source file \
  --input test.h264 \
  --width 640 --height 480 --fps 25 \
  --port 8554 \
  --http-port 8080
```

### 模式二：USB 摄像头

```bash
./rtsp_analysis_server \
  --source camera \
  --device 0 \
  --width 1280 --height 720 --fps 30 \
  --port 8554 \
  --http-port 8080
```

### 模式三：RTSP 拉流转发

```bash
./rtsp_analysis_server \
  --source rtsp \
  --input rtsp://192.168.1.100:554/stream \
  --port 8554 \
  --http-port 8080
```

### 禁用 AI（仅转码推流）

```bash
./rtsp_analysis_server \
  --source file --input test.h264 \
  --no-ai --port 8554
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
| `--source` | `file` | 视频源：`camera` / `file` / `rtsp` |
| `--device` | `0` | 摄像头设备索引 |
| `--input` | `test.h264` | 文件路径或 RTSP URL |
| `--width` | `640` | 输出宽度（像素） |
| `--height` | `480` | 输出高度（像素） |
| `--fps` | `25` | 帧率 |
| `--port` | `554` | RTSP 端口 |
| `--http-port` | `8080` | HTTP API 端口 |
| `--suffix` | `live` | RTSP 路径后缀 |
| `--detect-model` | `models/face_detection.onnx` | 检测模型路径 |
| `--recog-model` | `models/face_recognition.onnx` | 识别模型路径 |
| `--db` | `faces.json` | 人脸数据库路径 |
| `--log` | `events.jsonl` | 事件日志路径 |
| `--analyze-fps` | `5` | AI 分析帧率（降低此值可减少 CPU 占用） |
| `--no-ai` | — | 禁用 AI，仅编码推流 |

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
./rtsp_analysis_server --port 8554

# 或给二进制添加 CAP_NET_BIND_SERVICE
sudo setcap 'cap_net_bind_service=+eip' ./rtsp_analysis_server
```

**Q: AI 模型加载失败**

程序会打印警告并继续运行，但不做 AI 分析。请确认：
1. `models/` 目录下存在对应 `.onnx` 文件
2. OpenCV 编译时包含 DNN 模块：`pkg-config --libs opencv4 | grep dnn`

**Q: ffplay 播放花屏**

H.264 流可能在 P 帧前缺少 SPS/PPS。检查 FFmpeg GOP 设置（`-g` 参数），确保关键帧间隔不超过 2 秒。
