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
- **上游 quickSnap（2026-07-09 起）**：ZL 型号上，wm 由上电首进程 `quickSnap`（[`quicksnap-app-spec.md`](quicksnap-app-spec.md)）`fork+execv` 拉起，handoff = `wm -m <2|3>`（**无 `-rtc`**）。quickSnap 拍所有片（落 `/tmp/media/`），wm 只跑 `-m 2`(上传)/`-m 3`(心跳)，**都不拍照、不 init IMP**。m2/m3 走 **lean 启动**（§2.1：无卡 / 无 DB / 无重传）。

---

## 1. 身份与 CLI

| 项 | 值 |
|----|----|
| binary 名 | `wm`（新建，源码 `src/app/wm/`） |
| CLI | `wm -m <0\|1\|2\|3>`（**无 `-rtc` 入参**——wm 自跑时间链，见 §6） |
| 与旧 app 关系 | **并存**：`htc_workmode_app` / `htc_main_app -wm` 暂留，不破坏现有 spawn 契约；是否让 `media_app` 改 spawn `wm` 留后续 |
| 平台 | **双平台必须编译**：真机（`toolchain.cmake`，进 `build/bin/wm`）+ 仿真（`-DBUILD_FOR_SIMULATION=ON`，进 `build_sim/bin/wm`） |
| devtest 钩子 | 保留 `HTC_TEST_NO_POWEROFF`：置 1 则关机走 `_exit(0)` 而非 `Misc::poweroff()`，供 devctl 同 boot 重跑（与旧 app 一致） |

---

## 2. 模式（`-m`）

四个模式。`-m 0/1`（capture）是**循环架构**（见 §3）；**`-m 2/3`（lean）是 one-shot**（quickSnap handoff，见 §2.1）。`-m 2/3` 都不拍照、不 init IMP。

| `-m` | 名 | Capture lane | Upload lane | WiFi | 典型用途 |
|------|----|-------------|------------|------|---------|
| **0** | CAPTURE_ONLY | 有 | **无** | **关**（离线） | 省电拍照/录影，文件留 SD，下次 boot 传 |
| **1** | CAPTURE+UPLOAD | 有 | 有（首个 Capture 完成后才调度，之后并发） | 开 | 拍完即传 |
| **2** | UPLOAD_ONLY（**lean**） | **无** | 有（扫 `/tmp/media/` quickSnap 产物） | 开 | 上传 quickSnap 本次拍的片；**无卡可跑、无重传**（§2.1） |
| **3** | HEARTBEAT（**lean**） | **无** | **无** | 开 | 单次心跳上报在线；**不碰 /tmp、不上传**（§2.1） |

> 说明：`-m 0` 离线 → 无 NTP，时间只靠 RTC/MCU（见 §6.3）。

### 2.1 lean 启动模式（`-m 2` / `-m 3`，无卡 / 无 DB）

> 来源：grill 2026-07-09（决策见 §12.2）。ZL 上 wm 由 quickSnap 拉起只跑 `-m 2`/`-m 3`，二者均**不拍照、不 init IMP**（IMP 懒初始化经 `sharedVideo()`，仅 capture 触发；m2/m3 跳过 `CaptureLane` → 不触发）。

**为何 lean**：quickSnap 不用 DB、不依赖 SD（片落 `/tmp/media/` RAM）。下游 m2/m3 同理——**无卡可工作 = 必要功能**；SD 仅用于「想长期保留照片」的场景（本次不做保留，follow-up）。DB（SQLite）只服务 capture（m0/m1 写元数据/缩略图）及其他产品形态；m2/m3 不 capture → 不需要 DB。lean 触发**按模式**（m2/m3），非配置开关——因 DB 需求 = capture 需求，是架构属性。

**lean 启动差异表**（`commonStartup`，vs m0/m1）：

| 步骤 | m0/m1（保持现状） | **m2/m3（lean）** |
|---|---|---|
| S2 `DatabaseManager::init` | 执行（capture 写元数据/缩略图） | **跳过**（上传链路 DB-free） |
| S3 `MediaScanner` | 执行 | **跳过**（`cfg.skipMediaScanner=true`） |
| S11 `mountSDCard` | fatal（DB 在 SD 上） | **non-fatal**：尝试挂、失败记 warning 继续；挂上则 SD 可用于日志 |
| factory/update config | 执行 | **跳过**（`skipFactoryConfig`/`skipUpdateConfig`——lean 避免访问 SD） |
| log file | `/mnt/sdcard/logs/app.log` | SD 在→同左；**SD 缺→回退 `/tmp/wm.log`**（m2/m3 短命，/tmp 够） |

