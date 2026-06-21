# 工作模式选择与切换行为规格

## 1. 目的

定义 `t32_cam` 当前工作模式 (Work Mode) 相关行为的仓库级权威描述，覆盖：
- 启动时工作模式是如何被选择的
- `WorkMode` 模块当前真实职责
- `CMD_MOBILE` 与硬件工作模式之间的关系
- HTTP `POST /api/v1/system/workmode` 的当前语义
- 已确认事实与仍待验证项

本规格以当前代码为准，不以历史 reference 或产品设想中的目标闭环为准。

## 2. 当前状态

- 状态：已完成一轮代码校准
- 当前启动模式选择：主要来自 `WorkMode::getWorkingMode()`
- 当前远程模式切换接口：`POST /api/v1/system/workmode`
- 当前 HTTP 接口语义：接受请求，不等于完成真实模式切换
- 当前 `CMD_MOBILE`：是主程序的一种命令路径，不等于 `WorkMode` 枚举中的某个模式值

## 3. 当前权威代码入口

- `src/app/workmode/WorkMode.h`
- `src/app/workmode/WorkMode.cpp`
- `src/app/workmode/CMakeLists.txt`
- `src/app/main_app.cpp`
- `src/app/media_app.cpp`
- `src/service/http_server/http_api_v1.cpp`

历史参考但不作为唯一依据：
- `doc/reference/20260324-http-api-reference.md`
- `doc/reference/20260324-http-api-client-quick-reference.md`
- `doc/job/directory_reorganization_plan.md`

## 4. 当前模块分层

### 4.1 `WorkMode` 已是 app 层模块
当前目录结构已表明：
- `WorkMode` 在 `src/app/workmode/`
- 由 `src/app/CMakeLists.txt` 通过 `add_subdirectory(workmode)` 接入
- 构建目标为 `app_workmode`

因此当前 `WorkMode` 的正确归属是：
- app 层启动模式选择模块

而不是：
- HTTP service 层
- 通用系统状态管理中心
- 运行时远程切换控制器

### 4.2 当前模块能力非常有限
`WorkMode` 当前公开能力只有：
- `getWorkingMode()`
- 非 MCU 场景下的 `setWorkingModePins(...)`

没有看到：
- `setWorkingMode(...)`
- 运行时切换接口
- 持久化模式写回
- 事件广播
- 与 HTTP 接口直接接线

因此不要把它写成“完整工作模式管理器”。
那是错误描述。

## 5. 当前工作模式枚举

`WorkMode.h` 当前定义：
- `WORKING_MODE_SNAP_ONLY = 0`
- `WORKING_MODE_SNAP_UPLOAD = 1`
- `WORKING_MODE_UPLOAD_ONLY = 2`
- `WORKING_MODE_TEST_ONLY = 3`
- `WORKING_MODE_UVC = 4`
- `WORKING_MODE_MAX`

这说明当前代码里的工作模式语义，核心仍然是：
- 抓拍
- 上传
- 测试
- UVC/RTSP

不是一个专门围绕 mobile pairing 流程设计的 mode state machine。

## 6. 当前启动时如何决定模式

### 6.1 非 MCU 场景
`WorkMode::getWorkingMode()` 当前会：
- 读取两根 GPIO 模式脚 `mode_pin_0` / `mode_pin_1`
- 根据高低电平组合决定模式

映射关系当前是：
- `LOW, LOW` -> `WORKING_MODE_SNAP_ONLY`
- `LOW, HIGH` -> `WORKING_MODE_SNAP_UPLOAD`
- `HIGH, LOW` -> `WORKING_MODE_UPLOAD_ONLY`
- 其他 -> `WORKING_MODE_TEST_ONLY`

### 6.2 MCU 场景
`WorkMode::getWorkingMode()` 当前会调用：
- `MCU::getInstance()->readWorkingMode()`

映射关系：
- `-1` -> `WORKING_MODE_SNAP_UPLOAD`
- `0` -> `WORKING_MODE_SNAP_ONLY`
- `1` -> `WORKING_MODE_SNAP_UPLOAD`
- `2` -> `WORKING_MODE_UPLOAD_ONLY`
- `3` -> `WORKING_MODE_TEST_ONLY`
- 其他 -> `WORKING_MODE_MAX`

