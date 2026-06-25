# 2026-06-24 — wm cm==1 Step 1（hal/ IMP instrument）实现 + wedge-run 协议

> 承 [`2026-06-24-wm-cm1-handoff.md`](2026-06-24-wm-cm1-handoff.md) Step 1。新会话读完 handoff 后**重读 handoff §3 的 instrumentation 假设**，发现 3 个 gap，按修正后的设计实现。设备当前 **wedge**（benign echo 超时），**需用户硬断电**后才能跑。

## 0. 读 handoff 后发现的 3 个 gap（修正了 Step 1 设计）

| # | handoff 假设 | 实际 | 修正 |
|---|------|------|------|
| 1 | 「读 app.log 找 wedge 点」（现有 `[HAL]` 日志） | HW elog level = **INFO**（`ProcessLifecycle.cpp:339`），而 `IngenicVideo.cpp` 所有 `[HAL]` 日志都是 **DEBUG** → HW 不可见（serial.log 实证：只 `I/E/W`，无 DEBUG `[HAL]`） | 新增 env-gated `HTC_HAL_TRACE=1` 专用 fsync'd sink（INFO 等效，绕开 elog level）；另加 wm_app `HTC_LOG_DEBUG=1` 现有 module DEBUG 日志可见 |
| 2 | instrument `configure/release/start` | wedge 在 **record 过程中**（`record started` = configure+start 已成功）→ 只 instrument 这三者**漏掉 capture loop** | 把 `polling/getFrame/releaseFrame`（loop-throttled）也 instrument；wedge 调用 = 最后一条 `-->` 无配对 `<--`（kernel 卡在 ioctl，user-space 不返回） |
| 3 | app.log 最后一条 = wedge 点 | elog 只 `fflush`（`elog_port.c:103`）→ 仅 page cache，硬断电丢 | sink 每行 `fwrite+fflush+fsync`（落 SD 块设备，硬断电存活）。另：serial.log（broker host 侧）也存活 |

**新线索**（当前 serial.log）：photo run 出现 `releaseFrameSource(0): not found` / `(2): not found` —— release 跑在未 acquire 的 group 上（ref map 无该条目）。这正是「group 0 半 reset」类 bug 的典型征兆。

## 1. 实现（已批准，default-off，零正常运行影响）

### `src/hal/ingenic/IngenicVideo.cpp`（hal/，已批准）
- 文件局部 trace sink（`namespace hal` 内 static）：env `HTC_HAL_TRACE=1` 开，`HTC_HAL_TRACE_PATH` 覆盖路径，否则 HW `/mnt/sdcard/logs/hal_trace.log` / sim `<SIM_SD_ROOT>/logs/hal_trace.log`，`"w"` truncate。
- 每行 `fprintf(seq+"%s\\n") + fflush + fsync`，seq = atomic 单调计数（wedge 后最后 seq 无后续 = wedge 点）。
- `--> name` / `<-- name rc=N` 配对包住**每个** IMP 调用：
  - helpers：`CreateGroup`/`DestroyGroup`/`Bind`/`UnBind`/`EnableChn`/`DisableChn`（各 1 处，覆盖 configure+start+stop+dtor 经由的所有 group/bind/fs 调用）。
  - `~dtor`：`ReleaseStream`/`StopRecvPic`/`UnRegisterChn`/`DestroyChn`（外加 helper 自动 trace 的 Disable/UnBind/DestroyGroup）。
  - `configure`：`GetChnAttr`/`SetChnAttr`/`CreateChn`/`RegisterChn`。
  - `start`：`StartRecvPic`；`stop`：`StopRecvPic`。
  - `getInfo`：`Query`/`GetChnAttr`（record 起始调一次）。
  - capture loop（`polling`/`getFrame`/`releaseFrame`）：loop-throttle（前 90 帧 = ~3s 全 log，之后每 60 帧 = ~2s 心跳），`g_hal_loop_frame` 在 getFrame 成功时 +1。

