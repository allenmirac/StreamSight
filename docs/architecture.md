# StreamSight 项目架构说明文档

## 1. 项目概述

StreamSight 是一个基于 C++11 的实时视频流媒体与智能分析系统。项目以高性能 RTSP/RTP 流媒体服务为核心，集成 AI 人脸检测与识别能力，并扩展了系统可观测性组件，形成了一套覆盖"采集—分析—编码—分发—观测"完整链路的流媒体处理平台原型。

项目的核心能力包括：

- **流媒体基础能力**：支持 H.264、H.265、G711A、AAC、VP8 等多种音视频格式的 RTSP 推流与分发，支持单播、组播及摘要认证。
- **FFmpeg C API 进程内管线**：基于 libavformat/libavcodec/libswscale 的 3-stage 流水线（Demux+Decode → AI Process → Encode+Output），RingBuffer 背压 + FrameDropPolicy 自适应丢帧，替代旧有 fork+pipe 子进程方案。
- **AI 视频分析能力**：基于 OpenCV DNN 和 ONNX 模型实现实时人脸检测（YuNet）与人脸识别（ArcFace），支持人脸库管理、视频帧叠加标注和检测事件记录。
- **EffectPlugin 可扩展插件体系**：统一 IEffectPlugin 接口，支持 Analysis/Overlay/Transform/Extract 四类插件，FaceRecognitionPlugin 作为首个内置插件，EffectFactory 支持 JSON 配置化动态创建。
- **可观测性能力**：提供流级的运行时指标采集与查询接口，EventBus 线程安全事件发布/订阅，LatencyTracer RAII 延迟追踪。
- **HTTP 服务能力**：StreamApiServer 合并式 API 服务，支持旧版路由 + v1 session CRUD + latency 查询，单端口统一接入。

---

## 2. 总体架构

StreamSight 采用分层架构设计，从底层网络通信到上层业务调度共划分为七个核心模块层，各层职责明确、耦合度低。

### 2.1 核心层次划分

| 层次 | 目录 | 职责 |
|------|------|------|
| 网络通信层 | `src/net/` | 提供基于 Reactor 模式的异步事件驱动网络框架，封装 epoll/select、TCP 连接管理、定时器和内存管理 |
| RTSP 流媒体层 | `src/xop/` | 实现 RTSP/RTP/RTCP 协议栈，包括服务端、推流端、媒体会话管理、多种编码格式的媒体源封装、RTCP SR 发送和 SEI 延迟标记注入 |
| AI 分析层 | `src/ai/` | 提供多类型视频源接入、人脸检测与识别、帧分析调度、画面叠加绘制、H.264 软编码（fork+pipe）和事件日志 |
| FFmpeg 管线层 | `src/ffmpeg/` | 提供基于 FFmpeg C API 的进程内媒体管线：StreamSession 统一单流会话抽象，StreamPipeline 3-stage 并行管线（Demux+Decode → AI Process → Encode+Output），FrameDropPolicy 自适应丢帧，支持音视频混合和多协议输出 |
| EffectPlugin 插件层 | `src/effect/` | 提供 IEffectPlugin 统一接口、EffectChain 有序执行链、EffectFactory JSON 配置化创建，FaceRecognitionPlugin 作为首个内置插件 |
| API 服务层 | `src/api/` | StreamApiServer 合并式 HTTP API，整合旧版路由（/api/current、/api/faces 等）和 v1 session CRUD（/api/v1/sessions）及 latency 查询 |
| 可观测性层 | `src/observe/` | 提供线程安全的内存键值指标注册与快照导出（MetricsRegistry）、RAII 延迟追踪（LatencyTracer）和模板化 EventBus 事件发布/订阅 |
| 示例程序层 | `example/` | 包含不同场景的入口程序，主入口为 ffmpeg_streamer（StreamSession + API server） |
| 测试层 | `tests/` | 单元测试和集成测试，覆盖 EventBus、EffectFactory、StreamSession、StreamApiServer 及 stress tester |

### 2.2 模块调用关系

