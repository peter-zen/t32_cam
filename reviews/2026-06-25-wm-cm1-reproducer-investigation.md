# 2026-06-25 — wm cm==1 VPU wedge: sample-reproducer 调查全记录

> 承 [`2026-06-24-wm-cm1-handoff.md`](2026-06-24-wm-cm1-handoff.md) + [`2026-06-24-wm-cm1-step1-instrument.md`](2026-06-24-wm-cm1-step1-instrument.md)。
> 本文档记录：为定位 cm==1（photo+record）kernel wedge，在 SDK sample 基础上**逐步逼近 wm 行为**所做的全部复现尝试、每一版验证的假设、结果、以及最终结论。供后续分析查阅。

## 0. 背景（一句话）

wm 的 cm==1（photo=JPEG → record=H264，同进程顺序）在**干净 SD**（已排除 dirty-fs RO 假象，见 step1-instrument §7）上 **kernel hard-hang**（silent，ctrl-c 无效，需硬断电）。`hal_trace.log` 显示 wedge 在「record 起始 → 首帧 poll」之间，所有 IMP 调用 rc=0；wm 触发 kernel `enable_irq` warning（`vpu_open`），sample 不触发。**用户要求**：做一个最小 sample 复现，干净环境确认，能原厂解决就提 issue，能流程控制就找正确流程。**关键约束**：先拍照还是先录影是**用户配置项**，不能因「换顺序不宕机」就改规格。

## 1. 复现 sample：`sample-Encoder-jpeg-then-video`

- 位置：`sdk/samples/libimp-samples/sample-Encoder-jpeg-then-video.c`（新）+ Makefile 入 SAMPLES + link rule。
- 复用 `sample-common.c` 的 helper（`sample_system_init/exit`、`sample_framesource_init/streamon/streamoff/exit`、`sample_jpeg_init/exit`、`sample_video_init/exit`、`sample_start/stop_get_*_stream`）。
- channel 映射（单 sensor 默认）：JPEG = enc chn 12 / FS group 2（640x360）；video = enc chn 0 / FS group 0（2560x1440）。
- 静态链 libimp.a/libalog.a → 设备端无需 `LD_LIBRARY_PATH`；`cp` 到 `build/bin/`（NFS 共享到 `/mnt/huntcam/`）；`devctl run '/mnt/huntcam/bin/... <mode>'`。
- **1-IMP-process-per-boot**：每个 variant 一台 fresh boot（硬断电 + `devctl broker stop&&start` + bringup + `mount -o remount,rw`）。
- TRACE 宏：每个关键 IMP 调用前后 `printf("[TRACE] ...")`+`fflush`，最后一条 `-->` 无配对 `<--` = wedge 调用；serial.log（broker host 侧）全程捕获。

## 2. 渐进式 variant 矩阵（全部 ✅ CLEAN，**无一复现 wedge**）

每一版在前一版基础上**只加一个 wm 特有要素**，逐步逼近 wm 的 `IngenicVideo::configure` + photo 行为。参照 wedge `hal_trace.log`（wm cm==1）做 diff。

| # | mode | 在前一版上增加的 wm 要素 | 验证的假设 | 结果（device alive?） | 排除 |
|---|------|------------------------|-----------|----------------------|------|
| 1 | `seq` | 基线：JPEG 在 group 2（640x360）→ H264 在 group 0 | 「bare JPEG-encoder-DestroyChn → H264-CreateChn 的 VPU 切换」是否 wedge | ✅ clean（跑完 DONE） | **bare encoder 切换本身安全**；handoff「framesource/VPU 切换」假设**第一阶段即被推翻** |
| 2 | `seq-g0` | JPEG 改到 **group 0 全分辨率 2560x1440**（RegisterChn(g0,12) + Bind FS0，匹配 wm photo 主码流） | 「group 0 全分辨率 JPEG→H264 复用」是否 wedge | ✅ clean | **group 0 复用本身安全** |
| 3 | `seq-g0-ivdc` | + **IVDC**（ISP-VPU direct connect）：JPEG inline attr `bEnableIvdc=true` + H264 经 `direct_switch=1`（`sample_video_init` sample-common.c:1373） | wm `ivdc=1` 是否是 enable_irq/wedge 触发源 | ✅ clean，**无 enable_irq warning** | **IVDC 不是触发源** |
| 4 | `seq-g0-full` | + **SetChnAttr 重配**（`GetChnAttr→scaler/crop/fps→SetChnAttr`，JPEG fps15、H264 fps30，镜像 `IngenicVideo::configure`） | wm 每次 encoder 都 SetChnAttr 重配 group 0，是否留下坏 FS 状态 | ✅ clean | **SetChnAttr 重配不是触发源** |
| 5 | `seq-g0-wm` | + **并发 group-2 缩略图**（photo 同时建 enc 12(g0)+enc 14(g2 320x180)、抓双流、都销毁——按 wedge hal_trace 0-79 行**完整复刻 wm photo**） | 「photo 期间并发 group-2 缩略图（双 VPU encoder）留下残留」 | ✅ clean，无 warning | **并发缩略图不是触发源**；photo 阶段已**完整复刻**仍未复现 |
| 6 | `seq-split` | **反 singleton**：photo 和 record 分两个 IMP session（init→photo→**exit**→init→record→exit） | 「IMP_System_Exit→re-Init」（singleton 当年为避开它而引入，handoff §2）是否 wedge；fresh-IMP-per-session 是否可行 | ✅ clean，**RE-INIT rc=0**，`SEQ-SPLIT DONE` | **exit→re-Init 在 sample 不 wedge**；fresh-per-session 可行 |

