# TCP Event 代码入口与载荷形状参考

## 1. 目的

给后续会话提供最小充分入口，用于快速回答：
- TCP event 从哪里启动
- 事件报文长什么样
- 当前谁在发事件
- 当前默认端口是多少
- 当前限制在哪里

## 2. 当前最重要代码入口

### 2.1 服务实现
- `src/service/event/TcpEventService.h`
- `src/service/event/TcpEventService.cpp`

### 2.2 主编排入口
- `src/app/main_app.cpp`

关注：
- `CMD_MOBILE` 中 `TcpEventService::start(kDefaultTcpEventPort)`
- 退出路径中的 `stop()`

### 2.3 当前业务事件发布入口
- `src/service/http_server/PhotoJobManager.cpp`

这里目前是最明确的真实业务接线点。

### 2.4 构建接线
- `src/service/event/CMakeLists.txt`
- `src/service/CMakeLists.txt`
- `src/service/http_server/CMakeLists.txt`
- `src/app/CMakeLists.txt`

## 3. 当前关键常量与函数

### 3.1 默认端口
- `kDefaultTcpEventPort = 5000`

### 3.2 生命周期函数
- `start(uint16_t port = kDefaultTcpEventPort)`
- `stop()`
- `isRunning()`
- `hasClient()`
- `port()`

### 3.3 发布函数
- `publish(const TcpEventMessage& message)`

### 3.4 连接管理函数
- `acceptLoop()`
- `replaceClientLocked(int clientFd)`
- `closeClientLocked()`

### 3.5 ID / level 辅助函数
- `buildEventId()`
- `levelToString(TcpEventLevel level)`

## 4. 当前 envelope 形状

当前 `publish()` 统一构造：
```json
{
  "version": 1,
  "event_id": "evt_...",
  "category": "camera",
  "type": "camera.photo.completed",
  "timestamp": 1710000000,
  "level": "info",
  "data": {},
  "sequence": 1
}
```

注意：
- 末尾是 `\n`
- 客户端应按 NDJSON 按行解析

## 5. 当前拍照事件载荷形状

### 5.1 成功事件
类型：
- `camera.photo.completed`

`data` 当前包含：
- `job_id`
- `client_request_id`
- `status`
- `photo`

其中 `photo` 包含：
- `photo_id`
- `filename`
- `filepath`
- `size`
- `timestamp`

### 5.2 失败事件
类型：
- `camera.photo.failed`

`data` 当前包含：
- `job_id`
- `client_request_id`
- `status`
- `error_message`

## 6. 当前行为要点

- 只有一个 event client
- 新连接替换旧连接
- 没有客户端时直接丢事件
- 发送失败时直接关闭当前 client
- `sequence` 在新 client 接入后重新从 1 开始

## 7. 默认阅读顺序

进入 TCP Event 主题时，建议按下面顺序：
1. `doc/knowledge/specs/tcp-event-service-behavior.md`
2. `doc/knowledge/decisions/tcp-event-optional-side-channel-model.md`
3. 本文
4. 再去读：
   - `src/service/event/TcpEventService.cpp`
   - `src/service/http_server/PhotoJobManager.cpp`
   - `src/app/main_app.cpp`

## 8. 当前不应再重复的错误理解

- 不要把 TCP Event 当成主控制通道
- 不要把设计稿中所有事件类别当成当前已实现
- 不要假设有 ACK / replay / 缓存
- 不要假设支持多客户端订阅
- 不要假设 `event_port` 已经通过 HTTP / mDNS 对外稳定暴露