网络通信层为所有上层模块提供异步 I/O 能力。RTSP 流媒体层构建在网络层之上，对外暴露 RTSP 服务端和推流端接口。FFmpeg 管线层通过 StreamSession 统一管理 EventLoop、RtspServer 和 StreamPipeline 的生命周期，StreamPipeline 以 3-stage 并行管线完成解封装→解码→AI 处理→编码→多输出的全流程，EffectChain 在 AI 处理阶段有序执行注册的 IEffectPlugin。StreamApiServer 提供查询路由和 v1 session CRUD 接口，是多流会话的唯一注册表。可观测性层以横切方式嵌入各层，采集和暴露运行指标，EventBus 提供模块间松耦合事件通知。

### 2.3 数据流转概览

核心数据流有两条路径：

**主路径（ffmpeg_streamer，当前主推）**：输入源 → FFmpeg 解封装 → 视频解码 → BGR24 转换 → EffectChain（有序执行 IEffectPlugin[]）→ YUV420P 转换 → libx264 编码 → MultiOutputAdapter（RTSP + RTMP）→ 客户端。音频流并行处理：音频解码 → PCM 重采样 → AudioOutputAdapter → RTSP 音频通道。

分析结果并行流向 EventLogger（JSONL 持久化）和 StreamApiServer（REST API 查询）。

---

## 3. 目录结构说明

项目根目录下的主要目录和文件及其用途如下表所示：

| 路径 | 类型 | 说明 |
|------|------|------|
| `CMakeLists.txt` | 文件 | CMake 构建配置（主构建方式），定义 4 个构建目标 + 5 个 test 目标 + stress 目标 |
| `README.md` / `README_CN.md` | 文件 | 中英文项目说明，含功能介绍、快速开始指南和目录结构 |
| `LICENSE` | 文件 | MIT 开源许可证 |
| `docs/` | 目录 | 项目文档，含架构说明、API 接口文档、安装指南、CDN 设计、延迟测试方案和技术问答 |
| `example/` | 目录 | 4 个入口程序源文件，主入口为 ffmpeg_streamer.cpp |
| `src/` | 目录 | 全部库和模块源码，按功能分为 8 个子目录（net/xop/ai/ffmpeg/effect/api/observe/3rdpart） |
| `tests/` | 目录 | 测试源文件，覆盖 EventBus、EffectFactory、StreamSession、StreamApiServer 及 stress tester |
| `scripts/` | 目录 | 辅助脚本：analyze_latency.py（延迟分析）、run_latency_baseline.sh（一键基线测试）、stress_test.py（压力测试）、tune_kernel.sh |
| `models/` | 目录 | AI 模型文件存放目录，需单独下载 ONNX 模型 |
| `pic/` | 目录 | 测试用媒体资源（图片、视频文件） |
| `build/` | 目录 | CMake 构建目录，编译产物输出到 build/bin/ |
| `runtime/` | 目录 | 运行时数据目录（latency_events.jsonl、events.jsonl 等） |

---

## 4. 核心模块分析

### 4.1 网络基础模块（src/net）

该模块实现了一个基于 Reactor 模式的跨平台异步事件驱动网络框架，是整个系统的通信基础。

**事件循环与调度器**

- `EventLoop` 是事件循环的核心入口，封装了 I/O 事件的分发逻辑。
- `TaskScheduler` 为任务调度器的抽象基类，定义了事件注册、移除和循环调度的统一接口。
- `EpollTaskScheduler` 和 `SelectTaskScheduler` 分别是 Linux（epoll）和 Windows（select）平台的具体实现，通过条件编译在编译期选择。
- `Channel` 封装了文件描述符及其关注的事件类型和回调函数，是事件分发的基本单元。

**TCP 通信组件**

- `TcpServer` 提供 TCP 服务端能力，管理 Acceptor 和所有活跃连接的声明周期。
- `TcpConnection` 表示单个 TCP 连接，维护收发缓冲区，处理消息的读取与发送。
- `TcpSocket` 封装底层 socket 操作（创建、绑定、监听、设置选项）。
- `Acceptor` 负责监听端口并接受新连接，将新 socket 注册到事件循环中。

**数据缓冲与读写**

