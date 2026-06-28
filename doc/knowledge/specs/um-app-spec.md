# um 程序规格（um-app-spec）

> 用途：新建独立 binary `um`（usermode）的行为规格——把已实现并验证的长驻交互服务（RTSP / HTTP 控制+回放 / mDNS / day-night / MCU）编排成一个稳定的「长驻服务」程序，与 `wm`（一次性任务）对应。
> 来源：2026-06-28 grill 会话定型（决策见 §10 决策日志）。**真相源 = 本文 + 代码**；本文为 um 的权威 spec。
>
> **治理规则（重要）**：后续若修改 um 功能，**必须同步更新本 spec**；若实现与 spec 冲突，**先询问是否更改 spec**，不要默默改实现。关联 [`wm-app-spec.md`](wm-app-spec.md)（姊妹篇，一次性任务侧）、[`scenario-test-manifest.md`](scenario-test-manifest.md)（模块验证状态真相源）、[`phase1-module-stabilization-plan.md`](phase1-module-stabilization-plan.md)。

---

## 0. 背景与定位

- `wm`（一次性任务：snap/record/upload/ntp）已基本就绪，启动 **um**（长驻交互服务）规划。
- **关键差异 vs wm**：wm 要从零写 task（UploadTask/capture_lane/snap_task/record_task），因为生产捕获路径缠在一起且不写 DB/thumbnail。**um 侧单元早已是独立 linkable 库**——
  - `media_rtsp`（`RtspServer`，`src/media/rtsp/RtspServer.h`）
  - `http_server`（`src/service/http_server/`，控制端点 + 渐进式回放）
  - `discovery_service`（`MdnsService`，`src/service/discovery/MdnsService.h`）
  - `mcu_service`（`McuService`/`McuCache`）
  - `daynight`（`src/hardware/daynight/`，ISP 日夜切换）
- legacy 只在 `WorkModeRunner.cpp::runCommands` 里对这些库做 **wiring**：`CMD_MOBILE` 分支（`WorkModeRunner.cpp:580-697`，含 day/night init :583、`MdnsService::start` :635、`while(keepRunning())` :680）+ `CMD_RTSP_SERVER`（:702，`runRtspServerUntilSignal` :705）。
- 故 **um 的工作不是"拆模块"，而是"组合现成库 + 写 lifecycle"**——新写 `um_app` 编排器，读 `runCommands` CMD_MOBILE 作调用顺序参考，写自己的 bring-up/idle-wait/teardown；um 上线后退役 `runCommands` CMD_MOBILE/RTSP 两分支。
- **硬约束继承**（与 wm 同源）：1-IMP-per-boot——um（RTSP + day/night 占 IMP）与 wm **不能同 boot 共存 / 运行时切换**，故 um 是 **whole-boot 进程**，仅 signal/MCU/poweroff 退出；进程内永不 `IMP_System_Exit`（`sharedVideo()` 单例 + channel 级释放）。

---

## 1. 身份与 CLI

| 项 | 值 |
|----|----|
| binary 名 | `um`（新建，源码 `src/app/um_app.cpp`） |
| CLI | `um [--no-rtsp] [--no-audio] [--no-mdns] [--no-http] [--force-day] [--record-stream1]` —— **无模式 flag**（不像 wm 的 `-m 0/1/2`），全是可选 `--no-*`/配置开关 |
| 与旧 app 关系 | **并存**：`htc_workmode_app -wm 3/4` / `htc_main_app -m/-rs` 暂留，不破坏现有 spawn 契约；media_app 改 spawn `um` 留后续（§9） |
| 平台 | **双平台必须编译**：真机（`toolchain.cmake`，进 `build/bin/um`）+ 仿真（`-DBUILD_FOR_SIMULATION=ON`，进 `build_sim/bin/um`） |
| devtest 钩子 | 保留 `HTC_TEST_NO_POWEROFF`：置 1 则关机走 `_exit(0)` 而非 `Misc::poweroff()`，供 devctl 同 boot 重跑（与 wm 一致） |

> flag 镜像 legacy `-m` 子参数（`--no-rtsp`/`--no-audio`/`--force-day`/`--record-stream1`，见 ADR §7.1），并加 `--no-mdns`/`--no-http` 以覆盖 legacy `-wm 4`（rtsp-only）等价形态。

---

## 2. um 提供什么（bring-up 集）

um 启动时按序拉起下列服务（**默认全开**，可用 §7 的 flag 关闭单项）：

