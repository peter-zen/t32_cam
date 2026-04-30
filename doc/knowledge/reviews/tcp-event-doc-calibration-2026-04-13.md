# TCP Event 文档校准记录

日期：2026-04-13

## 1. 本轮目标

把 TCP event 相关历史设计文档与当前代码状态对齐，建立下一批正式迁移样板。

## 2. 本轮读取范围

代码：
- `src/service/event/TcpEventService.h`
- `src/service/event/TcpEventService.cpp`
- `src/service/event/CMakeLists.txt`
- `src/service/http_server/PhotoJobManager.cpp`
- `src/service/http_server/CMakeLists.txt`
- `src/app/main_app.cpp`

历史文档：
- `doc/design/tcp_event_design.md`

## 3. 本轮新增产出

已新增：
- `doc/knowledge/specs/tcp-event-service-behavior.md`
- `doc/knowledge/decisions/tcp-event-optional-side-channel-model.md`
- `doc/knowledge/refs/tcp-event-code-entry-and-payload-shape.md`
- `doc/knowledge/playbooks/tcp-event-simu-verification.md`

## 4. 本轮校准出的关键结论

### 4.1 event server 已经真实落地
当前不是只有设计稿。
代码已经明确存在：
- `TcpEventService`
- `event_service` 构建目标
- `CMD_MOBILE` 中启动/停止逻辑

### 4.2 当前是可选旁路，不是硬前置
`main_app.cpp` 当前对 TCP event 的处理是：
- 启动失败仅 warning
- 不阻断 HTTP / RTSP 主链路

所以不能把它写成和 mDNS 同级的硬依赖。

### 4.3 当前业务接线明显少于设计稿范围
设计稿定义了大量：
- storage
- power
- camera
- device

事件类别。

但当前代码里最明确的实际 publish 接线点主要是：
- `PhotoJobManager`

所以不能把设计稿范围误写成当前全部已落地事实。

### 4.4 当前协议形态已经固定到代码
当前代码已明确：
- 端口默认 `5000`
- NDJSON over TCP
- 单客户端替换模型
- 无 ACK / replay / 缓存

这些现在应被视为当前事实，而不是仅仅设计意图。

## 5. 当前推荐文档结构

### 已建立
- `specs/tcp-event-service-behavior.md`
- `decisions/tcp-event-optional-side-channel-model.md`
- `refs/tcp-event-code-entry-and-payload-shape.md`
- `playbooks/tcp-event-simu-verification.md`

### 后续可补
- `bugs/tcp-event-single-client-and-no-replay-limits.md`
- APP 对接结果或真机联调 review 文档

## 6. 结论

TCP Event 已适合作为该仓库下一批正式迁移主题。

原因：
- 设计文档完整
- 当前代码入口清晰
- 设计目标与现状成熟度之间有明显差异
- 非常适合通过 spec / decision / refs / playbook 结构把当前事实收敛出来

## 7. 下一步建议

优先级建议：
1. 如果后续要补齐异步控制链路，建议继续迁移 `workmode` 主题
2. 如果 APP 对接 TCP event 开始推进，补一篇联调 review
3. 如果单客户端/无重放限制开始成为问题，再补 bug 文档
