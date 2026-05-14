# StreamSight 的 CDN 模拟调度设计

## 1. 背景

原始的 StreamSight 项目主要聚焦于单路实时流媒体分析链路：

VideoSource -> AI Analysis -> Frame Overlay -> H264 Encode -> RTSP Push

该链路已经解决了如下核心问题：
- 基于 RTSP/RTP 的流媒体传输
- 基于 FFmpeg 的解码与编码
- AI 实时分析与结果叠加
- 端到端时延优化
- 高帧率 / 高并发场景下的稳定性问题

但如果希望该项目更贴近流媒体基础设施与平台开发岗位，仅有单路分析链路还不够，还缺少一层“调度、分发、平台化”的能力。

因此，本项目在原有基础上扩展了一层 **CDN 风格的边缘调度模拟层**。

这层设计**并不试图实现一个真实的分布式 CDN**，而是在单机环境中模拟 CDN/边缘系统的核心思想，包括：
- 流任务分类
- 边缘节点能力差异
- 负载感知调度
- 区域感知分发
- 故障切换
- 可观测性

目标是把 StreamSight 从一个“单链路流媒体分析程序”，升级为一个**具备边缘调度能力的流媒体处理平台原型**。

---

## 2. 设计目标

### 2.1 功能目标
1. 支持多维度流任务分类  
2. 支持异构边缘节点模拟  
3. 支持基于区域、负载、能力和时延约束的任务调度  
4. 支持节点不可用或过载时的故障切换  
5. 支持流级与节点级指标采集，增强系统可观测性  

### 2.2 工程目标
1. 复用现有 `net`、`xop`、`ai` 模块  
2. 在不推翻原有媒体处理链路的前提下引入控制平面  
3. 保持系统模块化，便于未来扩展：
   - 录制
   - HLS/RTMP 输出
   - 配置中心
   - 策略中心
   - HTTP 管理接口

---

## 3. 整体架构

优化后的系统可分为四个逻辑层：

### 3.1 数据平面（Data Plane）
负责真正的媒体流处理：
- 输入接入
- 解码
- AI 分析
- 画面叠加
- 编码
- RTSP 推流

这一部分主要由现有的 `ai` 和 `xop` 模块实现。

### 3.2 控制平面（Control Plane）
负责流任务生命周期与调度：
- 流任务抽象
- 分类
- 调度
- 流创建 / 停止
- 故障切换

对应模块：
- `control/StreamTask`
- `control/Classifier`
- `control/Scheduler`
- `control/StreamManager`

### 3.3 边缘模拟层（Edge Simulation Plane）
负责模拟边缘节点及其承载能力：
- 异构节点抽象
- 任务队列和 worker 线程池
- 节点级负载统计
- 任务接纳 / 拒绝

对应模块：
- `cdn_sim/EdgeNode`
- `cdn_sim/EdgeNodePool`
- `cdn_sim/ThreadPool`

### 3.4 可观测性层（Observability Plane）
负责系统运行状态的可见性：
- 流级指标
- 节点级指标
- 调度 / failover 计数

对应模块：
- `observe/MetricsRegistry`

---

## 4. 核心设计概念

## 4.1 StreamTask

所有流请求统一抽象成 `StreamTask`。

一个 `StreamTask` 包含以下信息：
- 流 ID
- 输入 URI / 输入类型
- 宽高 / FPS
- 目标码率
- 是否启用 AI
- 是否启用叠加
- 区域信息
- 分析频率
- 是否录制 / 输出开关
- 分类标签

这层抽象的作用是：让调度器面对的是统一的“任务对象”，而不需要关心底层 pipeline 细节。

---

## 4.2 多维流分类

为了模拟 CDN 风格的任务调度，系统会从多个维度对每一路流进行分类。

### 4.2.1 Region（区域）
表示流来源区域或期望就近接入区域：
- Local
- North
- East
- South
- West

作用：
- 模拟“就近边缘接入”的调度思想

### 4.2.2 BitrateClass（码率等级）
根据目标码率划分：
- Low
- Medium
- High
- Ultra

作用：
- 表征网络压力和编码压力

### 4.2.3 LatencyClass（时延等级）
根据业务场景划分：
- Realtime
- Interactive
- Standard
- Archive

