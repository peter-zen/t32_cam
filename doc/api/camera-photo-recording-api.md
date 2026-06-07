# T32 Camera Photo & Recording HTTP API

## 基础信息

- **Base URL**: `http://<device_ip>/api/v1`
- **默认端口**: `80`
- **内容格式**: `application/json`
- **CORS**: 已开启，支持跨域请求

## 通用响应格式

```json
{
  "code": 0,
  "message": "success",
  "data": {}
}
```

- `code=0` 表示成功，非零表示错误
- `message` 为提示信息
- `data` 为业务数据

---

## 一、拍照接口

### 1.1 单张拍照

触发一次拍照。支持同步（`sync`）和异步（`async`）两种响应模式。

```
POST /camera/photo
```

**请求参数 (Body)**

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|------|------|------|--------|------|
| `channel` | int | 否 | 0 | 摄像头通道 |
| `save` | bool | 否 | true | 是否保存到本地 |
| `format` | string | 否 | "jpg" | 图片格式 |
| `quality` | int | 否 | 85 | 图片质量 (1-100) |
| `response_mode` | string | 否 | "sync" | 响应模式：`sync` / `async` |
| `client_request_id` | string | 否 | "" | 客户端请求标识（用于异步模式） |

**同步模式请求示例**

```bash
curl -X POST http://<device_ip>/api/v1/camera/photo \
  -H "Content-Type: application/json" \
  -d '{
    "channel": 0,
    "save": true,
    "format": "jpg",
    "quality": 85,
    "response_mode": "sync"
  }'
```

**同步模式响应示例**

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "photo_id": "photo_1715234567",
    "filename": "IMG_20240509_120000.jpg",
    "filepath": "/mnt/sdcard/DCIM/IMG_20240509_120000.jpg",
    "size": 2048000,
    "timestamp": 1715234567,
    "response_mode": "sync",
    "status": "completed"
  }
}
```

**异步模式请求示例**

```bash
curl -X POST http://<device_ip>/api/v1/camera/photo \
  -H "Content-Type: application/json" \
  -d '{
    "channel": 0,
    "save": true,
    "format": "jpg",
    "quality": 85,
    "response_mode": "async",
    "client_request_id": "req-12345"
  }'
```

**异步模式响应示例**

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "response_mode": "async",
    "status": "accepted",
    "job_id": "job_1715234567",
    "client_request_id": "req-12345",
    "submitted_at": 1715234567000,
    "result_query": "/api/v1/camera/photo/status?job_id=job_1715234567",
    "event": {
      "enabled": true,
      "connected": true,
      "port": 9000,
      "success_event": "camera.photo.completed",
      "failed_event": "camera.photo.failed"
    }
  }
}
```

> 异步模式下，客户端可通过 `result_query` 轮询查询结果，或监听 TCP 事件服务推送的事件。

---

### 1.2 查询拍照状态

```
GET /camera/photo/status?job_id=<job_id>
```

**请求参数 (Query)**

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `job_id` | string | 否 | 异步任务 ID。不传则返回最新任务或当前拍照状态 |

**响应示例（异步任务查询）**

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "job_id": "job_1715234567",
    "task_type": "single",
    "status": "completed",
    "progress": 100,
    "client_request_id": "req-12345",
    "submitted_at": 1715234567000,
    "updated_at": 1715234569000,
    "photo": {
      "photo_id": "photo_1715234567",
      "filename": "IMG_20240509_120000.jpg",
      "filepath": "/mnt/sdcard/DCIM/IMG_20240509_120000.jpg",
      "size": 2048000,
      "timestamp": 1715234567
    }
  }
}
```

**状态枚举**

| status | 说明 |
|--------|------|
| `accepted` | 已接收 |
| `processing` | 处理中 |
| `completed` | 已完成 |
| `failed` | 失败 |

---

### 1.3 连拍

```
POST /camera/photo/burst
```

**请求参数 (Body)**

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|------|------|------|--------|------|
| `count` | int | 否 | 3 | 连拍张数 |
| `interval` | int | 否 | 1000 | 间隔毫秒数 |

**请求示例**

```bash
curl -X POST http://<device_ip>/api/v1/camera/photo/burst \
  -H "Content-Type: application/json" \
  -d '{"count": 5, "interval": 500}'