- `BufferReader` 和 `BufferWriter` 提供面向消息的二进制流读写操作，支持按字节序读写整型、字符串等数据类型。
- `RingBuffer` 是一个循环缓冲区，用于高效的内存数据中转，常用于音视频数据的暂存。

**辅助组件**

- `Timer` 提供基于时间的事件触发机制，支持单次和周期定时任务。
- `Timestamp` 封装高精度时间戳操作。
- `Pipe` 封装 Unix 管道，用于线程间或进程间的通知机制。
- `MemoryManager` 提供内存池管理，减少频繁内存分配带来的性能开销。
- `Logger` 和 `log.h` 提供统一的日志宏，支持多级别日志输出。

### 4.2 RTSP 与媒体模块（src/xop）

该模块实现了完整的 RTSP/RTP 协议栈，支持多种音视频编码格式的流化传输。

**RTSP 协议组件**

- `RtspServer` 是 RTSP 服务端的顶层类，负责管理 MediaSession 集合，提供会话创建、查找和删除接口。
- `RtspConnection` 处理单个客户端的 RTSP 交互流程，解析 RTSP 请求消息（OPTIONS、DESCRIBE、SETUP、PLAY、TEARDOWN 等），维护 RTP 传输通道。
- `RtspMessage` 封装 RTSP 请求和响应消息的构建与解析。
- `RtspPusher` 实现 RTSP 推流端，可向远端 RTSP 服务器推送媒体流。
- `RtpConnection` 管理 RTP 数据包的封包与发送，支持 RTP over TCP 和 RTP over UDP 两种传输模式。
- `DigestAuthentication` 实现 RFC 2617 摘要认证，提供 RTSP 服务的安全访问控制。

**媒体源组件**

- `MediaSource` 是所有媒体源的抽象基类，定义了获取 SDP 媒体描述和帧数据的统一接口。
- `H264Source` 和 `H264Parser` 负责 H.264 码流的封装、SPS/PPS 解析和时间戳管理。
- `H265Source` 提供 H.265/HEVC 码流的 RTSP 封装支持。
- `AACSource`、`G711ASource` 和 `VP8Source` 分别提供 AAC 音频、G.711A 音频和 VP8 视频格式的支持。
- `RtcpMessage` 实现 RTCP Sender Report (SR) 报文的构建与 NTP 时间戳生成，`RtpConnection` 每 5 秒周期性发送 SR，提供 NTP↔RTP 时间戳映射供客户端计算延迟。
- `SeiLatencyMarker` 提供 H.264 SEI NAL 单元的注入工具，可在 IDR 帧前嵌入发送端时间戳，用于跨机器延迟测量。

**媒体会话管理**

- `MediaSession` 表示一个 RTSP 会话，关联唯一的会话标识符（suffix），管理该会话下的多个媒体源（同一会话可同时包含视频和音频轨道）。会话还维护了客户端连接和断开的通知回调，供上层进行连接状态跟踪。

- `media.h`、`rtp.h` 和 `rtsp.h` 分别定义了媒体类型枚举、RTP 包头结构和 RTSP 协议常量。

### 4.3 AI 智能分析模块（src/ai）

该模块实现了从视频源接入到 AI 分析再到编码输出的完整数据处理管线。

**视频源接入**

- `VideoSource` 是视频源的抽象接口，定义了 Open、Close、GrabFrame、GetWidth、GetHeight、GetFPS 等统一操作。
- `CameraSource` 基于 OpenCV VideoCapture 实现 USB/V4L2 摄像头的实时帧采集。
- `FileSource` 实现本地视频文件的读取，支持循环播放模式。
- `RtspPullSource` 通过 FFmpeg 子进程从远端 RTSP 地址拉流并解码为 OpenCV 帧数据。

**人脸检测与识别**

- `FaceDetector` 基于 OpenCV DNN 模块加载 ONNX 模型（YuNet），对输入帧进行人脸检测，输出人脸边界框和置信度。
- `FaceRecognizer` 加载 ArcFace ONNX 模型，从检测到的人脸区域提取 512 维特征向量，用于后续的余弦相似度比对。
- `FaceDatabase` 管理人脸特征库，支持从 JSON 文件加载和持久化保存，提供特征向量的注册、删除和最近邻查询。

