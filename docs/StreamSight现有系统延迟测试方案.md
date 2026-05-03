# StreamSight 现有系统延迟测试方案

## 1. 测试目标

StreamSight 是一个融合了视频采集、AI 分析、H.264 编码、RTSP 分发和 CDN 边缘调度的实时流媒体系统。延迟是衡量其实时性的核心指标，直接影响用户体验和系统可用性。

本次测试的目标是：

1. **建立延迟基线**：量化当前系统在不同场景下的端到端延迟和各模块耗时，形成可复现的基准数据。
2. **定位延迟瓶颈**：通过模块级埋点数据，识别延迟贡献最大的环节（采集、AI 推理、编码、网络发送、客户端缓冲或调度排队）。
3. **验证调度开销**：评估 CDN 边缘调度模块引入的额外延迟，确认控制平面的性能代价在可接受范围内。
4. **支撑后续优化**：为 AI 推理异步化、编码管线独立化、帧丢弃策略等优化方向提供量化依据。

测试覆盖的延迟类型包括：端到端延迟、采集延迟、AI 分析延迟（检测 + 识别）、编码延迟、RTSP 分发延迟、CDN 边缘转发延迟、HTTP API 响应延迟，以及各环节的组合延迟。

---

## 2. 延迟指标定义

| 指标名称 | 符号 | 定义 | 测量方式 |
|----------|------|------|----------|
| 端到端延迟 | T_e2e | 视频帧在源端产生的时刻到客户端解码显示该帧的时刻之差 | 画面时间戳对比法或客户端接收时间戳减去源端采集时间戳 |
| 采集延迟 | T_cap | 调用 GrabFrame() 到获取到 cv::Mat 帧数据的耗时 | VideoSource::GrabFrame() 前后埋点 |
| AI 检测延迟 | T_det | 单帧进入 FaceDetector::Detect() 到返回人脸框列表的耗时 | FaceDetector 推理前后埋点 |
| AI 识别延迟 | T_rec | 单个人脸区域进入 FaceRecognizer::Extract() 到返回特征向量的耗时 | FaceRecognizer 推理前后埋点 |
| 叠加绘制延迟 | T_ovl | FrameOverlay::Draw() 的耗时 | Draw() 调用前后埋点 |
| 编码延迟 | T_enc | 原始帧进入 H264Encoder::EncodeFrame() 到编码数据通过回调输出的耗时（含 FFmpeg 子进程 pipe 通信） | EncodeFrame() 入口到 OutputCallback 触发的时间差 |
| RTSP 分发延迟 | T_rtsp | 编码数据推入 RTSP 发送路径（PushFrame）到 RTP 包通过 socket 发出的耗时 | PushFrame 到 RtpConnection::SendRtpPacket 完成 |
| 网络传输延迟 | T_net | RTP 包从服务端发出到客户端接收的时间 | 需要服务端和客户端时钟同步后对比 |
| CDN 调度延迟 | T_sched | StreamTask 进入 Scheduler::SelectNode() 到 EdgeNode::Submit() 完成的时间 | 调度流程前后埋点 |
| 边缘排队延迟 | T_queue | 任务提交到 EdgeNode 线程池到实际开始执行的等待时间 | Submit() 完成到 PipelineRunner::Run() 首行代码执行 |
| HTTP API 延迟 | T_api | HTTP 请求到达 HttpApiServer 到响应返回的耗时 | 请求处理函数入口到出口 |
| 平均延迟 | T_avg | 所有采样帧延迟的算术平均值 | 统计计算 |
| P95 延迟 | T_p95 | 95% 的帧延迟低于该值 | 排序后取 95 百分位 |
| P99 延迟 | T_p99 | 99% 的帧延迟低于该值 | 排序后取 99 百分位 |
| 最大延迟 | T_max | 采样期间出现的最大延迟 | 取最大值 |
| 抖动 (Jitter) | J | 相邻两帧延迟差值的标准差，反映延迟的稳定性 | 计算帧间延迟差的标准差 |

**端到端延迟的组成关系**：

```
T_e2e = T_cap + T_det + T_rec + T_ovl + T_enc + T_rtsp + T_net + T_client_buf
```