### 6.3 当前结果会被缓存
`WorkMode` 当前使用：
- `working_mode`
- `already_get_mode`

因此：
- 第一次读取后会缓存
- 当前没有看到运行时重新探测或刷新机制

这意味着当前它更像“启动期模式判定”，而不是持续可变的运行时模式源。

## 7. `main_app.cpp` 中的当前模式编排

### 7.1 启动命令与工作模式是两套概念
`main_app.cpp` 当前同时存在：
- 命令位标志：`CMD_SNAP` / `CMD_UPLOAD` / `CMD_MOBILE` / `CMD_RTSP_SERVER` 等
- `workingMode` 枚举：`WORKING_MODE_SNAP_ONLY` / `WORKING_MODE_UVC` 等

这两者不是同一层概念。

### 7.2 默认启动时由工作模式映射到命令组合
在未通过 CLI 参数显式指定的路径里，`main_app.cpp` 会：
1. 获取 `working_mode`
2. 根据 `switch (working_mode)` 映射到具体 `command`

当前映射包括：
- `WORKING_MODE_SNAP_ONLY` -> `CMD_SNAP`
- `WORKING_MODE_UPLOAD_ONLY` -> `CMD_CONN_NET | CMD_DHCP | CMD_NTP | CMD_UPLOAD`
- `WORKING_MODE_TEST_ONLY` -> `CMD_MOBILE`
- `WORKING_MODE_SNAP_UPLOAD` -> `CMD_SNAP | CMD_CONN_NET | CMD_DHCP | CMD_NTP | CMD_UPLOAD`
- `WORKING_MODE_UVC` -> `CMD_CONN_NET | CMD_DHCP | CMD_RTSP_SERVER`

### 7.3 一个关键事实：`TEST_ONLY` 当前映射到 `CMD_MOBILE`
这点非常重要。
当前代码明确是：
- `WORKING_MODE_TEST_ONLY` -> `CMD_MOBILE`

因此当前 `CMD_MOBILE` 更像：
- 某条测试/移动接管命令路径

而不是一个与 `WorkMode` 枚举一一同构的“工作模式值”。

如果把 `CMD_MOBILE` 直接当成 `mode=...` 的目标模式值，那是概念混淆。

## 8. HTTP `/api/v1/system/workmode` 当前真实语义

### 8.1 当前 handler 行为
`http_api_v1.cpp` 当前 `api_v1_system_workmode()`：
- 校验路径必须精确匹配 `/api/v1/system/workmode`
- 只接受 `POST`
- 要求 JSON body 中存在整数 `mode`
- 记录日志 `Requested work mode switch: %d`
- 返回：
  - `mode`
  - `accepted=true`

### 8.2 当前没有真实切换接线
在当前 handler 中，没有看到：
- 调用 `WorkMode` 设置函数
- 更新 MCU
- 更新 GPIO
- 触发主流程切换
- 停止/重启 `CMD_MOBILE` 相关服务
- 持久化到配置

所以当前 `/api/v1/system/workmode` 的正确描述只能是：
- “模式切换请求已接收”接口
- 不是“工作模式已实际切换完成”接口

### 8.3 当前接口更像协议占位/接收确认
这和 `/api/v1/system/datetime` 的语义类似：
- 有请求解析
- 有参数检查
- 有 accepted response
- 但未证明后端闭环已存在

因此当前不能写成：
- 通过 HTTP 调这个接口就能真正切换设备工作模式

## 9. 当前与 mDNS / TCP Event / HTTP 主链路的关系

### 9.1 历史目标态
历史文档和整体方向隐含的目标态大致是：
- 手机发现设备
- 通过 HTTP 请求切换 workmode
- 从 `CMD_MOBILE` 退出，进入目标工作模式

### 9.2 当前代码事实
当前代码并不能证明这个闭环已经存在。
理由：
- `CMD_MOBILE` 的进入是主程序启动命令选择结果
- `WorkMode` 只负责启动期读模式
- `/api/v1/system/workmode` 没有看到真实模式切换接线
- 没有看到 mDNS / TCP Event / HTTP 因 workmode 改变而做生命周期切换

