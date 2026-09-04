# StreamSight 架构清理计划

> 交接文档 · 2026-09-04 · 目标：消除「多流管理」职责错位与 LEGACY 死代码，收敛为清晰的单流/多流两级架构。

## 0. 一句话诊断

不是运行时 bug，是**架构分层错位 + LEGACY 死代码未清理**：代码里有 3 套「多流管理」机制职责交叉，其中一整层（控制调度层）已被主链路废弃却仍留在库里。

---

## 1. 现状：当前的真实链路

主入口 `example/ffmpeg_streamer.cpp` 的依赖链路（见 `CMakeLists.txt:196-213`）：

```
net + xop + observe + ai + ffmpeg + effect + api
```

**完全不包含** `control/` 和 `cdn_sim/` 两层。

真正的运行关系（当前代码事实）：

```
StreamApiServer   (多流: sessions_ 管 N 个 StreamSession)
  └─ StreamSession (单流会话，内部又藏了一个「多流管理器」)
       └─ PipelineManager (多流管理器，被当单流容器用，map 里永远只有 1 项)
            └─ StreamPipeline (真正干活的单流 3-stage 管线)
```

---

## 2. 问题清单

### 问题 1：LEGACY 控制调度层是死代码，未清理

`src/control/`（StreamManager/Classifier/Scheduler/PipelineRunner/PolicyCenter）与 `src/cdn_sim/`（EdgeNode/EdgeNodePool/ThreadPool）整层只被一个 LEGACY example 引用：

- `CMakeLists.txt:101-112`：`CONTROL_SRCS` + `CDN_SIM_SRCS` **只**链接进 `rtsp_edge_analysis_server`。
- `CMakeLists.txt:182-194`：`rtsp_edge_analysis_server` 位于 `if(BUILD_LEGACY_TARGETS)` 内，默认 `OFF`（`CMakeLists.txt:26`）。
- 主链路 `ffmpeg_streamer`（`CMakeLists.txt:197-213`）不含这两层。

结论：当前运行链路与 `StreamManager`/`Scheduler` 无关，它们只是「存在」，造成认知负担。

### 问题 2：「多流管理」重复三份

三个类各维护一份 `unordered_map<string,...> + Add/Remove/StopAll`：

| 类 | 成员 | 管什么 | 状态 |
|---|---|---|---|
| `control::StreamManager` | `streams_` (`StreamManager.h:47`) | StreamContext | LEGACY，带调度 + failover |
| `ffmpeg::PipelineManager` | `pipelines_` (`PipelineManager.h:53`) | StreamPipeline | 新，但被当单流用（见问题 3） |
| `api::StreamApiServer` | `sessions_` (`StreamApiServer.h:74`) | StreamSession | 新，真正的对外注册表 |

### 问题 3：职责错位 —— 「单流」类里藏着「多流」管理器

`StreamSession` 号称「单流会话」，内部却持有 `PipelineManager`（`StreamSession.h:189`），且只用其单流能力：

```cpp
// StreamSession.cpp:354
pipeline_mgr_.AddStream(cfg_.rtsp_suffix, pcfg);  // 用 "live" 当 key，只放一条
// StreamSession.cpp:142 / 156
pipeline_mgr_.GetStats(cfg_.rtsp_suffix);          // 查这一条
// StreamSession.cpp:131 / 360
pipeline_mgr_.StopAll();                            // 停
```

真正的多流管理在 `StreamApiServer`（`sessions_`）。于是形成「单流套多流」的别扭嵌套。

---

## 3. 事实依据（文件行号速查）

| 事实 | 位置 |
|---|---|
| `BUILD_LEGACY_TARGETS` 默认 OFF | `CMakeLists.txt:26` |
| `CONTROL_SRCS` / `CDN_SIM_SRCS` 定义 | `CMakeLists.txt:101-112` |
| `rtsp_analysis_server`（LEGACY） | `CMakeLists.txt:171-179` |
| `rtsp_edge_analysis_server`（LEGACY，唯一用 control/cdn_sim 的目标） | `CMakeLists.txt:182-194` |
| `ffmpeg_streamer`（主入口，不含 control/cdn_sim） | `CMakeLists.txt:197-213` |
| `StreamManager` 注册表 + 接口 | `src/control/StreamManager.h:26-28,47` |
| `PipelineManager` 注册表 | `src/ffmpeg/PipelineManager.h:37-49,53` |
| `StreamApiServer` session 注册表 | `src/api/StreamApiServer.h:49-54,74` |
| `StreamSession` 内部持有 PipelineManager | `src/ffmpeg/StreamSession.h:189` |
| `StreamSession` 单流用法 | `src/ffmpeg/StreamSession.cpp:131,142,156,354,360` |
| 架构文档仍把 LEGACY 调度层描述为核心设计（与代码事实冲突） | `docs/architecture.md:268-279` |

