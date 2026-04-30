# TCP Event SIMU 验证操作手册

## 1. 目的

给后续会话提供一条最小充分、可重复的 TCP Event 仿真验证路径，用于：
- 在 Simu 下启动 `CMD_MOBILE` 主链路
- 连接 TCP event server
- 触发一个当前已知可发布的事件
- 验证 NDJSON 报文形状与单客户端行为

这篇文档是操作手册，不是行为规格。请先结合：
- `../specs/tcp-event-service-behavior.md`
- `../decisions/tcp-event-optional-side-channel-model.md`
- `../refs/tcp-event-code-entry-and-payload-shape.md`

## 2. 适用范围

适用于：
- `BUILD_FOR_SIMULATION=ON` 的 PC 仿真验证
- `CMD_MOBILE` 路径
- 当前已知的 photo async 事件验证

不适用于：
- 证明所有设计稿事件类型都已接入
- 证明客户端可靠投递、断线恢复、ACK/replay
- 证明真机端和手机端事件消费链路全部闭环

## 3. 当前推荐验证思路

当前最稳的做法不是凭空等事件，而是：
1. 启动 `htc_main_app -m`
2. 连接 `5000/tcp`
3. 通过 HTTP async photo 触发 `PhotoJobManager`
4. 观察 `camera.photo.completed` 或 `camera.photo.failed`

原因很简单：
- 当前代码里最明确的真实业务 publish 点就是拍照任务完成/失败
- 其他设计稿里的事件类别不能假设已经可触发

## 4. 前置条件

### 4.1 已完成仿真构建
在仓库根目录执行：
```bash
mkdir -p build_sim && cd build_sim
cmake -DBUILD_FOR_SIMULATION=ON ..
make -j$(nproc) htc_main_app
```

### 4.2 准备 TCP 客户端工具
常见可选工具：
- `nc`
- `ncat`
- `socat`

至少有一个即可。

### 4.3 避免端口冲突
当前默认相关端口：
- HTTP：由 `[MDNS] CtrlPort` 决定
- RTSP：由 `[MDNS] RtspPort` 决定
- TCP event：固定默认 `5000`

若 HTTP/RTSP 端口冲突，可通过临时配置覆盖。
TCP event 当前代码里未看到独立配置键，因此默认按 `5000` 验证。

## 5. 标准验证流程

### 5.1 启动 `CMD_MOBILE`
在 `build_sim` 目录执行：
```bash
CONFIG_FILE=/path/to/temp-config.ini ./htc_main_app -m
```

### 5.2 检查端口监听
另一个终端执行：
```bash
ss -lntp | grep 5000
```

预期看到：
- `5000/tcp` 已监听

如果 `5000` 没起来，不要先查 photo；先查 event server 启动。

### 5.3 连接事件服务
例如使用 `nc`：
```bash
nc 127.0.0.1 5000
```

建立连接后，当前不会自动收到欢迎包或状态快照。
这不是故障，而是当前协议本来就没有首包。

### 5.4 触发 async photo
另一个终端执行类似：
```bash
curl -X POST http://127.0.0.1:8080/api/v1/camera/photo \
  -H 'Content-Type: application/json' \
  -d '{"channel":0,"save":true,"format":"jpg","quality":85,"response_mode":"async","client_request_id":"evt-probe"}'
```

注意：
- 这里的 HTTP 端口要与你配置中的 `CtrlPort` 一致
- 如果你用的是 18080，就把命令里的 8080 改掉

### 5.5 观察事件输出
TCP 客户端中应看到一行 JSON，类似：
```json
{"version":1,"event_id":"evt_...","category":"camera","type":"camera.photo.completed","timestamp":...,"level":"info","data":{...},"sequence":1}
```

失败场景则可能看到：
- `camera.photo.failed`

## 6. 推荐检查项

### 6.1 envelope 检查
确认：
- 有 `version`
- 有 `event_id`
- 有 `category`
- 有 `type`
- 有 `timestamp`
- 有 `level`
- 有 `data`
- 有 `sequence`
- 末尾按行分隔

### 6.2 成功事件检查
若收到 `camera.photo.completed`，重点看：
- `data.job_id`
- `data.client_request_id`
- `data.status`
- `data.photo.filepath`
- `data.photo.filename`

### 6.3 失败事件检查
若收到 `camera.photo.failed`，重点看：
- `data.job_id`
- `data.client_request_id`
- `data.status`
- `data.error_message`

## 7. 单客户端替换验证

当前实现是单客户端模型。
可这样验证：
1. 先开终端 A：`nc 127.0.0.1 5000`
2. 再开终端 B：`nc 127.0.0.1 5000`
3. 再触发 async photo

预期：
- 只有后连入的客户端收到事件
- 先前连接会被替换

如果你预期两个终端都能同时收消息，那预期本身就是错的。

## 8. 推荐排查顺序

### 8.1 `5000/tcp` 没监听
优先查：
- `CMD_MOBILE` 是否真的走到启动分支
- 日志里是否有 `tcp event server started on port 5000`
- 端口是否被别的进程占用

### 8.2 端口监听正常，但没有任何输出
优先查：
- 是否真的建立了 TCP client 连接
- 是否触发了 async photo 而不是 sync photo
- 是否触发时 HTTP 请求本身失败
- 当前是否有实际事件源在 publish

### 8.3 连接后没首包
这是正常行为。
当前协议不自动发送欢迎包和状态快照。

### 8.4 触发 photo 后还是没事件
优先查：
- photo job 是否真的进入 async 模式
- `job_id` 是否生成
- 服务日志里是否有 photo 失败
- 当前连接是否已被另一个客户端替换

### 8.5 收到事件但字段和设计稿不一样
优先以代码为准，不要先以设计稿为准。
当前应回头核对：
- `TcpEventService::publish()`
- `PhotoJobManager::publishFinishedEvent()`

## 9. 常见错误理解

- 不要把 sync photo 当成 event 验证入口
- 不要等待“自动状态快照”——当前没有
- 不要假设多客户端广播
- 不要把设计稿里的 storage/power/device 事件当成当前都能测
- 不要把 event 通道失败等同于 HTTP/RTSP 主链路失败

## 10. 推荐阅读顺序

进入 TCP Event 主题时建议按下面顺序：
1. `../specs/tcp-event-service-behavior.md`
2. `../decisions/tcp-event-optional-side-channel-model.md`
3. `../refs/tcp-event-code-entry-and-payload-shape.md`
4. 本文
5. 再看：
   - `src/service/event/TcpEventService.cpp`
   - `src/service/http_server/PhotoJobManager.cpp`
   - `src/app/main_app.cpp`