其中 T_client_buf 为客户端的接收缓冲和解码显示延迟，不在服务端直接测量范围内，但会通过端到端延迟反推。当关闭 AI 分析时，T_det、T_rec、T_ovl 均为 0。

---

## 3. 推荐测试方法

### 3.1 端到端延迟测试（画面时间戳对比法）

**原理**：在视频源画面中叠加当前系统时间（精确到毫秒），RTSP 客户端拉流播放时，截屏对比画面中的时间戳与客户端本地时钟，差值即为端到端延迟。

**实施步骤**：
1. 编写一个简单的 OpenCV 程序，通过 FileSource 或 CameraSource 读取帧后，在帧上用 `cv::putText()` 叠加当前 `std::chrono::steady_clock` 时间戳（毫秒级），再送入编码管线。
2. 在服务端使用 `--source file --input test.h264` 或 `--source camera` 启动 `rtsp_analysis_server`。
3. 客户端使用 `ffplay` 拉流，通过截图工具或屏幕录制，对比画面中显示的时间戳与客户端系统时钟。
4. 连续采样不少于 100 帧，计算平均延迟和 P95/P99 延迟。

**优点**：无需修改客户端代码，直观可靠，适用于端到端验证。
**局限**：依赖目视或 OCR 读取画面时间戳，精度受截图时机影响，建议结合代码埋点交叉验证。

### 3.2 代码埋点测试

在每个关键模块的入口和出口使用 `std::chrono::steady_clock::now()` 记录时间戳，计算单步耗时。

**统一埋点宏示例**：

```cpp
#include <chrono>
#include <string>
#include <vector>

struct LatencySample {
    uint64_t frame_id;
    std::string module;
    int64_t cost_us;
};

// 建议在 MetricsRegistry 或单独的 LatencyCollector 中维护
// std::vector<LatencySample> g_latency_samples;
// std::mutex g_latency_mutex;

#define PROFILE_SCOPE(frame_id, module_name) \
    auto _profile_start = std::chrono::steady_clock::now(); \
    auto _profile_fid = frame_id; \
    auto _profile_mod = module_name; \
    struct _ProfileGuard { \
        decltype(_profile_start) start; \
        uint64_t fid; \
        std::string mod; \
        ~_ProfileGuard() { \
            auto end = std::chrono::steady_clock::now(); \
            auto cost = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count(); \
            /* 写入全局 collector 或直接 fprintf 到日志文件 */ \
        } \
    } _profile_guard{_profile_start, _profile_fid, _profile_mod};
```

**关键埋点位置**（详见第 6 节）覆盖从采集到分发的完整链路。

### 3.3 日志方式测试

在代码埋点的基础上，将每帧的完整延迟链路输出为 JSONL 格式日志，便于后续 Python 脚本统计分析。

建议新增 `LatencyLogger` 类（可参考现有 `src/ai/EventLogger.cpp` 的实现模式），或复用现有 `EventLogger` 增加延迟字段。日志输出到 `latency.jsonl`，格式见第 7 节。

### 3.4 客户端拉流测试

使用不同客户端工具对比 RTSP 播放延迟：

**ffplay 低延迟模式**：

```bash
ffplay -fflags nobuffer -flags low_delay -framedrop \
       -analyzeduration 0 -probesize 32 \
       rtsp://127.0.0.1:8554/live
```

关键参数说明：`nobuffer` 关闭输入缓冲，`low_delay` 降低解码延迟，`framedrop` 允许丢帧以追赶实时性。

**OpenCV 客户端**：编写简单程序使用 `cv::VideoCapture` 打开 RTSP 流，在每帧 `cap >> frame` 后记录本地时间戳，与服务端日志中的 `rtsp_send_ts` 对比。注意 OpenCV 的 RTSP 后端（FFmpeg）自身存在缓冲，可通过设置环境变量 `OPENCV_FFMPEG_CAPTURE_OPTIONS="rtsp_transport;tcp|buffer_size;102400"` 降低缓冲。

**VLC 网络缓冲设置**：`--network-caching=200`（单位 ms，VLC 默认 1000ms）。

### 3.5 HTTP API 延迟测试

**单次请求测试**：