作用：
- 表达该流对时延的敏感程度

### 4.2.4 ComputeClass（计算负载等级）
根据分辨率、FPS、是否开启 AI 等因素划分：
- Light
- Medium
- Heavy
- Extreme

作用：
- 表征分析 + 编码带来的计算压力

分类器会将流运行参数转化为调度器更容易处理的标签。

---

## 4.3 EdgeNode 边缘节点模拟

`EdgeNode` 用于模拟一个边缘服务器。

每个节点包含：
- node_id
- region
- node type（HighCapacity / MediumCapacity / LowCapacity）
- worker 线程数
- 最大承载流数
- 最大队列长度
- 当前状态（Healthy / Busy / Degraded / Down）

### 4.3.1 异构节点能力
不同节点代表不同算力级别：
- 高算力边缘节点
- 中算力边缘节点
- 低算力边缘节点

这样可以模拟真实场景中不同机器配置下的处理能力差异。

### 4.3.2 节点状态
节点运行时可能处于不同状态：
- Healthy：健康，可正常接任务
- Busy：繁忙，接近过载
- Degraded：降级，不建议继续分配新任务
- Down：不可用

---

## 4.4 调度策略

调度器采用**打分式调度策略**，而不是写死的 if-else 分支。

### 4.4.1 候选节点过滤
在打分前，先过滤掉不合适的节点：
- 状态为 down
- 队列过深
- worker 利用率超过阈值
- 当前流数超过上限
- 节点能力与任务明显不匹配

### 4.4.2 打分维度
对每个候选节点计算一个综合分数：

score =
  region_weight * region_score +
  load_weight * load_score +
  capability_weight * capability_score +
  latency_weight * latency_score +
  failover_weight * failover_penalty

其中：

#### region_score
表示区域匹配程度：
- 同区域 -> 分数更低
- 跨区域 -> 分数更高

#### load_score
表示当前节点压力，通常综合：
- active workers / total workers
- pending tasks / max queue

#### capability_score
表示当前流任务与节点能力是否匹配：
- Extreme + Ultra 流分到 LowCapacity 节点时惩罚更大
- 轻量流分到任意节点惩罚较小

#### latency_score
表示节点近期平均 pipeline 延迟：
- Realtime 流更偏好延迟低的节点

#### failover_penalty
对 Busy / Degraded / Down 节点增加额外惩罚

### 4.4.3 节点选择
最终选择**综合分数最低**的节点。

之所以采用打分式而不是硬编码规则，主要因为：
- 更容易调参
- 更容易解释
- 后续更方便扩展新的策略维度

---

## 5. 故障切换策略

当某条流的 pipeline 运行失败时：
1. 判断 failover 次数是否超过 `max_failover`
2. 排除上一次失败的节点
3. 重新调度该任务到其他节点
4. 在新节点上重新启动 pipeline

这是一种在单机环境下模拟边缘节点故障切换的简化实现。

### 5.1 为什么需要 failover
流媒体系统里常见的失败原因包括：
- 输入源打开失败
- 编码阶段异常
- worker 节点过载
- 上游断流

### 5.2 当前 failover 的能力边界
当前版本支持：
- 对失败或可重试任务进行重新调度
- 将失败节点从本次候选中排除

当前版本**不支持**：
- 无中断的实时流迁移
- 跨机器 / 跨进程的状态热切换

这部分在面试中需要明确说明，避免把原型能力说成完整分布式能力。

---

## 6. 可观测性设计

为了支持故障定位和性能分析，系统通过 `MetricsRegistry` 输出运行指标。

### 6.1 流级指标
- `stream.<id>.pipeline_latency_ms`
- `stream.<id>.avg_pipeline_latency_ms`
- `stream.<id>.frame_count`
- `stream.<id>.dispatch_count`
- `stream.<id>.failover_count`

### 6.2 节点级指标
- `node.<id>.active_streams`
- `node.<id>.dispatch_count`
- `node.<id>.avg_pipeline_latency_ms`

### 6.3 调度级指标
- `scheduler.dispatch_total`
- `scheduler.failover_total`

