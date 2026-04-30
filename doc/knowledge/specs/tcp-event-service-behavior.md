# TCP Event Service 行为规格

## 1. 目的

定义 `t32_cam` 当前 TCP 事件服务 (TCP Event Service) 的仓库级权威行为描述，覆盖：
- 服务角色、端口与连接模型
- 报文结构与传输语义
- `CMD_MOBILE` 下与 HTTP / RTSP / mDNS 的接线关系
- 当前事件来源、成熟度与局限
- 已确认事实与仍待验证项

本规格以当前代码为准，不以历史设计稿中的目标范围为准。

## 2. 当前状态

- 状态：已完成一轮代码校准
- 当前服务形态：设备侧 TCP server，APP 侧 TCP client
- 当前默认端口：`5000`
- 当前接线位置：`CMD_MOBILE`
- 当前已确认事件来源：`PhotoJobManager` 异步拍照完成/失败事件

## 3. 当前权威代码入口

- `src/service/event/TcpEventService.h`
- `src/service/event/TcpEventService.cpp`
- `src/service/event/CMakeLists.txt`
- `src/service/http_server/PhotoJobManager.cpp`
- `src/service/http_server/CMakeLists.txt`
- `src/app/main_app.cpp`

历史参考但不作为唯一依据：
- `doc/design/tcp_event_design.md`

## 4. 当前代码结构与接线

### 4.1 event 服务已进入主构建链路
当前已确认：
- `src/service/CMakeLists.txt` 已 `add_subdirectory(event)`
- `src/service/event/CMakeLists.txt` 已生成 `event_service`
- `http_server` 已链接 `event_service`
- `htc_main_app` 已链接 `event_service`

因此 TCP Event 当前不是设计阶段设想，而是已进入构建链路的真实模块。

### 4.2 当前主封装是 `TcpEventService`
当前仓库没有把事件推送散落成多套 socket 逻辑。
对上层暴露的统一入口是：
- `start(port)`
- `stop()`
- `publish(message)`
- `isRunning()`
- `hasClient()`
- `port()`

### 4.3 当前不是通用消息总线
虽然设计目标想覆盖更多事件域，但按当前代码事实：
- 事件 server 已存在
- 但实际明确接入的业务事件来源，目前只看到 `PhotoJobManager`

所以当前更准确描述是：
- 已有可复用的 TCP 事件通道基础设施
- 但业务事件接入仍然偏少

不要把它写成“所有设计稿里的 event 类型都已落地”。

## 5. 当前启动入口与生命周期

### 5.1 `CMD_MOBILE` 下启动
`main_app.cpp` 当前在 `CMD_MOBILE` 路径中：
1. 启动 mDNS（若启用）
2. 启动 HTTP server
3. 尝试启动 `TcpEventService::start(kDefaultTcpEventPort)`
4. 启动 RTSP server

退出时：
- 停止 RTSP
- 停止 HTTP
- 停止 TCP event
- 停止 mDNS

### 5.2 TCP event 启动失败不会阻断主链路
这点很关键。
当前 `main_app.cpp` 中如果：
- `TcpEventService::start(...)` 失败

行为是：
- 仅打印 warning
- 不会 `goto main_exit`
- HTTP / RTSP 主链路继续启动

因此当前 TCP Event 的正确语义是：
- 可选增强通道
- 不是 `CMD_MOBILE` 的硬前置条件

这与 mDNS 不同。mDNS 启动失败在启用状态下会阻断主链路，而 TCP event 不会。

### 5.3 全局退出路径也会兜底 stop
`main_exit:` 路径当前还会再次调用：
- `TcpEventService::getInstance()->stop()`

因此当前 stop 逻辑是多点兜底的，而不是只依赖单一路径。

## 6. 当前连接模型

### 6.1 单客户端模型
`TcpEventService` 当前实现明确：
- 监听 TCP socket
- 只保留一个 `clientFd_`
- 新客户端连入时调用 `replaceClientLocked(clientFd)`
- 旧客户端会被关闭

因此当前是：
- 单客户端连接模型
- 新连接替换旧连接

### 6.2 无客户端时不缓存
`publish()` 当前如果：
- 服务未运行
- 或 `clientFd_ < 0`

则直接返回 `false`。

没有看到事件缓存、补发、重放逻辑。

因此当前语义是：
- 没有客户端就直接丢弃事件
- 不做离线事件队列

### 6.3 写失败即丢客户端
`publish()` 在发送失败时：
- 记录 warning
- `closeClientLocked()`
- 返回 `false`

因此当前没有：
- 重试发送
- ACK
- 断线补发