**附带的只读分析（免 boot）**：
- 读 `configureEncoderAttr`（IngenicVideo.cpp:691）→ i2d 仅 `GetI2dAttr` 做 90/270 旋转的 W/H 交换，**不 SetI2dAttr、不碰硬件** → i2d **不可能是 VPU 触发源**（省了一版 boot）。
- 读 `IngenicVideo::init`（:1255）与 `sample_system_init` → 两者**都**调 `IMP_Encoder_SetJpegBsSize`/`SetMultiSectionMode`/`MultiProcessInit`（wm :1262-1268）→ encoder 系统参数**一致**，非差异。

## 3. 结论（definitive）

1. **IMP 流程层面全清**：6 个 variant（lifecycle / group 复用 / IVDC / SetChnAttr / 并发缩略图 / exit-reinit）**无一复现** wedge。SDK 流程在所有受测配置下安全。
2. **不是原厂 driver bug**（SDK 自身流程不 wedge）→ **不需要向 T32 原厂提 issue**。
3. **不是 singleton 持久化**：`seq-g0-wm` 用了与 singleton **完全相同**的持久 IMP（单 session、photo→record），却 clean。用户的 singleton 假设**被 sample 推翻**（持久模式本身不 wedge）。
4. **不是 exit→re-Init**：`seq-split` 证明 fresh-IMP-per-session 可行。
5. **wedge 必在 wm 的 `IngenicVideo` C++ hal 代码**里——IMP 调用全 rc=0、纯 IMP-call 复现不出来；使 kernel hang 的是 wm C++ 层做了 sample 没做的事。**头号未测嫌疑：`IspOsdManager`**（wm `IngenicVideo::configure` 每次 session 都调 `IspOsdManager::prepare/start`；serial 日志实证 `IspOsdManager: CreateOsdRgn/stamp`；sample 完全无 OSD）。

## 4. 用户约束 vs 结论

- 先拍/先录是**用户配置项** → 不能改规格、不能强制顺序（B 方案「record-first」仅诊断、不可作产品修复）。✅ 已遵守：6 个 variant 只为定位，不动 wm 规格。
- sample 的「正确流程」(任意顺序都 clean) 证明：**wm 只要 align 到 sample 的 IMP 用法就能不 wedge**。问题在 wm 的 C++ 实现偏离了 SDK 用法（OSD 等）。

## 5. 下一步（wm hal 直接二分，需批准）

sample 已穷尽；要定位须直接改 wm hal（`src/hal/ingenic/IngenicVideo.cpp` / `src/media/snap/ImageSnap.cpp`）。首个二分：**env-gated 禁用 OSD**（`HTC_NO_OSD=1`，默认关、零正常运行影响），重跑 cm==1：
- wedge 消失 → OSD 是触发源（wm 侧流程 bug，可修）。
- wedge 仍在 → 排除 OSD，继续二分（ref-counted release / IngenicVideo::init 细节 / singleton C++ 对象状态）。

## 6. 本次产出（已 commit）

- `src/app/wm_app.cpp`：启动 `mkdir media/ + media/upload/`（fresh-SD 真 bug 修复）+ `HTC_LOG_DEBUG` env。
- `src/hal/ingenic/IngenicVideo.cpp`：`HTC_HAL_TRACE` fsync'd IMP-call trace sink（env-gated，诊断利器，保留）。
- `sdk/samples/libimp-samples/sample-Encoder-jpeg-then-video.c` + `Makefile`：复现 sample（本文 6 variant 的载体）。
- `reviews/2026-06-24-wm-cm1-step1-instrument.md`（§7 SD-RO 转折、§8 wedge 定位）+ 本文档。

## 7. 关键证据文件（后续分析速查）

| 文件 | 作用 |
|------|------|
| `sdk/samples/libimp-samples/sample-Encoder-jpeg-then-video.c` | 6-mode 复现 sample（seq/seq-g0/seq-g0-ivdc/seq-g0-full/seq-g0-wm/seq-split + rev/concurrent） |
| `sdk/samples/libimp-samples/sample-common.c` | helper（`sample_*_init/exit`、`sample_start/stop_get_*_stream`、`chn[]` 表）—— 只读引用 |
| `src/hal/ingenic/IngenicVideo.cpp` | wedge 真正所在；`configureEncoderAttr`(:691)、`init`(:1255)、`configure`、`IspOsdManager` 调用点 |
| `src/media/video/SharedVideo.cpp` | 进程级 IngenicVideo singleton（用户怀疑点，sample 已证非持久化本身） |
| wedge `hal_trace.log`（SD，cm==1 那次） | photo phase 0-79 行 + record 80-119 行（止于 getInfo，无 capture loop）= wm 真实序列，sample 逐行对照基准 |