### 6.4 这些指标的作用
可用于：
- 观察节点负载情况
- 对比不同节点上的流延迟
- 解释调度决策是否合理
- 验证 failover 是否发生
- 为后续 `/metrics` 接口或管理后台打基础

---

## 7. 为什么这个设计更贴近流媒体基础设施岗位

引入 CDN 模拟调度层后，项目在以下几个方面更接近真实流媒体平台：

1. 区分了**数据平面**和**控制平面**  
2. 从“单链路本地处理”升级成“边缘调度 + 任务分发”  
3. 引入了**流分类**这一平台化思路  
4. 引入了**负载均衡与故障切换**  
5. 引入了**可观测性能力**  
6. 为后续扩展预留了空间：
   - 录制
   - HLS/RTMP
   - 配置中心
   - 告警
   - 策略热更新

---

## 8. 当前版本的局限性

该版本仍然是模拟 / 原型系统，主要局限包括：

1. 边缘节点是在单机 / 单进程内模拟的，不是真实多机部署  
2. 没有真实的 CDN 缓存 / 回源 / 切片缓存逻辑  
3. failover 是重启式而非无缝迁移  
4. 当前可观测性仍以内存指标为主，尚未接入 Prometheus  
5. 当前输出主要围绕 RTSP（RTMP 已通过 ffmpeg_streamer 实现），尚未完整补齐 HLS/录制

这些局限在项目介绍中应主动说明，这样既真实，也更能体现你对系统边界的认识。

---

## 9. 后续可扩展方向

### 9.1 媒体能力扩展
- MP4 录制
- HLS 切片输出
- RTMP 推流（已实现：`ffmpeg_streamer` 支持 RTMP 输出）
- 多档转码 profile

### 9.2 控制平面扩展
- 支持 HTTP 动态创建 / 删除流
- 策略热更新
- 配置中心
- 动态降级策略

### 9.3 可观测性扩展
- `/metrics` 导出
- 结构化日志
- pipeline trace id
- 告警规则

### 9.4 边缘模拟扩展
- 多进程节点隔离
- 基于真实机器资源的采样
- 模拟 origin-edge 拓扑
- 增加 relay / segment cache 原型

---

## 10. 总结

CDN 模拟调度层是对 StreamSight 的一次控制平面与平台能力增强。

它并不声称自己是一个真实 CDN，而是在单机流媒体系统中引入了流媒体基础设施里非常关键的工程思想：
- 分类
- 异构节点能力建模
- 调度
- failover
- 可观测性

# `rtsp_edge_analysis_server` 的“模拟流程”

`rtsp_edge_analysis_server` 的“模拟流程”本质上是：

**把一条视频流先抽象成 `StreamTask`，再用“分类器 + 调度器”决定这条流应该落到哪个模拟边缘节点，最后由该节点在线程池里跑完整的视频处理 pipeline；如果节点执行失败，再触发一次重新选点和 failover。** 这套流程是你从单链路 `rtsp_analysis_server` 升级到“带边缘调度能力的流媒体处理平台原型”的关键。

你可以把它理解成下面这条主线：

**命令行输入 → 构造流任务 → 流分类 → 选边缘节点 → 节点执行 pipeline → 推 RTSP → 指标采集 → 失败切换**

---

## 1. 它到底在“模拟”什么

它模拟的不是“真实 CDN 集群”，而是：

**单机环境下的多边缘节点调度模型**

也就是在一个进程里，创建多个能力不同的 `EdgeNode`，例如：

* `edge_east_high`：高算力节点
* `edge_east_medium`：中算力节点
* `edge_west_low`：低算力节点

每个节点有自己的：

* 区域标签
* 节点类型
* 线程数
* 最大并发流数
* 最大排队深度

然后调度器根据流的属性，把流分给最合适的节点去跑。

所以这套代码模拟的是 4 件事：

1. **边缘节点异构**：不同节点算力不同
2. **就近分发**：区域匹配优先
3. **负载均衡**：忙的节点少接流
4. **故障切换**：当前节点失败就换下一个节点

---

## 2. 程序启动后的完整执行流程

### 第一步：主程序解析参数，生成一条 `StreamTask`

入口在 `example/rtsp_edge_analysis_server.cpp`。

