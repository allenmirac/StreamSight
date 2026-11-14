# 测试说明

本文说明 StreamSight 的测试分层、运行方式、各测试目标覆盖什么，以及新增测试时应该放在哪里。
环境依赖与首次构建见 [setup.md](setup.md)，贡献流程与代码风格见 [CONTRIBUTING.md](../CONTRIBUTING.md)。

## 1. 测试分层

测试使用 GoogleTest 编写，由 ctest 统一编排。按外部依赖拆成五个测试目标，保证纯协议逻辑可以在没有
FFmpeg/OpenCV 的环境中构建和运行：

| ctest 目标 | 测试源码 | 外部依赖 | ctest 标签 | 用例数 |
|---|---|---|---|---|
| `test_core` | `tests/unit/core/` | 无（仅 GoogleTest） | `fast;tsan` | 152 |
| `test_rtsp_integration` | `tests/integration/` | 无（仅 GoogleTest） | `heavy;tsan` | 7 |
| `test_ai_core` | `tests/unit/ai/` | OpenCV | `heavy` | 24 |
| `test_ffmpeg` | `tests/unit/ffmpeg/` | FFmpeg + OpenCV | `heavy` | 32 |
| `test_api` | `tests/unit/api/` | FFmpeg + OpenCV | `heavy;http` | 7 |

截至 2026-11-14，ctest 共注册 222 个用例。依赖缺失时 CMake 只跳过对应目标：`test_core` 与
`test_rtsp_integration` 始终构建；`test_ai_core` 需要 OpenCV；`test_ffmpeg` 与 `test_api`
需要 FFmpeg + OpenCV。

以下两个工具只采集性能数据，不包含断言，也不注册进 ctest：

| 工具 | 用途 |
|---|---|
| `streamsight-netbench` | net 层 RingBuffer/TCP 吞吐与延迟微基准 |
| `streamsight-stress` | 多流并发压力测试、延迟与资源指标采集 |

## 2. 运行测试