```bash
curl -w "\ntime_total: %{time_total}s\n" -o /dev/null -s \
     http://localhost:8080/api/current
```

**压力测试**（使用 hey 或 wrk）：

```bash
# hey 压测（每秒 50 请求，持续 30 秒）
hey -n 1500 -c 10 http://localhost:8080/api/status

# wrk 压测
wrk -t4 -c10 -d30s http://localhost:8080/api/current
```

**测试接口列表**：`/api/status`、`/api/current`、`/api/events?limit=100`、`/api/faces`。

### 3.6 CDN 边缘节点延迟测试

针对 `rtsp_edge_analysis_server`，在 `PipelineRunner::Run()` 入口和 `StreamManager::OnStreamExit()` 出口之间测量完整管线延迟。同时在 `Scheduler::SelectNode()` 前后埋点，测量调度决策耗时。

测试变量包括：边缘节点数量（1/3/5/10 个）、并发流数（1/2/4/8 路）、节点能力层级混合（全 High / High+Medium / High+Medium+Low）。观察线程池排队深度和工作线程利用率对延迟的影响。

---

## 4. 测试环境说明

| 环境项 | 推荐配置 |
|--------|----------|
| 操作系统 | Ubuntu 22.04 LTS（Linux 6.x，WSL2 亦可） |
| CPU | x86_64，不少于 4 核，建议 8 核以上 |
| 内存 | 不少于 8 GB |
| GPU/NPU | 本次测试以 CPU 推理为主（OpenCV DNN 后端），可选测 OpenCL/OpenVINO 后端 |
| 编译器 | GCC 9+ 或 Clang 14+，C++11 标准 |
| OpenCV | 4.x，含 DNN 模块 |
| FFmpeg | 4.x，`ffmpeg` 命令在 PATH 中可用 |
| ffplay | 来自 FFmpeg 编译包，用于客户端拉流 |
| ONNX 模型 | models/face_detection.onnx（YuNet）、models/face_recognition.onnx（SFace/ArcFace） |
| 视频源 | 摄像头（USB/UVC）、H.264 裸流文件（test.h264）、MP4 文件（test.mp4）、RTSP 拉流源 |
| 测试分辨率 | 640x480、1280x720、1920x1080 |
| 测试帧率 | 15fps、25fps、30fps（匹配视频源和编码器配置） |
| 网络环境 | 本地回环（localhost）测试以排除广域网波动；也可在同一局域网内跨机器测试 |

**测试变量矩阵**：

| 变量 | 可选值 |
|------|--------|
| 视频源类型 | FileSource / CameraSource / RtspPullSource |
| 分辨率 | 640x480 / 1280x720 / 1920x1080 |
| 帧率 | 15 / 25 / 30 |
| AI 分析 | 开启 (FaceDetector + FaceRecognizer) / 关闭 (--no-ai) |
| 画面叠加 | 开启 / 关闭 |
| CDN 边缘模块 | 经过 (rtsp_edge_analysis_server) / 不经过 (rtsp_analysis_server) |
| 并发流数 | 1 / 2 / 4 / 8 路 |
| 边缘节点数 | 1 / 3 / 5 个 |

---

## 5. 测试场景设计

### 场景一：纯 RTSP 文件推流延迟测试

**目的**：测量不经过 AI 分析时，系统的基础流媒体推流延迟，作为延迟基线。

**步骤**：
1. 启动服务端：`./rtsp_h264_file test.h264`
2. 客户端使用 ffplay 低延迟模式拉流：`ffplay -fflags nobuffer -flags low_delay -framedrop rtsp://127.0.0.1:554/live`
3. 若 H.264 文件帧中不含时间戳，可在推流端用 `H264Source` 注入 SEI 时间戳信息。
4. 连续运行 5 分钟，客户端记录每帧接收时间，统计延迟分布。

**预期**：该场景延迟主要由文件读取 I/O、编码等待和客户端缓冲组成，应低于 200ms。

### 场景二：RTSP 推流端到端延迟测试

**目的**：测试 rtsp_pusher → rtsp_server → 客户端播放的完整链路延迟。

