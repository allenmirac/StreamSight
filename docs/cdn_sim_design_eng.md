# CDN Simulation Design for StreamSight

## 1. Background

The original StreamSight project focuses on a single-stream real-time media analysis pipeline:

VideoSource -> AI Analysis -> Frame Overlay -> H264 Encode -> RTSP Push

This pipeline already solves core problems such as:
- RTSP/RTP media transport
- FFmpeg-based decode/encode
- AI analysis and result overlay
- End-to-end latency optimization
- Stability issues under high frame rate / high concurrency

However, to make the project more aligned with media infrastructure and streaming platform development, the system is extended with a **CDN-style edge scheduling simulation layer**.

This layer does **not** attempt to implement a real distributed CDN, but instead simulates the core ideas of CDN/edge systems inside a single-machine environment:
- task classification
- edge node capacity differentiation
- load-aware scheduling
- region-aware dispatch
- failover
- observability

The goal is to evolve StreamSight from a "single media analysis pipeline" into a **media processing platform prototype with edge scheduling capability**.

---

## 2. Design Goals

### 2.1 Functional goals
1. Support multi-dimensional stream classification
2. Simulate heterogeneous edge nodes with different capacities
3. Dispatch stream tasks based on region, load, capability and latency constraints
4. Support failover when a node is unavailable or overloaded
5. Expose metrics for stream-level and node-level observability

### 2.2 Engineering goals
1. Reuse the existing `net`, `xop`, and `ai` modules
2. Introduce a control plane without rewriting the original media pipeline
3. Keep the system modular and extensible for later support of:
   - recording
   - HLS/RTMP output
   - config center
   - policy center
   - HTTP management APIs

---

## 3. High-Level Architecture

The optimized architecture is divided into four logical planes:

### 3.1 Data Plane
Responsible for actual media processing:
- source access
- decode
- AI analysis
- overlay
- encode
- RTSP push

This is mainly implemented by the existing `ai` and `xop` modules.

### 3.2 Control Plane
Responsible for stream lifecycle and dispatch:
- stream task abstraction
- classification
- scheduling
- stream creation / stopping
- failover

Implemented in:
- `control/StreamTask`
- `control/Classifier`
- `control/Scheduler`
- `control/StreamManager`

### 3.3 Edge Simulation Plane
Responsible for simulating edge nodes and their capacities:
- heterogeneous node abstraction
- task queue and worker pool
- node-level load statistics
- dispatch acceptance / rejection

Implemented in:
- `cdn_sim/EdgeNode`
- `cdn_sim/EdgeNodePool`
- `cdn_sim/ThreadPool`

### 3.4 Observability Plane
Responsible for system visibility:
- stream metrics
- node metrics
- dispatch/failover counters

Implemented in:
- `observe/MetricsRegistry`

---

## 4. Core Design Concepts

## 4.1 StreamTask

All stream requests are abstracted into a unified `StreamTask`.

A `StreamTask` includes:
- stream identity
- source URI / source type
- width / height / fps
- target bitrate
- AI enable flag
- overlay enable flag
- region
- analyze fps
- recording / output flags
- classification tags

This abstraction allows the scheduler to reason about a stream request without caring about the internal implementation details of the pipeline.

---

## 4.2 Multi-dimensional Stream Classification

To simulate CDN-like task dispatch, each stream is classified along multiple dimensions.

### 4.2.1 Region
Represents source region or desired nearest edge:
- Local
- North
- East
- South
- West

Purpose:
- simulate “nearest edge” routing logic

### 4.2.2 BitrateClass
Derived from target bitrate:
- Low
- Medium
- High
- Ultra

Purpose:
- represent network and encoding pressure

### 4.2.3 LatencyClass
Derived from business scenario:
- Realtime
- Interactive
- Standard
- Archive

Purpose:
- express latency sensitivity

### 4.2.4 ComputeClass
Derived from resolution, fps and AI settings:
- Light
- Medium
- Heavy
- Extreme

Purpose:
- represent compute pressure on analysis + encoding

The classifier converts runtime task parameters into scheduler-friendly tags.

---

## 4.3 EdgeNode Simulation

An `EdgeNode` simulates an edge server in a CDN-like deployment.

Each node has:
- node_id
- region
- node type (HighCapacity / MediumCapacity / LowCapacity)
- worker thread count
- max stream count
- max queue size
- runtime status (Healthy / Busy / Degraded / Down)

### 4.3.1 Heterogeneous Node Capacity
Different nodes simulate different machine capacities:
- high-capacity edge
- medium-capacity edge
- low-capacity edge

This reflects real-world differences in CPU and task handling ability.

### 4.3.2 Node Status
Node runtime status may change based on load or failure:
- Healthy: can accept tasks
- Busy: near overload, may reject new tasks
- Degraded: degraded service, should be penalized
- Down: unavailable