```bash
cmake -S . -B build -DBUILD_TESTS=ON
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

按标签选择用例：

```bash
ctest --test-dir build -L fast --output-on-failure    # test_core，无需 FFmpeg/OpenCV
ctest --test-dir build -L heavy --output-on-failure   # 需要外部依赖的完整测试
ctest --test-dir build -L http --output-on-failure    # test_api
ctest --test-dir build -L tsan --output-on-failure    # test_core + test_rtsp_integration
```

`tsan` 只是标签，不代表启用了 ThreadSanitizer。真正的 sanitizer 构建需要
`-DENABLE_SANITIZERS=thread`；`-DENABLE_SANITIZERS=address,undefined` 启用 ASan + UBSan。
两者都只对测试目标生效。具体命令见 [setup.md §3](setup.md#3-编译)。

也可以直接运行某个测试二进制：

```bash
./build/bin/test_core --gtest_list_tests
./build/bin/test_core --gtest_filter='GopCache.*'
```

ctest 为每个用例设置 60 秒超时，测试发现超时为 30 秒。

## 3. 覆盖内容

### 3.1 核心协议与基础设施（`test_core`）

无外部依赖，覆盖 net、rtsp、observe 模块，以及可脱离 OpenCV 的 AI/API 纯逻辑。

| 测试文件 | 被测组件 | 用例数 | 覆盖要点 |
|---|---|---:|---|
| `test_ring_buffer.cpp` | `net::RingBuffer` | 13 | FIFO、满/空边界、覆盖最旧项、`PushOrDrop` 三种结果、`PruneStale`、关键帧保留、单生产者单消费者并发顺序 |
| `test_buffer_reader_writer.cpp` | `net::BufferReader` / `BufferWriter` | 12 | BE/LE 整数读写、CRLF 查找、按分隔符提取、扩容、发送队列容量与失败保留 |
| `test_timer_queue.cpp` | `net::TimerQueue` | 6 | 一次性/重复定时器、取消、到期顺序、剩余时间 |
| `test_event_loop.cpp` | `net::EventLoop` / `TcpServer` | 5 | 跨线程触发、提交顺序、退出唤醒、定时器、loopback echo |
| `test_rtsp_message.cpp` | `rtsp::RtspRequest` | 21 | OPTIONS/DESCRIBE/SETUP/PLAY/TEARDOWN/GET_PARAMETER、三种 Transport、Digest 认证、分片请求、畸形输入、各响应报文构建 |
| `test_h264_parser.cpp` | `rtsp::H264Parser` | 6 | 3/4 字节起始码、连续 NAL、无起始码、尾部填充零 |
| `test_digest_auth.cpp` | `rtsp::DigestAuthentication` | 4 | RFC 2617 已知向量、参数变化、错误密码 |
| `test_media_session.cpp` | `rtsp::MediaSession` | 5 | 源增删查、同通道替换、SDP、会话 ID、suffix |
| `test_rtp_rtcp.cpp` | `H264Source` / `RtcpMessage` | 6 | H.264 单包与 FU-A 分片边界、发送回调失败中断、RTCP SR 布局 |
| `test_media_sources.cpp` | H.265/AAC/G.711/VP8 source | 8 | H.265 FU 分片、AAC ADTS 剥离与 AU header、G.711 与 VP8 打包、payload type/时钟率 |
| `test_rtcp_parser.cpp` | `rtcp::RtcpParser` | 9 | SR/RR/SDES/BYE、复合包、多报告块、畸形包、RTT 计算与回绕 |
| `test_gop_cache.cpp` | `rtsp::GopCache` | 14 | 参数集保留与失效、音频与 GOP 清理、FU-A IDR 组装、多 NAL AU、H.265 参数集排序、快照深拷贝 |
| `test_metrics_registry.cpp` | `observe::MetricsRegistry` | 5 | gauge/counter、缺失值、前缀快照、前缀删除、并发累加 |
| `test_latency_tracer.cpp` | `observe::LatencyTracer` | 10 | 启停、统计聚合、最近事件顺序、环形缓冲裁剪、重置、JSONL 输出与转义 |
| `test_event_bus.cpp` | `observe::EventBus` | 3 | 订阅/退订、多订阅者、并发发布 |
| `test_face_database.cpp` | `ai::FaceDatabase` | 10 | 注册/查询/删除、阈值边界、维度校验、持久化、损坏文件、并发 |
| `test_json_util.cpp` | `api::JsonUtil` | 11 | 字符串/整数/布尔提取、嵌套对象、缺失与畸形输入、转义 |
| `test_vector_math.cpp` | `ai::VectorMath` | 4 | 点积、无效输入、L2 归一化、零向量 |

### 3.2 RTSP 服务端集成（`test_rtsp_integration`）

使用 `tests/support/rtsp_test_client.h` 在 loopback 上启动真实 `RtspServer`，按 RTSP/RTP/RTCP
报文做端到端验证：

| 用例 | 覆盖点 |
|---|---|
| `HandshakeAndStateMachine` | OPTIONS → DESCRIBE → SETUP → PLAY → TEARDOWN 状态机 |
| `SetupBeforeDescribeReturnsServerError` | 非法的请求顺序 |
| `RtcpReceiverReportUpdatesMetricsAndKeepsAlive` | TCP 交织通道上的 RTCP RR 解析、`rtcp.*` 指标、连接保活 |
| `UdpRtcpReceiverReportUpdatesMetricsAndKeepsAlive` | UDP RTCP RR 的同一路径 |
| `KeyframeGatesLivePackets` | 未收到关键帧前不向客户端发送直播包 |
| `LateClientReceivesCachedGopAndLiveFanout` | 晚加入客户端先收到缓存 GOP，再转入直播 fanout |
| `SlowClientDoesNotBlockFastClient` | 慢客户端不阻塞其他客户端 |

### 3.3 AI 与特效（`test_ai_core`）

| 测试文件 | 被测组件 | 用例数 | 覆盖要点 |
|---|---|---:|---|
| `test_face_detector_nms.cpp` | `FaceDetector::Nms` / YuNet 解码 | 9 | 重叠框抑制、阈值边界、相同框、空输入、合成输出张量解码、低分过滤、异常 shape |
| `test_frame_overlay.cpp` | `FrameOverlay` | 3 | 人脸框绘制、空结果、越界框不越界写 |
| `test_frame_analyzer.cpp` | `FrameAnalyzer` | 3 | 空 detector、`analyze_fps=0`、低帧率复用缓存 |
| `test_event_logger.cpp` | `EventLogger` | 6 | JSONL 字段、`min_faces` 过滤与边界、追加写入、不可写路径、并发不交错 |
| `test_effect_factory.cpp` | `EffectFactory` / `EffectChain` | 3 | 插件创建、未知插件、链式增删 |

模型推理精度不在自动化测试范围内；这里的用例验证的是解码、后处理、过滤和输出逻辑。

### 3.4 FFmpeg 管线（`test_ffmpeg`）

| 测试文件 | 被测组件 | 用例数 | 覆盖要点 |
|---|---|---:|---|
| `test_ffmpeg_utils.cpp` | `FFmpegUtils` | 4 | 错误码转字符串、PTS 转 90kHz、有理数转换、`FrameToMat` 零拷贝 |
| `test_output_adapters.cpp` | `MultiOutputAdapter` / `RtspOutputAdapter` / `RtmpOutputAdapter` | 10 | 多适配器分发的失败传播、RTSP 会话校验、空包跳过、RTMP 打开前写入与关闭安全 |
| `test_pacer.cpp` | `Pacer` | 6 | 非法帧率禁用、首帧不睡眠、逐帧间隔、落后重同步、重置 |
| `test_session_lifecycle.cpp` | `StreamSession` | 12 | 停止态构造、默认配置、重复 Stop、无效输入、效果更新失败、启动失败回收 |

`StreamPipeline` 的线程调度、重连和背压时序不在单元测试范围内；测试覆盖的是其依赖的 RingBuffer
原语、Pacer 和输出适配器契约。

### 3.5 HTTP API（`test_api`）

`test_session_registry.cpp`（7 个用例）覆盖 `StreamApiServer` 的内存会话注册表：创建/列出/查询/
删除、外部会话注册、结果与事件写入、空依赖安全。HTTP 路由 handler、JSON 响应体、并发请求和
multipart 上传目前没有自动化覆盖。

## 4. 明确不覆盖的范围

- HTTP 路由、响应 JSON、并发请求、multipart 上传；
- ffprobe/ffplay 端到端拉流、音视频时长、音频输出端到端；
- `StreamPipeline` 的线程调度、断流重连和背压时序；
- 模型推理精度、性能回归门槛、覆盖率门禁；
- `RtspPusher`（尚未接线）与依赖外部 SRS 的 RTMP 端到端链路。

新增这些方向的测试时，优先复用 `tests/support/` 下的辅助设施，并在本文件补充覆盖说明。

## 5. Sanitizer 与 CI

`.github/workflows/tests.yml` 包含四个 job：

| Job | 构建 | 运行 |
|---|---|---|
| `fast` | `test_core` | `ctest -L fast` |
| `full` | 全部测试目标 | `ctest` |
| `asan` | `-DENABLE_SANITIZERS=address,undefined`，全部目标 | 全部 ctest |
| `tsan` | `-DENABLE_SANITIZERS=thread`，`test_core` | `ctest -L tsan` |

ASan 车道开启泄漏检测，第三方库的已知全局泄漏通过 `tests/lsan_suppressions.txt` 精确抑制，
不整体关闭泄漏检测。

## 6. 测试约定

- 文件命名：`test_<被测组件>.cpp`，与 `src/<模块>/<组件>` 对应；CMake 用
  `file(GLOB ... CONFIGURE_DEPENDS)` 收集，新增文件后无需手改源文件列表。
- 用例命名：`TEST(<组件>, <行为>)`，行为用英文短语描述，一个用例只验证一个行为。
- 不依赖仓库根目录的相对路径：文件读写使用 `tests/support/test_temp_dir.h`；网络测试绑定
  `127.0.0.1` 并使用 `tests/support/free_port.h` 获取端口；时序测试使用
  `tests/support/fake_clock.h` 或给出上下界，避免无界 sleep。
- `MetricsRegistry` 和 `LatencyTracer` 是进程级单例：用例使用唯一的 scope/stage 名，必要时先
  `Reset()`，避免顺序依赖。
- 不允许出现「两种结果都算通过」的断言；断言应反映当前真实行为。

## 7. 新增测试

1. 按依赖选择目录：纯协议/基础设施放 `tests/unit/core/`，需要 OpenCV 放 `tests/unit/ai/`，
   需要 FFmpeg 管线放 `tests/unit/ffmpeg/`，HTTP API 放 `tests/unit/api/`，真实网络交互放
   `tests/integration/`。
2. 按第 6 节的约定编写用例；集成测试可复用 `tests/support/rtsp_test_client.h`。
3. 重新构建并运行对应标签：

   ```bash
   cmake --build build -j$(nproc)
   ctest --test-dir build -R <组件名> --output-on-failure
   ```

4. 如果新增了目标或标签，同步更新 `CMakeLists.txt`、本文件和 `CONTRIBUTING.md`。
