# WorkMode / UserMode 进程拆分决策

## 1. 决策主题

明确 `htc_workmode_app` 与未来的 `htc_usermode_app` 的职责边界：按**运行语义**
把启动命令路径分到两个进程。

- WorkMode 进程：一次性任务（拍照 / 录影 / 上传，做完即退）
- UserMode 进程：长驻交互服务（RTSP / HTTP / mDNS，等连接或信号才退）

## 2. 背景

从 `htc_main_app` 拆出 `htc_workmode_app`（T16 Phase C-3）后，`workmode_app` 当前
接管全部 `-wm 0..4`。但 `src/app/workmode_app.cpp` 顶部注释表明它仍是 **C3 UNSPAWNED**
—— `media_app` 仍 spawn `main_app -wm`，要等 **C4 / T17** 才 repoint 为 spawn
`workmode_app`。

也就是说 `workmode_app` 尚未真正上线。**现在是定义 `-wm` 归属成本最低的窗口**：
在 repoint 之前把边界定下来，比上线后再搬家省事得多。

## 3. 事实基础：CLI 入口与 CMD_* 的重叠

当前代码中，多条 CLI 入口命中 `runCommands` 的同一个分支：

| CLI 入口 | 当前所在进程 | → command 位图 | 运行语义 |
|---|---|---|---|
| `-m` / `--mobile` | `main_app` (`main_app.cpp:219`) | `CMD_MOBILE` | **长驻服务** |
| `-wm 3` TEST_ONLY | `workmode_app` (`WorkModeRunner.cpp:880`) | `CMD_MOBILE` (+RGB blink30) | **长驻服务** ← 与 `-m` 重叠 |
| `-rs` / `--rtsp-server` | `main_app` (`main_app.cpp:235`) | `CMD_RTSP_SERVER` | **长驻服务** |
| `-wm 4` UVC | `workmode_app` (`WorkModeRunner.cpp:887`) | `CMD_CONN_NET\|DHCP\|CMD_RTSP_SERVER` | **长驻服务** ← 与 `-rs` 重叠 |
| `-wm 0/1/2` | `workmode_app` | `CMD_SNAP` / +`CONN+DHCP+NTP+UPLOAD` | **一次性任务** |

关键事实：

- `-m` 与 `-wm 3` 命中 `runCommands` 的**同一个 `CMD_MOBILE` 分支**
  （`WorkModeRunner.cpp:577-697`），内部 `while (ctx.lc.keepRunning())` 等信号循环。
- `-rs` 与 `-wm 4` 共享 `CMD_RTSP_SERVER`（`WorkModeRunner.cpp:699`，`runRtspServerUntilSignal`）。
- 这是一个**对称关系**，不止 `-wm 3`：`-wm 4` 与 `-rs` 同样重叠。

`WorkModeRunner.cpp:580` 的注释也印证了这一点：
> Day/Night initialization for CMD_MOBILE (covers both `-m` and `-wm 3` paths)

## 4. 拆分标准：运行语义，而非"-m 重叠"

判定一个 `-wm` 归 workmode 还是 usermode 的标准是它的**运行语义**：

- **一次性任务**（`runCommands` 执行完返回，进程进入 shutdown tail 退出）→ workmode
- **长驻服务**（`runCommands` 内含 `while(keepRunning())` 等信号循环）→ usermode

按此标准：

- workmode：`-wm 0`（SNAP_ONLY）、`-wm 1`（SNAP_UPLOAD）、`-wm 2`（UPLOAD_ONLY）
- usermode：`-wm 3`（TEST_ONLY / CMD_MOBILE）、`-wm 4`（UVC / CMD_RTSP_SERVER）

决定性特征是 `runCommands` 内的长驻循环：`CMD_MOBILE`（`WorkModeRunner.cpp:680`）
与 `CMD_RTSP_SERVER`（`WorkModeRunner.cpp:702` 的 `runRtspServerUntilSignal`）都阻塞
等待信号 / 连接，而 `CMD_SNAP` / `CMD_UPLOAD` 等执行完即返回。

## 5. 决策结论

1. `htc_workmode_app` 只保留 `-wm 0/1/2`（一次性任务）。
2. 新建 `htc_usermode_app` 接管长驻交互服务，以 `-m` 语义为基底（保留子参数），
   并入 `-wm 3` / `-wm 4` 的等价路径。
3. `workModeToCommand`（`WorkModeRunner.cpp:862`）中 `WORKING_MODE_TEST_ONLY` /
   `WORKING_MODE_UVC` 两个 case 在 workmode 侧视为**越界**，迁出后由 usermode 处理。
4. 本决策只锁定 `-wm` 侧归属。当前 `-m` / `-rs` 仍在 `main_app`；它们是否一并
   迁入 `usermode`，留待 C4 编排时定（见 §7.4）。

## 6. 与现有 ADR 的关系（正交，不冲突）