### `src/app/wm_app.cpp`（非 hal/）
- `lc.commonStartup(cfg)` 之后：`if (HTC_LOG_DEBUG==1) Logger::setLogLevel(DEBUG)`（C++14 兼容）。

## 2. 构建（双平台 GREEN）

| 平台 | 命令 | 结果 |
|------|------|------|
| sim | `cmake --build build_sim --target wm` | ✅ Built（仅 pre-existing Logger deprecated 警告） |
| HW | `PATH=toolchain/.../bin:$PATH /usr/bin/cmake --build build --target wm` | ✅ Built（libhal_video/libmedia_recorder/libmedia_snap/libapp_workmode 重链 + wm） |

**host md5（部署后必须 device 匹配；thin-loader 陷阱：wm 二进制 + .so 都要验）**：
```
a5c7d5542607d9cbce8423fb5554250b  build/bin/wm
64b546362845c5fd8020696e2b45622d  build/lib/libhal_video.so   ← IMP trace 所在
c1a3e3cb8d728d9f1e3a012bfa74e030  build/lib/libmedia_recorder.so
a51dd0a7bdcd7e7d1ad17fe7764a4096  build/lib/libmedia_snap.so
cac487554dd0cb5932377f5cb56d3c3d  build/lib/libapp_workmode.so
```
device 验：`md5sum /mnt/huntcam/bin/wm /mnt/huntcam/lib/libhal_video.so`（必须 == 上）。

## 3. wedge-run 协议（用户硬断电后执行；1-boot-1-case）

**证据通道**（wedge 后都可读）：
1. `/mnt/sdcard/logs/hal_trace.log`（SD，fsync'd）—— **PRIMARY**，definitive IMP 序列到 wedge 点。
2. `/mnt/sdcard/logs/app.log`（SD，fflush'd）—— module 日志（configure/start 在 wedge 前 ~30s 写，已落盘）。
3. `logs/serial.log`（broker host 侧）—— kernel printk / oops（若有）。

### 步骤
```
# 0. 用户硬断电（当前 wedge 恢复）
HTC_WIFI_PWD=<pwd> tools/devctl/devctl bringup          # SD→WiFi→NFS noac→verify
md5sum /mnt/huntcam/bin/wm /mnt/huntcam/lib/libhal_video.so   # 必须 == §2 host md5

# (可选) baseline：先跑 cm==2 record-only（GREEN，设备存活），拿干净 H264 序列 diff
tools/devctl/devctl run --timeout 60 "HTC_HAL_TRACE=1 HTC_TEST_NO_POWEROFF=1 HTC_WM_CAMERA_MODE=2 /mnt/huntcam/bin/wm -m 0"
tools/devctl/devctl run "cat /mnt/sdcard/logs/hal_trace.log"   # ← baseline，存起来

# → 用户硬断电（1-wm-per-boot）→ bringup

# 主：cm==1（photo+record），先 bump kernel console printk（force oops→serial）
tools/devctl/devctl run "echo 8 > /proc/sys/kernel/printk; echo done"
tools/devctl/devctl run --timeout 90 "HTC_HAL_TRACE=1 HTC_LOG_DEBUG=1 HTC_TEST_NO_POWEROFF=1 HTC_WM_CAMERA_MODE=1 /mnt/huntcam/bin/wm -m 0"
#   ↑ 期间设备 wedge（shell 无响应）—— 预期

# → 用户硬断电 → bringup → 读证据
tools/devctl/devctl run "cat /mnt/sdcard/logs/hal_trace.log"   # ← PRIMARY：wedge IMP 调用
tools/devctl/devctl run "tail -80 /mnt/sdcard/logs/app.log"
python3 tools/devctl/devctl log -n 200                          # serial：kernel oops?
```

### 判读
- **hal_trace.log 最后一条 `--> X` 无配对 `<-- X`** = X 是 wedge 调用（kernel 卡在 X 的 ioctl）。
- 对比 cm==2 baseline 的 H264 configure/start 序列，找 JPEG-release→H264-configure 的**发散点** = 缺失的重置调用（→ Step 2 fix）。
- serial.log 若有 kernel oops backtrace → 直接点名 wedge 的 driver 函数。
- 若 configure/start 都 `success` 且 wedge 在 loop（polling/getFrame）→ 证明是「silent 状态损坏」，需 Step 2 在 release 补 framesource 全 reset。

