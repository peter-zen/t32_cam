# event_port 暴露缺口探针操作手册

## 1. 目的

给后续会话提供一条最小充分、可重复的验证路径，用于：
- 确认 event server 当前确实存在
- 确认 HTTP / mDNS 当前确实没有暴露 `event_port`
- 避免把“能连上 5000”误写成“客户端已能自动发现 5000”

这篇文档是操作手册，不是行为规格。请先结合：
- `../specs/event-port-exposure-gap.md`
- `../decisions/event-port-http-vs-mdns-exposure-strategy.md`
- `../refs/event-port-code-entry-and-missing-fields.md`

## 2. 适用范围

适用于：
- 校验 event server 存在
- 校验 HTTP / mDNS 暴露缺口
- 为后续补字段前建立基线

不适用于：
- 证明客户端当前已具备自动发现 event port 能力

## 3. 推荐验证思路

当前最稳的验证分三步：
1. 证明 `5000/tcp` 确实在监听
2. 证明 HTTP `device/info` 没有 `event_port`
3. 证明 mDNS TXT Record 没有 `event_port`

## 4. 探针步骤

### 4.1 检查 event server 监听
在 `CMD_MOBILE` 运行后执行：
```bash
ss -lntp | grep 5000
```

### 4.2 检查 HTTP 设备信息
```bash
curl http://127.0.0.1:8080/api/v1/device/info
```

确认返回中没有：
- `event_port`

### 4.3 检查 mDNS TXT
如果有 Bonjour 工具，可浏览 TXT；若无，至少应回看代码：
- `MdnsTxtPayload`
- `MdnsTxtRecord::build()`

当前确认点是：
- 没有 `event_port`

## 5. 当前验证结论应该怎么写

如果只做了这些探针，你最多只能写：
- event server 存在，默认端口是 5000
- 但 HTTP / mDNS 当前未暴露该端口

你不能写：
- 客户端当前可自动发现 event 通道
- discovery 链路已经闭环

## 6. 常见错误理解

- 不要把默认端口当成对外协议承诺
- 不要把 APP 也许能硬编码连上 5000 写成“暴露链路已完成”
- 不要把 event server 存在和 event discovery 存在混为一谈

## 7. 推荐阅读顺序

进入该主题时建议按下面顺序：
1. `../specs/event-port-exposure-gap.md`
2. `../decisions/event-port-http-vs-mdns-exposure-strategy.md`
3. `../refs/event-port-code-entry-and-missing-fields.md`
4. 本文