**分析调度与画面处理**

- `FrameAnalyzer` 作为分析调度的核心，控制分析频率（可配置的每秒分析帧数），协调检测、识别和数据库查询流程，生成统一的 `AnalysisResult` 结构。支持注册事件回调函数，当检测结果中包含新人脸时触发通知。
- `FrameOverlay` 在原始帧上绘制人脸边界框和识别结果标签（姓名或 "unknown"），生成标注后的输出帧。

**编码与输出**

- `H264Encoder` 通过 FFmpeg 子进程实现 H.264 软编码。父进程通过管道将 OpenCV 帧数据（BGR 或 YUV）写入 FFmpeg 的 stdin，后台读取线程从 FFmpeg 的 stdout 解析 NAL 单元，通过回调函数将编码后的 H.264 数据推入 RTSP 服务端。

**日志与 Web 服务**

- `EventLogger` 以 JSON Lines 格式将检测事件写入 `events.jsonl` 文件，每条事件包含时间戳、帧序号和人脸列表。
- `HttpApiServer` 基于 cpp-httplib 库提供 REST API 服务，支持查询当前帧分析结果、历史事件列表、人脸库管理（增删查）和服务状态。

### 4.4 FFmpeg C API 管线模块（src/ffmpeg）

该模块基于 FFmpeg C API（libavformat / libavcodec / libavutil / libswscale）实现了进程内媒体管线，替代了旧有的 fork/pipe FFmpeg 子进程方案。当前包含以下核心组件：

**StreamSession — 统一会话抽象**

- `StreamSession` 是面向使用者的单流会话入口，将 EventLoop、RtspServer、MediaSession、FaceRecognitionPlugin、EffectChain、StreamPipeline/FFmpegStreamer、OutputAdapter 封装在统一的 Start/Stop/GetStatus 接口之后。
- `StreamSessionConfig` 统一配置输入源、编码参数、管线模式（serial/parallel）、RingBuffer 大小、AI 开关、音频开关、RTMP URL 和 Effect JSON 配置。
- 支持两种管线模式：`serial`（FFmpegStreamer 单线程 demux+decode+AI+encode+output）和 `parallel`（StreamPipeline 3-stage 并行管线）。
- 内建 client-aware pipeline gating：仅在有 RTSP 客户端连接时驱动管线，无客户端时阻塞等待，节省 CPU。
- `SessionStatus` 提供 frames_processed、frames_dropped、uptime_seconds、ring fill、backpressure_events 等运行时状态。
- 通过 `SessionEventBus`（EventBus<FrameProcessedEvent>）向外部观察者发布帧处理事件。
- 支持 `UpdateEffects(effects_json)` 动态替换 EffectChain。

**FFmpegStreamer — 单线程管线（serial 模式）**

- 在单个线程内完成解封装→视频解码→BGR 转换→AI 回调→YUV 转换→编码→多输出的全流程。
- 支持视频和音频双轨处理：音频包被解码后通过 AudioOutputAdapter 推入 RTSP 音频通道。
- 适用于简单场景，延迟最低（无线程切换开销）。

**StreamPipeline — 3-stage 并行管线（parallel 模式）**

- 参考 SRS 设计，将处理流程拆分为三个独立线程 stage：
  - Stage 1: Demux+Decode（解封装 + 视频解码 → BGR24）
  - Stage 2: AI Process（EffectChain 有序执行，帧叠加）
  - Stage 3: Encode+Output（YUV 转换 + libx264 编码 + 多协议输出）
- Stage 间通过 `RingBuffer<DecodedFrame>` 和 `RingBuffer<ProcessedFrame>` 连接，实现线程隔离。
- `PipelineConfig` 统一配置各 stage 参数、RingBuffer 大小、FrameDropPolicy 和输出适配器列表。
- 支持 client-aware gating：DemuxDecodeLoop 在无客户端时阻塞等待。
- 提供详细的 stats：frames_decoded/dropped/pruned、ring fill、backpressure_events。

**IOutputAdapter — 输出适配器体系**

