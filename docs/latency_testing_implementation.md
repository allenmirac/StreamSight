# StreamSight 延迟测试实现文档

## 1. 新增文件

| 文件 | 说明 |
|------|------|
| `src/observe/LatencyTracer.h` | 延迟追踪单例、LatencyScope RAII、便捷宏 |
| `src/observe/LatencyTracer.cpp` | 实现：JSONL 写入、环形缓冲区、统计聚合、后台写入线程 |
| `scripts/analyze_latency.py` | Python 分析脚本，计算 avg/p50/p95/p99/max/jitter |
| `scripts/run_latency_baseline.sh` | 一键基线测试脚本 |
| `docs/latency_testing_implementation.md` | 本文件 |

## 2. 修改文件

| 文件 | 改动 |
|------|------|
| `CMakeLists.txt` | OBSERVE_SRCS 添加 LatencyTracer.cpp |
| `Makefile` | 添加 src/observe/ 编译规则，TARGET4 链接 observe 对象 |
| `src/ai/FrameAnalyzer.cpp` | 添加 frame_analyze_total 埋点 |
| `src/ai/FaceDetector.cpp` | 添加 face_detection 埋点 |
| `src/ai/FaceRecognizer.cpp` | 添加 face_recognition 埋点 |
| `src/ai/FaceDatabase.cpp` | 添加 face_database_search 埋点 |
| `src/ai/FrameOverlay.cpp` | 添加 frame_overlay 埋点 |
| `src/ai/H264Encoder.cpp` | 添加 h264_encode 埋点 |
| `src/ai/FileSource.cpp` | 添加 capture_frame 埋点 |
| `src/ai/CameraSource.cpp` | 添加 capture_frame 埋点 |
| `src/ai/RtspPullSource.cpp` | 添加 rtsp_pull_receive 埋点 |
| `src/ai/HttpApiServer.cpp` | 添加 3 个延迟查询 API + handler 耗时埋点 |
| `src/xop/RtpConnection.cpp` | 添加 rtp_send 埋点 |
| `src/control/Scheduler.cpp` | 添加 scheduler_select 埋点 |
| `src/control/PipelineRunner.cpp` | 添加 pipeline_run 埋点 |
| `src/control/StreamManager.cpp` | 添加 stream_manager_dispatch 埋点 |
| `src/cdn_sim/EdgeNode.cpp` | 添加 edge_node_enqueue 埋点 |

## 3. 如何开启延迟测试

设置环境变量后启动程序：

```bash
export STREAMSIGHT_LATENCY_ENABLE=1
export STREAMSIGHT_LATENCY_LOG=runtime/latency_events.jsonl
export STREAMSIGHT_LATENCY_BUFFER_SIZE=10000

./bin/rtsp_analysis_server --source file --input pic/test.h264 --port 8554 --http-port 8080 --no-ai
```

不设置环境变量时，所有埋点宏展开为空，零性能开销。

## 4. 环境变量说明

| 变量 | 默认值 | 说明 |
|------|--------|------|
| `STREAMSIGHT_LATENCY_ENABLE` | (空) | 设置为 `1` 或 `true` 开启延迟追踪 |
| `STREAMSIGHT_LATENCY_LOG` | `runtime/latency_events.jsonl` | JSONL 日志输出路径 |
| `STREAMSIGHT_LATENCY_BUFFER_SIZE` | `10000` | 内存环形缓冲区大小（条数） |

## 5. 运行延迟基线测试

**方式一：一键脚本**
```bash
bash scripts/run_latency_baseline.sh
```

**方式二：手动运行**
```bash
# 1. 创建运行时目录
mkdir -p runtime

# 2. 设置环境变量
export STREAMSIGHT_LATENCY_ENABLE=1
export STREAMSIGHT_LATENCY_LOG=runtime/latency_events.jsonl

# 3. 启动服务（无 AI 模式）
./bin/rtsp_analysis_server --source file --input pic/test.h264 \
    --port 8554 --http-port 8080 --no-ai --suffix live &

# 4. 等待几秒，拉流测试
ffplay -fflags nobuffer -flags low_delay -framedrop rtsp://127.0.0.1:8554/live

# 5. Ctrl+C 停止服务

# 6. 分析日志
python3 scripts/analyze_latency.py runtime/latency_events.jsonl
```