程序先从命令行拿参数，比如：

* `--stream-id live_001`
* `--source file|camera|rtsp`
* `--input test.h264`
* `--region east`
* `--bitrate 4096`
* `--fps 25`
* `--no-ai`

然后组装成一个 `StreamTask`。这个对象就是“调度单元”。

它包含的关键字段有：

* 流 ID：`stream_id`
* 输入类型：文件、摄像头、RTSP 拉流
* 分辨率、帧率、目标码率
* 是否启用 AI
* 是否启用 overlay
* 来源区域 `region`
* 最大 failover 次数

这里要注意一点：

**`StreamTask` 还不是分类后的最终任务，它只是原始请求。**

---

### 第二步：创建模拟边缘节点池 `EdgeNodePool`

主程序里会创建 3 个边缘节点，大概是这样：

* 东部高算力节点：16 worker，8 路并发，32 队列
* 东部中算力节点：8 worker，4 路并发，16 队列
* 西部低算力节点：4 worker，2 路并发，8 队列

这一步模拟的是：

* 不同区域有不同节点
* 不同节点承载能力不同
* 高负载流不应该跑到低算力节点上

也就是说，这不是简单的“开 3 个线程池”，而是给每个线程池赋予了：

* 节点身份
* 区域属性
* 容量上限
* 调度权重语义

---

### 第三步：初始化 `SchedulerPolicy`

调度策略里会设置几个权重：

* `region_weight`
* `load_weight`
* `capability_weight`
* `latency_weight`
* `failover_weight`

还有几个阈值：

* `max_queue_threshold`
* `busy_util_threshold`

这个阶段决定了调度器更偏向哪种策略。

比如你当前代码的意思大致是：

* 区域就近比较重要
* 当前负载也很重要
* 节点能力匹配也很重要
* 时延和 failover 是辅助因素

面试时你可以这样讲：

> 我没有写死 if-else 去选节点，而是抽象成了带权重的 scoring policy，方便后续按场景调整“就近优先”还是“负载优先”。

这个说法会比“我做了简单负载均衡”强很多。

---

### 第四步：`StreamManager.Start()` 启动统一 RTSP 服务

这里有一个很关键的设计：

**RTSP 服务不是挂在某个具体边缘节点上的，而是由 `StreamManager` 统一持有。**

也就是：

* `StreamManager` 先创建 `EventLoop`
* 再创建 `RtspServer`
* 绑定统一端口，比如 `0.0.0.0:554`

这意味着你的系统虽然模拟了多个边缘节点，但对外仍然是一个统一入口。

这个设计很适合“平台原型”叙事：

* 控制面统一管理
* 数据面由不同节点执行
* 输出通过统一 RTSP 服务对外暴露

---

### 第五步：`StartStream(task)` 开始一条流

这是整个模拟流程最重要的入口。

它会做三件事：

#### 1）先分类 `classifier_.Apply(task)`

分类器会根据任务属性给这条流打标签：

* `bitrate_class`
* `compute_class`
* `latency_class`
* `priority`

例如：

* 码率 <= 1000 kbps -> `Low`
* 1M~4M -> `Medium`
* 4M~8M -> `High`
* > 8M -> `Ultra`

如果启用了 AI，再结合：

* 分辨率
* FPS
* analyze_fps

去判断计算负载：

* `Light`
* `Medium`
* `Heavy`
* `Extreme`

还会推导实时性类别：

* 有 AI：通常归到 `Realtime`
* 录制但无 AI：更偏 `Archive`
* 普通推流：`Standard`

所以这里的本质是：

**把“视频输入参数”转换成“调度可理解的资源画像”。**

这一步特别重要，因为没有分类，调度器就只能按粗糙的线程数分配，讲不出“CDN 分类策略”。

---

#### 2）调用 `scheduler_->SelectNode(task)` 选节点

调度器会遍历所有 `EdgeNode`，筛出能接任务的节点，然后打分。

先做准入过滤：

* 节点不能是 `Down`
* 节点活跃流数不能超上限
* 队列长度不能超阈值
* worker 利用率不能过高
* 节点能力不能明显不匹配

然后对候选节点算分。

---