**步骤**：
1. 启动接收端 `rtsp_server`，监听 554 端口。
2. 启动推流端 `rtsp_pusher`，将本地文件或摄像头推送到 `rtsp://127.0.0.1:554/live`。
3. 客户端通过 ffplay 拉流，统计推流端到播放端延迟。

**关键测量点**：推流端 RtspPusher 发送时间戳 vs 客户端接收时间戳。

### 场景三：AI 分析服务延迟测试

**目的**：测量开启 AI 分析后各模块的耗时分布，对比 AI 开关对端到端延迟的影响。

**步骤**：
1. 开启 AI：`./rtsp_analysis_server --source file --input test.h264 --port 8554 --http-port 8080`
2. 关闭 AI：`./rtsp_analysis_server --source file --input test.h264 --port 8554 --no-ai`
3. 分别运行 5 分钟，通过埋点日志收集每帧的模块级耗时。
4. 对比两种模式下的 T_e2e、T_enc 和总延迟分布。
5. 分析 AI 推理是否是瓶颈（预期 T_det + T_rec 占总延迟的 40%~70%）。

### 场景四：边缘节点转发延迟测试

**目的**：评估 CDN 调度模块引入的额外延迟。

**步骤**：
1. 启动 3 个 EdgeNode（High/Medium/Low 各一个）的 `rtsp_edge_analysis_server`。
2. 测试 1/2/4 路并发流下的调度延迟和排队延迟。
3. 对比同一流在 `rtsp_analysis_server`（无调度）和 `rtsp_edge_analysis_server`（有调度）下的端到端延迟差异。
4. 记录 `scheduler.dispatch_total`、`scheduler.failover_total` 和节点级 `active_streams`、`avg_pipeline_latency_ms` 等指标。

**关键测量点**：Scheduler::SelectNode 耗时、EdgeNode::Submit 到 PipelineRunner 实际执行的等待时间（排队延迟）。

### 场景五：HTTP API 查询延迟测试

**目的**：测量各 HTTP API 接口的响应延迟。

**步骤**：
1. 在 `rtsp_analysis_server` 运行期间（AI 开启），每 2 秒用 curl 请求一次 `/api/current`，记录响应时间。
2. 使用 hey 工具以不同并发（1/5/10/20）对 `/api/status` 和 `/api/current` 进行 30 秒压测。
3. 记录 P50、P95、P99 和 QPS。

---

## 6. 建议埋点位置

以下埋点位置按数据流顺序排列，覆盖从采集到分发的完整链路：

| 序号 | 文件 | 函数/位置 | 测量内容 | 变量名建议 |
|------|------|-----------|----------|------------|
| 1 | `src/ai/CameraSource.cpp` | `GrabFrame()` 前后 | 摄像头帧采集耗时 | `T_cap` |
| 2 | `src/ai/FileSource.cpp` | `GrabFrame()` 前后 | 文件帧读取耗时 | `T_cap` |
| 3 | `src/ai/RtspPullSource.cpp` | `GrabFrame()` 前后 | RTSP 拉流帧获取耗时 | `T_cap` |
| 4 | `src/ai/FrameAnalyzer.cpp` | `Analyze()` 前后 | 完整 AI 分析耗时（含检测 + 识别 + 查询） | `T_ai_total` |
| 5 | `src/ai/FaceDetector.cpp` | `Detect()` 前后 | ONNX 推理耗时（人脸检测） | `T_det` |
| 6 | `src/ai/FaceRecognizer.cpp` | `Extract()` 前后 | ONNX 推理耗时（特征提取） | `T_rec` |
| 7 | `src/ai/FrameOverlay.cpp` | `Draw()` 前后 | 画面标注绘制耗时 | `T_ovl` |
| 8 | `src/ai/H264Encoder.cpp` | `EncodeFrame()` 入口 | 编码任务提交时间戳 | `T_enc_start` |
| 9 | `src/ai/H264Encoder.cpp` | `OutputCallback` 调用处 | 编码数据回调时间戳 | `T_enc_end` |
| 10 | `src/xop/RtspServer.cpp` | `PushFrame()` 入口 | 帧推入 RTSP 会话时间戳 | `T_push_start` |
| 11 | `src/xop/RtpConnection.cpp` | RTP 包发送函数（sendto/write）前后 | RTP 包实际发送耗时 | `T_rtsp` |
| 12 | `src/xop/RtspPusher.cpp` | 推流帧发送前后 | 推流端发送耗时 | `T_push` |
| 13 | `src/control/PipelineRunner.cpp` | `Run()` 主循环中每次迭代前后 | 单帧处理全链路耗时 | `T_pipeline` |
| 14 | `src/control/Scheduler.cpp` | `SelectNode()` 前后 | 调度决策耗时 | `T_sched` |
| 15 | `src/cdn_sim/EdgeNode.cpp` | `Submit()` 入口到任务实际执行 | 节点排队等待时间 | `T_queue` |
| 16 | `src/ai/HttpApiServer.cpp` | 各 HTTP 处理函数前后 | API 请求处理耗时 | `T_api` |