| 服务 | 库 | 通道/说明 | flag 关闭 |
|------|----|----------|----------|
| **RTSP 预览** | `media_rtsp`/`RtspServer` | CH1 主码流；audio 可选（`--no-audio`→`HTC_NO_AUDIO`） | `--no-rtsp` |
| **HTTP 控制** | `http_server` | `CameraService` 端点 + `CameraParameterRegistry`；`STATUS` 端点 on-demand 读 MCU | `--no-http` |
| **HTTP 渐进式回放** | `http_server::api_v1_camera_video_playback` | fMP4/mp4 + playback_token | （随 `--no-http`） |
| **mDNS 发现** | `discovery_service`/`MdnsService` | 主动广播服务记录供客户端发现 | `--no-mdns` |
| **day/night ISP 自动切换** | `daynight` | 保证 RTSP 预览日夜画质 | `--force-day`（强制 DAY，不走自动） |

- **MCU**：on-demand——每个 `STATUS` 请求同步读一次 MCU I2C（**不开后台轮询线程**），与 wm 的"work mode sync 直通"一致。代价：每次 STATUS 一趟 I2C（STATUS 非热路径，可接受）。
- **RGB blink**：**不做**（不闪 LED）。与 wm（无 blink）一致。
- **mobile pairing 运行时切换**：**不做**——`POST /api/v1/system/workmode` 返 **HTTP 501**（work mode firmware-only，无运行时切换路径；1-IMP-per-boot 也禁止运行时切进程）。

---

## 3. 架构：server-lifecycle

um 内核 = **server-lifecycle**（**非** wm 的 task-scheduler）。三阶段：

1. **bring-up**：按 §2 顺序拉起各服务（day/night → mDNS → HTTP → RTSP）；任一必需服务起不来按 §5 兜底。
2. **idle-wait**：阻塞在 stop-condition 上，周期检查 §4 的 idle 判定 + 监听外部 signal/MCU。
3. **teardown**：收到 stop-condition → §6 teardown 序 → poweroff。

**为何不复用 wm 的 task-scheduler**：scheduler 的 slot/pending/idle-grace 模型是为**一次性 task（做完即退）**设计的；um 的服务是**长驻**（server 不"完成"，整 session 并发运行），强行套 task 语义会扭曲。um **复用 wm 的原语**（时间链 §5、IMP `sharedVideo` 纪律、teardown 硬化 §6、`HTC_TEST_NO_POWEROFF`、devctl `ctrl-c` 钩子），**不复用 scheduler**。

> 回放/控制请求是 server 内部的 request-handler（每次请求一个瞬态流/处理），不是顶层调度 task——无需 scheduler 介入。

---

## 4. 关机触发（idle-timeout 自关机）

um 长驻期间，**何时退出**由 idle-timeout 决定（电池相机的核心约束：用户弃用 app 后不能让 IMP 编码器空耗）：

- **idle 判定（活跃）**：满足任一即"活跃"，重置 T 计时——
  - 有 **RTSP 客户端连接中**（正在预览）；或
  - 收到 **HTTP 请求在活跃窗口内**（控参/回放/STATUS）。
- **mDNS 不算活跃**：mDNS 是 um 主动广播（被动发现），客户端"发现到" ≠ "在用"，单靠发现不该让 um 醒着。
- **触发**：持续 **无活跃** 达 `HTC_UM_IDLE_TIMEOUT_MS`（默认待定，建议 5–10 min；测试时调小）→ `Power::requestShutdown()` → signal 路径进 §6 teardown → poweroff。
- **外部触发仍生效**：SIGTERM/signal（devctl `ctrl-c`）、MCU 强制关机（低电/换挡，override 流程，另见 wm-app-spec §3.6 同源）。
- idle 期间有新活跃 → 取消计时、续命。

> 与 wm 的关系：wm 的 idle-grace（全 slot 空 G 秒）是"任务做完了就关"；um 的 idle-timeout 是"没人用了就关"——语义不同，但都走 `Power::requestShutdown()` → 同一 teardown 路径。

---

## 5. 时间链（复用 wm，原样）

um **复用 wm 的时间链编排**（`wm-app-spec.md` §6，已 `time_test` 真机 7/7 GREEN）：

- 启动、拉起任何服务**之前**：RTC → MCU → NTP 三步链 + `ntpSynced` 标志（详见 wm spec §6.1）。
- um 常联网（要 serve 必有 IP）→ NTP 可靠，时间链几乎总能在 NTP 步收口。
- **关机回写 MCU 时间**（§6）：`ntpSynced==true` 直接用系统时间；`false` 先补 NTP；结果仍须 plausible 才写。仅写**时间**，不写 PID/UPID/UPWD。