所以当前更准确的描述是：
- workmode 切换的协议入口已有雏形
- 但运行时闭环尚未形成代码事实

## 10. 当前明确结论

### 10.1 可以明确写成事实的
- `WorkMode` 当前位于 `src/app/workmode/`
- `WorkMode` 当前职责是启动期模式判定
- 非 MCU 下主要通过 GPIO 读取模式
- MCU 下通过 `readWorkingMode()` 读取模式
- `WORKING_MODE_TEST_ONLY` 当前会映射到 `CMD_MOBILE`
- HTTP `/api/v1/system/workmode` 当前会校验并返回 `accepted=true`
- 当前未见 HTTP handler 与真实模式切换执行链接线

### 10.2 不能写得过满的
- `/api/v1/system/workmode` 已完成真实模式切换
- workmode 变化会自动联动 mDNS / TCP event / RTSP / HTTP 生命周期
- `WorkMode` 已是统一运行时模式中心
- 当前 mobile 配对完成后一定会切到目标工作模式

这些说法都会过度承诺。

## 11. 与历史参考相比的关键校准

### 11.1 已落地的
- app 层 `WorkMode` 模块重组
- HTTP workmode 请求接口存在
- 主程序已把启动期工作模式映射到命令组合

### 11.2 仍停留在目标态、未见充分代码证明的
- 远程切换工作模式闭环
- 与 `CMD_MOBILE` 生命周期的真实联动
- 远程切换后的持久化、广播、事件通知

## 12. 仍待验证项

- `media_app.cpp` 中工作模式选择与主程序是否完全一致
- 真机 MCU 写回工作模式的能力是否已有别处实现
- `/api/v1/system/workmode` 后续是否计划驱动进程级重编排
- mobile pairing 完成后的目标模式迁移是否有独立实现分支

## 13. 推荐后续拆分

后续如果继续治理，建议补：
- `decisions/workmode-vs-cmd-mobile-layering.md`
- `refs/workmode-code-entry-and-mode-mapping.md`
- `playbooks/workmode-startup-and-http-probe.md`
- `bugs/workmode-http-accepted-but-no-runtime-switch.md`

其中 `decisions/workmode-vs-cmd-mobile-layering.md` 与
`decisions/workmode-usermode-process-split.md` 已落地。本规格先承担当前行为
权威入口，避免继续把产品意图与代码现状混写。

## 14. 进程归属（workmode / usermode 拆分）—— 目标态决策

> 本节是**决策目标态**，不是当前代码事实。迁移在 C4 repoint 前执行（详见
> `decisions/workmode-usermode-process-split.md`）。在代码真正迁出前，本规格
> 其余章节仍以“`-wm 0..4` 全部由 workmode 处理”为当前事实。

### 14.1 当前代码事实（未变）
- `-wm 0..4` 当前全部由 `htc_workmode_app`（及 `main_app -wm`）处理。
- `-wm 3` → `CMD_MOBILE`，`-wm 4` → `CMD_RTSP_SERVER`（见 §7.2 映射）。

### 14.2 决策目标态
按**运行语义**拆进程：
- **workmode（一次性任务，做完即退）**：`-wm 0` / `-wm 1` / `-wm 2`
- **usermode（长驻交互服务，等连接 / 信号）**：`-wm 3`（CMD_MOBILE）/ `-wm 4`（CMD_RTSP_SERVER）

判定依据是 `runCommands` 内是否存在 `while(keepRunning())` 长驻循环，而非
“是否与 `-m` 重叠”。`-m`（`main_app.cpp:219`）与 `-wm 3` 同走 `CMD_MOBILE`，
`-rs`（`main_app.cpp:235`）与 `-wm 4` 同走 `CMD_RTSP_SERVER`，是对称重叠。

### 14.3 迁移后本规格需同步更新项
代码迁出后，§5（枚举语义）与 §7.2（workMode→command 映射）中
`WORKING_MODE_TEST_ONLY` 与 `WORKING_MODE_UVC` 两项需补充“由 usermode 处理”
的归属说明。