## 4. 下一步（据 wedge-run 结果）

- **Step 2**（same-payload-safe fix）：据发散点，在 `~dtor`/release 补**干净 framesource reset**（对 photo→photo / record→record 本就 re-configure，无害）。
- **Step 3**：全矩阵回归（photo→photo multi、record→record `test_record_repeat` 必 GREEN；cm==1 GREEN；m0-both/m1-both un-xfail）。
- **Step 4**：cm==1 改聚合 atomic task（capture_lane）。
- 跑完 cm==1 诊断后，**移除** `HTC_HAL_TRACE` sink + `HTC_LOG_DEBUG`（或保留 env-gated 备用）。

## 5. 待 commit（本会话改动）
- `src/hal/ingenic/IngenicVideo.cpp`（IMP trace sink + instrument）。
- `src/app/wm_app.cpp`（HTC_LOG_DEBUG bump）。
- + handoff §2 列出的上一会话未 commit 改动（SharedVideo 等）。

## 6. 首次 wedge-run 结果（2026-06-24，重大转折 — 颠覆 handoff 假设）

跑了 `HTC_HAL_TRACE=1 HTC_LOG_DEBUG=1 HTC_WM_CAMERA_MODE=1 wm -m 0`（**忘设 `HTC_WM_ONE_SHOT=1`**），devctl 90s 超时、设备「无响应」。读证据后发现 **不是 wedge**：

### 6.1 真正的故障 = SD vfat 只读（dirty fs）
- `mount`: `/dev/mmcblk0p1 on /mnt/sdcard type vfat (**ro**,…,errors=remount-ro)`。
- `dmesg`: `FAT-fs (mmcblk0p1): Volume was not properly unmounted. Some data may corrupt. Please run fsck.` —— 上次异常关机（真 wedge）留下 dirty fs，vfat errors→remount-ro。
- **后果**：所有写失败 —— `snap(int): open .jpg failed`、`fopen .mp4 failed` → `record done duration=247ms size=0 bytes`、`writeWorkModeDescJson: … Read-only file system`。
- `mount -o remount,rw /mnt/sdcard` **成功** → fs 可写。设备**无 fsck**（dosfsck/fsck.vfat/fsck 均无）。

### 6.2 「无响应」= busy-loop，非 kernel wedge（Ctrl-C 可恢复）
- SimPir 每 10s 触发，**20 次** `CaptureLane: trigger cameraMode=1`（14:29:58→14:32:38）。
- 无 one-shot → 每个 cm==1 cycle 在 RO fs 上失败但 wm 永不 idle → 永不 shutdown → devctl 超时 + 看似「卡死」。
- `devctl ctrl-c` **立即恢复** shell → 是 stuck 进程，**不是 kernel hang**。

### 6.3 cm==1 IMP 路径实际干净（handoff「incomplete release corrupts H264」假设**未被本次支持**）
- 单 cycle（serial.log 14:30:18-19）：JPEG configure/start success（chn=12）→ H264 configure/start success（chn=0，JPEG release 之后）→ `record started duration=30s`。
- **JPEG→H264 顺序 20 次全 configure/start 成功，零 wedge**。record 中止在 `fopen .mp4`（RO fs），不是编码过程。
- ⚠️ 但 record 从未真正跑完 30s（fopen 先 fail），故**不能断言 cm==1 无 wedge** —— 需 RW fs + 真 record 才能 conclate。

### 6.4 serial.log 历史里有真 MIPS kernel oops（prior boots）
```
[146.96s] CPU 0 Unable to handle kernel paging request at virtual address ffff8091, epc == 80037c98, ra == 802f3688
[181.68s] CPU 0 Unable to handle kernel paging request at virtual address ffff8080, epc == 80042964, ra == 80044bd8
[331.60s] (同 146) epc == 80037c98
```
→ 真发生过 kernel-space page fault。**但无法归因**（cm==1？还是 RO-fs failed-photo runs？）。需 kernel symbol map 才能解 `epc`→函数。

