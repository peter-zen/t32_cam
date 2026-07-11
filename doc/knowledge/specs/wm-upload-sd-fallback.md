# wm upload SD fallback（上传失败落卡 + 下次启动续传）

> 用途：定义 `wm -m 2`（UploadOnly，quickSnap handoff）上传失败时的 **SD 落卡兜底** 与 **下次启动续传** 机制。
> 当前实现：待落地。触点见 §5（`src/app/wm_app.cpp` / `src/app/workmode/wm_scheduler.cpp` / `src/app/workmode/wm_sweep.{h,cpp}` / 新增递归移动工具）。
> 上游 spec：[`wm-app-spec.md`](wm-app-spec.md)、[`wm-task-slot-scheduler.md`](wm-task-slot-scheduler.md)、[`quicksnap-app-spec.md`](quicksnap-app-spec.md)、[`upload-protocol-spec.md`](upload-protocol-spec.md)。

## 1. 背景：要补的缺口

quickSnap（boot 首程序）在 SD 未 ready 时把拍录产物写到 **tmpfs** `/tmp/media/<ts>/`（`<ts>` = `YYYYMMDD_HHMMSS`），随后 fork `wm -m 2 -d /tmp/media/<ts>/` 上传。

`UploadTask`（`upload_task.cpp`）成功即删 desc + 媒体（capture-upload-forget）；部分失败保留 desc（按 file 粒度 `F_UploadedTag`），同 boot 内重扫续传。

**缺口**：tmpfs 是 RAM，reboot 即清。上传因网络差超时（task 被杀 → shutdown）时，`/tmp/media/<ts>/` 下未传完的产物在下次启动已消失 → `wm_sweep`（`wm_sweep.cpp`，扫 `/tmp/media/` 兜底）空手而归 → **数据丢失**。

本机制：超时放弃时把 tmpfs 工作目录**落 SD**，下次启动在本次 `-d` 传完后**续传 SD 上的滞留目录**。

## 2. 既有超时机制（本机制复用，不新增）

- 上传 task 超时 = **activity-based**：`timeoutAgeMs = now - lastActivityMs_`（`upload_task.cpp:93`）。`lastActivityMs_` 在「传完 desc / 被 wake token 唤醒 / connect 成功」时刷新；connect grace 30s 从 start 起算。
- 无进展达 `uploadTimeoutMs` → `tick` 置 `events.uploadTimedOut` → `requestShutdown(UploadTimeout)`（`wm_task_scheduler.cpp:160`）→ wrapper `Power::requestShutdown()`（SIGTERM）+ break（`wm_scheduler.cpp:241`）。
- 媒体上传本身 `while(!isUploadFinished())` 无界（`upload_task.cpp:334`），但网络差 → `ensureConnected()` 失败 → `didWork=false` → 自然走到超时。**本机制的超时即此 task 超时，默认调为 120s**（见 §4 Q4）。

## 3. Model / 流程

### 3.1 Boot N（网络差，上传失败落卡）

1. quickSnap → `/tmp/media/<ts>/`（tmpfs）→ fork `wm -m 2 -d /tmp/media/<ts>/`。
2. wm commonStartup S11 尝试 `Misc::mountSDCard("/mnt/sdcard")`（lean：失败不阻断，继续；`ProcessLifecycle.cpp:477`）。
3. wm_app 构建 `workDirs` = `[< -d 归一化 >, …SD 滞留（本 boot 通常空）]`（§5.2）。
4. UploadTask 扫描；网络差 → `ensureConnected()` 失败 → `didWork=false` → park → **120s** 无进展超时。
5. `tick` → `uploadTimedOut` → `Power::requestShutdown()`（SIGTERM）→ break。
6. `run()` 出循环后：`scheduler.stopAll()`（join upload 线程，文件静默）→ **persist 钩子**（仅 `reason==UploadTimeout` 且 `HTC_WM_SD_FALLBACK≠0`）：retry-mount SD → move `/tmp/media/<ts>/` → `/mnt/sdcard/media/<ts>/`（§5.3）。
7. wm_app 尾序：writebackMcu → flush → `Misc::poweroff()`（`wm_app.cpp:409-416`）。

### 3.2 Boot N+1（网络好，本次传完后续传 SD 滞留）

1. quickSnap → `/tmp/media/<ts2>/` → `wm -m 2 -d /tmp/media/<ts2>/`。
2. S11 mount SD（成功）。
3. `workDirs` = `[<ts2>（-d，[0]）, …sorted SD 滞留 = /mnt/sdcard/media/<ts>/]`。**`-d` 必在 [0]**（§4 ordering 修正）。
4. 单个 UploadTask 按 dir 顺序：先 drain `dir[0]`（`-d`，`scanAndUploadOnePass` 每 pass 把 dir[0] 的 pending desc 全做完才进 dir[1]）→ 整目录删除 → 进 `dir[1]`（SD 滞留）→ drain → 整目录删除 → park → idle-grace → poweroff。