**埋点输出建议**：在每个埋点处，将 `(frame_id, module_name, start_ts_us, end_ts_us, cost_us)` 写入线程局部缓冲区，由后台线程批量刷入文件，避免 I/O 阻塞影响测量精度。

---

## 7. 延迟日志格式设计

### 7.1 逐帧延迟日志（latency.jsonl）

每行一个 JSON 对象，记录一帧在各模块的耗时分布：

```json
{
  "frame_id": 1024,
  "stream_id": "camera_01",
  "timestamp_ms": 1710000000123,
  "source": "camera",
  "width": 640,
  "height": 480,
  "fps": 25,
  "ai_enabled": true,
  "costs": {
    "capture_us": 3200,
    "ai_analyze_us": 31200,
    "detect_us": 18700,
    "recognize_us": 12400,
    "overlay_us": 2100,
    "encode_us": 8600,
    "rtsp_send_us": 1300,
    "total_server_us": 46400
  },
  "faces_detected": 2,
  "edge_node": "edge_east_high",
  "schedule_cost_us": 450,
  "queue_wait_us": 820
}
```

### 7.2 HTTP API 延迟日志

```json
{
  "timestamp_ms": 1710000000500,
  "endpoint": "/api/current",
  "method": "GET",
  "status_code": 200,
  "response_time_us": 1200,
  "faces_count": 2
}
```

### 7.3 日志分析方法

将 `latency.jsonl` 导入 Python pandas 进行分析：

```python
import pandas as pd
import json

records = []
with open("latency.jsonl") as f:
    for line in f:
        records.append(json.loads(line))

df = pd.json_normalize(records)

# 各模块平均延迟
print(df[["costs.capture_us", "costs.detect_us", "costs.recognize_us",
           "costs.encode_us", "costs.rtsp_send_us", "costs.total_server_us"]].mean())

# P95 延迟
print(df["costs.total_server_us"].quantile(0.95) / 1000)  # ms

# P99 延迟
print(df["costs.total_server_us"].quantile(0.99) / 1000)  # ms

# 最大延迟
print(df["costs.total_server_us"].max() / 1000)  # ms

# 抖动（帧间延迟差值的标准差）
df["diff"] = df["costs.total_server_us"].diff().abs()
print(df["diff"].std() / 1000)  # ms jitter

# 各模块耗时占比
cost_cols = ["costs.capture_us", "costs.detect_us", "costs.recognize_us",
             "costs.overlay_us", "costs.encode_us", "costs.rtsp_send_us"]
for col in cost_cols:
    pct = df[col].mean() / df["costs.total_server_us"].mean() * 100
    print(f"{col}: {pct:.1f}%")
```

---

## 8. 测试命令示例

### 8.1 启动服务端

```bash
# 基础 RTSP 服务端
./rtsp_server

# H.264 文件推流
./rtsp_h264_file

# AI 分析服务（文件源 + AI 开启）
./rtsp_analysis_server --source file --input ../pic/test.mp4 \
    --port 8554 --http-port 8080 --suffix live --analyze-fps 5

# AI 分析服务（文件源 + 无 AI）
./rtsp_analysis_server --source file --input ../pic/test.mp4 \
    --port 8554 --no-ai --suffix live

# AI 分析服务（摄像头源 + AI 开启）
./rtsp_analysis_server --source camera --device 0 \
    --width 1280 --height 720 --fps 25 \
    --port 8554 --http-port 8080

# 边缘调度服务
./rtsp_edge_analysis_server --source file --input ../pic/test.mp4 \
    --stream-id live_001 --region east --bitrate 4096 \
    --port 8554 --suffix live_001
```

