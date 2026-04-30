# mDNS 设备发现行为规格

## 1. 目的

定义 `t32_cam` 当前仓库中 mDNS / DNS-SD (Multicast DNS / DNS Service Discovery) 设备发现能力的代码校准结论，覆盖：
- 服务发布的当前入口与生命周期
- `CMD_MOBILE` 下 mDNS / HTTP / RTSP 的接线关系
- `MdnsService` 与 `MdnsTxtRecord` 的职责划分
- 配置、默认值、归一化逻辑与当前限制
- 已确认事实与仍待验证项

这篇规格以当前代码为准，不以历史方案稿的目标表述为准。

## 2. 当前状态

- 状态：已完成一轮代码与历史文档交叉校准
- 当前主入口：`CMD_MOBILE`
- 当前实现形态：进程内服务注册，不是独立 daemon
- 当前三方库：`third_party/tinysvcmdns`
- 当前自有封装：`src/service/discovery/`

## 3. 当前权威代码入口

- `src/app/main_app.cpp`
- `src/service/discovery/MdnsService.h`
- `src/service/discovery/MdnsService.cpp`
- `src/service/discovery/MdnsTxtRecord.h`
- `src/service/discovery/MdnsTxtRecord.cpp`
- `src/service/discovery/CMakeLists.txt`
- `third_party/tinysvcmdns/CMakeLists.txt`
- `tests/test_mdns_txt_record.cpp`
- `tests/test_mdns_model_normalization.cpp`

历史参考但不作为唯一依据：
- `doc/spec/mdns-device-discovery-spec.md`
- `doc/solution/20260313-t32-mdns-integration-plan.md`
- `doc/roadmap/20260313-t32-mdns-integration-roadmap.md`
- `doc/analysis/20260313-t32-mdns-simu-verification.md`

## 4. 当前代码结构与分层

### 4.1 第三方与自有封装已分离
当前仓库已按较合理方式落地：
- 第三方库：`third_party/tinysvcmdns/`
- 自有封装：`src/service/discovery/`

而不是把 mDNS 逻辑散落到 `main_app.cpp` 或 `src/network/`。

### 4.2 discovery 模块已接入构建
当前已确认：
- `third_party/CMakeLists.txt` 已 `add_subdirectory(tinysvcmdns)`
- `src/service/CMakeLists.txt` 已 `add_subdirectory(discovery)`
- `src/service/discovery/CMakeLists.txt` 已生成 `discovery_service`
- `discovery_service` 链接 `easylogger` 与 `tinysvcmdns`

因此 mDNS 已不是方案稿里的“待接入设计”，而是已进主构建链路。

### 4.3 当前不是独立进程模型
当前代码中没有单独的 mDNS daemon 生命周期编排。
`MdnsService` 由 `main_app.cpp` 直接调用，属于 in-process service。

因此任何把当前实现写成“独立后台进程”或“独立系统服务”的说法，都是错的。

## 5. 当前启动入口与生命周期

### 5.1 当前主入口是 `CMD_MOBILE`
`main_app.cpp` 当前在 `command & CMD_MOBILE` 路径里：
1. 读取 HTTP/RTSP 端口配置
2. 真机下执行 Wi‑Fi / DHCP 前置
3. 获取网卡名与 IPv4
4. 若启用 mDNS，则调用 `MdnsService::start(...)`
5. 启动 HTTP Server
6. 启动 TCP Event Server
7. 启动 RTSP Server
8. 退出流程中停止 mDNS / RTSP / HTTP / TCP Event

因此当前正确描述是：
- mDNS 是 `CMD_MOBILE` 工作流的一部分
- 与 HTTP / RTSP 并列被主程序编排

### 5.2 Simu 与真机入口策略不同
当前代码明确区分：
- 真机：`CMD_MOBILE` 下需要 Wi‑Fi 与 DHCP 前置
- Simu：`#ifdef BUILD_FOR_SIMULATION` 下跳过真实 Wi‑Fi / DHCP，改为检测可用本机网卡并取 IP

因此历史方案里“Simu 也走 CMD_MOBILE，但跳过真实 WiFi/DHCP”这一点，当前已经落地，不再只是建议。

### 5.3 mDNS 失败会阻断 `CMD_MOBILE`
当前 `main_app.cpp` 中如果：
- 已启用 mDNS
- `MdnsService::start(...)` 返回失败

则直接记录错误并 `goto main_exit`。

这意味着当前语义是：
- 在启用 mDNS 的情况下，mDNS 启动是 `CMD_MOBILE` 主链路前置条件之一
- 不是“可有可无的旁路增强”

## 6. 参数来源与默认值

### 6.1 端口来自 MDNS 配置段
当前 `CMD_MOBILE` 中：
- `CtrlPort` 用于 HTTP server 端口
- `RtspPort` 用于 RTSP server 端口

两者都通过 `getConfiguredPort(config, INI_SECTION_MDNS, ...)` 读取。

这意味着当前 `[MDNS]` 配置段不只是 discovery 自身配置，还承担 HTTP/RTSP 对外暴露端口的配置来源。

### 6.2 mDNS 开关
当前通过：
- `isMdnsEnabled(config)`
- 实现：`INI_KEY_MDNS_ENABLE` 非 0 即启用

所以当前 mDNS 不是绝对强制，而是“默认启用、可配置关闭”。

### 6.3 服务类型默认值
`MdnsServiceParams` 中默认：
- `serviceType = "_t32cam._tcp"`

`MdnsService::normalizeParams()` 会进一步归一化：
- 若为空，回退到 `_t32cam._tcp.local`
- 若未带 `.local`，自动补 `.local`

