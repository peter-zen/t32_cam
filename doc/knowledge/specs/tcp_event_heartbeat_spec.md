# TCP Event Heartbeat Spec

## 1. Background

当前 TCP event 通道在 APP 建连后，仅在真实业务事件发生时才会有数据输出。
当链路长时间空闲时，APP 无法仅依靠 event 通道判断相机是否仍在线。

因此，需要在现有 TCP event 通道上增加 heartbeat 机制，为 APP 提供稳定的在线活跃信号。

## 2. Scope

本规格只覆盖相机侧 heartbeat 发送能力：

- 复用现有 TCP event 通道
- 复用现有 NDJSON 与统一 JSON envelope
- 默认 heartbeat 周期为 `5s`

以下内容不在本次范围内：

- APP 侧断线判定算法
- 重连机制
- ACK / 握手 / 注册报文
- heartbeat 周期配置化
- 其它事件族扩展

## 3. Event Definition

### 3.1 Event Name

- `category`: `device`
- `type`: `device.heartbeat`
- `level`: `info`

### 3.2 Data Payload

`data` 最小字段定义如下：

| 字段 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `heartbeat_interval_ms` | int | 是 | 当前 heartbeat 目标周期，首版固定 `5000` |

### 3.3 Example

```json
{
  "version": 1,
  "event_id": "evt_1737276325000_0005",
  "category": "device",
  "type": "device.heartbeat",
  "timestamp": 1737276325,
  "sequence": 5,
  "level": "info",
  "data": {
    "heartbeat_interval_ms": 5000
  }
}
```

传输时仍使用 NDJSON，即每条 JSON 末尾追加 `\n`。

## 4. Timing Semantics

### 4.1 Idle-gap Heartbeat

heartbeat 的职责是补足空闲期的活跃信号，而不是要求独立于业务事件永久保持固定节拍。

规则如下：

1. 当 TCP event client 建立连接后，如果连接上尚未有任何成功发送的 event，则允许相机尽快发送第一条 heartbeat。
2. 之后，当最近一次成功发送的 event 距当前时间已达到或超过 `5s` 时，相机发送一条 heartbeat。
3. 如果这 `5s` 内已经有其它业务 event 成功发送，则这些业务 event 本身即可视为链路活跃信号，不强制额外补发 heartbeat。

### 4.2 Activity Definition

下列 event 都可视为“链路活跃”：

- `device.heartbeat`
- 现有业务 event，例如：
  - `camera.photo.completed`
  - `camera.photo.failed`
- 后续所有通过同一 `TcpEventService` 成功发送的合法 event

## 5. Envelope And Sequence Rules

heartbeat 必须完全复用当前 TCP event 统一 envelope：

- `version`
- `event_id`
- `category`
- `type`
- `timestamp`
- `sequence`
- `level`
- `data`

其中：

- `event_id` 继续使用现有生成规则
- `sequence` 继续使用当前 TCP 连接内单调递增规则
- 新 client 替换旧 client 后，`sequence` 从新连接重新计数

## 6. Failure Semantics

heartbeat 的发送失败处理必须与现有 event 发送失败保持一致：

- socket 写失败时，关闭当前 event client
- 不引入 ACK、重发或补发窗口
- 不缓存未送达 heartbeat

## 7. Implementation Notes

推荐在 `src/service/event/TcpEventService.*` 内实现：

- 由 event service 统一维护最近一次成功发送时间
- 由 event service 自身的后台线程检查是否进入空闲窗口
- heartbeat 与业务 event 共用同一发送路径，确保 envelope、`sequence`、错误处理语义一致

## 8. Acceptance Summary

实现完成后，应满足：

1. TCP event 通道空闲时，相机能持续补发 heartbeat。
2. heartbeat 目标周期为 `5s`。
3. 业务 event 活跃时，不要求强制额外发送 heartbeat。
4. heartbeat 继续使用现有 TCP event 协议格式和错误处理语义。
