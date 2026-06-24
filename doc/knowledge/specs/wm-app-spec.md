# wm 程序规格（wm-app-spec）

> 用途：新建独立 binary `wm` 的行为规格——把 Phase-1 已在 T32 真机/仿真验证通过的单功能模块（snap / record / upload / ntp / mcu）编排成一个稳定的「一次性任务」程序。
> 来源：2026-06-23 grill 会话定型（决策见 §12 决策日志）。**真相源 = 本文 + 代码**；本文为 wm 的权威 spec。
>
> **治理规则（重要）**：后续若修改 wm 功能，**必须同步更新本 spec**；若实现与 spec 冲突，**先询问是否更改 spec**，不要默默改实现。关联 [`scenario-test-manifest.md`](scenario-test-manifest.md)（模块验证状态真相源）、[`phase1-module-stabilization-plan.md`](phase1-module-stabilization-plan.md)。

---

## 0. 背景与定位

- 旧 `htc_workmode_app` 因「单进程共享关机并发 teardown」crash 不收敛，已退回 Phase-1 单功能稳定化（见 manifest）。
- 单功能验证（record/snap/upload 真机绿、ntp/mcu/http 仿真绿）完成后，**新建独立 binary `wm`** 重新组合这些稳定模块，**不复用、不修改** `htc_workmode_app`。
- `wm` 只承接**一次性任务**模式（`-m 0/1/2`）；长驻模式（旧 `-wm 3` TEST_ONLY / `-wm 4` UVC）拆到**另一个 usermode app**（见 [`../decisions/workmode-usermode-process-split.md`](../decisions/workmode-usermode-process-split.md)）。
- **硬约束继承**：1-wm-per-boot（IMP 驱动不支持一 boot 内 ≥2 个 IMP 进程）；进程内永不 `IMP_System_Exit`（单例 + channel 级释放）。

---

## 1. 身份与 CLI

| 项 | 值 |
|----|----|
| binary 名 | `wm`（新建，源码 `src/app/wm/`） |
| CLI | `wm -m <0\|1\|2>`（**无 `-rtc` 入参**——wm 自跑时间链，见 §6） |
| 与旧 app 关系 | **并存**：`htc_workmode_app` / `htc_main_app -wm` 暂留，不破坏现有 spawn 契约；是否让 `media_app` 改 spawn `wm` 留后续 |
| 平台 | **双平台必须编译**：真机（`toolchain.cmake`，进 `build/bin/wm`）+ 仿真（`-DBUILD_FOR_SIMULATION=ON`，进 `build_sim/bin/wm`） |
| devtest 钩子 | 保留 `HTC_TEST_NO_POWEROFF`：置 1 则关机走 `_exit(0)` 而非 `Misc::poweroff()`，供 devctl 同 boot 重跑（与旧 app 一致） |

---

## 2. 模式（`-m`）

三个模式，全部**循环架构**（非 one-shot；见 §3）。`-m 2` 不涉及拍照。

| `-m` | 名 | Capture lane | Upload lane | WiFi | 典型用途 |
|------|----|-------------|------------|------|---------|
| **0** | CAPTURE_ONLY | 有 | **无** | **关**（离线） | 省电拍照/录影，文件留 SD，下次 boot 传 |
| **1** | CAPTURE+UPLOAD | 有 | 有（首个 Capture 完成后才调度，之后并发） | 开 | 拍完即传 |
| **2** | UPLOAD_ONLY | **无** | 有（立即 drain SD 遗留 desc） | 开 | 补传上次没传完的文件 |

> 说明：`-m 0` 离线 → 无 NTP，时间只靠 RTC/MCU（见 §6.3）。

---

## 3. 架构：任务调度器

wm 内核 = 一个**任务调度器**（非旧 EventLoop 的平移，是新编排）。

### 3.1 结构