### 8.2 客户端拉流

```bash
# ffplay 低延迟拉流
ffplay -fflags nobuffer -flags low_delay -framedrop \
       -analyzeduration 0 -probesize 32 \
       rtsp://127.0.0.1:8554/live

# VLC 低延迟拉流（200ms 网络缓冲）
vlc --network-caching=200 rtsp://127.0.0.1:8554/live

# OpenCV Python 客户端拉流并记录时间戳
python3 -c "
import cv2, time
cap = cv2.VideoCapture('rtsp://127.0.0.1:8554/live')
while True:
    t0 = time.time()
    ret, frame = cap.read()
    t1 = time.time()
    print(f'frame_recv_ms={(t1-t0)*1000:.1f}')
"
```

### 8.3 HTTP API 测试

```bash
# 查询当前帧分析结果
curl -s http://localhost:8080/api/current | python3 -m json.tool

# 查询状态（含耗时）
curl -w "\nHTTP %{http_code}  time_total: %{time_total}s\n" \
     -o /dev/null -s http://localhost:8080/api/status

# 查询事件列表
curl -s "http://localhost:8080/api/events?limit=50" | python3 -m json.tool

# 查询人脸库
curl -s http://localhost:8080/api/faces

# 注册人脸
curl -X POST http://localhost:8080/api/faces \
     -F "name=Alice" -F "image=@photo.jpg"

# 压测 /api/status
hey -n 2000 -c 10 http://localhost:8080/api/status

# 压测 /api/current
hey -n 1000 -c 5 http://localhost:8080/api/current
```

---

## 9. 结果记录表格

### 9.1 端到端延迟汇总表

| 测试场景 | 视频源 | 分辨率 | 帧率 | AI | 边缘 | T_avg (ms) | T_p95 (ms) | T_p99 (ms) | T_max (ms) | Jitter (ms) | CPU% | 备注 |
|----------|--------|--------|------|-----|------|------------|------------|------------|------------|-------------|------|------|
| 纯推流 | H264文件 | 640x480 | 25 | 关 | 关 | | | | | | | 基线 |
| 纯推流 | H264文件 | 1920x1080 | 30 | 关 | 关 | | | | | | | |
| AI分析 | H264文件 | 640x480 | 25 | 开 | 关 | | | | | | | |
| AI分析 | 摄像头 | 1280x720 | 25 | 开 | 关 | | | | | | | |
| AI分析 | H264文件 | 1920x1080 | 25 | 开 | 关 | | | | | | | |
| 边缘调度 | H264文件 | 640x480 | 25 | 开 | 3节点 | | | | | | | |
| 边缘调度 | H264文件 | 640x480 | 25 | 开 | 5节点 | | | | | | | |
| 并发(4路) | H264文件 | 640x480 | 25 | 开 | 5节点 | | | | | | | |

### 9.2 模块耗时分解表

| 帧率 | 采集 (ms) | 检测 (ms) | 识别 (ms) | 叠加 (ms) | 编码 (ms) | RTSP发送 (ms) | 调度 (ms) | 排队 (ms) | 服务端总计 (ms) |
|------|-----------|-----------|-----------|-----------|-----------|---------------|-----------|-----------|------------------|
| 15fps | | | | | | | | | |
| 25fps | | | | | | | | | |
| 30fps | | | | | | | | | |

### 9.3 HTTP API 延迟表

| 接口 | 平均响应 (ms) | P95 (ms) | P99 (ms) | QPS (hey c=10) |
|------|---------------|----------|----------|-----------------|
| /api/status | | | | |
| /api/current | | | | |
| /api/events | | | | |
| /api/faces | | | | |

---

## 10. 延迟分析方法

### 10.1 瓶颈定位策略