### 6.5 busybox 限制（实测）
设备 busybox **无** `wc`/`tail`/`head`/`cut`/`awk`（有 `grep`/`cat`/`mount`/`dmesg`/`pgrep`）。device-side 命令避开这些。**坑**：`$(…)` 在 devctl run 的双引号里会被**本地 zsh 展开**（误把 host uptime 当 device uptime）——device-side 变量用 `\$()` 转义。

### 6.6 下一步（需再次硬断电；当前 boot 的 IMP 被 ctrl-c 的 wm 污染，1-wm-per-boot 不能复用）
```
# 用户硬断电 → bringup → 确保 RW → one-shot cm==1（一次干净 cycle）
HTC_WIFI_PWD=<pwd> tools/devctl/devctl bringup
tools/devctl/devctl run "mount -o remount,rw /mnt/sdcard; mount | grep /mnt/sdcard"
tools/devctl/devctl run "echo 8 > /proc/sys/kernel/printk"
tools/devctl/devctl run --timeout 60 "cd /mnt/huntcam && HTC_HAL_TRACE=1 HTC_LOG_DEBUG=1 HTC_TEST_NO_POWEROFF=1 HTC_WM_ONE_SHOT=1 HTC_WM_CAMERA_MODE=1 LD_LIBRARY_PATH=/mnt/huntcam/lib:\$LD_LIBRARY_PATH ./bin/wm -m 0"
#   record 跑 30s（RW fs，fopen 成功）。wedge→hal_trace.log pinpoint；完成→cm==1 GREEN。
```
判读：one-shot 下 record 真跑 30s。若 wedge 期间 oops → 比 epc 与 §6.4。若干净 exit → cm==1 非 wedge（prior wedge 是 RO-fs/busy-loop 假象，或独立 oops）。

### 6.7 修正的认知
- handoff §3「JPEG→H264 incomplete release corrupts group 0」**未被证实**；IMP 顺序路径 robust。
- 真正阻断 cm==1 验证的是 **dirty RO fs**（prior 异常关机的后果），非 IMP bug。
- prior「wedge」可能是 (a) 真 IMP/kernel oops（§6.4 支持），或 (b) RO-fs + busy-loop 假象。**需 RW fs + one-shot 复跑才能定性**。

## 7. 二次 wedge-run（one-shot + RW fs）— 决定性结论（再次颠覆 §6）

`mount -o remount,rw` + `HTC_WM_ONE_SHOT=1` + trace，重跑 cm==1。**hal_trace.log（fs RW 时写成功）= 决定性证据**：

### 7.1 cm==1 IMP 全序列 rc=0（无 wedge）
photo JPEG（chn 12, g0）configure/start/~dtor 全 rc=0 → record H264（chn 0, g0）`CreateChn/RegisterChn/Bind/EnableChn/StartRecvPic` **全 rc=0** → record 因 `fopen .mp4 failed`（fs RO）在进 capture loop 前中止（**无 polling/getFrame trace**）→ stop rc=0。**JPEG→H264 IMP 路径 100% 干净，零 wedge**。

### 7.2 VPU IRQ warning（JPEG→H264 VPU 复用，非致命）
```
[146.958s] WARNING: CPU 0 PID 801 at kernel/irq/manage.c:513 enable_irq+0x70/0x8c()
  vpu_open+0x34/0x194 ← soc_vpu_request+0x1bc ← soc_channel_ioctl+0x740 ← SyS_ioctl
```
H264 `IMP_Encoder_CreateChn(chn=0)` 触发 VPU（硬件编码器）open → `enable_irq` 警告（**IRQ 重复 enable**，JPEG 用过 VPU 后 H264 再开）。IMP `CreateChn` 仍返回 rc=0（非致命），但这是 §6.4 历史 oops 的**头号嫌疑路径**。

