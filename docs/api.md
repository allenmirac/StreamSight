# REST API 接口文档

HTTP API 默认监听 `http://0.0.0.0:8080`。所有响应均为 JSON，包含 CORS 头。

---

## GET /api/status

返回服务器运行状态。

**响应**

```json
{
  "status": "running",
  "uptime_seconds": 3600,
  "port": 8080
}
```

---

## GET /api/current

返回最近一帧的分析结果。

**响应**

```json
{
  "timestamp_ms": 1710000000000,
  "frame_id": 1234,
  "faces": [
    {
      "name": "Alice",
      "confidence": 0.951,
      "similarity": 0.720,
      "recognized": true,
      "box": { "x": 100, "y": 50, "width": 80, "height": 90 }
    },
    {
      "name": "unknown",
      "confidence": 0.823,
      "similarity": 0.210,
      "recognized": false,
      "box": { "x": 300, "y": 120, "width": 65, "height": 75 }
    }
  ]
}
```

**字段说明**

| 字段 | 类型 | 说明 |
|------|------|------|
| `timestamp_ms` | int64 | Unix 毫秒时间戳 |
| `frame_id` | int | 帧序号 |
| `faces[].name` | string | 识别出的姓名，未识别为 `"unknown"` |
| `faces[].confidence` | float | 检测置信度 [0,1] |
| `faces[].similarity` | float | 与最近匹配的余弦相似度 |
| `faces[].recognized` | bool | 是否超过识别阈值 |
| `faces[].box` | object | 人脸框像素坐标 |

---

## GET /api/events?limit=100

返回最近 N 条包含人脸的检测事件。

**查询参数**

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `limit` | int | 100 | 返回的最大事件数 |

**响应**：与 `/api/current` 格式相同的 JSON 数组。

```json
[
  { "timestamp_ms": ..., "frame_id": ..., "faces": [...] },
  ...
]
```

---

## GET /api/faces

返回已注册的人脸姓名列表。

**响应**

```json
["Alice", "Bob", "Charlie"]
```

---

## POST /api/faces

注册新人脸（或覆盖同名记录）。

**请求**：`multipart/form-data`

| 字段 | 类型 | 说明 |
|------|------|------|
| `name` | string | 人员姓名 |
| `image` | file | 人脸图片（JPEG/PNG/BMP，包含清晰正面人脸） |

**示例**

```bash
curl -X POST http://localhost:8080/api/faces \
  -F "name=Alice" \
  -F "image=@alice.jpg"
```

**响应（成功）**

```json
{ "ok": true, "name": "Alice" }
```

**响应（失败）**

```json
{ "error": "Cannot decode image" }
```

---

## DELETE /api/faces/{name}

删除指定姓名的人脸记录。

**示例**

```bash
curl -X DELETE http://localhost:8080/api/faces/Alice
```

**响应**

```json
{ "ok": true, "name": "Alice" }
```

若姓名不存在：

```json
{ "ok": false, "name": "Unknown" }
```

---

---

## GET /api/latency/stats

返回各模块延迟统计（avg/p50/p95/p99/max），需先开启 LatencyTracer（设置 `STREAMSIGHT_LATENCY_ENABLE=1`）。

**响应**

```json
{
  "stages": [
    {
      "stage": "ai.face_detection",
      "count": 150,
      "avg_us": 18700,
      "p50_us": 17500,
      "p95_us": 25300,
      "p99_us": 28900,
      "max_us": 32100
    }
  ]
}
```

---

## GET /api/latency/recent?limit=10

返回最近 N 条延迟追踪事件。

**查询参数**

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `limit` | int | 10 | 返回的最大事件数 |

---

## POST /api/latency/reset

重置内存中的延迟统计数据（不删除磁盘日志文件）。

**响应**

```json
{ "ok": true }
```

---

## GET /api/v1/sessions

返回所有活跃 session 的 ID 列表及基本状态。

**响应**

```json
{
  "sessions": [
    {
      "id": "session_1",
      "running": true,
      "frames_processed": 1500,
      "uptime_seconds": 60
    }
  ]
}
```

---

## POST /api/v1/sessions

创建新的 StreamSession 并启动。

**请求**：JSON body

```json
{
  "input_url": "pic/test.mp4",
  "width": 640,
  "height": 480,
  "fps": 25,
  "rtsp_suffix": "live",
  "http_port": 8080,
  "bitrate": 2000000,
  "pipeline_mode": "serial",
  "enable_ai": true,
  "analyze_fps": 5,
  "enable_audio": true,
  "rtmp_url": "",
  "effects_json": ""
}
```

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `input_url` | string | (必填) | 输入 URL（文件路径 / `v4l2:/dev/videoN` / `rtsp://...`） |
| `width` | int | 640 | 输出宽度 |
| `height` | int | 480 | 输出高度 |
| `fps` | int | 25 | 帧率 |
| `rtsp_suffix` | string | "live" | RTSP 路径后缀 |
| `http_port` | int | 8080 | HTTP API 端口 |
| `bitrate` | int | 2000000 | 编码码率 (bps) |
| `pipeline_mode` | string | "serial" | 管线模式：`serial` 或 `parallel` |
| `enable_ai` | bool | true | 是否启用 AI 分析 |
| `analyze_fps` | int | 5 | AI 分析帧率 |
| `enable_audio` | bool | true | 是否启用音频 |
| `rtmp_url` | string | "" | RTMP 推流地址（空则不推） |
| `effects_json` | string | "" | Effect 插件 JSON 配置 |

**响应（成功）**

```json
{ "ok": true, "session_id": "session_1" }
```

---

## GET /api/v1/sessions/:id

返回指定 session 的详细状态。

**响应**

```json
{
  "session_id": "session_1",
  "running": true,
  "frames_processed": 1500,
  "frames_dropped": 3,
  "uptime_seconds": 60,
  "rtsp_port": 8554,
  "http_port": 8080,
  "decode_ring_fill": 2,
  "process_ring_fill": 1,
  "backpressure_events": 0
}
```

---

## DELETE /api/v1/sessions/:id

停止并删除指定 session。

**响应（成功）**

```json
{ "ok": true }
```

**响应（不存在）**

```json
{ "ok": false, "error": "session not found" }
```

---

## PUT /api/v1/sessions/:id/effects

动态更新 session 的 Effect 插件配置。

**请求**：JSON body

```json
{
  "detect_model": "models/face_detection.onnx",
  "recog_model": "models/face_recognition.onnx",
  "analyze_fps": 10
}
```

**响应（成功）**

```json
{ "ok": true }
```

---

## GET /api/v1/sessions/:id/results

返回指定 session 最近的 AI 检测结果。

**响应**

```json
{
  "session_id": "session_1",
  "results": [
    {
      "frame_id": 1234,
      "timestamp_ms": 1710000000123,
      "faces": [
        { "name": "Alice", "confidence": 0.95, "recognized": true }
      ]
    }
  ]
}
```

---

## 错误响应

| HTTP 状态码 | 场景 |
|-------------|------|
| 400 | 缺少必要参数或图片无法解码 |
| 404 | session 不存在 |
| 422 | 图片中无法提取人脸特征 |
| 500 | session 创建失败 |
| 503 | 模型/数据库未加载 |
