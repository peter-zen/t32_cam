# event_port 暴露链路校准记录

日期：2026-04-13

## 1. 本轮目标

把 event_port 的对外暴露问题从 TCP Event / mDNS / HTTP 三个主题交叉处收敛出来，建立一份独立迁移样板。

## 2. 本轮读取范围

代码：
- `src/service/event/TcpEventService.h`
- `src/service/event/TcpEventService.cpp`
- `src/app/main_app.cpp`
- `src/service/http_server/http_api_v1.cpp`
- `src/service/discovery/MdnsTxtRecord.h`
- `src/service/discovery/MdnsTxtRecord.cpp`

历史文档：
- `doc/design/tcp_event_design.md`
- `doc/spec/mdns-device-discovery-spec.md`
- `doc/solution/20260313-t32-mdns-integration-plan.md`

## 3. 本轮新增产出

已新增：
- `doc/knowledge/specs/event-port-exposure-gap.md`
- `doc/knowledge/decisions/event-port-http-vs-mdns-exposure-strategy.md`
- `doc/knowledge/refs/event-port-code-entry-and-missing-fields.md`
- `doc/knowledge/playbooks/event-port-discovery-gap-probe.md`

## 4. 本轮校准出的关键结论

### 4.1 event server 已有，但暴露链路没补齐
当前默认端口 `5000` 已存在且在 `CMD_MOBILE` 中启动，但 HTTP / mDNS 都没有暴露该字段。

### 4.2 这是“discovery gap”，不是“feature missing”
问题不在 event server 本身，而在客户端如何获知它。

### 4.3 历史设计文档已经识别过这个缺口
`tcp_event_design.md` 里已经明确写过：
- HTTP 设备信息补 `event_port`
- mDNS TXT 补 `event_port`

所以这里不是新需求，而是已知待落地项。

## 5. 当前推荐文档结构

### 已建立
- `specs/event-port-exposure-gap.md`
- `decisions/event-port-http-vs-mdns-exposure-strategy.md`
- `refs/event-port-code-entry-and-missing-fields.md`
- `playbooks/event-port-discovery-gap-probe.md`

### 后续可补
- `bugs/event-port-not-exposed-to-clients.md`
- 落地后的协议更新 review 文档

## 6. 结论

event_port 暴露链路适合作为跨主题交叉治理样板。

原因：
- 代码入口清晰
- 设计建议明确
- 现状缺口也明确
- 很适合把“有服务”与“可发现”这两个常被混淆的概念拆开写清楚

## 7. 下一步建议

优先级建议：
1. 若后续准备动代码，可先决定 HTTP / mDNS 双暴露的落点
2. 若不改代码，也至少应在上层文档中持续明确当前 discovery gap
3. 下一阶段可考虑把本轮迁移方法沉淀成跨主题治理经验