```

**响应示例**

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "job_id": "burst_1715234567",
    "status": "processing",
    "total_count": 5
  }
}
```

---

### 1.4 定时拍照

```
POST /camera/photo/timer
```

**请求参数 (Body)**

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|------|------|------|--------|------|
| `action` | string | 否 | "start" | 操作：`start` / `stop` |
| `interval` | int | 是(start时) | 0 | 间隔秒数（必须 > 0） |
| `count` | int | 否 | 0 | 总张数（0 表示无限） |
| `channel` | int | 否 | 0 | 摄像头通道 |

**启动定时拍照**

```bash
curl -X POST http://<device_ip>/api/v1/camera/photo/timer \
  -H "Content-Type: application/json" \
  -d '{
    "action": "start",
    "interval": 10,
    "count": 0,
    "channel": 0
  }'
```

**响应示例**

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "timer_id": "timer_1715234567",
    "status": "running",
    "interval": 10,
    "total_count": 0,
    "completed_count": 0,
    "channel": 0
  }
}
```

**停止定时拍照**

```bash
curl -X POST http://<device_ip>/api/v1/camera/photo/timer \
  -H "Content-Type: application/json" \
  -d '{"action": "stop"}'
```

**响应示例**

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "timer_id": "timer_1715234567",
    "status": "stopped",
    "completed_count": 12
  }
}
```

---

## 二、录影接口

### 2.1 开始录影

```
POST /camera/video/start
```

**请求参数 (Body)**

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|------|------|------|--------|------|
| `channel` | int | 否 | 0 | 摄像头通道 |
| `duration` | int | 否 | 0 | 录影时长（秒），0 表示手动停止 |
| `audio` | bool | 否 | true | 是否录制音频 |

**请求示例**

```bash
curl -X POST http://<device_ip>/api/v1/camera/video/start \
  -H "Content-Type: application/json" \
  -d '{"channel": 0, "duration": 0, "audio": true}'
```

**响应示例**

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "status": "recording",
    "channel": 0
  }
}
```

---

### 2.2 停止录影

```
POST /camera/video/stop
```

**请求示例**

```bash
curl -X POST http://<device_ip>/api/v1/camera/video/stop
```

**响应示例**

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "status": "stopped"
  }
}
```

---

### 2.3 查询录影状态

```
GET /camera/video/status
```

**响应示例**

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "status": "recording",
    "duration": 120,
    "filepath": "/mnt/sdcard/DCIM/VID_20240509_120000.mp4"
  }
}
```

**状态说明**

| status | 说明 |
|--------|------|
| `recording` | 正在录影 |
| `idle` | 空闲/未录影 |

---

## 三、媒体文件接口

### 3.1 照片列表

```
GET /camera/photos?offset=0&limit=20
```

**请求参数 (Query)**

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|------|------|------|--------|------|
| `offset` | int | 否 | 0 | 偏移量 |
| `limit` | int | 否 | 20 | 每页数量 |

**响应示例**

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "photos": [
      {
        "id": 1,
        "type": 1,
        "path": "/mnt/sdcard/DCIM/IMG_20240509_120000.jpg",
        "size": 2048000,
        "timestamp": 1715234567,
        "duration": 0,
        "width": 1920,
        "height": 1080,
        "container_type": "",
        "playback_capable": false,
        "playback_reason": "",
        "range_supported": false,
        "seek_support": false,
        "seek_granularity_ms": 0,
        "effective_gop_frames": 0,
        "effective_gop_ms": 0,
        "download_url": "/api/v1/camera/files/download?id=1"
      }
    ],
    "total": 100,
    "offset": 0,
    "limit": 20
  }
}
```

---

### 3.2 视频列表

```
GET /camera/video/list?offset=0&limit=20
```