- **pending 队列**：待调度的 task。
- **Capture lane**：执行拍照/录影 task，**lane 内串行**（IMP 单 sensor，一次一个）；新触发把 Capture task 入 pending，再被调度进 lane。
- **Upload lane**：执行上传 task，**与 Capture lane 并发**（后台 drain，复用 `UploadWorker`）。
- **Shutdown task**：**单独摆放**，不在 pending；终态。

### 3.2 task 类型可扩展

不写死「Capture/Upload/Shutdown」三个。task 按**类型**分类（接口/标签），便于未来加类型（如「日志上报」「固件检查」）。当前实例化三类。

### 3.3 调度规则

1. Capture 与 Upload **不互斥**，可同时执行。
2. **启动时一次性 gate**：Upload lane **等首个 Capture task 完成后**才被调度启动（保证有东西可传）。`-m 2` 无 Capture → Upload 立即启动。
3. Upload 运行中**允许** Capture 继续触发执行。
4. 每个 task 可有超时（见 §3.4）。

### 3.4 Shutdown 触发（自管关机）

**关机不由外部触发**（MCU 强制关机是另一条 override 流程，见 §3.6）。wm 自管：

- **idle-grace**：pending 队列 **且** Capture lane **且** Upload lane 三者**全空，并持续 `G` 秒**（`HTC_WM_IDLE_GRACE_MS`，默认 30000），才 ready Shutdown task。
  - 全空期间有新触发/新 upload → 取消计时、续命。
- **Upload 超时**：Upload task 自带 `HTC_UPLOAD_TIMEOUT_MS`（默认 60000，自首次入队起算）；没传完 → `Power::requestShutdown()`（SIGTERM 中断 worker 阻塞 I/O；该 desc `F_UploadedTag` 保持 0，下次 `-m 2` 重传）→ signal 路径进 Shutdown。
- Shutdown task 一旦被调度：**屏蔽外部 Capture 触发**，**不可中断**地走 teardown（§7）→ 保证一定关机。

> **2026-06-24 修订（Slice 1 实现发现）**：原写「Shutdown task 不做 upload flush」**不成立**——`UploadWorker::stop()` 在 worker 阻塞 I/O（auth/upload 的 recv/send）上会**卡死**（21 desc auth-fail 时 m2 hang，已复现）。改为 **upload-timeout→`requestShutdown`(SIGTERM) + wm_app 尾 `flush(30000)+stop()`**，同 EventLoop proven 模式。详见 [`reviews/2026-06-24-wm-slice1-m2.md`](../../reviews/2026-06-24-wm-slice1-m2.md)。若要严格执行「不 flush」，需先把 `UploadWorker::stop` 改成可中断阻塞 I/O（单独任务）。

### 3.5 one-shot 退化（可选）

架构上**允许重触发**；若要「拍一次就关」，设 `HTC_WM_ONE_SHOT=1`：首个 Capture 完成后**屏蔽触发**→ 自然 drain → Shutdown。默认 0（可重触发）。

### 3.6 MCU 强制关机（override，单独流程）

MCU 有权随时强制关机（如低电、换挡）。这是**另一条流程**，**不走调度器 Shutdown task**：直接 abort 一切 → teardown → poweroff。本 spec 仅轻点其存在，不展开其触发协议。

---

## 4. 拍照/录影触发源

可插拔 **Trigger 接口**（`-m 2` 无 Capture、不涉及；`-m 0`/`-m 1` 才有）。Trigger 把 Capture task 入 pending 队列。

初始实现（按环境选）：
- **PIR**（真机）：硬件 PIR 事件。
- **SimPir**（sim/test）：定时器，复用现有 `SimPirTrigger`，间隔 `HTC_SIM_PIR_INTERVAL_MS`（默认 10000）。
- **信号注入**：外部信号触发（调试/集成用）。

> 未来加调度/手动触发 = 加一个 Trigger 实现，不动调度器。

---

## 5. Capture task 行为

由 `setting.json`（产品配置）驱动：

