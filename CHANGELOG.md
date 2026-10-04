# 更新日志

本项目所有重要变更记录于此，格式遵循 [Keep a Changelog](https://keepachangelog.com/zh-CN/)，
版本号遵循 [Semantic Versioning](https://semver.org/lang/zh-CN/)。

> 本项目尚未发布正式版本，以下为 `main` 分支的累计变更。

## [Unreleased]

### Added

- FFmpeg C API 三阶段进程内管线（Demux+Decode → AI Process → Encode），替代 fork+pipe 子进程方案。
- 自研 RTSP/RTP 协议栈（rtsp），支持 H.264/H.265/AAC 的局域网直连拉流（服务端 `RtspServer`）。
- RTSP 推流客户端 `RtspPusher`（已实现但**尚未接线**：`main.cpp` 无对应参数，作为预留能力）。
- EffectPlugin 插件体系与 EffectFactory JSON 配置化，内置人脸检测/识别插件（YuNet + ArcFace ONNX）。
- EventBus 线程安全事件发布/订阅，StreamSession 单会话抽象。
- StreamApiServer 合并 REST API（legacy 路由 + v1 session CRUD + 特效动态配置）。
- 管线延迟测量 `LatencyTracer`（P50/P95/P99）与 HTTP 查询接口。
- 多流压测工具 `streamsight-stress`（并行/串行模式、warmup、JSON 输出）。
- Python 压测编排器 `scripts/stress_test.py` 与矩阵配置 `scripts/stress_config.yaml`。
- EventLoop/TaskScheduler 循环统计、RingBuffer 峰值与背压事件追踪。

### Changed

- 事件驱动管线按 RTSP 客户端存在与否动态启停，降低空载开销。
- 线程模型优化与管线健壮性增强。
- 架构收敛：`StreamSession` 直接持有 `StreamPipeline`（移除中间 `PipelineManager`），`StreamApiServer` 成为唯一多流注册表。

### Fixed

- 修复管线死锁问题。
- 修复 `Stop()` 中线程 join 与 session 自然结束（视频 EOF）时主循环退出逻辑。
- 修复人脸检测在 OpenCV 4.5.4 上恒为空：`cv::FaceDetectorYN` 与当前 YuNet 模型的 anchor 布局不兼容（`detect()` 抛 `Layer with requested id=-1`），改为基于通用 `cv::dnn` 接口自行解码 YuNet 输出。此前检测埋点的 ~2ms 实为 `resize + 抛异常` 耗时，现为真实推理耗时。
- 新增文件源帧率 pacing（`--pace-fps`，默认按 `--fps`），文件源不再全速消费（此前 30fps 片源实测 ~180fps）。
- 补齐 serial 模式管线埋点（`pipeline/demux_decode`、`pipeline/encode`），此前 serial 延迟不可观测。

### Removed

- 移除 fork+pipe 子进程编码方案，改为 FFmpeg C API 进程内管线。
- 移除其遗留源码：`ai::VideoSource`/`CameraSource`/`FileSource`/`RtspPullSource`/`H264Encoder`/`HttpApiServer`（`src/ai/` 下 11 个文件）及对应的 CMake `file(GLOB)` 收集方式，`AI_SRCS` 改为显式列表。`src/ai/` 仅保留被 `FaceRecognitionPlugin` 使用的算法组件（`FaceDetector`/`FaceRecognizer`/`FaceDatabase`/`FrameAnalyzer`/`FrameOverlay`/`EventLogger`）。
- 移除 LEGACY 控制调度层（`src/control/`）与 CDN 边缘模拟层（`src/cdn_sim/`）及对应 legacy 示例（`rtsp_analysis_server`、`rtsp_edge_analysis_server`）。
- 移除 `PipelineManager`，多流注册表全项目仅保留 `StreamApiServer` 一份。
