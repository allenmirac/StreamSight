# 延迟测量与追踪

StreamSight 在进程内通过 `observe::LatencyTracer` 记录关键阶段的耗时，用于定位解码、AI 处理、
编码、RTP 发送和 HTTP 请求的延迟分布。延迟数据可以通过 JSONL 日志、HTTP API 和
`scripts/analyze_latency.py` 三种方式查看，不依赖外部 APM 或链路追踪服务。

## 1. 工作方式

- 埋点通过 `STREAMSIGHT_LATENCY_SCOPE` / `STREAMSIGHT_LATENCY_SCOPE_WITH_IDS` 宏放在函数或代码块
  作用域内，`LatencyScope` 析构时提交耗时。
- 事件先进入内存环形缓冲区（用于 `/api/latency/recent` 和聚合统计），同时由后台线程追加写入
  JSONL 文件。
- 并行管线还会记录一条跨线程的完整链路事件 `pipeline.full`，表示从采集到编码输出的延迟。

延迟追踪默认关闭。关闭时每个埋点只做一次原子标志检查与少量字符串构造，不采集时间戳、不写日志；
开销很小，但不是宏完全展开为空。开启后每次 scope 会增加时间戳采集、加锁写入和字符串构造的开销，
通常远小于一帧的处理预算。

## 2. 开启追踪

环境变量在进程启动、`LatencyTracer` 单例首次初始化时读取，因此必须在启动程序前设置：

```bash
export STREAMSIGHT_LATENCY_ENABLE=1
export STREAMSIGHT_LATENCY_LOG=runtime/latency_events.jsonl
export STREAMSIGHT_LATENCY_BUFFER_SIZE=10000

./build/bin/streamsight --input pic/test.h264 --port 8554 --http-port 8080 --no-ai
```

| 变量 | 默认值 | 说明 |
|---|---|---|
| `STREAMSIGHT_LATENCY_ENABLE` | (空) | 设置为 `1` 或 `true` 开启延迟追踪 |
| `STREAMSIGHT_LATENCY_LOG` | `runtime/latency_events.jsonl` | JSONL 日志输出路径；目录不存在时自动创建 |
| `STREAMSIGHT_LATENCY_BUFFER_SIZE` | `10000` | 内存环形缓冲区保留的事件条数，只影响查询与统计，不限制日志文件大小 |

## 3. 埋点范围

| module.stage | 触发位置 | 生效条件 |
|---|---|---|
| `pipeline.demux_decode` | 串行与并行管线的解封装 + 解码阶段 | 视频处理启动后 |
| `pipeline.encode` | 串行管线 `FFmpegStreamer` 的缩放、编码与输出 | 串行模式（`--pipeline-mode serial`，默认） |
| `pipeline.full`（`event=frame`） | 并行管线编码输出线程，耗时 = 采集时间 → 编码输出时间 | 并行模式（`--pipeline-mode parallel`） |
| `ai.frame_analyze_total` | `FrameAnalyzer::Analyze`，包含其内部各 AI 阶段 | 启用 AI 效果时 |
| `ai.face_detection` | `FaceDetector::Detect` | 启用 AI 效果时 |
| `ai.face_recognition` | `FaceRecognizer` 特征提取 | 启用 AI 效果时 |
| `ai.face_database_search` | `FaceDatabase` 相似度检索 | 启用 AI 效果时 |
| `ai.frame_overlay` | `FrameOverlay` 绘制 | 启用 AI 效果时 |
| `rtsp.rtp_send` | `RtpConnection` 发送 RTP 包 | 有 RTSP 客户端拉流时 |
| `http.http_request_total` | `StreamApiServer` 的 HTTP 请求处理 | 设置了 `--http-port` 时 |

`ai.frame_analyze_total` 是外层 scope，内部包含检测、识别、检索和绘制等子 scope。
`scripts/analyze_latency.py` 会按同一线程上的时间区间嵌套关系计算各阶段的 exclusive 时间，
避免简单相加造成重复统计。跨线程的 `pipeline.full` 不参与该嵌套计算。

不同模式下的可用埋点不同：串行模式有 `pipeline.encode`，并行模式用 `pipeline.full` 观察端到端
延迟；两者都有 `pipeline.demux_decode`。

## 4. 运行延迟基线

一键脚本会检查二进制和测试素材、设置环境变量并启动服务：

```bash
bash scripts/run_latency_baseline.sh
```

也可以手动运行：