| 现象 | 可能瓶颈 | 验证方法 |
|------|----------|----------|
| T_det + T_rec 占比 > 50% | AI 推理是主瓶颈 | 对比 --no-ai 模式下的延迟，若显著下降则确认 |
| T_enc 占比 > 30% | FFmpeg 编码是瓶颈 | 检查 FFmpeg 进程 CPU 占用，尝试降低编码预设（-preset ultrafast） |
| T_rtsp 占比 > 10% | 网络发送队列积压 | 检查 RtpConnection 发送缓冲区大小，对比 TCP/UDP 模式差异 |
| 客户端端到端显著高于服务端总延迟 | 客户端缓冲过大 | 降低 ffplay -analyzeduration 和 -probesize 参数 |
| T_queue 随并发流数线性增长 | 线程池饱和 | 增加 EdgeNode worker 线程数或降低任务提交速率 |
| T_sched > 10ms | 调度器开销过大 | 减少候选节点数量，或缓存上一次调度结果 |

### 10.2 对比分析方法

1. **AI 开关对比**：将同一视频源在 `--no-ai` 和开启全量 AI 模式下的 `total_server_us` 对比，差值即为 AI 引入的额外延迟。
2. **分辨率递增对比**：固定其他变量，分别测试 640x480、1280x720、1920x1080 三种分辨率，观察编码延迟和 AI 推理延迟的缩放趋势。
3. **并发递增对比**：通过边缘调度服务同时运行 1/2/4/8 路流，观察 T_queue 和 T_enc 的变化趋势，判断系统是否为计算密集型瓶颈。
4. **边缘 vs 无边缘对比**：同一流分别通过 `rtsp_analysis_server` 和 `rtsp_edge_analysis_server` 运行，差值即为调度层额外开销。

---

## 11. 初步优化建议

根据可能的测试结果，预判优化方向如下：

| 优化方向 | 触发条件 | 实施要点 |
|----------|----------|----------|
| 降低客户端缓冲 | T_e2e 中 T_client_buf 占比高 | ffplay 使用 `-fflags nobuffer -flags low_delay` 参数；VLC 设置 `--network-caching=100` |
| AI 推理异步化 | T_det + T_rec > 30ms 且导致帧率下降 | 将检测和识别移至独立线程，管线线程仅取最新推理结果，允许推理慢于帧率时跳过部分帧 |
| 编码线程独立化 | T_enc > 10ms 且 FFmpeg CPU 占用高 | 将 EncodeFrame 从管线线程中解耦，使用编码队列 + 独立编码线程 |
| 帧丢弃策略 | T_total 持续超过帧间隔（1000/fps ms） | 在 GrabFrame 后检查时间预算，超过阈值则跳过当帧的 AI 分析和编码，直接读取下一帧 |
| 固定大小队列控制延迟 | ThreadPool 排队深度随负载无限增长 | 为 EdgeNode 的 max_queue 设置合理上限，超出后拒绝或丢弃新任务 |
| ONNX 推理优化 | OpenCV DNN 推理成为固定高开销环节 | 尝试 OpenCV DNN 的 OpenCL/OpenVINO 后端，或切换为 ONNX Runtime 并启用图优化和 INT8 量化 |
| 批量指标统计替代频繁日志写入 | I/O 在 latency 链路中的占比 > 5% | 使用 MetricsRegistry 内存聚合，每秒批量导出一次，避免每帧写日志 |
| 高延迟模块增加 MetricsRegistry 指标 | 缺少模块级延迟数据时 | 在 FaceDetector、H264Encoder 等关键模块注册整型指标，按帧更新，支持外部查询 |

---

## 12. 总结

StreamSight 现有系统的延迟测试，核心思路可以概括为"三层递进"：

1. **建立基线**：先在不修改代码的情况下，通过 ffplay 拉流、curl 测试和画面时间戳对比，获取端到端延迟和 API 响应延迟的初始数据。
2. **模块分解**：通过 `std::chrono::steady_clock` 在关键模块入口/出口埋点，输出 JSONL 延迟日志，用 Python 脚本批量分析各模块耗时占比和分布特征，精确定位瓶颈环节。
3. **优化验证**：在瓶颈定位后，实施针对性优化（如 AI 异步化、编码解耦、帧丢弃策略），通过对比优化前后的延迟数据验证效果。