**响应示例**

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "videos": [
      {
        "id": 2,
        "type": 2,
        "path": "/mnt/sdcard/DCIM/VID_20240509_120000.mp4",
        "size": 52428800,
        "timestamp": 1715234567,
        "duration": 120,
        "width": 1920,
        "height": 1080,
        "container_type": "mp4",
        "playback_capable": true,
        "playback_reason": "",
        "range_supported": true,
        "seek_support": true,
        "seek_granularity_ms": 2000,
        "effective_gop_frames": 30,
        "effective_gop_ms": 1000,
        "download_url": "/api/v1/camera/files/download?id=2",
        "playback_url": "/api/v1/camera/video/playback?id=2"
      }
    ],
    "total": 50,
    "offset": 0,
    "limit": 20
  }
}
```

---

### 3.3 下载文件

```
GET /camera/files/download?id=<id>
GET /camera/files/download?token=<token>
```

**请求参数 (Query)**

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `id` | int | 是* | 媒体文件 ID |
| `token` | string | 是* | 播放/下载令牌（与 `id` 二选一） |

> 响应为二进制文件流，HTTP Header 中包含 `Content-Disposition: attachment`。

---

### 3.4 视频播放

支持 HTTP Range 请求，可用于边下边播。

```
GET /camera/video/playback?id=<id>
```

**请求参数 (Query)**

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `id` | int | 是 | 视频文件 ID |

> 仅支持 `fmp4` 和 `mp4` 格式。响应为 `video/mp4` 二进制流。

---

### 3.5 删除文件

```
POST /camera/files/delete
```

**请求参数 (Body)**

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `file_ids` | string[] | 是 | 文件路径或 ID 数组 |

**请求示例**

```bash
curl -X POST http://<device_ip>/api/v1/camera/files/delete \
  -H "Content-Type: application/json" \
  -d '{
    "file_ids": [
      "/mnt/sdcard/DCIM/IMG_20240509_120000.jpg",
      "/mnt/sdcard/DCIM/VID_20240509_120000.mp4"
    ]
  }'
```

**响应示例**

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "total": 2,
    "success": 2,
    "failed": 0,
    "results": [
      {
        "file_id": "/mnt/sdcard/DCIM/IMG_20240509_120000.jpg",
        "status": "success"
      },
      {
        "file_id": "/mnt/sdcard/DCIM/VID_20240509_120000.mp4",
        "status": "success"
      }
    ]
  }
}
```

---

## 四、实时预览接口

### 4.1 单帧预览

```
GET /camera/preview?channel=0&width=1920&height=1080&format=jpeg
```

**请求参数 (Query)**

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|------|------|------|--------|------|
| `channel` | int | 否 | 0 | 摄像头通道 |
| `width` | int | 否 | 1920 | 目标宽度 |
| `height` | int | 否 | 1080 | 目标高度 |
| `format` | string | 否 | "jpeg" | 格式：`jpeg` / `mjpeg` |

> `format=jpeg` 时返回单张 JPEG 图像（二进制）。

---

### 4.2 MJPEG 实时流

```
GET /camera/preview?channel=0&width=640&height=360&format=mjpeg
```

> `format=mjpeg` 时返回 `multipart/x-mixed-replace` 流，每帧间隔约 200ms。客户端断开连接后自动停止推送。

---

### 4.3 缩略图

```
GET /camera/thumbnail?file_path=<path>
GET /camera/thumbnail?channel=0&size=320
```

**请求参数 (Query)**

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|------|------|------|--------|------|
| `file_path` | string | 否 | "" | 按文件路径获取缩略图 |
| `channel` | int | 否 | 0 | 按通道实时抓取（与 `file_path` 二选一） |
| `size` | int | 否 | 320 | 实时抓取的尺寸（宽高相同） |

> 响应为 `image/jpeg` 二进制图像。

---

## 附录：错误码

| HTTP Status | API Code | 说明 |
|-------------|----------|------|
| 200 | 0 | 成功 |
| 200 | 1001 | 拍照/连拍失败 |
| 200 | 1002 | 定时拍照已在运行或启动失败 |
| 200 | 1005 | 录影已在进行或启动失败 |
| 200 | 1006 | 录影未开始或停止失败 |
| 200 | 1007 | 拍照任务未找到 / 预览帧捕获失败 |
| 200 | 1008 | 缩略图捕获失败 |
| 400 | 400 | 请求参数错误 / JSON 解析失败 |
| 404 | 404 | 资源未找到（文件/媒体/属性） |
| 405 | 405 | HTTP 方法不允许 |
| 415 | 415 | 不支持的媒体格式 |