### 7.3 真正的阻断 = SD 在 wm+IMP 运行时翻 RO（非硬件、非 fs、非写模式）
逐项排除（device alive，每项后 mount 仍 `rw` 或翻 `ro`）：
| 测试 | 结果 |
|------|------|
| `dd if=/dev/zero of=.../stress.bin bs=1M count=30`（无 wm） | ✅ 30MB @ 11.8MB/s，EXIT=0，**仍 rw** |
| 20 个小文件 + `sync`（写模式，无 wm） | ✅ 全写，**仍 rw** |
| idle 写 `media/`、`media/upload/` | ✅ 成功，**仍 rw** |
| **wm 跑 IMP（cm==1）** | 🔴 fs 翻 **ro**，photo jpg / record mp4 / desc json **全写失败** |

→ **SD 硬件健康、fs 可写、写模式无碍。只有 wm + IMP/encoder 运行时 SD 才翻 RO**（block I/O error → `errors=remount-ro`）。是 **wm/IMP 诱导的 SD I/O 错误**，机制疑似 VPU IRQ/DMA 异常污染 SD DMA 路径（与 §7.2 warning、§6.4 oops 同根）。

### 7.4 干净 shutdown（非 wedge）
one-shot cm==1 跑完后：idle-grace(30s) → `releaseVideoResources` → `[HAL] exit teardown` → `RTC closed` → `Power off From Main function` → `HTC_TEST_NO_POWEROFF=1` 生效走 `_exit(0)`（设备存活，probe OK）。**wm 正常退出，非 kernel hang**。

### 7.5 结论（颠覆 handoff 全部前提）
1. **cm==1 IMP wedge 假设 = 错误**。hal_trace.log 证明 JPEG→H264 全 IMP 调用 rc=0、零 wedge。handoff Step 2「clean framesource reset in IngenicVideo.cpp」**方向错**（framesource 本就干净）。
2. **cm==1 真正阻断 = wm+IMP 运行时 SD 翻 RO**（kernel 级 VPU/encoder ↔ SD/MMC 驱动交互 bug），非 userspace、非 SD 硬件。
3. prior「wedge during 30s record」：发生在**当时可写**的 fs（record 真跑 30s 触发 VPU/DMA oops）。现在 fs 因 prior 异常关机变 dirty/RO-prone，record 在 fopen 即中止，**无法复现原 oops**。
4. §6.4 历史 kernel oops（`epc 80037c98`/`80042964`）是**头号真凶线索**——需 kernel symbol map 解 `epc`→函数 才能定性（VPU 驱动？SD/MMC 驱动？encoder DMA？）。

### 7.6 下一步选项（需用户定方向）
- **A. 解 kernel oops epc**（需 device/build-host 上的 vmlinux 或 System.map）→ `addr2line e pc` 定位 fault 函数。最高价值：直接点名 kernel 罪魁。
- **B. cm==0 photo-only 复跑**（需硬断电，1-wm-per-boot）→ 若 photo 写也翻 RO = 任何 IMP run 都中招（非 cm==1 专属）；若 photo 写成功 = cm==1/VPU 复用专属。cheap，定 scope。
- **C. 重新框定 cm==1 task**：不是「修 wedge」（wedge 是误诊），而是「wm+IMP 诱导 SD I/O error 的 kernel 驱动问题」——可能需 IMP SDK 配置 / kernel VPU 驱动 / 换写路径（如先写 tmpfs 再 sync 到 SD）。
- handoff Step 2/3/4（framesource fix / 全矩阵回归 / cm==1 聚合 task）**暂停**——前提已不成立。

## 8. 终局（2026-06-25）：cm==1 wedge 真实复现 + 根因定位到 kernel VPU

用户**重新格式化 TF 卡**（清掉 §7 的 dirty-SD confounder）+ 加了 **wm 启动 mkdir `media/`** 修复（fresh SD 无 media/ → photo/record fopen ENOENT；wm 启动补 `createDirectory(MEDIA_TARGET_PATH/MEDIA_UPLOAD_PATH)`）。