> config（`MS_IP`/`MS_PORT`/`PID`/NTP）在 flash `/config/htc/config.ini`（与 quicksnap.json 同目录），**无卡也能读** → lean 可连服务器。

**`-m 2`（UPLOAD_ONLY，lean）行为**：
1. lean 启动（上表）。
2. **ingest**：读 `/tmp/media/info.json`（quickSnap manifest：`{files:[..], dir:"<ts>"}`）→ 造 desc（`device.PID` 来自 flash config；`file_inf[].F_FilePath`=`/tmp/media/<dir>`、`F_FileName`=manifest 文件名；转换调 `manifest::createDescInfoFile`——不复用 processCmdSnap，/tmp→SD 搬移作废）→ 写到 UploadTask 扫描目录（`/tmp`）。
3. `WmScheduler.run()` → UploadTask（slot 2）扫 `/tmp` → `uploadOneDesc` 传 desc + 媒体（协议同 §8）→ 成功后删文件 → idle-grace → poweroff。
4. **不 init IMP、不扫 SD、不写 DB、无跨 boot 重传**（无卡→无持久层→失败即丢，产品取舍，可接受）。

**`-m 3`（HEARTBEAT，lean）行为**：
1. lean 启动（上表）。
2. connect + auth mgmt（`MS_IP`/`MS_PORT`，flash config）。
3. **单次** `sendHeartbeat()` → poweroff。
4. **不碰 `/tmp`、不上传、不 init IMP**。「周期性心跳」= **MCU 周期唤醒**设备（每次唤醒 = 1 boot = quickSnap → `wm -m 3` → 单次心跳），非单 boot 内 loop。m3 **不走 capture/upload slot 模型**，heartbeat 作为 scheduler 的一个新「动作」，套薄 scheduler 外壳以统一 signal/poweroff/§7 MCU 回写尾序。

**协议约束**：server **强制要求 desc 元数据**（[`upload-protocol-spec.md`](upload-protocol-spec.md) §5：先传 desc JSON 再传 file）——故 m2 必须从 manifest 造 desc 再传，不能只传裸媒体。

---

## 3. 架构：任务调度器

wm 内核 = 一个**task slot 调度器**（非旧 EventLoop 的平移，是新编排）。详细机制见
[`wm-task-slot-scheduler.md`](wm-task-slot-scheduler.md)；该文档是 §3 的展开规格。

### 3.1 结构

- **slot 1 / Capture task**：拍照、录影、拍照+录影都作为一个原子 task，type=1。产出落盘后**不 enqueue 任何 upload 对象**。
- **slot 2 / Upload task**：上传 task，type=2，**新建 wm 私有 `UploadTask`**（移植 `UploadWorker` 的 lazy connect/auth + per-desc 上传逻辑；**不复用、不修改** legacy 共享的 `UploadWorker`——后者仍服务于 `htc_workmode_app`）。`UploadTask` 自扫 SD 取 desc。
- **`SlotOutputPort`（signal）**：scheduler wrapper 持有的唤醒信号通道（`mutex`+`cv`+计数）。slot 1 收尾后由 wrapper push 一个 wake token（**不携带路径**）；空闲等待中的 upload task 被 wake 后重扫 SD 取真实 desc。真实数据源是 SD 上 `F_UploadedTag==0` 的 desc 文件（持久、跨 boot resume），故 port 退化为唤醒信号而非数据通道——capture task 不直连任何 upload 对象。
- **Shutdown task**：special terminal flow，不放入普通 slot；进入后 lock 所有 slot。

### 3.2 task 类型可扩展

task 按**类型**分类（接口/标签），便于未来加类型（如「日志上报」「固件检查」）。当前普通 task type 为 Capture=1、Upload=2；Shutdown 是终态流程。

### 3.3 调度规则

