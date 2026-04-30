# event_port 暴露链路现状规格

## 1. 目的

定义 `t32_cam` 当前 `event_port` 对外暴露链路的仓库级权威描述，覆盖：
- TCP Event 默认端口当前如何确定
- HTTP 设备信息接口是否暴露 `event_port`
- mDNS TXT Record 是否暴露 `event_port`
- 设计目标与当前代码事实之间的差距
- 已确认事实与仍待验证项

本规格以当前代码为准，不以设计文档中的后续建议为准。

## 2. 当前状态

- 状态：已完成一轮代码校准
- 当前 TCP Event 默认端口：`5000`
- 当前端口来源：`kDefaultTcpEventPort`
- 当前对外暴露状态：未见通过 HTTP 或 mDNS 稳定暴露
- 当前问题性质：不是 event server 缺失，而是发现/公布链路缺口

## 3. 当前权威代码入口

- `src/service/event/TcpEventService.h`
- `src/service/event/TcpEventService.cpp`
- `src/app/main_app.cpp`
- `src/service/discovery/MdnsTxtRecord.h`
- `src/service/discovery/MdnsTxtRecord.cpp`
- `src/service/http_server/http_api_v1.cpp`

历史参考但不作为唯一依据：
- `doc/design/tcp_event_design.md`
- `doc/spec/mdns-device-discovery-spec.md`
- `doc/solution/20260313-t32-mdns-integration-plan.md`

## 4. 当前端口来源

### 4.1 TCP Event 默认端口已固定在代码
`TcpEventService.h` 当前定义：
- `constexpr uint16_t kDefaultTcpEventPort = 5000;`

`main_app.cpp` 当前在 `CMD_MOBILE` 中直接调用：
- `TcpEventService::start(service::kDefaultTcpEventPort)`

因此当前事实是：
- event server 端口已确定
- 且当前是写死默认值，不是动态发现值

### 4.2 当前未见独立配置键
和 `CtrlPort` / `RtspPort` 不同，当前代码未显示：
- 独立的 `INI_KEY_EVENT_PORT`
- 独立的 `[MDNS] EventPort`
- 独立的 HTTP 配置读取逻辑

因此当前 event port 仍然是：
- 内部代码常量
- 而不是配置驱动接口能力

## 5. 当前 HTTP 暴露情况

### 5.1 `device/info` 未暴露 `event_port`
`build_device_info_json()` 当前只包含：
- `pid`
- `camera_ver`
- `camera_model`
- `camera_build`
- `mcu_ver`

没有：
- `event_port`

### 5.2 其他 V1 公开接口中也未见 `event_port`
从当前 `http_api_v1.cpp` 已校准部分看，未见：
- 设备信息补充 `event_port`
- system/storage/camera 通用元数据中补充 `event_port`

因此当前不能写成：
- HTTP 设备信息接口已把 event port 暴露给客户端

## 6. 当前 mDNS 暴露情况

### 6.1 当前 TXT payload 结构里没有 `event_port`
`MdnsTxtPayload` 当前字段只有：
- `deviceFamily`
- `model`
- `serialNumber`
- `firmwareVersion`
- `rtspPort`
- `ctrlPort`
- `macAddress`
- `status`

没有：
- `eventPort`

### 6.2 当前 TXT 生成逻辑也没有 `event_port`
`MdnsTxtRecord::build()` 当前只生成：
- `device_family`
- `model`
- `sn`
- `fw_ver`
- `rtsp_port`
- `ctrl_port`
- `mac`
- `status`

没有：
- `event_port`

### 6.3 `buildMdnsParams()` 也未传 event port
`main_app.cpp` 中 `buildMdnsParams(...)` 当前只接收：
- `ctrl_port`
- `rtsp_port`

并只把这两者写进 TXT payload。

因此当前不能写成：
- mDNS TXT Record 已经对外公布 event port

## 7. 当前真实问题定义

这里的关键不是“有没有 event server”，而是：
- event server 已有
- 但 discovery / metadata 暴露链路还缺

因此当前问题应定义为：
- event_port exposure gap
而不是：
- tcp event feature missing

这两者差别很大。

## 8. 与历史设计文档相比的校准

### 8.1 设计目标是明确存在的
`doc/design/tcp_event_design.md` 已明确建议：
- 在 HTTP 设备信息接口中补充 `event_port`
- 在 mDNS TXT Record 中补充 `event_port`

因此这不是凭空新想法，而是旧设计中已经识别出的后续步骤。

### 8.2 但当前代码尚未落地
当前代码校准结果表明：
- 这些建议还没有进入现有 HTTP / mDNS 实现

所以当前必须把它写成：
- 设计建议/待补项
而不是：
- 当前已完成能力

## 9. 当前明确结论

### 9.1 可以明确写成事实的
- TCP Event 当前默认端口是 `5000`
- 当前端口由 `kDefaultTcpEventPort` 决定
- `CMD_MOBILE` 中会直接启动该端口上的 event server
- 当前 HTTP `device/info` 未暴露 `event_port`
- 当前 mDNS TXT Record 未暴露 `event_port`
- event_port 暴露缺口已在历史设计文档中被明确识别

### 9.2 不能写得过满的
- 客户端当前可通过 HTTP 自动发现 event port
- 客户端当前可通过 mDNS 自动发现 event port
- event 通道的对外发现链路已经闭环

这些说法都不成立。

## 10. 仍待验证项

- APP 侧当前是否硬编码 `5000` 或通过其他私有约定拿端口
- 后续更合理的暴露位置应优先选 HTTP、mDNS，还是两者都补
- 如果 event port 将来可配置，配置源应放哪一层

## 11. 推荐后续拆分

后续如果继续治理，建议补：
- `decisions/event-port-http-vs-mdns-exposure-strategy.md`
- `refs/event-port-code-entry-and-missing-fields.md`
- `playbooks/event-port-discovery-gap-probe.md`
- `bugs/event-port-not-exposed-to-clients.md`

当前这篇规格先承担“缺口被精确定义”的权威入口，避免继续把 event server 已存在误写成 discovery 链路也已闭环。