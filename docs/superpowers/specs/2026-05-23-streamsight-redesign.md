# StreamSight Project Redesign

Date: 2026-05-23
Status: approved

## 1. Project Positioning

**定位**: AI-Augmented Live Stream Processing Platform / Edge Intelligent Streaming Node

StreamSight 不是 SRS 替代品，而是一个自研的 AI 增强型直播流处理平台。核心差异化：在直播链路中嵌入帧级 AI 处理和内容理解能力。

**一句话**: StreamSight is a self-built AI-augmented live stream processing platform featuring an in-house RTSP server and FFmpeg C API pipeline, enabling real-time video effects, face recognition, and content understanding in the media path.

**三句话**:
- StreamSight 自研了 RTSP/RTP 协议栈（xop）和基于 FFmpeg C API 的进程内编解码管线，实现从多源输入到帧级 AI 处理再到多协议输出的全链路闭环
- 平台提供可扩展的 EffectPlugin 插件体系，人脸检测识别作为首个插件 demo，后续可扩展水印、安全检测、美颜等
- 通过 RTMP Push Client 对接外部 SRS/nginx-rtmp 分发层，预留 Content Understanding 层和 Agent 工具接口

## 2. RTMP Decision: External Distribution Only

**决策**: 不自研 RTMP Server

**理由**:
- RTMP Server 已有成熟方案（SRS/nginx-rtmp），重复实现不带来新知识
- StreamSight 核心价值在"处理管线 + AI 增强"，不在协议分发层
- RTSP 自研已展示了协议工程能力
- RtmpOutputAdapter（RTMP Push Client）遵循 IOutputAdapter 接口，未来如需 RTMP Input 可扩展

**保留**: `ffmpeg/RtmpOutputAdapter` — RTMP Push Client，将处理后流推到外部 SRS/nginx-rtmp

**外部依赖**: Docker SRS / nginx-rtmp，提供 docker-compose.yml

## 3. Code Duplication Analysis

**结论**: xop 和 ffmpeg 之间不存在重复实现

- `xop/` — 协议实现层：RtspServer (RTSP 状态机 + RTP 封包 + 会话管理), RtspPusher, H264Source
- `ffmpeg/` — 管线 + 适配器层：StreamPipeline, RtspOutputAdapter (AVPacket→xop::AVFrame 桥接), RtmpOutputAdapter (AVPacket→FLV→RTMP 桥接)

`RtspOutputAdapter` 是适配器（~100 行格式转换），不是协议实现（~3000 行）。两者正交。

**代码债务**:
- `ai/H264Encoder` — fork+pipe 旧方案，被 StreamPipeline 替代，需 deprecated
- `example/rtsp_analysis_server.cpp` — 使用旧 H264Encoder，标记 legacy
- `control/PipelineRunner.cpp` — 使用旧 H264Encoder，标记 legacy
- `ai/VideoSource 系列` — Phase 2 迁移到 ffmpeg/input/

## 4. Architecture

### 4.1 Overall

```
Ingest → StreamPipeline (Demux+Decode → AI Process → Encode) → Output Adapters
              │                                      │
              │  Stage2: AI Process                   │
              │  ├── EffectChain (IEffectPlugin[])     │
              │  └── Content Understanding (async)     │
              │                                      │
              ▼                                      ▼
         RTSP (自研)  +  RTMP Push (外部 SRS)
         同一处理后的流，画面和结果完全一致
```

### 4.2 Module Layout

```
src/
├── net/       Reactor 网络框架
├── xop/       RTSP/RTP 协议实现（自研）
├── rtmp/      RTMP 协议层（预留，当前不实现）
├── ffmpeg/    FFmpeg C API 管线 + IOutputAdapter
├── effect/    ✨ IEffectPlugin + EffectChain + FaceRecognitionPlugin
├── content/   ✨ SummaryService + 预留接口
├── api/       HTTP REST API
├── control/   流管理 + 调度
├── observe/   可观测性
├── cdn_sim/   CDN 边缘模拟
└── ai/        模型加载 (FaceDetector/FaceRecognizer/FaceDatabase)
               H264Encoder → deprecated
```

### 4.3 Effect Plugin Interface

```cpp
class IEffectPlugin {
public:
    virtual std::string Name() const = 0;
    virtual EffectCategory Category() const = 0;
    virtual bool Open(const std::string& config_json) = 0;
    virtual void Close() = 0;
    // Process BGR24 frame in-place, output structured result
    virtual bool Process(uint8_t* bgr, int w, int h, int ls,
                         EffectResult* result) = 0;
    virtual bool ModifiesFrame() const = 0;
};

class EffectChain {
    void AddPlugin(shared_ptr<IEffectPlugin> p);
    bool ProcessFrame(uint8_t* bgr, int w, int h, int ls,
                      vector<EffectResult>& results);
};
```