#### 3）创建 `StreamContext`，交给 `Dispatch`

`StreamContext` 是每条流的运行时状态，里面大概有：

* `task`
* `stop_flag`
* `node_id`
* `status`
* `failover_count`

它相当于流生命周期的控制块。

---

## 3. 调度器到底是怎么选节点的

`Scheduler::SelectNode()` 的核心不是随机选，也不是轮询，而是：

**对每个候选节点算一个 score，选分数最低的。**

### score 主要由 5 部分组成

#### 1）区域分 `region_score`

模拟“就近分发”。

* 同区域更优
* 本地也比较优
* 跨区域惩罚更高

比如流来自 `East`，那么：

* `edge_east_high`
* `edge_east_medium`

通常会比 `edge_west_low` 更有优势。

---

#### 2）负载分 `load_score`

看当前节点有多忙，主要参考：

* active worker 数
* pending queue 深度
* 活跃流数
* worker 利用率

越忙，分越高，越不容易被选中。

---

#### 3）能力匹配分 `capability_score`

这是你这个项目最有价值的一点。

例如：

* `Extreme + Ultra` 流，不适合放到 `LowCapacity`
* `Heavy` 流可以接受 `MediumCapacity`
* `Light` 流在哪都能跑

这一步体现的是：

**不是所有流都平等，而是根据计算负载和码率把流分层。**

这就是“分类策略”的核心。

---

#### 4）时延分 `latency_score`

如果这条流是 `Realtime`，那么：

* 平均 pipeline latency 更低的节点更优
* 编码队列短的节点更优

虽然你当前 v1 里这部分还比较轻，但它已经有接口和指标了。

---

#### 5）故障惩罚 `failover_penalty`

如果节点状态是：

* `Busy`
* `Degraded`
* `Down`

它的 penalty 会更高，甚至直接被过滤掉。

---

## 4. 节点选中之后发生什么

进入 `Dispatch(ctx, node)`。

这一步会：

1. 把 `ctx->node_id` 设置为当前节点
2. 把流状态改成 `Running`
3. 记录一些 metrics
4. 调用 `node->Submit(task, fn)`

这里的关键是：

**流不是立刻同步执行，而是提交给节点内部线程池。**

也就是说：

* `StreamManager` 负责调度
* `EdgeNode` 负责承接执行
* `PipelineRunner` 负责真正跑媒体处理链路

这是一个很标准的“控制面 / 执行面”分离思路。

---

## 5. `EdgeNode::Submit()` 模拟了什么

它模拟的是“边缘节点接流”。

节点内部会：

* 检查自身是不是还能接任务
* 增加 `active_streams`
* 更新 metrics
* 把实际执行逻辑丢进线程池

你可以把它理解成：

**某个边缘节点收到调度中心下发的一路流处理任务，然后在自己的工作线程里启动这一路流。**

所以虽然底层还是单机线程池，但抽象层面已经非常接近：

* 边缘节点接单
* 节点内部执行
* 节点统计本地负载

---

## 6. `PipelineRunner::Run()` 跑的是什么

这一步就是你原来 `rtsp_analysis_server` 的完整链路，只不过现在它被包装成了“由某个边缘节点承接的一条流任务”。

执行顺序是：

### 1）创建输入源 `VideoSource`

根据 `SourceType` 选择：

* `CameraSource`
* `FileSource`
* `RtspPullSource`

---

### 2）按需初始化 AI 模块

如果启用了 AI，就会初始化：

* `FaceDetector`
* `FaceRecognizer`
* `FaceDatabase`
* `FrameAnalyzer`
* `FrameOverlay`
* `EventLogger`

如果模型没加载成功，会自动退化成非 AI 模式。

这一点其实很好讲：

> AI 是可插拔的负载，而不是系统主流程的强依赖。

---

### 3）创建 RTSP `MediaSession`

为这条流创建一个独立的 RTSP session：

* suffix 一般来自 `task.session_suffix`
* 只挂 H264 source
* 后续编码输出直接 push 进去

---

### 4）启动 `H264Encoder`

编码器把 OpenCV 的 frame 编成 H264，然后通过 callback 推进 RTSP server。

也就是：

