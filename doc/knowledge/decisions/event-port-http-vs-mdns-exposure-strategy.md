# event_port 通过 HTTP 还是 mDNS 暴露的策略决策

## 1. 决策主题

明确 `t32_cam` 后续若补 `event_port` 暴露链路，为什么应优先考虑：
- HTTP 设备信息接口
- mDNS TXT Record

并评估两者关系，而不是继续让客户端依赖隐式约定或硬编码。

## 2. 当前事实基础

当前代码中：
- event server 已存在，默认端口 `5000`
- 但 `device/info` 没有 `event_port`
- mDNS TXT Record 也没有 `event_port`

所以当前问题不是有没有 event server，而是客户端如何知道它。

## 3. 为什么不能继续依赖隐式约定

如果客户端继续默认硬编码 `5000`，短期能跑，但工程上不稳。

问题包括：
- 端口一旦未来改成可配置，客户端立即失配
- 新协作者会误以为这是协议保证，而不是当前偶然事实
- HTTP / mDNS 已经承担控制与发现职责，不把 event_port 纳入其中会导致协议信息分裂

## 4. HTTP 暴露的价值

在 `device/info` 中补 `event_port` 的好处：
- 和控制面同源
- 设备被发现后，客户端立刻能通过权威控制接口拿到 event 入口
- 易于扩展更多 capability 字段

缺点：
- 客户端必须先知道 HTTP ctrl_port 才能进一步拿 event_port

因此 HTTP 更适合作为：
- 权威查询面

## 5. mDNS 暴露的价值

在 TXT Record 中补 `event_port` 的好处：
- discovery 阶段就能一次拿到 control / stream / event 三类入口
- 更适合移动端“发现即建模”流程

缺点：
- TXT 字段需要保持简洁和兼容
- 发现链路本身若失败，仍需 HTTP 兜底

因此 mDNS 更适合作为：
- 快速发现面

## 6. 当前推荐策略

最佳策略不是二选一，而是：
1. HTTP 设备信息接口补 `event_port`，作为权威来源
2. mDNS TXT Record 补 `event_port`，作为快速发现来源

这样：
- mDNS 提供 discovery convenience
- HTTP 提供 authoritative confirmation

## 7. 当前决策结论

后续若补 `event_port` 暴露链路，建议采用：
- HTTP + mDNS 双暴露模型

但在当前代码尚未落地前，文档必须继续写明：
- 这仍是待补项，不是当前事实

## 8. 何时需要重新评估

出现以下需求时应重新评估：
- event port 改为可配置
- event server 生命周期不再只绑定 `CMD_MOBILE`
- 设备存在多种 event channel 或多端口能力

在这些变化出现前，双暴露模型是最稳的方案。