```bash
mkdir -p runtime

export STREAMSIGHT_LATENCY_ENABLE=1
export STREAMSIGHT_LATENCY_LOG=runtime/latency_events.jsonl

# 无 AI 模式：观察 pipeline 与 rtsp 延迟
./build/bin/streamsight --input pic/test.h264 \
    --port 8554 --http-port 8080 --no-ai --suffix live

# 另一个终端拉流，触发 rtsp.rtp_send
ffplay -fflags nobuffer -flags low_delay -framedrop rtsp://127.0.0.1:8554/live
```

观察 AI 分析延迟时去掉 `--no-ai`，并按需设置 `--analyze-fps`：

```bash
./build/bin/streamsight --input pic/test.h264 \
    --port 8554 --http-port 8080 --analyze-fps 5 --suffix live
```

停止服务后分析日志：

```bash
python3 scripts/analyze_latency.py runtime/latency_events.jsonl
```

## 5. HTTP 查询接口

服务运行时可以直接查询内存中的延迟数据：

```bash
# 模块级统计（avg/p50/p95/p99/max）
curl -s http://localhost:8080/api/latency/stats | python3 -m json.tool

# 最近 N 条事件
curl -s "http://localhost:8080/api/latency/recent?limit=10" | python3 -m json.tool

# 重置内存统计（不删除磁盘日志）
curl -X POST http://localhost:8080/api/latency/reset
```

响应字段与错误码见 [api.md](api.md)。

## 6. 分析脚本

```bash
# 基本用法：按 module.stage 聚合
python3 scripts/analyze_latency.py runtime/latency_events.jsonl

# 只看 face_detection
python3 scripts/analyze_latency.py runtime/latency_events.jsonl --stage ai.face_detection

# 按总耗时排序，只看前 5 个阶段
python3 scripts/analyze_latency.py runtime/latency_events.jsonl --top 5

# JSON / CSV 输出
python3 scripts/analyze_latency.py runtime/latency_events.jsonl --json
python3 scripts/analyze_latency.py runtime/latency_events.jsonl --csv
```

输出按 stage 给出 count、avg、p50、p90、p95、p99、max、jitter 和耗时占比；JSON/CSV 输出便于
接入其他分析工具或画图。

## 7. 事件格式

JSONL 每行一个事件对象：

| 字段 | 类型 | 说明 |
|---|---|---|
| `timestamp_ms` | int64 | 事件提交时的 Unix 毫秒时间戳（`system_clock`） |
| `trace_id` | string | 追踪 ID，当前埋点通常为空 |
| `stream_id` | string | 流 ID；`pipeline.full` 与并行 `demux_decode` 会带 |
| `frame_id` | int64 | 帧序号；可用时为该帧编号 |
| `module` | string | 模块名：`pipeline` / `ai` / `rtsp` / `http` |
| `stage` | string | 阶段名，见第 3 节 |
| `event` | string | `scope`（RAII 埋点）或 `frame`（`pipeline.full`） |
| `start_us` | int64 | 开始时间（`steady_clock` 微秒，不是 Unix 时间） |
| `end_us` | int64 | 结束时间（`steady_clock` 微秒） |
| `duration_us` | int64 | 耗时（微秒） |
| `thread_id` | string | 线程 ID |
| `extra` | string | 可选 JSON 片段，当前埋点一般不带 |

`start_us`/`end_us` 使用 `steady_clock`，适合计算耗时，不适合直接换算成绝对时间；
需要绝对时间时使用 `timestamp_ms`。

## 8. 常见问题

**日志没有生成**

- 确认进程启动前已设置 `STREAMSIGHT_LATENCY_ENABLE=1`；
- 确认日志目录可写；程序会在 stderr 打印 `[LatencyTracer] Cannot open log` 等错误信息。

**统计为空或缺少某些 stage**

- `rtsp.rtp_send` 只在有客户端拉流时产生；
- `ai.*` 只在启用 AI 效果时产生；
- `pipeline.encode` 只存在于串行模式，并行模式请观察 `pipeline.full`；
- HTTP 接口只有在设置 `--http-port` 后可用。

**日志文件过大**

- `STREAMSIGHT_LATENCY_BUFFER_SIZE` 只控制内存中保留的事件条数，不控制文件大小；
- 长时间压测时可在测试前删除旧日志（`rm -f runtime/latency_events.jsonl`），或让每次测试使用不同的
  `STREAMSIGHT_LATENCY_LOG` 路径。

**关闭追踪后还有开销吗**

有少量固定开销：每个埋点仍会构造 `LatencyScope` 并检查一次原子标志，但不采集时间戳、不做统计、
不写日志。需要绝对零开销时只能在编译期移除埋点，本项目不提供该开关。

**编译失败**

- 需要 GCC 9+ 或 Clang 9+（C++17）；
- 延迟追踪源码位于 `src/observe/LatencyTracer.cpp`，由 CMake 的 `OBSERVE_SRCS` 统一收集。