- `IOutputAdapter` 是输出适配器的抽象接口，定义 PushFrame/PushAudio 统一方法。
- `RtspOutputAdapter` 将编码帧推入 RTSP 会话。
- `RtmpOutputAdapter` 通过 RTMP 推流到外部 SRS/nginx-rtmp 服务器。
- `MultiOutputAdapter` 支持多路同时输出（如 RTSP + RTMP）。
- `AudioOutputAdapter` 将 FFmpeg 解码的 PCM 音频帧构建为 `xop::AVFrame` 并推入 RTSP 音频通道。

**FrameDropPolicy — 自适应丢帧策略**

- `max_frame_age_us`：帧最大存活时间，超过此时间的帧视为过期。
- `time_window_us`：滑动时间窗口宽度，窗口外的旧帧被修剪。
- `start_drop_ratio`：RingBuffer 填充比例阈值，超过此比例时启动主动时间修剪。
- `prefer_keep_keyframe`：优先保留 I 帧（关键帧）而非 P 帧。
- 在 PushOrDrop（被动，buffer 满时）和 PruneStale（主动，fill ratio 超阈值时）两处生效。

**StreamerConfig** 统一配置输入源、编码参数（libx264 preset/tune/bitrate/GOP）、输出适配器列表和 AI/音频回调。

### 4.5 EffectPlugin 插件模块（src/effect）

该模块提供可扩展的视频特效/分析插件体系，所有插件实现统一接口，支持有序链式执行和 JSON 配置化创建。

**IEffectPlugin — 统一插件接口**

- 定义 `Name()`、`Category()`、`Open(config_json)`、`Close()`、`Process(bgr_data, w, h, linesize, result)`、`ModifiesFrame()` 六个纯虚方法。
- 插件按 `EffectCategory` 分为四类：Analysis（检测、识别）、Overlay（水印、标注框叠加）、Transform（美颜、色彩校正）、Extract（关键帧提取）。
- `Process()` 直接操作 BGR24 原始像素数据（可原地修改），并通过 `EffectResult` 输出结构化分析结果。
- `ModifiesFrame()` 标记是否修改像素数据，用于优化（只读分析的插件可跳过帧拷贝）。

**EffectChain — 有序执行链**

- 维护 `IEffectPlugin` 的有序列表，按注册顺序依次执行。
- `ProcessFrame()` 将每帧依次通过所有插件，收集所有插件的 `EffectResult`。

**FaceRecognitionPlugin — 内置人脸识别插件**

- 实现完整的人脸检测+识别+数据库查询+叠加标注功能，是 `IEffectPlugin` 的首个 reference implementation。
- 内部聚合 FaceDetector（YuNet ONNX）、FaceRecognizer（ArcFace ONNX）、FaceDatabase 和 FrameOverlay。
- 通过 JSON config 配置模型路径、数据库路径、分析帧率等参数。

**EffectFactory — JSON 配置化创建**

- 静态工厂方法 `Create(name, config_json)` 根据名称字符串查找已注册的插件创建器。
- 通过 `Register(name, creator)` 支持自定义插件注册扩展。
- 内置 `FaceRecognition` 插件的注册和创建逻辑。

### 4.6 API 服务模块（src/api）

**StreamApiServer — 合并式 HTTP API 服务**

- 将旧版路由（原 `ai::HttpApiServer`）和 v1 session 管理 API 合并到单一端口（默认 8080）。
- 旧版路由：`/api/status`、`/api/current`、`/api/events`、`/api/faces`（CRUD）、`/api/latency/stats`、`/api/latency/recent`、`/api/latency/reset`。
- v1 session 路由：`GET/POST /api/v1/sessions`、`GET/DELETE /api/v1/sessions/:id`、`PUT /api/v1/sessions/:id/effects`、`GET /api/v1/sessions/:id/results`。
- Session 注册表支持 CreateSession（从 config 自动创建 StreamSession）、RegisterSession（注册已创建的 session）、RemoveSession、GetSessionStatus、ListSessions。
- 线程安全：result/event 缓存和 session 注册表分别使用独立 mutex 保护。
- 基于 cpp-httplib 库，内置 CORS 支持。

### 4.7 可观测性模块（src/observe）

