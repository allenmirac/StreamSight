# StreamSight 项目架构说明文档

## 1. 项目概述

StreamSight 是一个基于 C++11 的实时视频流媒体与智能分析系统。项目以高性能 RTSP/RTP 流媒体服务为核心，集成 AI 人脸检测与识别能力，并扩展了 CDN 风格边缘节点模拟调度层和系统可观测性组件，形成了一套覆盖"采集—分析—编码—分发—调度—观测"完整链路的流媒体处理平台原型。

项目的核心能力包括：

- **流媒体基础能力**：支持 H.264、H.265、G711A、AAC、VP8 等多种音视频格式的 RTSP 推流与分发，支持单播、组播及摘要认证。
- **AI 视频分析能力**：基于 OpenCV DNN 和 ONNX 模型实现实时人脸检测（YuNet）与人脸识别（ArcFace），支持人脸库管理、视频帧叠加标注和检测事件记录。
- **边缘调度模拟能力**：在单进程内模拟异构边缘节点池，通过分类器对流任务进行多维画像，由加权评分调度器完成就近分发和负载均衡。
- **故障切换能力**：当边缘节点执行失败时，系统支持排除故障节点后的重新调度和任务恢复。
- **可观测性能力**：提供流级、节点级和调度级的运行时指标采集与查询接口。
- **HTTP 服务能力**：内嵌 REST API 供外部查询分析结果、管理人脸库和获取服务状态。

---

## 2. 总体架构

StreamSight 采用分层架构设计，从底层网络通信到上层业务调度共划分为七个核心模块层，各层职责明确、耦合度低。

### 2.1 核心层次划分

| 层次 | 目录 | 职责 |
|------|------|------|
| 网络通信层 | `src/net/` | 提供基于 Reactor 模式的异步事件驱动网络框架，封装 epoll/select、TCP 连接管理、定时器和内存管理 |
| RTSP 流媒体层 | `src/xop/` | 实现 RTSP/RTP 协议栈，包括服务端、推流端、媒体会话管理、多种编码格式的媒体源封装 |
| AI 分析层 | `src/ai/` | 提供多类型视频源接入、人脸检测与识别、帧分析调度、画面叠加绘制、H.264 软编码、事件日志和 HTTP API 服务 |
| 控制调度层 | `src/control/` | 定义流任务抽象，实现流分类、加权评分调度、流水线执行编排和故障切换逻辑 |
| CDN 模拟层 | `src/cdn_sim/` | 模拟异构边缘节点及其线程池，提供节点接纳判断、负载评分和能力匹配计算 |
| 可观测性层 | `src/observe/` | 提供线程安全的内存键值指标注册与快照导出 |
| 示例程序层 | `example/` | 包含不同场景的入口程序，覆盖基础 RTSP 服务、推流、AI 分析服务和边缘调度服务 |

### 2.2 模块调用关系

网络通信层为所有上层模块提供异步 I/O 能力。RTSP 流媒体层构建在网络层之上，对外暴露 RTSP 服务端和推流端接口。AI 分析层通过视频源接口获取帧数据，经分析管线处理后，将标注后的帧送入 H.264 编码器，最终由 RTSP 层完成分发。控制调度层将整条 AI 分析流水线封装为可调度的流任务，通过 CDN 模拟层提供的边缘节点池进行任务分发和执行。可观测性层以横切方式嵌入各层，采集和暴露运行指标。

### 2.3 数据流转概览

核心数据流的路径为：视频源采集原始帧 → AI 分析管线处理（检测、识别、叠加）→ H.264 编码 → RTSP/RTP 打包分发 → 客户端播放。同时，分析结果并行流向事件日志系统和 HTTP API 服务，供外部查询和持久化。

---

## 3. 目录结构说明

项目根目录下的主要目录和文件及其用途如下表所示：