## 6. 运行 AI 分析延迟测试

```bash
export STREAMSIGHT_LATENCY_ENABLE=1
export STREAMSIGHT_LATENCY_LOG=runtime/latency_events.jsonl

./bin/rtsp_analysis_server --source file --input pic/test.h264 \
    --port 8554 --http-port 8080 --analyze-fps 5 --suffix live
```

停止后分析各模块耗时占比：
```bash
python3 scripts/analyze_latency.py runtime/latency_events.jsonl
```

## 7. HTTP 延迟查询接口

服务运行时可通过以下接口查询延迟数据：

```bash
# 查看模块级统计（avg/p50/p95/p99/max）
curl -s http://localhost:8080/api/latency/stats | python3 -m json.tool

# 查看最近 N 条事件
curl -s "http://localhost:8080/api/latency/recent?limit=10" | python3 -m json.tool

# 重置内存统计数据（不删除磁盘日志）
curl -X POST http://localhost:8080/api/latency/reset
```

## 8. analyze_latency.py 使用说明

```bash
# 基本用法
python3 scripts/analyze_latency.py runtime/latency_events.jsonl

# 只看 face_detection
python3 scripts/analyze_latency.py runtime/latency_events.jsonl --stage ai.face_detection

# Top 5 耗时模块
python3 scripts/analyze_latency.py runtime/latency_events.jsonl --top 5

# JSON 输出
python3 scripts/analyze_latency.py runtime/latency_events.jsonl --json

# CSV 输出
python3 scripts/analyze_latency.py runtime/latency_events.jsonl --csv
```

输出示例：
```
Stage                                Count   Avg(ms)       P50       P90       P95       P99       Max    Jitter    Share%
ai.frame_analyze_total                 150     45.200    42.100    58.300    62.500    68.100    72.300     5.200    45.0%
ai.face_detection                      150     18.700    17.500    22.100    25.300    28.900    32.100     2.100    18.6%
ai.face_recognition                    150     12.400    11.800    15.200    17.100    19.500    21.000     1.500    12.3%
ai.h264_encode                         150      8.600     7.900    10.200    12.100    14.300    16.500     1.200     8.6%
...
```

## 9. JSONL 字段说明

每行一个 JSON 对象：

| 字段 | 类型 | 说明 |
|------|------|------|
| `timestamp_ms` | int64 | Unix 毫秒时间戳 |
| `trace_id` | string | 追踪 ID |
| `stream_id` | string | 流 ID |
| `frame_id` | int64 | 帧序号 |
| `module` | string | 模块名（ai/xop/control/cdn_sim/http） |
| `stage` | string | 阶段名（face_detection/h264_encode/...） |
| `event` | string | 事件类型（"scope" 或 "mark"） |
| `start_us` | int64 | 开始时间（微秒，steady_clock） |
| `end_us` | int64 | 结束时间（微秒） |
| `duration_us` | int64 | 耗时（微秒） |
| `thread_id` | string | 线程 ID |
| `extra` | string | 额外 JSON 片段（可选） |

## 10. 常见问题

### 日志没有生成
- 确认设置了 `STREAMSIGHT_LATENCY_ENABLE=1`
- 检查 `runtime/` 目录是否有写权限
- 查看 stderr 是否有 `[LatencyTracer] Cannot open log` 错误

### 编译失败
- 确保使用 GCC 4.8+ 或 Clang 3.3+（C++11）
- 确认 CMakeLists.txt 或 Makefile 已包含 `src/observe/LatencyTracer.cpp`

### 没有 HTTP 接口数据
- 确认 HttpApiServer 已启动（设置了 `--http-port` 参数）
- 确认延迟追踪已开启（环境变量）

### 日志过大
- 设置 `STREAMSIGHT_LATENCY_BUFFER_SIZE=1000` 减小内存占用
- 日志文件本身可在测试后删除或归档
- 建议每次测试前清理：`rm -f runtime/latency_events.jsonl`

### 埋点影响性能
- 不设置环境变量时零开销（宏展开为空）
- 开启后每次 scope 约 2~5 微秒开销（原子操作 + 时间戳获取）
- 在 25fps 下每帧 10 个 scope，总开销约 0.05ms，影响可忽略
- 如果仍需降低开销，可增大 `STREAMSIGHT_LATENCY_BUFFER_SIZE` 减少锁竞争