- `MetricsRegistry` 是一个线程安全的内存键值指标注册表，支持整型和浮点型指标的自增、赋值和读取。通过互斥锁保证多线程环境下的数据一致性，提供 `Snapshot()` 方法导出当前全部指标的键值对快照。
- `LatencyTracer` 是基于 RAII 的延迟追踪系统，通过 `LATENCY_TRACE_SCOPE(name)` 宏在关键模块入口/出口自动记录耗时。数据以 JSONL 格式输出，支持环境变量开关控制（零性能开销），配套 Python 分析脚本计算 avg/p50/p95/p99/max/jitter。
- `EventBus<EventType>` 是模板化的线程安全事件发布/订阅系统。通过 `Subscribe(fn)` 注册回调并返回 Handle，`Unsubscribe(handle)` 取消订阅，`Publish(event)` 同步通知所有订阅者。使用 `std::recursive_mutex` 保证在回调中安全地订阅/取消订阅。

当前采集的指标覆盖流级维度（pipeline 延迟、帧计数等）。EventBus 用于 StreamSession 向外部观察者发布 FrameProcessedEvent，实现 AI 分析结果与 API 服务的松耦合通信。

---

## 5. 示例程序说明

| 程序 | 源文件 | 用途 |
|------|--------|------|
| `rtsp_server` | `example/rtsp_server.cpp` | 基础 RTSP 服务端示例，展示如何创建 EventLoop、RtspServer 和 MediaSession |
| `rtsp_pusher` | `example/rtsp_pusher.cpp` | RTSP 推流器示例，展示如何将本地流推送到远端 RTSP 服务器 |
| `rtsp_h264_file` | `example/rtsp_h264_file.cpp` | 本地 H.264 文件推流示例，读取 H.264 裸流文件并通过 RTSP 分发 |
| `ffmpeg_streamer` | `example/ffmpeg_streamer.cpp` | ★ 主入口：StreamSession + StreamApiServer，支持 serial/parallel 管线、EffectPlugin、RTMP 输出、session CRUD |
| `streamsight-stress` | `tests/stress_tester.cpp` | 压力测试工具，支持多流并发、性能基线采集和 backpressure 验证 |
| `test_*` | `tests/test_*.cpp` | 单元/集成测试：test_event_bus、test_effect_factory、test_stream_session、test_api_server |

---

## 6. 模型与测试资源

### 6.1 AI 模型

项目依赖两个 ONNX 格式的深度学习模型，需放置在 `models/` 目录下：

| 模型文件 | 用途 | 来源 |
|----------|------|------|
| `face_detection.onnx` | 人脸检测（YuNet），输入 BGR 图像，输出人脸边界框和置信度 | OpenCV Zoo |
| `face_recognition.onnx` | 人脸识别（SFace/ArcFace），输入对齐后的人脸区域，输出 512 维特征向量 | OpenCV Zoo |

### 6.2 测试资源

`pic/` 目录下的测试资源用于开发调试和功能验证：

| 文件 | 用途 |
|------|------|
| `test.h264` | H.264 裸流文件，用于 FileSource 输入测试 |
| `test.mp4` | MP4 封装视频文件，用于视频源兼容性测试 |
| `test.png` | 静态人脸图片，用于人脸注册和识别精度测试 |
| `1.pic.JPG` | 项目架构示意图（供 README 引用） |

---

## 7. 系统运行流程

### 7.1 流媒体处理典型链路

以 `ffmpeg_streamer`（主入口）为例，系统启动后的完整运行流程如下：

1. **初始化阶段**：解析命令行参数构建 `StreamSessionConfig`，创建 `StreamSession`。Session 内部创建 EventLoop + RtspServer 并绑定端口，创建 MediaSession 注册 H.264/AAC 源，按需通过 EffectFactory 创建 FaceRecognitionPlugin 并加入 EffectChain，初始化输出适配器（RTSP/RTMP）。

2. **运行阶段**（serial 单线程 / parallel 3-stage 并行管线）：
   - 输入源 → 解封装 → 解码 → BGR24 转换 → EffectChain（有序执行 IEffectPlugin，完成人脸检测/识别/叠加）→ YUV420P 转换 → libx264 编码 → 输出适配器将 NAL 单元推入 RTSP 会话 → RTP 打包发送给客户端。
   - 分析结果通过 EventBus 发布 `FrameProcessedEvent`，供外部观察者订阅。