---

## 4. 目标架构

把「管流」收敛到**唯一 owner**，每层只做一件事：

| 层 | 类 | 唯一职责 |
|---|---|---|
| 数据面·管线 | `StreamPipeline` | 单条流的 3-stage 处理（demux→AI→encode→output） |
| 数据面·会话 | `StreamSession` | 单条流完整生命周期（Start/Stop/GetStatus/EventBus），**直接持有 StreamPipeline，去掉中间 Manager** |
| 应用面·API | `StreamApiServer` | **唯一的**多流注册表 + HTTP 暴露（session CRUD） |
| ~~控制面~~ | ~~StreamManager/Classifier/Scheduler/cdn_sim~~ | **删除**（或彻底隔离成独立可选模块） |

原则：**「流」只有两级 —— 单流(StreamSession) 与 多流(StreamApiServer)，中间不再有任何「Manager」叠床架屋。**

---

## 5. 解决路径（4 个独立步骤，可分会话执行）

### Step 1 · 删 LEGACY

- 删除 `src/control/`、`src/cdn_sim/`。
- 删除 `example/rtsp_edge_analysis_server.cpp`、`example/rtsp_analysis_server.cpp`。
- 删除 `CMakeLists.txt` 中 `CONTROL_SRCS`、`CDN_SIM_SRCS` 定义与 `BUILD_LEGACY_TARGETS` 相关块（26、101-112、171-194、220-223）。
- **验证**：`BUILD_LEGACY_TARGETS` 开关不再出现，且默认构建无残留引用报错。

### Step 2 · 扁平化 StreamSession

- 将 `StreamSession` 的 `pipeline_mgr_` 替换为直接持有 `StreamPipeline`（parallel 模式）或 `FFmpegStreamer`（serial 模式）。
- 消除「单流套多流」错位：`StreamSession` 不再出现 `AddStream(rtsp_suffix)` 这种伪多流调用。
- 若 `PipelineManager` 的多流能力新链路用不上，则删除该类。

### Step 3 · 确立 StreamApiServer 为唯一注册表

- `PipelineManager` 的多流能力若仍需：并入 `StreamApiServer`，全项目只保留一份 Add/Remove/StopAll。
- 若无需：删除 `PipelineManager`，`StreamApiServer` 成为唯一多流 owner。

### Step 4 · 文档同步

- 更新 `docs/architecture.md`（当前把 LEGACY 调度层写成核心设计，见 268-279）。
- 更新 `README.md` / `README_CN.md` 的分层图与构建说明。
- 更新 `CHANGELOG.md` 记录架构收敛。

---

## 6. 验收标准

- [ ] 默认构建（`BUILD_LEGACY_TARGETS=OFF`）通过，且 `control/`、`cdn_sim/` 目录已删除。
- [ ] 全项目 `grep -rn "StreamManager\|PipelineRunner\|cdn_sim"` 无命中（或仅存在于隔离模块）。
- [ ] `StreamSession` 内不再持有 `PipelineManager`。
- [ ] 多流注册表全项目仅 `StreamApiServer` 一份。
- [ ] `docs/architecture.md` 描述与代码事实一致。

---

## 7. 风险与注意事项

- **删除前先确认调度/failover 是否还有真实需求**：如果未来要恢复边缘调度能力，Step 1 应改为「隔离成独立可选模块」而非删除，并保留 `cdn_sim_design.md` 作为设计参考。
- **注意 `net/TaskScheduler` 不是 LEGACY**：`src/net/` 下的 `TaskScheduler`/`EpollTaskScheduler`/`SelectTaskScheduler` 是网络层核心基础设施，与 `control/Scheduler`（边缘节点调度）是两回事，勿误删。
- 分步提交，每步跑一遍构建与 `tests/`，避免大爆炸式改动。
