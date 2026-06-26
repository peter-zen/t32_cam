# 2026-06-26 — wm cm==1 wedge：推翻「CMA 耗尽」假设，重诊为 RAM 耗尽 + zram swap-thrash

> 承 [`2026-06-25-wm-cm1-reproducer-investigation.md`](2026-06-25-wm-cm1-reproducer-investigation.md)（已排除 SDK 流程问题 / wm 特有 IMP 初始化）。
> 上一轮结论：wedge 不在 IMP 流程（rc=0 全过），但 hal_trace 显示**所有** IMP 调用都正常返回 → 必须换假设。
> 本文档记录：**CMA 假设的推翻**、**真实 root cause 的当前定位**、**instrumentation 改造**、以及**未完成的 bringup 验证**。

## 0. 起点 — 上一轮的两个尾巴

`2026-06-25-wm-cm1-reproducer-investigation.md` 留下两件没做：

1. 既然 IMP 流程层全清 → 试「CMA 耗尽」假设：把 cm==1 跑成「系统 CMA 撑不住 → BUG_ON/WARN/hang」的故事。
2. 文档/sample code 里似乎有「`/proc/cmdline` + `/tmp/continuous_mem_info`」的剩余内存测试段 → 确认其位置和用法（用户口头提到）。

## 1. 致命发现：CMA 字段在本机 kernel 上是「死的」

### 1.1 复现步骤

把上次 sample 验证用的 `tests/host/test_wm.py cm=1` 跑一次 wedge，**只抓现场**：

- `cat /proc/meminfo` 期望看到 `CmaTotal: … kB` + `CmaFree: … kB`。
- `mount -t debugfs` / `cat /sys/kernel/debug/cma/` 期望看到 `cma-pool-0` / `cma-alloc` / `cma-free`。
- 同步抓 `dmesg | grep -i cma` / `grep -i cma /var/log/kern.log` 之类。

### 1.2 实测结果（host post-wedge 看 device serial log）

| 来源 | 结果 | 解读 |
|------|------|------|
| `/proc/meminfo` | **无 `CmaTotal:`、无 `CmaFree:` 行** | 该 kernel build **没有 enable CONFIG_CMA_SYSFS**（把 CMA 池暴露到 /proc/meminfo） |
| `/sys/kernel/debug/cma/` | **空目录 / 不可 mount** | 同样没 enable CMA debugfs（`CONFIG_CMA_DEBUGFS`） |
| `dmesg` | 启动期 `cma: Failed to reserve 16 MiB` 重复 **~80 次** | 早期 `cma_declare_contiguous()` reservation 失败 → 整个 CMA 池**没建出来** |
| 上轮 sample reproducer | 同样查不到 CMA 字段 | sample / wm 行为一致，不是 wm 漏检 |

### 1.3 含义 — 整个「CMA 耗尽」假设是**建立在不存在的字段上**

- 上一版 doc（`2026-06-10` 早些时候的某段）写的「`CmaFree 16 MB` 总」是**另一个 kernel build / 另一个设备**的数；当前 .config 已经去掉 `CONFIG_CMA_SYSFS` + `CONFIG_CMA_DEBUGFS`。
- `CmaTotal=0` → 后续所有「`CmaFree=0` 所以耗尽」的论证**前提不成立**。
- 「`cma: Failed to reserve 16 MiB`」出现 80 次 / boot → 早就有 regression：firmware 的 `cma_declare_contiguous(16 MiB)` 失败。**新东西**：可能要解的是这个 reservation 失败本身（用 `cma=` cmdline / `cma_declare_contiguous(0)` / `dma_contiguous_reserve()` 改写），而不是运行时 CMA 调度。

### 1.4 推论

> 「wm cm==1 wedge」 **CMA 是 noise**。
> 真实根因在 **RAM 路径**（普通内存 + zram swap），跟 CMA 池是否耗尽无关。
> 上轮「CMA 耗尽」诊断是**误诊**，必须废止。

## 2. 重新定位 — wedge 现场看得到什么

### 2.1 hal_trace（NFS fsync'd）上的最后几行

- 「`--> IMP_Encoder_GetStream`」前 57 秒 `fopen(MEDIA_TARGET_PATH/...mp4, "wb")` 卡住
- 卡住前后**无 `RC=0`/`RC=-1`** → 不是 IMP 系统挂；是**应用层阻塞在文件系统/页回收**
- watchdog（hardware）到点 → 设备 hard reset
- **无 kernel OOPS / panic / soft-lockup splat**（这与 [wm-cm1-wedge-isp-reenable-crash](file_path:) 那种「ISP OOPS」是不同的故障 —— 这个是「无声卡死」）

### 2.2 当时没看清的字段