| 路径 | 类型 | 说明 |
|------|------|------|
| `CLAUDE.md` | 文件 | 面向 Claude Code 的项目指南，包含构建命令、运行示例和架构概述 |
| `CMakeLists.txt` | 文件 | CMake 构建配置，支持 GCC/MinGW，自动查找 OpenCV 依赖 |
| `Makefile` | 文件 | GNU Make 构建配置（主要构建方式），定义 5 个构建目标及其依赖关系 |
| `README.md` / `README_CN.md` | 文件 | 中英文项目说明，含功能介绍、快速开始指南和目录结构 |
| `LICENSE` | 文件 | MIT 开源许可证 |
| `docs/` | 目录 | 项目文档，含架构说明、API 接口文档、安装指南和技术问答 |
| `example/` | 目录 | 5 个入口程序源文件，覆盖不同使用场景 |
| `src/` | 目录 | 全部库和模块源码，按功能分为 7 个子目录 |
| `models/` | 目录 | AI 模型文件存放目录，需单独下载 ONNX 模型 |
| `pic/` | 目录 | 测试用媒体资源（图片、视频文件） |
| `bin/` | 目录 | 编译产物输出目录，运行时的工作目录 |
| `build/` | 目录 | CMake 构建目录（可选构建方式） |
| `objs/` | 目录 | Make 构建产生的中间目标文件 |

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

### 4.4 控制调度模块（src/control）

该模块构建在数据平面之上，将单路流媒体处理管线抽象为可调度的流任务，实现了流分类、节点选择和故障切换的控制逻辑。

**任务抽象与分类**

- `StreamTask` 将所有流请求统一抽象为任务对象，包含流 ID、输入类型、分辨率、帧率、目标码率、区域标签、AI 开关、分析频率等字段。这层抽象使调度器面对统一的任务接口，无需关心底层管线细节。
- `Classifier` 对 StreamTask 进行多维分类，根据码率划分 BitrateClass（Low/Medium/High/Ultra），根据分辨率和 AI 负载划分 ComputeClass（Light/Medium/Heavy/Extreme），根据实时性需求划分 LatencyClass（Realtime/Interactive/Standard/Archive）。

**调度策略**

- `SchedulerPolicy`（定义于 PolicyCenter.h）封装调度权重配置，包括区域亲和权重、负载权重、能力匹配权重、时延权重和故障惩罚权重，以及队列深度和利用率的过滤阈值。
- `Scheduler` 实现加权评分调度算法：对边缘节点池中的候选节点先进行准入过滤（排除宕机、过载、能力不匹配的节点），再计算各维度的加权分数，最终选择综合分数最优的节点。

**流管理与执行**

- `StreamManager` 是控制平面的总协调器。它持有统一的 RTSP 服务端，接收 `StartStream` 请求后，先调用分类器打标签，再调用调度器选节点，最后将任务分发到目标边缘节点执行。它还负责故障切换：当 `PipelineRunner` 返回失败状态时，排除当前节点重新调度。
- `PipelineRunner` 在边缘节点的工作线程中执行完整的媒体处理管线（采集、AI 分析、编码、RTSP 推流），封装了原 `rtsp_analysis_server` 的全部逻辑。

### 4.5 CDN 边缘模拟模块（src/cdn_sim）

该模块在单进程内模拟具有不同算力等级的边缘节点，为调度器提供异构的计算资源抽象。

- `EdgeNode` 表示一个边缘节点，包含节点 ID、区域标签、能力层级（HighCapacity/MediumCapacity/LowCapacity）、工作线程数、最大流数、最大队列深度等属性。节点实时追踪活跃流数和工作线程利用率，根据负载动态计算健康状态（Healthy/Busy/Degraded/Down）。提供 `CanAccept`（准入判断）和 `Score`（调度评分）方法，供调度器调用。
- `EdgeNodePool` 管理一组 EdgeNode 实例，提供节点的增删和遍历接口。
- `ThreadPool` 是一个固定大小的线程池，每个 EdgeNode 拥有独立的线程池实例，用于并发执行分发到该节点的流处理任务。

该模块的价值在于：不需要真实的多机部署环境，即可在单机内验证就近分发、负载均衡和故障切换等 CDN 调度策略的有效性。

### 4.6 可观测性模块（src/observe）

- `MetricsRegistry` 是一个线程安全的内存键值指标注册表，支持整型和浮点型指标的自增、赋值和读取。通过互斥锁保证多线程环境下的数据一致性，提供 `Snapshot()` 方法导出当前全部指标的键值对快照。

当前采集的指标覆盖三个维度：流级指标（pipeline 延迟、帧计数、failover 次数）、节点级指标（活跃流数、调度次数、平均延迟）和调度级指标（调度总次数、failover 总次数）。

---

## 5. 示例程序说明