| 字段 | 含义 | 位置 |
|------|------|------|
| `cameraMode` | 0=仅拍照 / 1=拍照+录影 / 2=仅录影 / **3=并发拍录（不在 wm 范围）** | `Settings.h:60` |
| `burstNumber` | 连拍数 | `Settings.h:62` |
| `stillSize` / `videoSize` | 拍照/录影分辨率（索引 `SnapImgSize[]` / `VIDEO_SIZE_*`） | `Settings.h:32/37`，表 `Common.h:165-177` |
| `videoLength` | 录影时长（秒） | `Settings.h:65-66` |

### 5.1 支持范围

**`cameraMode` 0/1/2**（拍照 / 拍照+录影 / 录影）。**`3`（并发拍录）暂不纳入**（硬约束：>8M 无路径、CH2 上限 8M、录影 fps 受限；sample 级验证见 [`photo-video-concurrent-implementation-plan.md`](photo-video-concurrent-implementation-plan.md)）。

### 5.2 每个 Capture task 产出（4 件，缺一不可）

1. **媒体文件**：带时间戳文件名（格式 `%Y%m%d_%H%M%S`，时间来自 §6）。
2. **缩略图**：存 `thumbnails` DB（`MetadataDao::saveThumbnail`，`MetadataDao.cpp:93`）。
3. **元数据行**：存 `media_files` DB（`MetadataDao::addMedia`，`MetadataDao.cpp:48`；`type=1` photo / `2` video）。
4. **upload desc**：`F_UploadedTag=0`（给 Upload lane）。

### 5.3 ⚠️ 代码坑（必避）

旧 workmode 路径 `processCmdSnap` / `processCmdConcurrentSnapRecord`（`WorkModeRunner.cpp:94 / :287`）**不写 DB、不存缩略图**。wm 的 Capture task **必须走会写 DB 的路径**：
- 拍照：`ImageSnap::snap`（内部 `addMedia` at `ImageSnap.cpp:393`），并由调用方 `saveThumbnail`。
- 录影：`VideoRecorder`（内部 `addMedia` at `VideoRecorder.cpp:981`），并由调用方 `saveThumbnail`。

DB 引擎：SQLite 双库 `media_file.db`（元数据）+ `media_thumb.db`（缩略图 BLOB），`DatabaseManager.cpp`。

---

## 6. 时间链（spec 目标态，**最高风险，需新写编排**）

> ✅ **验证状态（2026-06-23 更新）**：时间链已用独立 `time_test` 骨架在 T32 真机验证通过（7/7 离线 case GREEN，见 [`../../reviews/2026-06-23-time-chain-hw-verification.md`](../../reviews/2026-06-23-time-chain-hw-verification.md)）。RTC get/set、MCU get/set、NTP（`www.aidetcloud.com` 真机可同步）、全链 + 关机回写均 HW 确认。链代码 `acquireTimeChain`/`writebackMcuTime` 可 lift 进 wm。下面是 spec 目标态；现状代码 gap 见 §6.4。**待定**：RTC-drift 边界（§11.5）。

### 6.1 启动获取链（wm 自跑，无 `-rtc` 入参）

启动时、调度器跑任何 task **之前**，wm 内部按序：

1. 读 RTC（`RTC::getTime`）；**plausible**（`TIME_PLAUSIBLE`，≥2026，`Common.h:214`）→ `settimeofday`，结束。
2. 否则读 MCU（`MCU::getDatetime`）；plausible → `settimeofday`，结束。
3. 否则 NTP（`Misc::ntpSyncAndWait`）；成功 → `settimeofday` + **写 RTC**（`RTC::setTime`）+ **置 `ntpSynced=true`**。
4. 三者全败 → 系统时间不可信（按 §6.3 兜底）。

> 关键差异 vs 现状：NTP 成功后**无条件写 RTC**（现状只在 `isRtcWorkWell` 时才写）。`ntpSynced` 标志是**新增**（现状无）。

### 6.2 `ntpSynced` 标志

- 新增布尔，**仅当本次 boot NTP 成功才置 true**。
- 用于关机回写 MCU 的判定（§7）。

### 6.3 无可信时间兜底（全模式通用 + m0 离线特化）