1. 不同 type 的 task 放入不同 slot；每个 slot 同时最多一个 task。
2. task 执行完成后从 slot 移除。
3. `-m 0` / `-m 1` 初始启动 slot 1；`-m 2` 初始启动 slot 2。
4. `-m 1` 中，slot 1 首个 task 收尾 → wrapper 向 `SlotOutputPort` push 一个 wake token → scheduler 触发 slot 2 → `UploadTask::start()` 起即扫 SD 取真实 desc。后续拍（slot 2 已运行）时 wake token 的作用是唤醒空闲等待中的 upload 重扫。
5. slot 1 收尾后**只**经 `SlotOutputPort` 推 wake token（signal）；真实 desc 由 upload task 扫 SD 自取。port push 必须发生在 `op=task_state type=1 ... to=Done` 之后。
6. 外部 trigger 只在 slot 1 为空时创建新的 type=1 task；slot 1 非空则忽略本次 trigger。
7. type=2 task 结束前必须确认（三者原子）：slot 1 空、upload parked（扫描无 `F_UploadedTag==0`、无在途）、且 port 无未消费 wake token。slot 1 非空 或 port 有未消费 token 时即使 upload 暂时空闲也不能结束（等上游落定，避免漏传 desc）。
8. 所有 slot 为空并持续 idle-grace 后，lock 全部 slot 并进入 Shutdown。
9. 每个 task 可有超时（见 §3.4）。
10. **slot 2 忙时不再新建 UploadTask**（`scheduleUpload` 在 slot 2 非空/locked 时直接返回 false，**不**先 `factory()` 造临时 task）。scan-driven 下，在跑的 upload 会经 capture-Done 的 wake token 自扫到新 desc，无需新建。**坑**：若先造临时 UploadTask 再被 `put` 拒绝，其析构 `~UploadTask→stop()` 会 `signalStop` **共享** wakePort，误杀正在 park 的前一轮 upload 线程 → 新 desc 漏传、slot 卡 Running 熬到 upload-timeout（多 trigger 并行 capture 时复现）。双保险：`scheduleUpload` 先判 slot 忙（不造临时）+ `UploadTask::stop()` 仅在 `worker_.joinable()`（确实 start 过）时 `signalStop`+abort+join——从未 start 的临时实例不碰共享 port；而 Done 态（poll 标完成、worker 仍 parked）必须 signalStop 唤醒以便 join，**不能**按 state==Running 门控（否则 join 永久阻塞 → tick 卡 slot.clear → 永不关机，2026-06-28 回归）。

#### 3.3.1 Slot 阻塞可观察性

每个 slot 暴露 `SlotBlockedReason`：

| 值 | 触发条件 |
|---|---|
| `None` | slot 正常运行（`Running`）或空且无新触发 |
| `WaitingUpstream` | type=2 slot 在 `Running`、upload 内部空闲（parked）但 slot 1 非空 或 port 有未消费 wake token（capture 刚产活未扫到）→ upload 被占住等上游落定，不能 Done |
| `Idle` | slot 空且无新触发、未进入 shutdown |
| `Cleanup` | slot 正在 `stop()` 阻塞等待 task 清理完成 |
| `Locked` | shutdown 已触发，slot lock 拒绝新触发 |

`blockedReason(type)` 是只读查询；runtime 通过 `[wm] op=slot_blocked` 日志对外暴露同序信息。完整定义见 [`wm-task-slot-scheduler.md`](wm-task-slot-scheduler.md) §1。

### 3.4 Shutdown 触发（自管关机）

**关机不由外部触发**（MCU 强制关机是另一条 override 流程，见 §3.6）。wm 自管：

- **idle-grace**：slot 1 **且** slot 2 **全空，并持续 `G` 秒**（`HTC_WM_IDLE_GRACE_MS`，默认 30000），才 ready Shutdown task。
  - 全空期间有新触发/新 upload → 取消计时、续命。
- **Upload 超时**：Upload task 自带 `HTC_UPLOAD_TIMEOUT_MS`（默认 60000，**activity-based**：timeout = `now - lastActivityMs_`，后者在每次有进展时刷新——传完 desc / 被 capture-Done 的 wake token 唤醒 / connect 成功；connect grace 30s 仍从 `start()` 起算保护慢 auth）。即「**无进展 60s 才超时**」——多 trigger 长寿命 upload 只要持续有进展就不超时，真卡死（connect-fail 空转、无 didWork 无 token）才超时。没传完 → `Power::requestShutdown()`（SIGTERM 中断 UploadTask 阻塞 I/O；该 desc `F_UploadedTag` 保持 0，下次 `-m 2` 重传）→ signal 路径进 Shutdown。
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
4. **upload desc**：`F_UploadedTag=0`（给 Upload lane）。**desc 文件名 = 对应媒体 basename（仅换扩展名为 `.json`）**：拍照 `<ts>_1.json` ↔ `<ts>_1.jpg`、录影 `<ts>.json` ↔ `<ts>.mp4`（`fileStem()` 从媒体路径推导，grill 2026-06-28——录影 desc 曾用「落盘时刻」`formatNow()` 命名，与 mp4「开始时刻」对不上，现统一为媒体同名）。desc→媒体的服务端关联仍靠 `file_inf.F_FileName`（确切名），文件名同名仅为人工/调试可读。