| 程序 | 源文件 | 用途 |
|------|--------|------|
| `rtsp_server` | `example/rtsp_server.cpp` | 基础 RTSP 服务端示例，展示如何创建 EventLoop、RtspServer 和 MediaSession |
| `rtsp_pusher` | `example/rtsp_pusher.cpp` | RTSP 推流器示例，展示如何将本地流推送到远端 RTSP 服务器 |
| `rtsp_h264_file` | `example/rtsp_h264_file.cpp` | 本地 H.264 文件推流示例，读取 H.264 裸流文件并通过 RTSP 分发 |
| `rtsp_analysis_server` | `example/rtsp_analysis_server.cpp` | 完整 AI 分析管线入口，整合视频源、人脸检测识别、画面叠加、编码和 RTSP 分发，支持三种输入模式和 HTTP API 服务 |
| `rtsp_edge_analysis_server` | `example/rtsp_edge_analysis_server.cpp` | CDN 边缘调度原型入口，在分析管线之上增加流任务分类、多节点调度和故障切换能力 |

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

### 7.1 AI 分析服务典型链路

以 `rtsp_analysis_server` 为例，系统启动后的完整运行流程如下：

1. **初始化阶段**：解析命令行参数，创建视频源对象（摄像头/文件/RTSP 拉流）并打开。按需加载 AI 模型和创建分析组件。初始化 H.264 编码器（启动 FFmpeg 子进程）。创建 RTSP 服务端并绑定端口，创建 MediaSession 并注册编码器输出回调。启动 HTTP API 服务。

2. **运行阶段**（管线线程循环）：
   - 从视频源抓取一帧 → 按目标分辨率缩放 → 按配置频率执行 AI 分析（人脸检测 → 特征提取 → 数据库匹配）→ 在帧上绘制检测框和识别结果 → 送入 H.264 编码器 → 编码器回调将 NAL 单元推入 RTSP 会话 → RTP 打包发送给客户端。
   - 分析结果并行写入事件日志文件（`events.jsonl`）和 HTTP API 缓存，供外部查询。

3. **退出阶段**：收到终止信号后，停止管线线程和 RTSP 事件循环，关闭编码器和视频源，保存人脸数据库，释放资源。

### 7.2 边缘调度服务流程

以 `rtsp_edge_analysis_server` 为例，在分析管线基础上增加了调度链路：

1. 创建多个 EdgeNode（不同区域和算力等级），初始化 EdgeNodePool。
2. 构建 SchedulerPolicy（配置各维度权重和阈值），创建 Scheduler。
3. StreamManager 统一持有 RTSP 服务端。
4. 调用 `StartStream(task)` → Classifier 对任务打分类标签 → Scheduler 遍历候选节点并评分 → 选择最优节点 → 节点线程池执行 PipelineRunner → 管线运行。
5. 若管线执行失败且未超过 failover 上限，排除当前故障节点重新调度到其他节点。

### 7.3 RTSP 客户端交互流程

客户端通过标准 RTSP 协议与服务器交互：OPTIONS（能力查询）→ DESCRIBE（获取 SDP 媒体描述）→ SETUP（建立 RTP 传输通道）→ PLAY（开始播放）→ TEARDOWN（结束会话）。RTP 数据包通过 TCP 或 UDP 通道持续推送给客户端，支持 H.264 关键帧索引和音视频同步。

---

## 8. 架构特点

1. **模块分层清晰**：网络层、协议层、分析层、控制层、模拟层、观测层各层独立，接口明确，易于理解和维护。
2. **网络层与业务层解耦**：基于 Reactor 模式的 EventLoop 提供了通用的异步 I/O 能力，RTSP 和 AI 模块均构建在统一的事件驱动框架之上。
3. **多类型视频源支持**：通过 VideoSource 抽象接口统一了摄像头、本地文件和 RTSP 拉流三种输入方式，扩展新输入类型只需实现接口即可。
4. **AI 可插拔设计**：AI 分析作为可选模块，通过 `--no-ai` 参数可降级为纯编码推流模式，AI 模型加载失败时自动回退。
5. **控制面与数据面分离**：StreamManager/Scheduler 负责调度决策，EdgeNode/PipelineRunner 负责执行，符合平台化设计思想。
6. **边缘场景模拟验证**：在单机内通过异构节点池模拟就近分发、负载均衡和故障切换，降低了架构验证的环境门槛。
7. **可观测性嵌入**：MetricsRegistry 以横切方式注入各模块，支持流级、节点级和调度级指标采集。
8. **跨平台兼容**：通过 EpollTaskScheduler 和 SelectTaskScheduler 的条件编译，同时支持 Linux 和 Windows 平台。
9. **多协议媒体格式支持**：覆盖 H.264、H.265、AAC、G.711A、VP8 等主流音视频编码格式。
10. **工程教学价值高**：项目融合了网络编程、流媒体协议、AI 推理、调度算法和系统观测等多个技术领域，适合作为面试展示、课程设计或工程优化实践项目。