> 时间链代码可直接从 wm lift（`acquireTimeChain`/`writebackMcuTime`），um 与 wm 共享同一套原子能力。

---

## 6. teardown + MCU 回写

收到 stop-condition（idle-timeout / signal / MCU override）后的 teardown 序（**复用已证的 legacy `-wm 3` teardown 序 + wm 原语**）：

1. 屏蔽新请求。
2. **回写 MCU 时间**（§5 规则，仅时间）。
3. 停 RTSP server → 停 HTTP server → 停 mDNS → day/night 安全关。
4. `Settings` 落盘。
5. `HTC_TEST_NO_POWEROFF` 置 1 → `_exit(0)`；否则 `Misc::poweroff()` + `while(1)`（真机断电）。

- **idle-timeout 走与 ctrl-c 同一条 teardown 路径**——不另开 teardown 分支，保证一致性。
- **day/night 安全关**：B3 已验证 `-wm 3`（含 day/night init）的干净 ctrl-c teardown（kill-switch 已于 2026-06-21 退役），故 um 继承 day/night 的 teardown 安全。

---

## 7. flag / 配置

| flag / 旋钮 | 来源 | 默认 | 说明 |
|------|------|------|------|
| `--no-rtsp` | CLI | off | 不起 RTSP server（CH1 不占） |
| `--no-audio` | CLI → `HTC_NO_AUDIO` | off | RTSP 不带音频 |
| `--no-mdns` | CLI | off | 不广播 mDNS（`-wm 4` 等价） |
| `--no-http` | CLI | off | 不起 HTTP server（控制+回放都关） |
| `--force-day` | CLI | off | 强制 DAY 模式，不走 day/night 自动 |
| `--record-stream1` | CLI | off | 录 stream1（legacy `-m` 子参保留） |
| `HTC_UM_IDLE_TIMEOUT_MS` | env | 待定（建议 300000–600000） | idle-timeout T（§4）；测试调小 |
| `HTC_UM_HTTP_ACTIVITY_WINDOW_MS` | env | 待定 | HTTP 请求"活跃窗口"（§4） |
| `HTC_TEST_NO_POWEROFF` | env | 0 | 1=关机走 `_exit(0)`（devtest） |
| NTP server / `MS_IP` / `MS_PORT` | config ini（`server` 段） | — | NTP 与上传服务器 |

> 原则（与 wm 一致）：**产品配置走 setting.json，运行时/调试旋钮走 HTC_\* env，启动开关走 CLI flag**。

---

## 8. 模块复用与验证状态

| 模块 | 复用源（库 / file:line） | 验证状态 | 用途 |
|------|--------------------------|---------|------|
| RTSP（CH1） | `media_rtsp`/`RtspServer`；legacy wiring `runRtspServerUntilSignal` `WorkModeRunner.cpp:705` | ✅ B4 HW GREEN（probe + 干净 teardown） | RTSP 预览 |
| HTTP 控制+回放 | `http_server`（`api_v1_camera_video_playback` `http_api_v1.cpp:~1068`） | ✅ sim（`test_http_api`/`test_camera_*`）+ B3 HW | 控制 + 渐进式回放 |
| mDNS | `discovery_service`/`MdnsService`；legacy `:635` start / `:647,653,676,687` stop | ✅ sim（`test_mdns_*`）+ B3 HW | 发现 |
| day/night | `daynight`；legacy init `:583` | ✅ B3 HW（含 day/night 干净 teardown） | 预览画质 |
| MCU | `mcu_service`/`McuCache`；on-demand 读 | ✅ sim（`test_mcu_service`） | STATUS 端点 + 时间链 |
| 时间链 | wm `acquireTimeChain`/`writebackMcuTime`（`time_test` 7/7 GREEN） | ✅ HW | 复用 wm |
| **um app 组合**（`src/app/um_app.cpp`） | 新写编排器 | ⏳ 待建 + 待测 | bring-up/idle-wait/teardown |

> **关键**：所有模块**功能已验证**（sim + HW probe），唯一未建/未测的是 **um binary 组合 + idle-timeout lifecycle**。且 um **无后端缺口**（rtsp/http/mdns 都可直探，不靠 mgmt/storage server）→ 比 wm（upload 依赖后端）**更易测**。

---

## 9. go-live / done 状态