`decisions/workmode-vs-cmd-mobile-layering.md` 建立的认知边界：

- `WorkMode` = 启动期模式判定（来源状态）
- `CMD_MOBILE` = 运行期执行命令（命令落点之一）

本决策是**另一个维度**：按运行语义拆"进程"。两者正交：

- 那份 ADR 回答"`WorkMode` 和 `CMD_MOBILE` 是不是一回事"（不是）。
- 本决策回答"哪条命令路径该放进哪个进程"（按是否长驻）。

本决策不修改那份 ADR 的任何结论。

## 7. 迁移兼容点（执行时不可丢失）

### 7.1 `-m` 子参数能力

`-m`（`main_app.cpp:221-232`）支持 `--no-rtsp` / `--no-audio` / `--force-day` /
`--record-stream1`。当前 `-wm 3` 不解析这些（`workmode_app.cpp:65` 的
`mobile_rtsp_enabled` 写死 `true`）。

usermode 若以 `-m` 为基底则天然保留这些能力；若以 `-wm 3` 为基底，**必须补回**
这四个子参数，否则相对 `-m` 退化。

### 7.2 RGB blink 副作用

- `-wm 3` 有 `rgbLed()->asyncBlink(30)`（`WorkModeRunner.cpp:878`）。
- `-wm 4` 无 blink。`-m` 无 blink。

迁移时需决定 blink 保留还是丢弃。建议：usermode 进入时 blink 作为"可连接"
视觉提示，与 workmode 一次性任务（无 blink）区分；落地前与产品确认。

### 7.3 CMake / link

长驻分支（`CMD_MOBILE` / `CMD_RTSP_SERVER`）当前仍在 `app_workmode::runCommands`
共享。两种实现路线：

- **A（轻）**：`usermode_app` 同样 link `app_workmode`，复用 `runCommands`，只是
  入口只接受 `-m` / `-wm 3` / `-wm 4`。
- **B（彻底）**：把 `CMD_MOBILE` / `CMD_RTSP_SERVER` 两个分支从 `runCommands`
  抽到独立 TU（如 `app_usermode`），workmode 不再 link。

路线 B 边界更干净但改动更大；建议先走 A（与 workmode_app 的 thin-shell 风格一致），
待稳定后再评估 B。需新增 `htc_usermode_app` target，link 闭包参考
`workmode_app`（`app_workmode` + `app_lifecycle` + `…`，见 `src/app/CMakeLists.txt:452`）。

### 7.4 时机

在 **C4 / T17 repoint 之前**完成归属拆分（repoint = `media_app` 改为 spawn
`workmode_app` 而非 `main_app -wm`）。repoint 后 `workmode_app` 正式承载 `-wm`，
再搬家会牵动 spawn 方与运行态，成本上升。

### 7.5 执行机制：Phase-1 模块稳定化

本 ADR 锁定的是 `-wm` 归属（目标态），未规定**如何把功能从 `main_app`/`runCommands`
搬进 wm/um**。该执行机制由 `specs/phase1-module-stabilization-plan.md`（2026-06-21 grill
定型）承担，要点：

- **抽取目标 = Phase-2 组成单元**：Phase-1 把每个功能从 `app_workmode::runCommands`
  瀑布里抽成干净 unit，wm/um 直接 compose 这些 unit（REPLACE 语义，非复制）；
  `runCommands` 瀑布在抽取过程中**逐步退役**。
- **wm/um/共享 三分**：wm={snap,record,upload,ntp}、um={rtsp,http控制+回放,mdns}、
  共享库={mcu,thumbnail/DB,manifest}、前置=network(`net`)。
- **先稳后组**：crash-prone(record/snap)用 loop-faithful 真机二进制 + 退役 kill-switch
  稳定后，才进 Phase-2 组合 + C4 repoint。
- 与本 ADR §7.1–7.4 的兼容点（`-m` 子参数、RGB blink、CMake link 路线 A/B、repoint 时机）
  在 Phase-2 组合时落地，本计划不修改这些结论。

## 8. 何时重新评估

- `-wm 3` / `-wm 4` 语义变化（如 TEST_ONLY 不再长驻）。
- 引入新的长驻 `-wm` 模式。
- mobile pairing 运行时切换闭环落地（见 `workmode-vs-cmd-mobile-layering.md §8`），
  届时 workmode / usermode 的进程边界可能随运行时重编排再调。

## 9. 相关文档

- `decisions/workmode-vs-cmd-mobile-layering.md` — WorkMode 与 CMD_MOBILE 概念分层（正交）
- `specs/workmode-selection-and-switching.md` — 工作模式选择与切换行为规格（§14 进程归属）
- `src/app/workmode_app.cpp` — workmode_app thin-shell（顶部注释记录 C3/C4/repoint）
- `src/app/workmode/WorkModeRunner.cpp` — `workModeToCommand` + `runCommands`（长驻分支所在）