- **m0 离线**：不开 WiFi，**不尝试 NTP**；时间只靠 RTC/MCU。
- 全链失败（RTC/MCU 都不可信，或 m0 无 NTP）：
  - **文件名**：用**兜底 plausible 时间**（固定近期日期，机制同 `snap_test.cpp:47-63 ensurePlausibleClock`）——仅用于文件名，**不写 RTC/MCU**。
  - **MCU 回写**：**跳过**（不写垃圾时间进 MCU）。
  - `ntpSynced=false`。下次有网络的 boot（`-m 1/2`）NTP 纠正并回填 RTC/MCU。

### 6.4 现状代码 gap（需新写/改）

| 现状 | spec 目标 |
|------|----------|
| `syncSystemTime`（`ProcessLifecycle.cpp:659-708`）只 RTC→MCU、无 NTP 步 | 三步链 RTC→MCU→NTP 一体 |
| NTP 是 `WorkModeRunner` CMD_NTP 单独步，且仅 `isRtcWorkWell` 时写 RTC | NTP 成功无条件写 system+RTC |
| **无 `ntpSynced` 标志** | 新增 |
| 关机 `syncWithMCU`（`ProcessLifecycle.cpp:623-657`）只判系统 plausible 就写 MCU | 「!ntpSynced 先 NTP」规则（§7） |

→ 复用 RTC/MCU/NTP **原子能力**，编排是新代码。

---

## 7. 关机 teardown + MCU 回写

Shutdown task（或 MCU override）的 teardown 序：

1. 屏蔽 Capture 触发（已做，见 §3.4）。
2. **回写 MCU 时间**（`MCU::setDatetime`，**仅时间**，不写 PID/UPID/UPWD 等）：
   - `ntpSynced==true` → 直接用系统时间。
   - `ntpSynced==false` → **先补一次 NTP**；成功用同步后时间、失败用当前系统时间；再写 MCU。
   - **结果仍须 plausible 才写**（不可信则跳过，§6.3）。
3. `Settings` 落盘（`saveToJsonFile`）。
4. `HTC_TEST_NO_POWEROFF` 置 1 → `_exit(0)`；否则 `Misc::poweroff()` + `while(1)`（真机断电）。

> 回写 MCU = **写时间**这一件事。旧 `syncWithMCU` 里 PID/UPID/UPWD「从 MCU 读→写 config 文件」的读回流程**不在 wm 范围**。

---

## 8. Upload lane

复用已验证的 `UploadWorker`（`src/app/workmode/upload_worker.cpp`）。

- 扫 `MEDIA_UPLOAD_PATH`（`app.h:27`，`SD_CARD_PATH/media/upload/`）下 desc.json。
- 上传 desc 本身 + `file_inf` 里的媒体文件，走 **TCP mgmt/storage server**（`MS_IP`/`MS_PORT`，**非 HTTP**，自定义二进制帧协议，`StorageServClient.cpp:121-218`）。
- **「未处理」= `F_UploadedTag==0`**（desc JSON 标志，desc 级 + 文件级，**非 DB 字段**）；传成功回写 tag=1，可选按 `FILE_MANAGE` 删源文件。
- 跨 boot 持久化 = desc 文件留 SD（未传完的 `F_UploadedTag` 保持 0，下次 `-m 2` 重传）。
- 超时：§3.4。

> devtest 无 mgmt/storage 后端 → 上传必失败。HW 验证只能断「worker 启动 + 干净失败 + 无 crash/hang」，**不能断上传成功**（与 manifest B2 一致）。

---

## 9. 配置旋钮