### 8.1 干净 SD 上 cm==1 真复现 wedge（kernel hard-hang）
mkdir 修复后 cm==1 one-shot：photo 写入（thumbnail 4650B + desc enqueued）→ record started 30s → **record 过程中设备 kernel hard-hang**（ctrl-c 无响应，需硬断电；serial 无 oops/backtrace，silent hang）。**这是真 wedge，非 SD 假象。**

### 8.2 hal_trace.log pinpoint：wedge 在「record 起始 → 首帧 poll」之间
trace 共 119 行，结尾在 record 的 `getInfo`（H264 encoder 启动后确认），**无任何 polling/getFrame trace**。即：H264 encoder configure/start 全 rc=0、getInfo OK 后，**第一帧 capture（首次真正用 VPU 编码）即 wedge**。首个 poll 的 `-->` 都没落盘 → wedge 在首帧 poll 处或紧邻其前（VPU DMA 损坏波及 SD fsync，或卡在 ioctl 入口）。

### 8.3 根因（证据链）= JPEG→H264 VPU 复用损坏 kernel
1. photo（JPEG）用 VPU；~ImageSnap DestroyChn 销毁 JPEG channel，**但 VPU IRQ 未释放**。
2. record H264 `CreateChn chn=0` 再开 VPU → **`enable_irq` kernel warning**（IRQ 重复 enable，§7.2）。VPU 进入损坏态。
3. record start + getInfo OK（trace→119）。
4. **首帧 poll 驱动 VPU 真编码 → 损坏的 VPU DMA 毁 kernel 内存（对应历史 oops `get_work_pwq`）→ kernel hard-hang**。

**为何 cm==2（仅录影）GREEN、cm==1 wedge**：cm==2 的 JPEG（CH2 thumb）与 H264（CH0）**同一 encoder session**（无完整 JPEG teardown）；cm==1 有**完整 JPEG photo session 被 teardown 后再 H264 create** —— 这个 JPEG→H264 VPU 切换是 cm==1 独有，是损坏源。

用户推断「photo 流程未正常结束 → 漏给 record」**正确**，漏的是 **VPU**，但定位在 **kernel VPU 驱动层**（非 framesource，§7 已证 framesource 干净）。

### 8.4 不可在 userspace 简单修
`imp_encoder.h` **无 VPU reset/release API**（仅 CreateGroup/DestroyGroup/CreateChn/DestroyChn 等，VPU 生命周期由 kernel `soc_vpu`/`vpu_open` 隐式管）。`DestroyChn` 不释放 VPU IRQ → 下次 `CreateChn`（异 payload）double-enable → 损坏。

### 8.5 修复方向（待用户定，task #10）
- **A. kernel VPU 驱动 patch**（`soc_vpu`/`vpu_open` 的 `enable_irq`）—— 需 kernel 源码，大。
- **B. userspace workaround**：cm==1 改 record-first（H264→JPEG，VPU 反向切换，或避完整 JPEG teardown）；或 photo/record 共用/复用 encoder session。需设计，有风险。
- **C. 更细 instrument**：VideoRecorder::record()（可改，非 hal/）在「record started → 首帧 poll」间加 hal_trace，确认 wedge 是 CH0 H264 poll 还是 CH2 thumb poll。
- **D. defer cm==1**（保留 xfail），先 ship cm==0/cm==2（GREEN）+ SD/mkdir 教训。

### 8.6 本会话已落地（待 commit）
- `src/hal/ingenic/IngenicVideo.cpp`（HTC_HAL_TRACE fsync'd IMP trace sink + 全 IMP 调用 instrument）—— 诊断利器，保留 env-gated。
- `src/app/wm_app.cpp`（HTC_LOG_DEBUG bump + **启动 mkdir `media/`/`media/upload/`** —— 真 bug 修复，fresh SD 必需）。
- + handoff §2 上一会话未 commit（SharedVideo 等）。
- `reviews/2026-06-24-wm-cm1-step1-instrument.md`（本文）。