> `-d` 优先由 dir 顺序保证；SD 滞留的 desc 已随落卡时一并移入，`ensureWorkDirDesc` 复用（保 `F_UploadedTag`），`exclusiveWorkDirs=true` 对 tmpfs / SD 两类工作目录一视同仁地「全成功即 removeDirectory」。**调度器 / UploadTask 零改动**。

## 4. 决策日志（2026-07-11 grill 定型）

| # | 决策 | 理由 |
|---|------|------|
| Q1 | **触发 = 既有 task 超时**（activity-based，调 120s）。放弃 → 落卡 → shutdown；续传仅在本次 `-d` 成功后。 | 不新增 per-file 超时；shutdown-sweep 一条路径覆盖所有 give-up 路径。 |
| Q2 | **移动单个 `-d` 目录**：`/tmp/media/<ts>/` → `/mnt/sdcard/media/<ts>/`（与 m1 扫描目录 `upload/` 同级）。 | `/tmp/media/` 下永远只有 `-d` 一个目录（用户确认）；`isTimestampDir` 过滤天然排除 `upload/` 兄弟，复用 `collectStrandedWorkDirs`。 |
| Q3 | **仅在 `UploadTimeout` 落卡**；落卡前 **retry-mount SD**，mount 仍失败则记错放弃（tmpfs reboot 即失，无可挽回）。 | 用户明确 scope 到超时；Signal/中断路径不落卡（按用户取舍）。 |
| Q4 | **单个合并 dir 的 UploadTask**（不拆两段）。 | 正常两路径（网络差超时 / `-d` 成功后续传）行为与两段完全等价，省去 factory/phase 状态机。 |
| Q5 | **仅 clean-timeout**（非 crash-resilient）。 | crash-resilient 需 capture 即写 SD，推翻 quickSnap→tmpfs 的 boot-phase 前提；零崩溃由 wm/um 稳定化 track 兜。 |
| Q6 | **碰撞 → 后缀** `_2/_3` + 放宽 resume 过滤；**ENOSPC** → 清半成品 + 记错；**开关** `HTC_WM_SD_FALLBACK`（默认开，"0" 关，同时 gate persist+resume）；**超时** 120s。 | 后缀无损、放宽正则对 tmpfs-only 目录安全；开关镜像 `HTC_WM_SWEEP_STRANDED` 模式。 |

### 4.1 ordering 修正（实现期必守）

`wm_app.cpp:376` 现有 `std::sort(workDirs)` 升序（老先传）。`/tmp/media/` 兜底实际恒空（用户确认），故排序当前是 no-op；但**一旦并入 SD 滞留**（来自历史 boot，`<ts>` 更老），全局升序会把滞留排到本次 `-d` 之前 → **先传 backlog 再传本次**，违反「本次先传」契约。

**修法**：`-d` 恒定 [0]，仅对 backlog 排序：

```
workDirs = [ normalize(-d) ,
             ...sorted( collectStrandedWorkDirs("/tmp/media/", -d)
                      ∪ collectStrandedWorkDirs("/mnt/sdcard/media/", -d) ) ]
```

`ensureWorkDirDesc` 照旧对每个 dir 跑一遍。

## 5. 触点

### 5.1 超时默认
- `wm_app.cpp:336` `int64_t uploadTimeoutMs = 60000;` → **`120000`**（env `HTC_UPLOAD_TIMEOUT_MS` 仍覆盖）。
- 不动 `wm_task_scheduler.h:74`（struct 默认，wm 路径显式覆盖、从不取它）与 `workmode_app.cpp:222`（legacy）——surgical。

### 5.2 续传 dir 列表（`wm_app.cpp:365-377`）
m2 构建 `workDirs` 处：保留 `-d` [0]；追加 SD 滞留 `collectStrandedWorkDirs("/mnt/sdcard/media/", quicksnapDir)`；仅对 backlog 段排序；整体 dedup；`ensureWorkDirDesc` 每目录。受 `HTC_WM_SD_FALLBACK` gate（关时不追加 SD 滞留）。