**解码/分析/叠加后的帧 → H264Encoder → AVFrame → RtspServer::PushFrame**

---

### 5）主循环处理 frame

循环里做的事情是：

* 从 source 抓帧
* resize 到目标分辨率
* 做 AI 分析
* 按需做 overlay
* 编码
* 推流
* 统计 latency

所以整个边缘节点执行的本质还是：

**输入接入 → AI/处理 → 编码 → 分发**

只是现在外层套上了一层“任务调度”。

---

## 7. 故障切换是怎么模拟的

当 `PipelineRunner::Run()` 返回后，会进入 `OnStreamExit(ctx, result)`。

如果：

* 不是用户主动 stop
* 且结果是 `Failed`
* 且 `failover_count < max_failover`

那么就会：

1. 重新调用 `scheduler_->SelectNode(ctx->task, ctx->node_id)`
2. 排除当前失败节点
3. 找新的节点
4. `failover_count++`
5. 再次 `Dispatch(ctx, next_node)`

所以 failover 的模拟路径是：

**节点 A 执行失败 → 调度器排除 A → 重新选 B → B 接手这条流**

这就是一个很完整的“边缘节点故障切换”演示模型。

虽然它还不是“状态迁移式”的无缝切换，但对于项目原型已经足够强了。

---

## 8. metrics 在模拟流程里起什么作用

主程序会周期性打印 `MetricsRegistry::Snapshot()`。

当前这套指标主要覆盖：

### 调度层

* `scheduler.dispatch_total`
* `scheduler.failover_total`

### 节点层

* `node.<id>.active_streams`
* `node.<id>.avg_pipeline_latency_ms`
* `node.<id>.dispatch_count`

### 流级

* `stream.<id>.frame_count`
* `stream.<id>.pipeline_latency_ms`
* `stream.<id>.avg_pipeline_latency_ms`
* `stream.<id>.failover_count`

这一步模拟的是：

**平台对流和节点运行状态的可观测性。**

也就是说，这个程序不是“只会跑”，而是“跑完还能看见自己怎么跑的”。

---

## 9. 你可以怎么向面试官讲这套模拟流程

推荐你用这套顺序：

### 第一层：它解决什么问题

> 原来的 `rtsp_analysis_server` 只能跑单条固定链路。我把它升级成了可以做流分类、边缘节点模拟和任务调度的原型系统。

### 第二层：它怎么模拟 CDN/边缘调度

> 我在单机内抽象了多个能力不同的 `EdgeNode`，每个节点有区域、容量和线程池，调度器根据流的区域、码率、计算负载和实时性要求来选点。

### 第三层：一条流怎么跑

> 命令行先构造 `StreamTask`，分类器打标签，调度器选节点，然后节点在线程池里跑 `PipelineRunner`，完成采集、AI 分析、编码和 RTSP 推流。

### 第四层：失败怎么办

> 如果节点执行失败，`StreamManager` 会触发 failover，排除当前节点重新调度到其他节点。

### 第五层：怎么观测

> 我增加了节点级和流级 metrics，能看到活跃流数、平均处理时延、调度次数和 failover 次数。

这套讲法很完整，而且非常贴“调度、分发、稳定性、平台化”。

---

## 10. 这套模拟流程里，哪些是已经实现的，哪些是还没完全实现的

### 已经实现了

* `StreamTask` 抽象
* 流分类
* 边缘节点池
* 节点评分调度
* 节点线程池承载执行
* pipeline 执行
* failover 重调度
* 基础 metrics

### 还只是原型/可继续加强

* 真正的多进程/多机边缘节点
* 更细粒度的 queue 分池
* 动态降级策略
* 录制 / RTMP / HLS 输出
* 更完整的 HTTP 管理 API
* 真正的 Prometheus / tracing

这点你一定要分清。面试里这样说会很稳。

---

## 11. 一句话总结 `rtsp_edge_analysis_server`

> `rtsp_edge_analysis_server` 是在原有 RTSP 视频分析链路上增加了“流任务抽象、分类调度、边缘节点模拟、故障切换和指标采集”的增强版入口程序，用单机多节点仿真的方式验证了 CDN/边缘分发场景下的流分类和调度流程。