这和设计文档的首版目标是一致的，但这里已经是代码事实，不再只是方案原则。

## 7. 当前报文语义

### 7.1 NDJSON 传输
当前 `publish()` 使用 `Json::FastWriter` 生成 JSON，并确保：
- 每条消息末尾都有 `\n`

因此客户端应按：
- UTF-8 JSON
- 每行一条事件
- NDJSON (Newline Delimited JSON)

来解析。

### 7.2 当前 envelope 字段
当前 `publish()` 固定生成：
- `version`
- `event_id`
- `category`
- `type`
- `timestamp`
- `level`
- `data`
- `sequence`

其中：
- `version = 1`
- `timestamp` 为秒级 Unix 时间
- `event_id` 由 `evt_<milliseconds>_<counter>` 生成
- `sequence` 是当前 TCP 连接内单调递增序号

### 7.3 level 取值
当前 `TcpEventLevel` 映射为：
- `INFO -> "info"`
- `WARN -> "warn"`
- `ERROR -> "error"`

## 8. 当前 sequence 与 event_id 语义

### 8.1 `event_id` 全局递增趋势
`buildEventId()` 当前使用：
- 毫秒时间戳
- 全局原子计数器 `eventCounter_`

因此 `event_id` 更接近全局唯一标识，而不是连接内序号。

### 8.2 `sequence` 是连接内序号
`sequence` 当前来自：
- `connectionSequence_ + 1`
- 发送成功后 `++connectionSequence_`
- 新客户端连接时 `replaceClientLocked()` 会把 `connectionSequence_` 清零

因此：
- `sequence` 只在当前连接生命周期内单调递增
- 新客户端接管后会从 1 重新开始

## 9. 当前已接入的业务事件

### 9.1 PhotoJobManager 已接入
`PhotoJobManager.cpp` 当前明确会在异步拍照任务结束时发布事件：
- `camera.photo.completed`
- `camera.photo.failed`

### 9.2 当前 payload 要点
对于拍照任务，当前 data 中包含：
- `job_id`
- `client_request_id`
- `status`
- 成功时：`photo`
- 失败时：`error_message`

其中成功时 `photo` 内包含：
- `photo_id`
- `filename`
- `filepath`
- `size`
- `timestamp`

### 9.3 当前没有看到更多域的实际 publish 接入
虽然设计稿列了：
- `storage`
- `power`
- `device`
- 更多 `camera` 状态

但当前代码检索结果里，明确实际 publish 的业务接线点主要是：
- `PhotoJobManager`

因此不能把设计文档中的所有事件类别写成“当前都已实现”。

## 10. 当前明确结论

### 10.1 可以明确写成事实的
- TCP event server 已作为独立模块接入构建
- 当前默认端口是 `5000`
- 当前传输语义是 NDJSON over TCP
- 当前是单客户端模型，新连接替换旧连接
- 无客户端时事件直接丢弃，不缓存
- 发送失败时会丢弃当前客户端
- `PhotoJobManager` 已接入拍照完成/失败事件
- TCP event 启动失败不会阻断 `CMD_MOBILE` 主链路

### 10.2 不能写得过满的
- 当前所有设计稿中的事件类别都已落地
- 当前已有 ACK / 重传 / 补发机制
- 当前支持多客户端并发订阅
- 当前 event port 已通过 HTTP 或 mDNS 全面暴露给外部
- 当前 event service 已形成完整通用事件平台

这些说法都会过度承诺。

## 11. 与历史设计稿相比的关键校准

### 11.1 已落地的
- 独立 TCP event server 模块
- NDJSON 报文模型
- 默认端口 `5000`
- 单客户端替换模型
- 事件失败不反向影响主业务结果

### 11.2 仍停留在设计目标、未见充分代码证明的
- storage / power / device 等多域事件全面接入
- `event_port` 已进入 HTTP 设备信息接口
- `event_port` 已进入 mDNS TXT Record
- 更丰富的异步状态通道

## 12. 仍待验证项

- 当前 APP 侧是否已有稳定 TCP client 对接实现
- 真实运行时 `event_port` 对外发现方式是否已补齐
- 多次重连、异常断开时 sequence / client 替换行为是否稳定
- 除 photo 任务外，后续是否已有其他模块正在接入事件发布

## 13. 推荐后续拆分

后续如果继续治理，建议补：
- `decisions/tcp-event-optional-side-channel-model.md`
- `refs/tcp-event-code-entry-and-payload-shape.md`
- `playbooks/tcp-event-simu-verification.md`
- `bugs/tcp-event-single-client-and-no-replay-limits.md`

当前这篇规格先承担当前行为权威入口，避免继续把设计稿目标态和现状混写。