因此当前实际注册的服务类型最终应视为：
- `_t32cam._tcp.local`

### 6.4 实例名与主机名默认逻辑
当前 `main_app.cpp` 会构造：
- `instanceName`
- `hostName`

而 `MdnsService` 内部还会再做归一化：
- 去空白/去包裹引号
- 主机名中的空格替换为 `-`
- 非字母数字/`-`/`.` 的字符替换为 `-`
- 自动转小写
- 若未带 `.local`，自动补 `.local`

因此 host name 当前不是裸透传配置值，而是有 sanitization 的。

## 7. TXT Record 当前模型

### 7.1 由 `MdnsTxtRecord` 统一生成
当前 TXT record 不是在 `main_app.cpp` 里临时拼装，而是通过：
- `MdnsTxtPayload`
- `MdnsTxtRecord::build()`

统一生成。

### 7.2 当前字段集合
根据 `MdnsTxtRecord.cpp` 与测试，当前字段包括：
- `device_family`
- `model`
- `sn`
- `fw_ver`
- `rtsp_port`
- `ctrl_port`
- `mac`
- `status`

### 7.3 当前默认值
`MdnsTxtPayload` 默认：
- `deviceFamily = "ckvison_t32cam"`
- `rtspPort = 554`
- `ctrlPort = 80`
- `status = "ready"`

注意：
- 这里的默认值并不等于运行时最终一定如此
- `main_app.cpp` 会把实际端口传入覆盖默认值

### 7.4 model 归一化
`normalizeMdnsModelValue()` 当前规则：
- 空值 -> 回退 `T32`
- `CXXX`（含去引号后的形式）-> 判为无效并回退 `T32`
- 其他值 -> 原样保留

并且该行为已有独立测试：
- `tests/test_mdns_model_normalization.cpp`

因此当前不能把 model 字段写成“完全来源于配置且不做修正”。

## 8. `MdnsService` 当前行为语义

### 8.1 start/refresh 语义
当前：
- `start(params)` 会先 `stopLocked()` 再 `startLocked(params)`
- `refresh(params)` 直接调用 `start(params)`

这意味着当前 refresh 不是增量更新，而是“停止后重启注册”。

### 8.2 status 更新语义
`updateStatus(status)` 当前：
- 如果服务未运行，仅更新缓存参数
- 如果服务运行中，则复制当前参数、改 `txt.status`、停止并重新启动

所以当前 status 更新也不是在线原位修改，而是重注册模型。

### 8.3 IP 校验
`startLocked()` 会检查：
- `ipAddress` 必须是有效 IPv4

若无效则直接失败。

因此当前实现不是“允许先空 IP 启动，再异步补地址”。

### 8.4 注册对象生命周期
当前通过 `mdnsd_register_svc(...)` 注册后：
- 若返回空，视为失败
- 若成功，立即 `mdns_service_destroy(service)`

这表明当前使用方式依赖 tinysvcmdns 的注册副作用模型，而不是持有一个长期 service handle 在上层反复操作。

## 9. 与历史方案相比，当前哪些已经落地

### 9.1 已落地的
- `third_party/tinysvcmdns/` 三方库落仓并接入构建
- `src/service/discovery/` 自有封装模块已建立
- `CMD_MOBILE` 生命周期已接入 mDNS 启停
- Simu 下已跳过真实 Wi‑Fi / DHCP
- TXT Record 生成与 model 归一化已有测试

### 9.2 不应继续写成“已完全产品化”的
- 真机手机端发现结果已经全面验证
- PTR / SRV / TXT / A 记录都已由外部 Bonjour 工具完成实测校准
- IP 变化、Wi‑Fi 断开、状态流转等动态重注册场景已经充分验证
- `/api/system/workmode` 已完成与 mDNS 生命周期联动

这些说法当前都写过头了。

## 10. 当前明确结论

### 10.1 可以明确写成事实的
- 当前 mDNS 发布能力已经接入仓库主构建
- 当前使用 `tinysvcmdns` + `MdnsService` + `MdnsTxtRecord` 的分层
- 当前主入口是 `CMD_MOBILE`
- 启用 mDNS 时，启动失败会阻断 `CMD_MOBILE`
- Simu 模式已跳过真实 Wi‑Fi / DHCP
- TXT record 当前包含 8 个核心字段
- model 字段存在显式归一化逻辑与测试覆盖

### 10.2 不能写得过满的
- 当前已完成跨平台手机端外部发现闭环验证
- 当前 discovery 生命周期已与所有工作模式统一抽象
- 当前 status 更新是轻量在线修改而非重启注册
- 当前所有网络变化场景都已自动重注册

## 11. 仍待验证项

- 真机局域网下 Android / iOS / Bonjour 工具的外部发现实测
- PTR / SRV / TXT / A 记录是否与期望规格完全一致
- Wi‑Fi 断开 / IP 变化时的自动恢复策略
- `CMD_RTSP_SERVER` 是否需要复用 mDNS 发布能力
- `status=streaming` 等动态状态切换是否真的被业务路径调用

## 12. 推荐后续拆分

后续如果继续治理，建议补：
- `decisions/mdns-cmd-mobile-lifecycle-model.md`
- `refs/mdns-code-entry-and-config-keys.md`
- `playbooks/mdns-simu-and-bonjour-verification.md`
- `bugs/mdns-network-change-and-reregistration-gaps.md`

当前这篇规格先承担当前行为权威入口，避免继续把 roadmap / solution / analysis 混在一起使用。