3. **退出阶段**：收到终止信号后，停止管线线程和 RTSP 事件循环，关闭 EffectPlugin 和视频源，释放资源。

### 7.2 RTSP 客户端交互流程

客户端通过标准 RTSP 协议与服务器交互：OPTIONS（能力查询）→ DESCRIBE（获取 SDP 媒体描述）→ SETUP（建立 RTP 传输通道）→ PLAY（开始播放）→ TEARDOWN（结束会话）。RTP 数据包通过 TCP 或 UDP 通道持续推送给客户端，支持 H.264 关键帧索引和音视频同步。

---

## 8. 架构特点

1. **模块分层清晰**：网络层、协议层、分析层、管线层、插件层、API 层、观测层各层独立，接口明确，易于理解和维护。
2. **网络层与业务层解耦**：基于 Reactor 模式的 EventLoop 提供了通用的异步 I/O 能力，RTSP 和 AI 模块均构建在统一的事件驱动框架之上。
3. **多类型视频源支持**：通过 FFmpeg 统一了文件、摄像头、RTSP 拉流等输入方式，扩展新输入类型无需改动管线。
4. **AI 可插拔设计**：AI 分析作为可选模块，通过 `--no-ai` 参数可降级为纯编码推流模式，AI 模型加载失败时自动回退。
5. **可观测性嵌入**：MetricsRegistry 以横切方式注入各模块，支持流级指标采集。
6. **跨平台兼容**：通过 EpollTaskScheduler 和 SelectTaskScheduler 的条件编译，同时支持 Linux 和 Windows 平台。
7. **多协议媒体格式支持**：覆盖 H.264、H.265、AAC、G.711A、VP8 等主流音视频编码格式。
8. **工程教学价值高**：项目融合了网络编程、流媒体协议、AI 推理、插件化架构和系统观测等多个技术领域，适合作为面试展示、课程设计或工程优化实践项目。

---

## 9. 当前架构可能的优化方向

1. **配置中心化**：当前配置分散在命令行参数和硬编码常量中，可引入配置文件（如 YAML/TOML）统一管理运行参数。
2. **日志系统统一化**：各模块使用不同的日志输出方式，可统一为结构化日志框架，支持日志级别动态调整和文件轮转。
3. **EffectPlugin 生态扩展**：当前仅有 FaceRecognitionPlugin，可扩展水印、马赛克、安全检测、美颜等插件。
4. **StreamPipeline stage 间零拷贝优化**：当前 DecodedFrame/ProcessedFrame 通过 shared_ptr 传递像素数据，可改为环形缓冲区 + 指针传递减少分配开销。
5. **RTSP 会话管理增强**：增加会话超时回收、并发流数限制和带宽统计能力。
6. **HTTP API 文档完善**：补充 OpenAPI/Swagger 规范文档，便于前端对接和自动化测试。
7. **测试体系扩展**：当前已有基础测试框架，可进一步提升覆盖率和增加集成测试场景。
8. **Docker 化部署**：编写 Dockerfile 和 docker-compose，实现一键构建和运行，降低环境搭建成本。
9. **CI/CD 构建流程完善**：接入 GitHub Actions 或 Jenkins，实现自动化编译、测试和发布。
10. **录制与多协议输出**：当前支持 RTSP 和 RTMP 输出，可扩展 MP4 录制和 HLS 切片输出能力。
11. **Content Understanding 集成**：Phase 3 规划中，接入视频摘要、场景理解等更高级的 AI 能力。

---

## 10. 总结

StreamSight 是一个融合了网络编程、流媒体协议、AI 视频分析、插件化架构和系统可观测能力的综合型 C++ 项目。它以自研 RTSP/RTP 协议栈和 FFmpeg C API 进程内管线为核心，通过分层架构实现了从视频接入、AI 分析、画面叠加、视频编码到多协议分发的完整媒体处理链路。引入了 EffectPlugin 可扩展插件体系、StreamSession 统一会话抽象、StreamPipeline 3-stage 并行管线、StreamApiServer 合并式 API 服务和 EventBus 事件总线，将项目从一个"单链路流媒体分析程序"升级为"具备平台化能力的流媒体处理系统"。