> **上传后清理（capture-upload-forget，grill 2026-06-28 拍板）**：Upload task 整体上传成功（desc 自身 + 全部媒体都到服务器）后，**删除 desc 和已传媒体文件**——wm 不在 SD 留存已传内容，也止 `media/upload/` 目录无界增长（否则慢速 NFS 上每轮扫描要重读所有累积 desc，拖慢「上传完→关机」）。部分成功（有媒体未传）保留 desc，下次扫描按 `file_inf` 的 `F_UploadedTag` 重传。wm 私有 `UploadTask` 不走旧 `FileManage` 配置门（始终删）；legacy `UploadWorker`/`WorkModeRunner` 仍受 `FileManage` 配置控制，不动。

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

**新建 wm 私有 `UploadTask`**（`src/app/workmode/upload_task.cpp`）作为 type=2 task；lazy connect/auth + per-desc 上传逻辑移植自已验证的 `UploadWorker`（`src/app/workmode/upload_worker.cpp`），但**不复用、不修改** `UploadWorker`——后者是 legacy `htc_workmode_app` 也用的共享服务，保持原状。capture task 不再 enqueue 任何 upload 对象。

- **m1**：`UploadTask` 自扫 `MEDIA_UPLOAD_PATH`（`app.h:27`，`SD_CARD_PATH/media/upload/`）下 `F_UploadedTag==0` 的 desc.json（capture 新鲜产物 + 跨 boot resume 积压）。
- **m2（lean）**：扫描目录指向 `/tmp`（ingest 把 desc 写到 `/tmp`）；启动时 ingest `/tmp/media/info.json` → 经 `manifest::createDescInfoFile` 造 desc（`F_FilePath` 指 `/tmp/media/<dir>`），复用 `uploadOneDesc` 上传 desc + 媒体。**不扫 SD、无跨 boot 重传**（§2.1）。⚠️ 当前扫 `/tmp` 根偏宽（follow-up `T19-RV-m2-tmp-scan-broad`：收窄到专用子目录）。
- 空闲时 `wait()` 在 `SlotOutputPort`（signal）上，被 capture-Done 的 wake token 唤醒后重扫。
- 上传 desc 本身 + `file_inf` 里的媒体文件，走 **TCP mgmt/storage server**（`MS_IP`/`MS_PORT`，**非 HTTP**，自定义二进制帧协议，`StorageServClient.cpp:121-218`）。
- **「未处理」= `F_UploadedTag==0`**（desc JSON 标志，desc 级 + 文件级，**非 DB 字段**）；传成功回写 tag=1，可选按 `FILE_MANAGE` 删源文件。
- 跨 boot 持久化 = desc 文件留 SD（未传完的 `F_UploadedTag` 保持 0，下次 `-m 2` 重传）。
- 超时：activity-based（`now - lastActivityMs_`，进展刷新；connect grace 30s 从 start 起算），§3.4。

> devtest 无 mgmt/storage 后端 → 上传必失败。HW 验证只能断「`UploadTask` 启动 + 干净失败 + 无 crash/hang」，**不能断上传成功**（与 manifest B2 一致）。

---

## 9. 配置旋钮

| 旋钮 | 来源 | 默认 | 说明 |
|------|------|------|------|
| `cameraMode` / `burstNumber` / `stillSize` / `videoSize` / `videoLength` | `setting.json`（产品配置） | — | 拍/录行为 |
| `HTC_WM_IDLE_GRACE_MS` | env | 2000 | idle-grace G（§3.4）。默认 2s：上传完即关机，不做 PIR 合并窗口（后续 PIR 走完整冷启流程；grill 2026-06-28 拍板，原 30s coalescing 语义废止） |
| `HTC_UPLOAD_TIMEOUT_MS` | env | 60000 | Upload task 超时（§3.4） |
| `HTC_WM_ONE_SHOT` | env | 0 | 1=首个 Capture 后屏蔽触发（§3.5） |
| `HTC_SIM_PIR_INTERVAL_MS` | env | 10000 | SimPir 间隔（sim/test，§4） |
| `HTC_SIM_PIR_COUNT` | env | 0 | SimPir 触发次数（0=无限；>0 触发 N 次后停，建模「PIR 停」以测多 trigger robustness + 关机路径。注意 fired 计数，非 accepted——要 N 个独立 capture 需间隔 > 单次 capture 时长，§4） |
| `HTC_TEST_NO_POWEROFF` | env | 0 | 1=关机走 `_exit(0)`（devtest，§1） |
| `HTC_WM_CONFIG_FILE` | env | unset | 真机调试时强制 wm 使用指定 config.ini；否则若 `/config/htc/config.ini` 缺失/测试 PID 且 `/mnt/huntcam/config.ini` 有真实 PID，wm 会自动切到 `/mnt/huntcam/config.ini`，与 `upload_test` 对齐 |
| NTP server / `MS_IP` / `MS_PORT` | config ini（`server` 段） | — | NTP 与上传服务器 |