---

## 9. 当前架构可能的优化方向

1. **配置中心化**：当前配置分散在命令行参数和硬编码常量中，可引入配置文件（如 YAML/TOML）统一管理运行参数。
2. **日志系统统一化**：各模块使用不同的日志输出方式，可统一为结构化日志框架，支持日志级别动态调整和文件轮转。
3. **AI 推理管线异步化**：当前检测和识别在管线线程中同步执行，可改为异步推理队列，减少 GPU/DNN 推理阻塞对帧率的影响。
4. **RTSP 会话管理增强**：增加会话超时回收、并发流数限制和带宽统计能力。
5. **边缘节点负载均衡策略优化**：当前为静态权重配置，可引入运行时自适应权重调整或强化学习策略。
6. **HTTP API 文档完善**：补充 OpenAPI/Swagger 规范文档，便于前端对接和自动化测试。
7. **测试体系补充**：当前缺少单元测试和集成测试，可引入 Google Test 框架覆盖核心模块。
8. **Docker 化部署**：编写 Dockerfile 和 docker-compose，实现一键构建和运行，降低环境搭建成本。
9. **CI/CD 构建流程完善**：接入 GitHub Actions 或 Jenkins，实现自动化编译、测试和发布。
10. **录制与多协议输出**：当前仅支持 RTSP 输出，可扩展 MP4 录制、HLS 切片输出和 RTMP 推流能力。

---

## 10. 总结

StreamSight 是一个融合了网络编程、流媒体协议、AI 视频分析、边缘计算模拟和系统可观测能力的综合型 C++ 项目。它以 RTSP 流媒体服务为核心，通过分层架构实现了从视频采集、AI 分析、画面叠加、视频编码到流媒体分发的完整媒体处理管线。在此基础之上，项目进一步扩展了控制调度层和 CDN 边缘模拟层，将单链路处理升级为具备任务分类、节点调度、故障切换和指标观测能力的流媒体处理平台原型。

该项目在工程上具有较高的完整性和可展示性：网络层封装扎实、协议栈实现规范、AI 管线流程清晰、调度逻辑设计合理。对于学习和展示 C++ 工程能力、流媒体系统设计思想以及分布式调度概念而言，是一个兼具深度和广度的实践项目。

---

## 附录：系统架构文字版示意图