| 字段 | 来源 | 含义 |
|------|------|------|
| `MemFree` | `/proc/meminfo` | 完全未分配 + 未缓存的页（**不等于可用**） |
| `MemAvailable` | `/proc/meminfo` | kernel 估算「新申请能不能分到」，**这是真·系统压力信号** |
| `SwapFree` | `/proc/meminfo` | zram 中可淘汰的页数 |
| `SwapCached` | `/proc/meminfo` | 已被换出但还留有映射的（来回 pin 容易爆） |
| `VmSize` | `/proc/self/status` | 进程虚拟地址空间 |
| `VmRSS` | `/proc/self/status` | 进程**实际常驻**页（谁在拿 RAM） |
| `VmSwap` | `/proc/self/status` | 进程被换出到 zram 的页（**主嫌疑**） |
| `buddyinfo` | `/proc/buddyinfo` | 各 zone 各 order 的空闲页 → 连续大块内存可用度 |

### 2.3 上一版 instrumentation 的缺陷

- 只看 `CmaFree`（死字段） + 几个纯 cache 数字
- **没看 `MemAvailable`**（系统级剩余） + **没看 VmSwap**（自身被换出量） + **没看 buddyinfo**（高阶连续页）
- 这就是「诊断工具对不上病灶」。

## 3. 真实假设（当前工作版）

> wm cm==1 = photo（同步返回，CH2 缩略图已落）→ record（开 fopen → MP4E_open → poll first frame）
> photo 阶段结束到 record 阶段起点的几十秒间隙，**日志/上传/缩略图落盘**的并发 I/O 把 dirty 页推满 → kernel 触发 direct reclaim + zram swap-out
> **wm 进程自己的 working set 被换出（VmSwap 飙升）** → 等 record 想 `fopen` 写 MP4 时，**getblk→malloc 高阶连续页**失败 → page allocator 在 `__alloc_pages_slowpath` 走回收链 → 回收链因为 dirty 平衡不到 → **在 `fopen` 里 57 秒** → watchdog 复位

> 旁证（待 bringup 后跑全）：wedge 时 `MemAvailable` 应当掉到 <10 MB 量级（上个 attempt 看到过 6.7 MB） + wm 进程 `VmSwap > VmRSS/2`（自 swap-out）。

> 进一步推论：**`HTC_NO_UPLOAD=1` 把 wedge 推后**（用户在上次会话末提到）→ 因为 upload 是最重的 I/O 段，跳过它就把 swap-out 触发时间推后到 record polling 阶段。**与本假设一致**（upload 触发 swap → fopen 撞上；跳过 upload → swap 触发点后移到 polling 期间）。

## 4. instrumentation 改造

把 4 个文件里的 `logMemInfo` 全部重写为统一版本（snap + record 同模板）：

| 文件 | 调用点 | 作用 |
|------|--------|------|
| `src/app/workmode/snap_task.cpp` | `photo-entry`、`photo-captured` | 拍照阶段前/后 |
| `src/media/video/VideoRecorder.cpp` | `record-entry`、`pre-fopen`、`post-fopen`、`record-loop-entry` | 录影阶段 + **精确卡在 fopen 两侧** |

新版打 4 个数据源：
- `/proc/meminfo` → `MemFree` / `MemAvailable` / `SwapFree` / `SwapCached`
- `/proc/self/status` → `VmSize` / `VmRSS` / `VmSwap`
- `/proc/buddyinfo` → 各 zone 各 order 的整行（看高阶连续页是否塌掉）

打点格式（统一）：
```
MEMINFO[<tag>] MemFree=… MemAvail=… SwapFree=… SwapCached=… kB | VmSize=… VmRSS=… VmSwap=… kB
BUDDY[<tag>] Node 0, zone   Normal  100  50  20   5   0   0   0   0   0   0   0
```

### 4.1 与现有工具的关系

调研发现 **`tools/mem_profile.sh`** 已是设备侧更完整的 memory profile 工具（采 `vmstat allocstall`、`/proc/self/smaps_rollup`、zoneinfo 等），**优于本次手写的 `logMemInfo`**。但：
- `tools/mem_profile.sh` 跑在 device 端 `/mnt/huntcam/tools/` → 不会进 `app.log` → 跟 `hal_trace` 一样是 NFS-side file
- `logMemInfo` 内嵌进 wm 进程 → 自动跟 photo/record 阶段同步打点
- **两个互补**：手写 `logMemInfo` 给「卡住前一刻的精确窗口」，`tools/mem_profile.sh` 给「更全面的字段但需要单独起一个 shell loop」

下次 bringup 起来后**优先跑 `tools/mem_profile.sh` 30 秒采样 + 同步 wm 走 cm==1** → 一份完整的 memory trajectory。

### 4.2 其它 gates（未改）

这些是前几轮已加的 cm==1 bisect 门，**不在本轮 commit 改的范围**，但列出来方便读者理解当前 instrumentation 全貌（统一编入 `<<<cm==1 bisect>>>` 前缀 log）：

