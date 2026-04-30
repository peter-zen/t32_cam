# WorkMode 与 CMD_MOBILE 分层关系决策

## 1. 决策主题

明确 `t32_cam` 当前为什么必须把下面两个概念分开：
- `WorkMode`：启动期工作模式来源
- `CMD_MOBILE`：主程序命令路径之一

如果把这两者混为一谈，后续关于：
- HTTP `/api/v1/system/workmode`
- mDNS / HTTP / RTSP 生命周期
- 手机配对完成后的模式切换

都会被错误理解。

## 2. 当前事实基础

当前代码中：
- `WorkMode` 定义在 `src/app/workmode/`
- `WorkMode::getWorkingMode()` 通过 GPIO 或 MCU 读取一个枚举值
- `main_app.cpp` 会把这个枚举值映射成一组命令位
- `CMD_MOBILE` 是命令位之一，不是 `WorkMode` 枚举项

这已经足够说明：
- 两者不是一个层次的东西

## 3. 为什么不能混同

### 3.1 `WorkMode` 是来源状态
`WorkMode` 解决的问题是：
- 设备启动时当前应处于什么工作语义

它更像启动配置/硬件选择结果。

### 3.2 `CMD_MOBILE` 是执行路径
`CMD_MOBILE` 解决的问题是：
- 主程序当前要走哪条运行分支
- 是否启动 mDNS / HTTP / TCP event / RTSP 这一组服务

它更像运行编排命令。

### 3.3 当前映射不是一一对应
最明显的代码事实是：
- `WORKING_MODE_TEST_ONLY` -> `CMD_MOBILE`

这已经直接说明：
- `CMD_MOBILE` 不是某个“mobile mode”枚举值本身
- 而是 `WorkMode` 经过启动编排后的命令落点之一

## 4. 为什么 HTTP workmode 现在不能宣称完成闭环

当前 `/api/v1/system/workmode` 只是：
- 解析 `mode`
- 打日志
- 返回 `accepted=true`

但没有代码证明：
- 它会修改 `WorkMode`
- 它会驱动 `CMD_MOBILE` 退出
- 它会触发主流程切换到别的命令组合

所以如果把 `WorkMode` 和 `CMD_MOBILE` 混为一谈，就很容易错误得出：
- “HTTP workmode 已经可以真正切模式”

这是不成立的。

## 5. 当前分层的收益

### 5.1 避免把启动期逻辑误当成运行期状态机
`WorkMode` 当前明显是启动期判定模型。
如果误把它当运行期状态中心，会强行赋予它并不存在的职责。

### 5.2 避免把 CLI/进程命令路径误当成产品模式定义
`CMD_MOBILE`、`CMD_RTSP_SERVER`、`CMD_UPLOAD` 这些本质是进程运行编排工具，不全是产品概念模式。

### 5.3 有助于后续补真闭环
未来如果要补真实 workmode 切换，正确做法应是：
- 先明确运行时状态中心放哪
- 再明确如何从 HTTP 请求驱动流程切换
- 再定义与 mDNS / RTSP / HTTP / TCP event 的联动

而不是继续把旧概念混着写。

## 6. 当前模型的不足

### 6.1 用户视角与代码视角不一致
对产品/APP 来说，用户会觉得“切模式”是一个统一动作。
但当前代码里：
- 启动期模式
- 命令位
- HTTP 协议入口

其实是分裂的。

### 6.2 缺少运行时写路径
当前看到读模式路径，却没看到成体系的写模式路径。
这正是 `/api/v1/system/workmode` 只能停在 accepted 语义的核心原因。

## 7. 决策结论

当前仓库应继续坚持以下认知边界：
1. `WorkMode` 是启动期模式判定模块
2. `CMD_MOBILE` 是主程序运行命令路径之一
3. 两者相关，但不是同一概念
4. 在真正运行时切换链路补齐前，不应把 HTTP workmode 接口写成已完成真实切换

## 8. 何时需要重新评估

出现以下需求时，应重新评估当前分层：
- 设备在运行中必须支持模式热切换
- APP 需要在配对后立即切换设备角色
- mDNS / RTSP / HTTP / TCP event 需要根据模式变化动态重编排
- 模式状态需要被持久化、查询、广播

在这些需求落地前，继续明确区分 `WorkMode` 和 `CMD_MOBILE` 是必要的。