---

## 4.4 Scheduling Strategy

The scheduler uses a **score-based dispatch strategy**.

### 4.4.1 Candidate filtering
Before scoring, nodes are filtered by:
- not down
- queue not too deep
- worker utilization below threshold
- stream count below max
- node capability not obviously mismatched

### 4.4.2 Score dimensions
For each candidate node, a score is computed:

score =
  region_weight * region_score +
  load_weight * load_score +
  capability_weight * capability_score +
  latency_weight * latency_score +
  failover_weight * failover_penalty

Where:

#### region_score
Measures region match:
- same region -> lower score
- remote region -> higher score

#### load_score
Measures current node pressure using:
- active workers / total workers
- pending tasks / max queue

#### capability_score
Measures suitability between stream cost and node type:
- Extreme + Ultra on LowCapacity -> strong penalty
- Light task on any node -> low penalty

#### latency_score
Measures node’s recent average pipeline latency:
- Realtime tasks prefer nodes with lower latency

#### failover_penalty
Applies extra penalty to Busy / Degraded nodes

### 4.4.3 Selection
The scheduler selects the candidate node with the **lowest score**.

This approach is preferable to hard-coded if/else rules because:
- easier to tune
- easier to explain
- more extensible for future policy updates

---

## 5. Failover Strategy

If a stream pipeline exits with failure:
1. check whether failover count exceeds `max_failover`
2. exclude the previous node
3. reschedule the same task to another node
4. restart pipeline on the new node

This simulates edge failover logic in a simplified single-machine environment.

### 5.1 Why failover is needed
In a media system, failures may happen due to:
- source open failure
- transient encoding failure
- overloaded worker node
- upstream disconnection

### 5.2 Scope of failover
Current version only supports:
- rescheduling failed or restartable tasks
- excluding the last failed node

Current version does **not** support:
- live pipeline migration without interruption
- stateful session handoff across processes/machines

This limitation should be stated honestly in interviews.

---

## 6. Observability Design

To support troubleshooting and performance analysis, the system exports metrics through `MetricsRegistry`.

### 6.1 Stream-level metrics
- `stream.<id>.pipeline_latency_ms`
- `stream.<id>.avg_pipeline_latency_ms`
- `stream.<id>.frame_count`
- `stream.<id>.dispatch_count`
- `stream.<id>.failover_count`

### 6.2 Node-level metrics
- `node.<id>.active_streams`
- `node.<id>.dispatch_count`
- `node.<id>.avg_pipeline_latency_ms`

### 6.3 Scheduler-level metrics
- `scheduler.dispatch_total`
- `scheduler.failover_total`

### 6.4 Purpose
These metrics are used to:
- observe node pressure
- compare stream latency across nodes
- explain dispatch decisions
- verify whether failover happened
- support later integration with `/metrics` or admin APIs

---

## 7. Why This Design Matches Streaming Infrastructure Work

This design makes the project closer to a real streaming platform in several ways:

1. It introduces a clear distinction between **data plane** and **control plane**
2. It models **edge scheduling** rather than only a single local pipeline
3. It adds **task classification**, which is a common idea in media platforms
4. It adds **load balancing and failover**
5. It introduces **observability**, a key requirement for production systems
6. It prepares the project for future expansion:
   - recording
   - HLS/RTMP output
   - config center
   - policy center
   - alerting

---

## 8. Current Limitations

This version is still a simulation / prototype, with the following limitations:

1. Edge nodes are simulated within a single process or single machine
2. No true multi-machine deployment
3. No real CDN cache / origin / segment cache / back-to-origin logic
4. Failover is restart-based rather than live migration
5. Current observability is in-memory only, not integrated with Prometheus yet
6. Current distribution output is RTSP-focused; HLS/RTMP/recording are planned extensions

These limitations are acceptable as long as they are explained clearly and honestly.

---

## 9. Future Extensions

### 9.1 Media capabilities
- MP4 recording
- HLS segment output
- RTMP push
- adaptive transcode profiles

### 9.2 Control plane capabilities
- HTTP APIs for stream creation / deletion
- policy hot reload
- config center
- dynamic degrade strategy

### 9.3 Observability capabilities
- `/metrics` export
- structured logs
- trace id for pipeline stages
- alerting rules

### 9.4 Edge simulation capabilities
- multi-process node isolation
- machine-level resource sampling
- simulated origin-edge topology
- segment cache / relay simulation

---

## 10. Summary

The CDN simulation layer is a control-plane and scheduling enhancement for StreamSight.

It does not claim to be a real CDN implementation. Instead, it introduces the key engineering ideas of streaming infrastructure:
- classification
- heterogeneous node capacity
- scheduling
- failover
- observability

This makes the project much more suitable for media platform / streaming infrastructure / CDN-related internship and new-grad positions.