| 旋钮 | 来源 | 默认 | 说明 |
|------|------|------|------|
| `cameraMode` / `burstNumber` / `stillSize` / `videoSize` / `videoLength` | `setting.json`（产品配置） | — | 拍/录行为 |
| `HTC_WM_IDLE_GRACE_MS` | env | 30000 | idle-grace G（§3.4） |
| `HTC_UPLOAD_TIMEOUT_MS` | env | 60000 | Upload task 超时（§3.4） |
| `HTC_WM_ONE_SHOT` | env | 0 | 1=首个 Capture 后屏蔽触发（§3.5） |
| `HTC_SIM_PIR_INTERVAL_MS` | env | 10000 | SimPir 间隔（sim/test，§4） |
| `HTC_TEST_NO_POWEROFF` | env | 0 | 1=关机走 `_exit(0)`（devtest，§1） |
| NTP server / `MS_IP` / `MS_PORT` | config ini（`server` 段） | — | NTP 与上传服务器 |

> 原则：**产品配置走 setting.json，运行时/调试旋钮走 HTC_\* env**（与现有 `HTC_UPLOAD_TIMEOUT_MS`/`HTC_TEST_RECORD_COUNT` 一致）。

---

## 10. 模块复用与验证状态

| 模块 | 复用源（file:line） | HW 验证 | 用途 |
|------|---------------------|--------|------|
| record（录影+缩略图+DB） | `VideoRecorder.cpp:981`（DB）、`:1171`（缩略图） | ✅ GREEN | Capture lane（录影） |
| snap（拍照+缩略图+DB） | `ImageSnap.cpp:393`（DB）、`ImageSnap::capture_thumbnail` | ✅ GREEN（`snap_test`） | Capture lane（拍照） |
| upload | `upload_worker.cpp` / `StorageServClient.cpp` | ✅ GREEN（仅干净失败） | Upload lane |
| ntp | `Misc::ntpSyncAndWait` `Misc.cpp:510` | ✅ HW（time_test 真机同步成功） | 时间链 |
| mcu | `MCU::getDatetime:778` / `setDatetime:691` | ✅ HW（time_test 往返 match） | 时间链 + 关机回写 |
| rtc | `RTC::getTime:122` / `setTime:62` | ✅ HW（time_test 往返 match） | 时间链 |
| DB | `MetadataDao` / `DatabaseManager` | ✅ GREEN（sim `test_database`） | 元数据 + 缩略图 |
| 时间链编排（RTC→MCU→NTP + ntpSynced + 关机回写规则） | `time_test`（`src/app/time_test.cpp`） | ✅ HW Stage 1（7/7） | 已验证，可 lift 进 wm |
| **wm app 组合**（3 模式 × cameraMode，`src/app/wm_app.cpp`） | `tests/host/test_wm.py` | ✅ HW 矩阵 5 PASSED / 2 xfail | m2/m0-photo/m0-record/m1-photo/m1-record GREEN；cm==1 combo WEDGE（§5.1 待定）|

> ⚠️ **时间链是落在 sim-only 模块（mcu/ntp）上的新编排，是全程序最高风险段**。建议优先把它做成可独立验证的单元（L1 sim golden + L2 真机冷启 case），用 §6.4 的 gap 表逐项收口。

---

## 11. 风险与关注点

1. ~~**时间链最高风险**（§10）：sim-only 模块 + 新编排。优先单独验证。~~ → **已收口（2026-06-23）**：`time_test` 真机 Stage 1 7/7 GREEN（review 见上）。RTC/MCU/NTP 原子 + 链编排 + 关机回写均 HW 验证。剩余 §11.5 待定。
2. **idle-grace G 调参**：G 太短 → 零散 PIR 场景下 m1 可能在下一个事件前关机；G 太长 → 空等耗电。需按部署事件节奏调，或由 MCU 唤醒窗口兜底（§3.6）。
3. **Capture lane 串行**：连拍/密集触发时 task 在 pending 排队，IMP 一次一个。
4. **devtest 无后端**：upload 只能验证干净失败；真上传需 mock 后端（留后续）。
5. **1-wm-per-boot**：每个 `-m` 跑都 1-boot-1-case（冷启间隔），先 md5 校验部署。

### 11.5 RTC-drift：维持现状（2026-06-23 已确认）