```
StreamSight 系统架构 (数据流 / 控制流)

┌─────────────────────────────────────────────────────────────────────────┐
│                           可观测性层 (src/observe/)                      │
│  MetricsRegistry: 流级指标 │ 节点级指标 │ 调度级指标                       │
└────────────────────────────┬────────────────────────────────────────────┘
                             │ 指标采集 (横切注入)
                             ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                          控制调度层 (src/control/)                       │
│                                                                         │
│  StreamTask ──▶ Classifier ──▶ Scheduler ──▶ StreamManager              │
│   (任务抽象)      (多维分类)      (加权评分)      (任务分发 + 故障切换)     │
│       │                                          │                       │
│       │                    ┌─────────────────────┘                       │
│       │                    │  Dispatch(ctx, node)                        │
│       ▼                    ▼                                             │
│  PipelineRunner ──▶ EdgeNode::Submit() ──▶ ThreadPool::Execute()         │
│   (管线执行)          (节点接流)               (线程池执行)                 │
└─────────────────────────────────────────────────────────────────────────┘
        │                              ▲
        │ 数据平面                       │ 节点状态上报
        ▼                              │
┌─────────────────────────────────────────────────────────────────────────┐
│                          CDN 模拟层 (src/cdn_sim/)                       │
│                                                                         │
│  EdgeNodePool ──▶ EdgeNode A (HighCapacity, East, 16 workers)           │
│               ──▶ EdgeNode B (MediumCapacity, East, 8 workers)          │
│               ──▶ EdgeNode C (LowCapacity, West, 4 workers)             │
│                                                                         │
│  每个 EdgeNode: RegionTag │ NodeStatus │ ThreadPool │ Stats              │
└─────────────────────────────────────────────────────────────────────────┘
                             ▲
                             │ 管线线程执行
                             │
┌────────────────────────────┴────────────────────────────────────────────┐
│                          AI 分析层 (src/ai/)                              │
│                                                                          │
│  ┌──────────┐    ┌──────────────┐    ┌──────────────┐    ┌────────────┐ │
│  │VideoSource│───▶│FrameAnalyzer │───▶│ FrameOverlay │───▶│H264Encoder │ │
│  │            │    │              │    │              │    │            │ │
│  │CameraSource│    │FaceDetector  │    │ 标注人脸框    │    │ FFmpeg子进程│ │
│  │FileSource  │    │FaceRecognizer│    │ 姓名标签      │    │ pipe通信   │ │
│  │RtspPullSrc │    │FaceDatabase  │    │              │    │ NAL回调    │ │
│  └──────────┘    └──────┬───────┘    └──────────────┘    └─────┬──────┘ │
│                         │                                       │        │
│                         ▼                                       ▼        │
│                  ┌──────────────┐    ┌──────────────┐    ┌────────────┐ │
│                  │ EventLogger  │    │HttpApiServer │    │   RTSP     │ │
│                  │ events.jsonl │    │ REST API     │    │  PushFrame │ │
│                  └──────────────┘    └──────────────┘    └──────┬─────┘ │
└─────────────────────────────────────────────────────────────────┬───────┘
                                                                  │
                                                                  ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                        RTSP 流媒体层 (src/xop/)                           │
│                                                                          │
│  RtspServer ◀── RtspConnection ◀── RtspMessage (解析 RTSP 请求)           │
│      │               │                                                   │
│      │          RtpConnection (RTP over TCP / RTP over UDP)              │
│      │               │                                                   │
│      ▼               ▼                                                   │
│  MediaSession ──▶ H264Source / H265Source / AACSource / G711ASource      │
│  (会话管理)       媒体源封装 │ SDP生成 │ 时间戳管理                         │
│                                                                          │
│  RtspPusher: 推流端 ──▶ 远端 RTSP 服务器                                  │
│  DigestAuthentication: RFC 2617 摘要认证                                  │
└─────────────────────────────────────────────────────────────────────────┘
                             ▲
                             │ 异步 I/O 事件驱动
                             │
┌─────────────────────────────────────────────────────────────────────────┐
│                         网络基础层 (src/net/)                             │
│                                                                          │
│  EventLoop (事件循环)                                                     │
│      │                                                                   │
│      ├── TaskScheduler ◀── EpollTaskScheduler (Linux epoll)              │
│      │                 ◀── SelectTaskScheduler (Windows select)          │
│      │                                                                   │
│      ├── TcpServer ──▶ Acceptor ──▶ TcpConnection ──▶ TcpSocket          │
│      │                                                                   │
│      ├── Channel (fd + 事件回调)                                          │
│      ├── Timer (定时任务)  +  Timestamp (高精度时间)                       │
│      ├── BufferReader / BufferWriter (二进制流读写)                       │
│      ├── RingBuffer (循环缓冲)  +  MemoryManager (内存池)                 │
│      ├── Pipe (线程间通知)                                                │
│      └── Logger (日志)                                                    │
└─────────────────────────────────────────────────────────────────────────┘
                             ▲
                             │ 数据流入
                             │
┌─────────────────────────────────────────────────────────────────────────┐
│                          视频源 / 客户端                                   │
│                                                                          │
│  摄像头 ──▶ USB/V4L2        RTSP 源 ──▶ 远端服务器                         │
│  本地文件 ──▶ .h264 / .mp4   RTSP 客户端 ──▶ ffplay / VLC                │
│  人脸图片 ──▶ .jpg / .png   HTTP 客户端 ──▶ curl / 浏览器                  │
└─────────────────────────────────────────────────────────────────────────┘

数据流主线:
  视频源 → GrabFrame() → FrameAnalyzer{Detect→Extract→Query}
  → FrameOverlay::Draw() → H264Encoder::EncodeFrame()
  → callback(NAL) → RtspServer::PushFrame() → RTP → 客户端

控制流主线:
  StreamTask → Classifier.Apply() → Scheduler.SelectNode()
  → StreamManager.Dispatch() → EdgeNode.Submit()
  → PipelineRunner.Run() → OnStreamExit() → [failover 重调度]

旁路输出:
  AnalysisResult → EventLogger::Log() → events.jsonl
  AnalysisResult → HttpApiServer::UpdateResult() → GET /api/current
```
