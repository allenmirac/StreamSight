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

## 错误响应

| HTTP 状态码 | 场景 |
|-------------|------|
| 400 | 缺少必要参数或图片无法解码 |
| 422 | 图片中无法提取人脸特征 |
| 503 | 模型/数据库未加载 |