### 5.3 落卡钩子（`wm_scheduler.cpp`，`run()` 出循环、`scheduler.stopAll()` 之后）
新增私有方法（拟）`WmScheduler::persistStrandedTmpDir()`，签名/语义：
- 仅当 `scheduler.shutdownReason() == UploadTimeout` 且 `HTC_WM_SD_FALLBACK != "0"`。
- `Misc::mountSDCard("/mnt/sdcard")` retry；失败 → 记错 return。
- 对 `workDirs_` 中前缀为 `QUICK_SNAP_DIR`（`/tmp/media/`）且**仍存在于盘上**的目录（= 未被成功上传删除的 tmpfs 工作目录，按用户确认即 `-d` 那一个）：递归 move 到 `/mnt/sdcard/media/<basename>/`。
- 碰撞（目标已存在）：basename 追加 `_2`/`_3`… 直至不冲突。
- ENOSPC/写失败：`removeDirectory` 清半成品目标，记错，return（tmpfs 源不动，reboot 即失）。

### 5.4 resume 过滤放宽（`wm_sweep.cpp:28`）
`isTimestampDir` 正则语义由 `^\d{8}_\d{6}$` → `^\d{8}_\d{6}(_\d+)?$`（手写实现：原 15 字符校验后，可选地接受 `_<digits>` 尾巴）。tmpfs-only 目录仍匹配，安全。

### 5.5 新增工具
- 递归 move（跨 fs：tmpfs→vfat，`rename(2)` 返 `EXDEV` → 递归 copy+delete）。仿既有 `Misc::removeDirectory`，**禁 fork-exec**（OOM-safe），放在 `common/misc/Misc` 或 `app_workmode` 命名空间下。

## 6. 边缘处理汇总

| 场景 | 处理 |
|------|------|
| 落卡时 SD 未 mount | retry-mount；仍失败 → 记错、不落卡（reboot 丢失，无 SD 无解） |
| 目标 `<ts>` 已存在（历史滞留 + 同秒重打戳） | basename 追加 `_2/_3`；resume 过滤放宽以识别 |
| SD 满 / copy ENOSPC | 清半成品目标、记错、return；tmpfs 源 reboot 即失 |
| 部分 desc 上传（部分 file tag=1） | move 原样保留 desc 的 per-file `F_UploadedTag`；resume 只传 tag=0、跳 tag=1（媒体已删） |
| 硬 crash（segfault/OOM/watchdog） | persist 钩子不跑 → 本次 capture 丢（非目标，见 §7） |
| lean boot SD mount 失败 | 本 boot 不扫 SD 滞留（listSubdirectories 空集，不崩）；推迟到 SD 可 mount 的 boot |

## 7. 非目标 / 假设

- **非 crash-resilient**：仅 clean `UploadTimeout` 路径落卡。硬 crash 丢本次 capture；零崩溃由 wm/um 稳定化 track 兜（见 [`wm-app-spec.md`](wm-app-spec.md)）。
- **scope = m2**（UploadOnly，quickSnap handoff）。m1（CaptureUpload，desc 直写 SD `/mnt/sdcard/media/upload/`，失败本就跨 boot 存活）、m3（Heartbeat，不上传）不在内。
- **tmpfs 假设**：`/tmp` 为 tmpfs（RAM），reboot 即清 —— 这是本机制存在的前提（quickSnap spec ④ `/tmp` tmpfs 上限为 open 实测项，与本机制正交）。
- resume 扫描在 wm_app 构建 `workDirs` 时（S11 mount 之后）**一次性**完成；upload 期间 SD 滞留集合静态（本 boot 仅落卡时写 SD，而落卡发生在 shutdown 后、不再上传）。

## 8. 验证（待 L2 真机）

- **sim**：mock 网络（`ensureConnected` 失败）→ 断言 `/tmp/media/<ts>` 被 move 到 `sim_sdcard_runtime/media/<ts>`；下次 sim boot 断言 `workDirs` 含 SD 滞留且 `-d` 在 [0]；mock 网络恢复 → 断言滞留 drain 后目录删除。
- **真机 m2**：① 故意断网 → 120s 超时 → 落卡 → poweroff；② 复网重启 → 本次 `-d` 传完后续传 SD 滞留 → 目录清空 → idle-grace 关机。
- **碰撞**：人为预置 `/mnt/sdcard/media/<ts>/` 再制造同 ts 落卡 → 断言落 `<ts>_2` 且 resume 能扫到。

## 9. 治理

- 真相源 = 本文件 + 代码。`wm-task-slot-scheduler.md` §1/§4（UploadTask / SlotOutputPort）不变；本机制只在其外围加 persist 钩子与 workDirs 构造变化，调度内核零改动。
- 落地后回写 `doc/knowledge/working-set.md`（新增 active track 或并入 wm-app track）+ `reviews/<date>-wm-upload-sd-fallback.md`。