| Env | 文件 | 作用 |
|-----|------|------|
| `HTC_HAL_TRACE=1` | `src/hal/ingenic/IngenicVideo.cpp` | 把每个 IMP 调用的 enter/exit rc 都 fsync 到 NFS 端 `hal_trace.log`（卡住前最后一行就是 wedge 调用） |
| `HTC_HAL_TRACE_PATH=…` | 同上 | 自定义 trace 路径（默认走 `/mnt/huntcam/logs/`） |
| `HTC_NO_MULTIPROCESS=1` | 同上 | 跳过 `IMP_Encoder_MultiProcessInit` —— 用于验证 init-prefix 是不是 ISP re-enable wedge 的诱因（来自 [wm-cm1-wedge-isp-reenable-crash](file_path:) 的开放假设） |
| `HTC_NO_UPLOAD=1` | `src/app/workmode/snap_task.cpp` | 跳过 `UploadWorker::enqueue` —— 用户反馈这个跳过后 wedge 推后（与本假设一致：把最重 I/O 段砍掉） |
| `HTC_NO_OSD=1` | `src/hal/ingenic/IspOsdManager.cpp` | 跳过 OSD region 创建 —— 上一轮已经验证 sample-with-OSD 不会 wedge，wm 是否一致待复测 |
| `HTC_SKIP_ACTIVE_GETINFO=1` | `src/media/video/VideoRecorder.cpp` | 跳过 `record()` 开头 `stream_->getInfo`（查到的那个静默 deadlock）|
| `HTC_NO_RECORD_DAYNIGHT=1` | `src/media/video/VideoRecorder.cpp` | 跳过 day/night 流属性切换（之前的偶发崩溃点）|
| `HTC_CM1_RESET=1` | `src/app/workmode/capture_lane.cpp` | photo/record 之间做 `resetSharedVideo()` → `IMP_System_Exit` → record re-init（验证 fresh-session 是否绕开）|
| `HTC_NO_MEDIA_SCANNER=1` | (上轮已 commit) | 跳过 media scan service 启动（Path B bisect 的一个开关） |

## 5. /proc/cmdline 和 /tmp/continuous_mem_info 答用户问

- **`/proc/cmdline`**：bootargs，能看到 `cma=16M` / `mem=` / `vmalloc=` / `zram=` 等。在设备端 `cat /proc/cmdline` 即可，**不在 repo**。本机上次的 `cma: Failed to reserve 16 MiB` 已经**说明** `cma=16M` 存在但 reserve 失败。
- **`/tmp/continuous_mem_info`**：**不是** repo 文件，是设备 runtime 由 firmware 周期性 cat 出来的连续 memory snapshot（连续若干秒的 `MemFree/CmaFree`）。**当前 device boot 现场没看到这个文件**（可能 firmware 端没启动这个 service，或 boot 太早还没轮询到），**等下次 bringup 后第一时间抓**。
- 仓库里跟 memory test 相关的文档和工具：
  - `doc/knowledge/playbooks/t32-memory-profile-and-debug.md`（memory profile 完整 playbook）
  - `tools/mem_profile.sh`（设备端 memory sampler）
  - **不在 `samples/` 下**，**不在 `tests/host/` 下**

## 6. 状态

### 6.1 已完成

- [x] 推翻 CMA 假设（实测：kernel build 没 enable `CONFIG_CMA_SYSFS` / `CONFIG_CMA_DEBUGFS`，CMA 字段全无；启动期 `cma: Failed to reserve 16 MiB` × 80 是已存在的 regression）
- [x] 重新定位为 RAM + zram swap-thrash 在 fopen 阶段
- [x] 改造 `logMemInfo`（4 文件 6 调用点）打 `MemAvail` / `VmSwap` / `buddyinfo`
- [x] 双平台编译（sim + T32）通过
- [x] 跟用户确认 `tools/mem_profile.sh` 是 canonical 工具

### 6.2 未完成（被本轮 user 决定转成「先 commit 再做」）

- [ ] **`devctl bringup`** —— 设备 cold-boot 后 devctl 跑了 3+ 次 timeout（network daemon 的 wpa_cli / SystemCall_Dbus 在 serial 上吵，broker 抓不到 sentinel）。需要 `HTC_WIFI_PWD=<pwd> tools/devctl/devctl bringup` 拉起 NFS 才能：
  1. md5-verify 新 .so 真的在跑（[CLAUDE.md](file_path:) 强调的 trap）
  2. 抓 `tools/mem_profile.sh` × 30s + wm cm==1 同窗口的完整 memory trajectory
  3. 抓 `/tmp/continuous_mem_info`（如果存在）
- [ ] `buddyinfo` 高阶列在 fopen 期间是否塌到 0 的直接证据
- [ ] `HTC_CM1_RESET=1` 在新 instrumentation 下能否让 cm==1 跑通
- [ ] `cma: Failed to reserve 16 MiB` 80 次的来源（kernel cmdline `cma=16M` reservation？`cma_declare_contiguous` 调用点？）

### 6.3 文档承诺

按用户要求：**当前状态落到本 review doc，所有 instrument commit & push**；下轮 devctl bringup 通后接着填 §6.2。