> 原则：**产品配置走 setting.json，运行时/调试旋钮走 HTC_\* env**（与现有 `HTC_UPLOAD_TIMEOUT_MS`/`HTC_TEST_RECORD_COUNT` 一致）。

---

## 10. 模块复用与验证状态

| 模块 | 复用源（file:line） | HW 验证 | 用途 |
|------|---------------------|--------|------|
| record（录影+缩略图+DB） | `VideoRecorder.cpp:981`（DB）、`:1171`（缩略图） | ✅ GREEN | Capture lane（录影） |
| snap（拍照+缩略图+DB） | `ImageSnap.cpp:393`（DB）、`ImageSnap::capture_thumbnail` | ✅ GREEN（`snap_test`） | Capture lane（拍照） |
| upload | 新 `upload_task.cpp`（移植自 `upload_worker.cpp`）/ `StorageServClient.cpp` | ✅ GREEN（仅干净失败，源自 UploadWorker） | Upload lane（wm 私有 UploadTask） |
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

### 12.2 决策日志（grill 2026-07-09，quickSnap handoff + lean 模式）

| # | 决策点 | 结论 |
|---|--------|------|
| 13 | quickSnap handoff | wm 由 quickSnap `fork+execv` 拉起为 `wm -m <2\|3>`（无 `-rtc`）；quickSnap 拍所有片落 `/tmp/media/` |
| 14 | m2/m3 不 init IMP | 现状已符合（跳 `CaptureLane`→不触发 `sharedVideo`），仅验证不改 |
| 15 | 无卡可工作 | **必要功能**；`mountSDCard` 对 m2/m3 降级 non-fatal；SD 仅用于日志/保留（保留 follow-up） |
| 16 | 无 DB 支援 | m2/m3 跳 DB init + MediaScanner（上传链路 DB-free）；DB 只给 capture/其他产品形态 |
| 17 | lean gating | **按模式**（m2/m3），非配置开关（DB 需求 = capture 需求） |
| 18 | m2 上传源 | `/tmp/media/info.json`→desc，UploadTask 扫 `/tmp`；**不扫 SD、无重传**（失败可接受丢失） |
| 19 | m3 语义 | connect+auth+**单次** sendHeartbeat+poweroff；周期性 = MCU 唤醒；薄 scheduler 外壳 |
| 20 | m2 不发心跳 | （暂）保持模式职责单一 |
| 21 | `QUICK_SNAP_DIR` 改名 | `/tmp/quick_snap/`→`/tmp/media/`（`app.h` + quickSnap 新码；不碰死引用） |
| 22 | 验收 | 扩 `test_wm_modes_matrix.py`（m3/no-SD//tmp-seed/no-DB）+ sim stub/HW 日志验 IMP-not-init |

---

## 13. 不在范围 / 后续

- 长驻模式（TEST_ONLY / UVC / PIR EventLoop）→ 另一个 usermode app。
- `cameraMode 3`（并发拍录）→ 后续，受 CH2 8M 约束。
- **cameraMode 读点迁 quicksnap.json + Settings 序列化摘 4 字段影子**（quicksnap-app-spec §2.3 γ 闭环）→ **defer**：m2/m3 不读 cameraMode、本 quickSnap 链路无影响；m0/m1 迁移作 follow-up。
- **SD 在场时把照片落一份到 SD 做长期保留**（§2.1「保留」语义）→ follow-up（本次 m2 只走 /tmp）。
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
- [`quicksnap-app-spec.md`](quicksnap-app-spec.md) — 上电首进程 quickSnap（m2/m3 上游 handoff、lean 模式依据）
- [`upload-protocol-spec.md`](upload-protocol-spec.md) — 上传协议（desc 元数据强制；m2 造 desc 的协议依据）
- [`photo-video-concurrent-implementation-plan.md`](photo-video-concurrent-implementation-plan.md) — 并发拍录（cameraMode 3，暂不在范围）