- **um Phase-1 done = binary 存在 + L2/sim 测试绿 + 与 legacy 并存**。
- media_app **暂不改 spawn**（仍 spawn legacy `-m`/`-wm 3`）。
- **repoint（media_app 改 spawn `um` + `wm`）作为 wm+um 联合 C4 步骤延后**——等两者都稳定后再做（与 wm-app-spec §13 "media_app 是否改 spawn wm 待 wm 稳定后定" 完全一致）。
- 入口触发：um **继承 media_app 现有模式选择**（现在挑 `-m`/`-wm 3` 的逻辑），不定义新入口。

---

## 10. 决策日志（grill 2026-06-28）

| # | 决策点 | 结论 |
|---|--------|------|
| 1 | 架构形态 | **server-lifecycle**（bring-up/idle-wait/teardown），**非** wm task-scheduler；复用 wm 原语 |
| 2 | bring-up 集 | 核心 rtsp+http+mdns 默认 IN；day/night IN；MCU on-demand 读；RGB blink OUT；mobile 运行时切换 OUT |
| 3 | 模式 | **无模式，只 flag**（`--no-*`），flag 覆盖 `-wm4`；无 `-m` 模式 flag |
| 4 | 关机触发 | **idle-timeout 自关机**（无 RTSP 客户端 + 无 HTTP 请求 持续 T）→ `Power::requestShutdown()`；signal/MCU 仍生效 |
| 5 | idle 活跃定义 | RTSP 连接中 **或** HTTP 请求窗口内；mDNS OUT |
| 6 | MCU STATUS | **on-demand 每请求读**（无轮询线程） |
| 7 | 时间链 | **复用 wm**（RTC→MCU→NTP + 关机回写）原样 |
| 8 | 构建路径 | **新写 um_app 编排器**（调库 API，读 runCommands CMD_MOBILE 作参考，退役 waterfall） |
| 9 | 入口 | 继承 media_app 现有模式选择 |
| 10 | teardown | 复用已证 `-wm3` teardown 序 + wm 原语；idle-timeout 走同一路径 |
| 11 | 测试契约 | L2 re-point B3/B4 + 新增 idle-poweroff + L1 sim smoke；无后端缺口；无模式矩阵 |
| 12 | go-live | done = binary + L2/sim 绿 + 并存；repoint 延后（联合 C4，与 wm 一致） |

---

## 11. 风险与关注点

1. **idle-timeout teardown 是新自触发路径**（非 signal）：必须与 ctrl-c teardown 同样干净——由新增 L2 idle-poweroff 测试覆盖（§测试）。
2. **um 占 IMP whole-boot**：若 um crash/hang 则整个 boot 死（无法重启 IMP）。但 **crash-prone 模块（record/snap）不在 um**，故风险 << wm。
3. **T 调参**：idle-timeout T 太短 → 用户短暂断连（切后台）即关机；太长 → 弃用后空耗。需按 mobile 使用节奏调，T 与 HTTP 活跃窗口均为 env 可调。
4. **devtest 无后端**：um 不依赖 mgmt/storage 后端（与 wm upload 不同），probe/teardown 可在 devtest 完整验证。
5. **1-IMP-per-boot**：um 跑即 wm 不能同 boot 跑；每个 um 跑都 1-boot-1-case（冷启间隔），先 md5 校验部署。

---

## 12. 不在范围 / 后续

- `media_app` 改 spawn `um`/`wm`（C4 repoint）→ wm+um 联合步骤，延后。
- 退役 legacy `-m`/`-wm 3`/`-wm 4`/`-rs` → um 真机稳定后。
- mobile pairing 运行时切换闭环（见 `workmode-vs-cmd-mobile-layering.md §8`）→ firmware-only，不在 um。
- T / HTTP 活跃窗口默认值 → spec 定（可逆）。
- MCU override 关机的触发协议细节 → 与 wm 共用，单独 spec。

---

## 13. 关联文档

- [`wm-app-spec.md`](wm-app-spec.md) — wm（一次性任务）姊妹 spec，um 复用其时间链/teardown 原语
- [`scenario-test-manifest.md`](scenario-test-manifest.md) — 模块验证状态真相源（B3/B4 在此）
- [`phase1-module-stabilization-plan.md`](phase1-module-stabilization-plan.md) — 11 模块稳定化（um 的模块来源 + Phase-2 拓扑）
- [`../decisions/workmode-usermode-process-split.md`](../decisions/workmode-usermode-process-split.md) — wm/um 拆分 ADR（§7.1–7.4 兼容点）
- [`../decisions/devtest-automation-loop.md`](../decisions/devtest-automation-loop.md) — devtest 闭环（probe + ctrl-c teardown idiom）