真机验证发现：内核会 **sys←RTC 同步**，故 RTC plausible 后 sys 几乎总 plausible → 链 §6.1 step1（sys plausible→停）几乎总命中，**wm 启动不会主动 NTP**。若 RTC plausible-但-漂移（电池没电停在旧 plausible 日期），本次 capture 文件名会用错时间，只有关机 writeback 才 NTP 纠正。

**决策（用户 2026-06-23 拍板）：选 (a) 维持现状**——信 plausible 的 sys、NTP 仅兜底。接受 RTC 漂移边界的文件名错（关机纠正）；换取冷启快、m0 离线友好。不改 §6.1。

---

## 12. 决策日志（grill 2026-06-23）

| # | 决策点 | 结论 |
|---|--------|------|
| 1 | 改旧 app vs 新建 | **新建 `wm`**，与旧 app 并存 |
| 2 | `-m 0/1/2` 语义 | 0=CAPTURE_ONLY / 1=CAPTURE+UPLOAD / 2=UPLOAD_ONLY |
| 3 | 架构 | **任务调度器**（pending + Capture lane + Upload lane + Shutdown task），task 类型可扩展 |
| 4 | 关机触发 | **自管**：全空持续 G 秒 → Shutdown；Upload 超时清除 task；Shutdown 不 flush；MCU override 另流程 |
| 5 | 触发源 | **可插拔 Trigger 接口**（PIR / SimPir / 信号注入） |
| 6 | cameraMode 范围 | **0/1/2**，不含并发(3)；每 Capture 产出 文件+缩略图DB+元数据DB+desc |
| 7 | upload 超时/flush | 硬超时清除 task → drain → Shutdown；Shutdown **不 flush** |
| 8 | 回写 MCU 内容 | **仅时间** |
| 9 | 时间链归属 | **wm 自跑**，无 `-rtc` 入参 |
| 10 | m0 时间策略 | **离线 + 兜底文件名**；不可信则跳过 MCU 回写 |
| 11 | binary 定位 | 新建 `wm`，`-m 0/1/2`，双平台，保留 `HTC_TEST_NO_POWEROFF` |
| 12 | 配置旋钮 | 产品走 setting.json，旋钮走 HTC_\* env（G=30s / upload=60s / one-shot 默认关） |

---

## 13. 不在范围 / 后续

- 长驻模式（TEST_ONLY / UVC / PIR EventLoop）→ 另一个 usermode app。
- `cameraMode 3`（并发拍录）→ 后续，受 CH2 8M 约束。
- `media_app` 是否改 spawn `wm`、旧 `htc_workmode_app` 退役时点 → 待 wm 稳定后定。
- MCU override 关机的触发协议细节 → 单独 spec。
- upload 真后端 mock（devtest 用）→ 后续。
- ~~时间链的 L1 sim golden + L2 真机冷启验证 case → 最高优先 follow-up。~~ → **done（2026-06-23）**：`time_test` + `test_time_chain.py` Stage 1 7/7 GREEN。Stage 2 在线 NTP 已被真机确认可同步（review 发现 1），风险很低；如需可补 `chain-online` case 收尾。

---

## 14. 关联文档

- [`scenario-test-manifest.md`](scenario-test-manifest.md) — 模块验证状态真相源（wm 复用依据）
- [`phase1-module-stabilization-plan.md`](phase1-module-stabilization-plan.md) — 11 模块稳定化（wm 的模块来源）
- [`../decisions/workmode-usermode-process-split.md`](../decisions/workmode-usermode-process-split.md) — wm/um 拆分 ADR（3/4 去 usermode app 的依据）
- [`workmode-selection-and-switching.md`](workmode-selection-and-switching.md) §14 — 旧 `-wm` 目标态（与本文 `-m` 不同体系，勿混）
- [`main-app-mode-behavior.md`](main-app-mode-behavior.md) — 旧 app 模式行为（对照）
- [`photo-video-concurrent-implementation-plan.md`](photo-video-concurrent-implementation-plan.md) — 并发拍录（cameraMode 3，暂不在范围）
