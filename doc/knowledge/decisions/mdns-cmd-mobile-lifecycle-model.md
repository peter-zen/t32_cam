# mDNS 与 CMD_MOBILE 生命周期模型决策

## 1. 决策主题

明确 `t32_cam` 当前为什么把 mDNS 设备发现能力绑定在 `CMD_MOBILE` 生命周期中，而不是：
- 做成独立 daemon
- 塞进 `src/network/`
- 挂到 `src/media/rtsp/`
- 做成和工作模式完全解耦的全局常驻服务

## 2. 当前事实基础

当前代码已经形成如下结构：
- `third_party/tinysvcmdns/`：第三方 mDNS 库
- `src/service/discovery/`：自有封装层
- `src/app/main_app.cpp`：在 `CMD_MOBILE` 中负责 mDNS / HTTP / RTSP 编排

这不是纸面设计，而是已经落地的代码事实。

## 3. 为什么不是独立 daemon

把 mDNS 做成独立 daemon，理论上可行，但当前项目上不合适。

原因：
1. `main_app.cpp` 已经掌握网络接口、IP、HTTP 端口、RTSP 端口与退出时机
2. 如果拆 daemon，会额外引入：
   - 进程间状态同步
   - 端口配置同步
   - 生命周期竞争
   - 退出清理复杂度
3. 当前项目规模不值得为了 mDNS 单独引入第二条服务编排链

所以当前采用 in-process service 是更合理的工程选择。

## 4. 为什么不放 `src/network/`

把 mDNS 放进 `src/network/` 是错误分类。

原因：
- `src/network/` 更偏主动连接型客户端能力
- mDNS 的职责是“对局域网发布本机服务”
- 它天然更接近 service registration / discovery，而不是 outbound client transport

如果把它塞进 `src/network/`，后续 discovery、control client、stream transport 会混成一团。

## 5. 为什么不放进 RTSP 模块

把 mDNS 挂到 `src/media/rtsp/` 也不对。

原因：
- 当前 mDNS 不是只发布 RTSP
- TXT Record 里同时暴露 `ctrl_port` 与设备元数据
- `CMD_MOBILE` 中实际同时启动的是：
  - mDNS
  - HTTP server
  - TCP event server
  - RTSP server

因此 mDNS 是跨 HTTP + RTSP 的设备发现层，不应降格为 RTSP 附属物。

## 6. 为什么绑定 `CMD_MOBILE`

当前绑定 `CMD_MOBILE` 是合理的，因为这个模式在产品语义上就是：
- 手机发现设备
- 建立控制连接
- 开始预览/接管

而当前业务链路中，完成这一流程需要的最小能力集合就是：
- mDNS：发现入口
- HTTP：控制入口
- RTSP：媒体入口

把三者放在同一个生命周期里，能显著减少中间不一致状态。

## 7. 当前模型的收益

### 7.1 生命周期清晰
进入 `CMD_MOBILE`：
- 网络就绪
- 启动 mDNS
- 启动 HTTP
- 启动 RTSP

退出 `CMD_MOBILE`：
- 停止 mDNS
- 停止 RTSP
- 停止 HTTP

这个顺序对当前项目是足够清晰且可维护的。

### 7.2 Simu 与真机共享主路径
当前代码已做到：
- 真机：执行 Wi‑Fi / DHCP 前置
- Simu：跳过真实网络动作，但仍走 `CMD_MOBILE` 主路径

这比另外造一条“只给 simu 用的 discovery 入口”更合理。

### 7.3 避免 feature flag 漂移
如果把 mDNS 做成一个运行期任意切换的小开关，会出现很多中间态：
- HTTP/RTSP 还在，但 mDNS 被停了
- mDNS 还在，但 control/stream 已停
- 设备正在 mobile 模式，但 APP 已无法发现

当前把 mDNS 绑定到模式生命周期，能减少这类状态组合爆炸。

## 8. 当前模型的代价

也必须明确，这个模型不是没有代价。

### 8.1 灵活性较低
如果将来要求：
- 在非 `CMD_MOBILE` 下也能被发现
- 只开放 RTSP 不开放 HTTP
- discovery 生命周期独立于工作模式

那么当前模型就会显得偏硬。

### 8.2 状态更新较粗糙
当前 `refresh()` / `updateStatus()` 都采用 stop + restart 注册模型。
这意味着：
- 语义正确但不够优雅
- 若未来状态更新更频繁，可能需要更细粒度方案

### 8.3 与 `/api/system/workmode` 的产品闭环仍未完全打通
历史方案里希望“添加成功后退出 mobile 模式”，但当前代码层并不能证明这一闭环已经彻底打通。

因此不能把这个未来目标写成当前事实。

## 9. 决策结论

当前仓库应继续坚持以下模型：
1. mDNS 作为 `src/service/discovery/` 中的 in-process service
2. 生命周期默认绑定 `CMD_MOBILE`
3. 与 HTTP / RTSP 共同组成手机发现/接入主链路
4. Simu 与真机共用同一主编排，只在网络前置动作上分支

在没有明确新需求前，不建议：
- 改成独立 daemon
- 移入 `src/network/`
- 绑定到 RTSP 子模块
- 设计复杂的全局常驻 discovery 模型

## 10. 后续何时需要重新评估

出现以下需求时，应重新评估该决策：
- 非 `CMD_MOBILE` 也必须持续可被发现
- 需要多工作模式共享 mDNS 但端口/状态不同
- 需要对 IP 漂移、网络切换做更细粒度自动重注册
- 手机端依赖 `status=streaming/updating` 等更频繁状态广播

在这些需求出现之前，继续保持当前模型更稳。