测试应优先覆盖场景一（纯推流基线）和场景三（AI 分析全链路），这两组数据能够快速建立起系统延迟的量级认知。边缘调度和 HTTP API 的延迟测试可作为第二阶段目标，在确认基础管线延迟可控后再推进。

---

## 附录：最小可行测试方案

以下方案旨在不大幅修改代码的前提下，使用现有工具和最少的埋点改动，快速获取第一版延迟基线。预计总工作量约 2~3 小时。

### A.1 不改代码，先用现有工具测

**步骤 1 — 端到端延迟粗测**（30 分钟）：

1. 启动 `rtsp_analysis_server --source file --input test.h264 --port 8554 --no-ai`。
2. 客户端执行：
   ```bash
   ffplay -fflags nobuffer -flags low_delay -framedrop rtsp://127.0.0.1:8554/live
   ```
3. 用手机或截屏工具连续截取 20 张以上画面，目测延迟（画面内容与实际时间的感知差异），记录大致范围。如果能录制屏幕，逐帧回放更准。

4. 重复上述测试，但加上 `--analyze-fps 5` 并开启 AI：
   ```bash
   ./rtsp_analysis_server --source file --input test.h264 --port 8554 --analyze-fps 5
   ```
   对比开启/关闭 AI 时的画面延迟感受差异。

**步骤 2 — HTTP API 延迟**（15 分钟）：

```bash
# 单次请求耗时
curl -w "time_total: %{time_total}s\n" -o /dev/null -s http://localhost:8080/api/current

# 连续 100 次快速统计
for i in $(seq 1 100); do
  curl -w "%{time_total}\n" -o /dev/null -s http://localhost:8080/api/status
done | awk '{sum+=$1; count++} END {print "avg=" sum/count "s"}'
```

### A.2 最小代码改动加埋点

**步骤 3 — 在 example/rtsp_analysis_server.cpp 的 RunPipeline 循环中加 3 行代码**（15 分钟）：

在 `RunPipeline()` 函数的主循环中，采集帧的前后和编码帧的前后各加一个 `steady_clock::now()`，计算 `read_ms` 和 `encode_ms`，直接 `fprintf` 到 stderr 或文件：

```cpp
// 在 RunPipeline 循环开始处
auto t0 = std::chrono::steady_clock::now();

// 在 GrabFrame 之后
auto t1 = std::chrono::steady_clock::now();
auto read_us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

// ... AI 分析 ...

// 在 EncodeFrame 之后
auto t2 = std::chrono::steady_clock::now();
auto total_us = std::chrono::duration_cast<std::chrono::microseconds>(t2 - t0).count();

fprintf(stderr, "frame=%d read_us=%ld total_us=%ld\n", frame_counter, read_us, total_us);
```

编译运行后，收集 500~1000 帧数据到文件：
```bash
./rtsp_analysis_server --source file --input test.h264 --port 8554 --http-port 8080 2> latency.log
```

用 Python 一行快速统计：
```python
import re, sys
vals = [int(re.search(r'total_us=(\d+)', l).group(1)) for l in open(sys.argv[1]) if 'total_us=' in l]
print(f"avg={sum(vals)/len(vals)/1000:.1f}ms  p95={sorted(vals)[int(len(vals)*0.95)]/1000:.1f}ms  max={max(vals)/1000:.1f}ms")
```
运行：`python3 stats.py latency.log`

### A.4 第一版基线报告

用以上数据即可输出第一版延迟基线：

| 指标 | 无 AI 模式 | 有 AI 模式 (5fps analyze) |
|------|-----------|--------------------------|
| 端到端延迟（ffplay 目测） | ~150ms | ~250ms |
| 服务端单帧总耗时 (total_us) | avg ~20ms | avg ~50ms |
| 编码耗时 | avg ~8ms | avg ~10ms |
| HTTP /api/current 响应 | avg ~2ms | avg ~3ms |
| HTTP /api/status 响应 | avg ~1ms | avg ~1ms |

这份基线数据虽然精度有限，但足以回答三个关键问题：系统是否满足实时性需求、AI 是否是主要瓶颈、是否需要进一步细化测试。后续可根据基线数据决定是否投入完整的埋点方案和多场景测试。