该项目在工程上具有较高的完整性和可展示性：网络层封装扎实、协议栈实现规范、管线架构清晰、插件体系可扩展。对于学习和展示 C++ 工程能力、流媒体系统设计思想以及平台化架构设计而言，是一个兼具深度和广度的实践项目。

---

## 附录：系统架构文字版示意图

```
StreamSight 系统架构 (数据流 / 控制流)

┌─────────────────────────────────────────────────────────────────────────┐
│                           可观测性层 (src/observe/)                      │
│  MetricsRegistry: 流级指标 │ LatencyTracer: RAII 延迟追踪 │ EventBus     │
└────────────────────────────┬────────────────────────────────────────────┘
                             │ 指标采集 / 事件订阅 (横切注入)
                             ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                          API 服务层 (src/api/)                           │
│                                                                         │
│  StreamApiServer: sessions_ (唯一多流注册表)                              │
│    GET/POST/DELETE /api/v1/sessions  ·  /api/current  ·  /api/faces      │
└────────────────────────────┬────────────────────────────────────────────┘
                             │ 创建 / 管理 StreamSession
                             ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                        FFmpeg 管线层 (src/ffmpeg/)                       │
│                                                                         │
│  StreamSession (单流会话: Start/Stop/GetStatus/EventBus)                  │
│    ├─ serial   → FFmpegStreamer (单线程 demux+decode+AI+encode+output)    │
│    └─ parallel → StreamPipeline (3-stage: Demux→AI→Encode + RingBuffer)   │
│                      │                                                   │
│                      │ Stage2: AI Process                                │
│                      ▼                                                   │
│            EffectChain (有序执行 IEffectPlugin[])                         │
│              └─ FaceRecognitionPlugin (内置)                              │
│                   └─ ai::FaceDetector/FaceRecognizer/FaceDatabase/Overlay │
│                                                                         │
│  输出适配器: RtspOutputAdapter → RTSP   ·   RtmpOutputAdapter → RTMP      │
└────────────────────────────┬────────────────────────────────────────────┘
                             │ NAL 单元 / 音视频帧
                             ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                        RTSP 流媒体层 (src/xop/)                           │
│                                                                          │
│  RtspServer ◀── RtspConnection ◀── RtspMessage (解析 RTSP 请求)           │
│      │                                                                   │
│      ▼                                                                   │
│  MediaSession ──▶ H264Source / H265Source / AACSource / G711ASource      │
└────────────────────────────┬────────────────────────────────────────────┘
                             │ 异步 I/O 事件驱动
                             ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                         网络基础层 (src/net/)                             │
│                                                                          │
│  EventLoop (事件循环)                                                     │
│      ├── TaskScheduler ◀── EpollTaskScheduler (Linux epoll)              │
│      │                 ◀── SelectTaskScheduler (Windows select)          │
│      ├── TcpServer ──▶ Acceptor ──▶ TcpConnection ──▶ TcpSocket          │
│      ├── Channel (fd + 事件回调)  ·  Timer (定时任务)                     │
│      └── BufferReader/Writer · RingBuffer · MemoryManager                │
└────────────────────────────┬────────────────────────────────────────────┘
                             │ 数据流入
                             ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                          视频源 / 客户端                                   │
│                                                                          │
│  摄像头/本地文件/RTSP 源 (FFmpeg 输入)   RTSP 客户端 ──▶ ffplay / VLC     │
│                                        HTTP 客户端 ──▶ curl / 浏览器      │
└─────────────────────────────────────────────────────────────────────────┘

数据流主线:
  输入源 → FFmpeg 解封装/解码 → BGR24 → EffectChain(IEffectPlugin[])
  → YUV420P → libx264 编码 → OutputAdapter → RtspServer::PushFrame → RTP → 客户端

旁路输出:
  FrameProcessedEvent → EventBus → StreamApiServer (GET /api/v1/sessions/:id/results)
```