FaceRecognitionPlugin wraps existing ai/FaceDetector + ai/FaceRecognizer + ai/FrameOverlay.

### 4.4 Content Understanding Layer (Phase 2-3)

- SummaryService: keyframe extraction, segment summarization
- ISearchService, IHighlightService, IRecommendService: reserved interfaces
- IAgentTool: tool schema + invocation chain (stub only, no LLM in Phase 1-2)

### 4.5 HTTP API

```
POST   /api/v1/sessions              create session
DELETE /api/v1/sessions/:id          stop session
GET    /api/v1/sessions              list sessions
GET    /api/v1/sessions/:id          status + metrics
PUT    /api/v1/sessions/:id/effects  update effect config
GET    /api/v1/sessions/:id/results  detection/summary/events
```

## 5. Development Phases

### Phase 1: Fix Positioning + Clean Debt (~2 weeks)

1. Add `docker-compose.yml` (SRS) and `docs/rtmp-distribution.md`
2. Deprecate `ai/H264Encoder`
3. Mark `rtsp_analysis_server.cpp` and `PipelineRunner.cpp` as legacy
4. Rewrite README + README_CN
5. Create `src/effect/` with IEffectPlugin, EffectChain, FaceRecognitionPlugin
6. Integrate EffectChain into `ffmpeg_streamer.cpp`

### Phase 2: Platformization (~2-3 weeks)

1. StreamSession abstraction
2. Effect pipeline configurable via JSON
3. HTTP API upgrade (session CRUD, effect config, metrics)
4. EventBus structured logging
5. Clean up legacy ai/ code (VideoSource, FrameAnalyzer, H264Encoder)

### Phase 3: Content Understanding (~3-4 weeks)

1. SummaryService prototype
2. Keyframe extraction based on face event density / scene changes
3. Reserved interfaces: ISearchService, IHighlightService, IRecommendService, IAgentTool
4. AgentService stub (tool schema + chain, no LLM)
5. Final README with architecture diagram and roadmap

## 6. Phase 1 Implementation Checklist

| # | Change | Files | Type |
|---|--------|-------|------|
| 1 | SRS docker-compose + config | `docker-compose.yml`, `srs.conf` | config |
| 2 | RTMP distribution guide | `docs/rtmp-distribution.md` | doc |
| 3 | Mark H264Encoder deprecated | `src/ai/H264Encoder.h` | comment |
| 4 | Mark PipelineRunner legacy | `src/control/PipelineRunner.h` | comment |
| 5 | Mark rtsp_analysis_server legacy | `example/rtsp_analysis_server.cpp` | comment |
| 6 | Rewrite README | `README.md`, `README_CN.md` | doc |
| 7 | IEffectPlugin interface | `src/effect/IEffectPlugin.h` | new code |
| 8 | EffectChain | `src/effect/EffectChain.h` | new code |
| 9 | FaceRecognitionPlugin | `src/effect/FaceRecognitionPlugin.h/.cpp` | new code |
| 10 | Integrate EffectChain into ffmpeg_streamer | `example/ffmpeg_streamer.cpp`, `CMakeLists.txt` | modify |

## 7. Resume Description

**Current (after Phase 1)**:
- 自研 RTSP/RTP 协议栈（~3000 行），基于 Reactor 模式（epoll）实现异步事件驱动，支持 H.264/H.265/AAC
- 基于 FFmpeg C API 构建进程内 3-stage 流媒体管线，RingBuffer 背压 + FrameDropPolicy 自适应丢帧
- 实现可扩展 IEffectPlugin/EffectChain 插件体系，集成 ONNX 人脸检测（YuNet）和识别（ArcFace）
- RTMP Push Client 对接 SRS 分发层，支持 RTSP 本地预览 + RTMP 直播分发双输出
- HTTP API 查询实时检测结果、运行指标和历史事件

**Future (after Phase 2-3)**:
- StreamSession 管理 + HTTP API + Effect 动态配置
- 视频摘要原型 + 关键帧提取
- Content Understanding Agent 工具接口预留

## 8. Not In Scope (Explicit)

- Self-implemented RTMP Server (deferred, not canceled)
- HLS/HTTP-FLV/WebRTC protocol implementation
- LLM integration in AgentService
- GPU acceleration / hardware encoding
- Multi-node distributed deployment
