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
| 7 | `seq-g0-wm-osd` | + **OSD region on group 0**（`IMP_ISP_Tuning_CreateOsdRgn(0)`，镜像 wm `IspOsdManager`） | wm photo 期间的 OSD engagement 是否是 enable_irq/wedge 触发源 | ✅ clean，`CreateOsdRgn handle=0` 正常 | **OSD 不是触发源**；photo 阶段 IMP 序列已**逐行复刻完整**仍未复现 |

**附带的只读分析（免 boot）**：
- 读 `configureEncoderAttr`（IngenicVideo.cpp:691）→ i2d 仅 `GetI2dAttr` 做 90/270 旋转的 W/H 交换，**不 SetI2dAttr、不碰硬件** → i2d **不可能是 VPU 触发源**（省了一版 boot）。
- 读 `IngenicVideo::init`（:1255）与 `sample_system_init` → 两者**都**调 `IMP_Encoder_SetJpegBsSize`/`SetMultiSectionMode`/`MultiProcessInit`（wm :1262-1268）→ encoder 系统参数**一致**，非差异。

## 3. 结论（definitive）

1. **IMP 流程层面全清**：7 个 variant（lifecycle / group 复用 / IVDC / SetChnAttr / 并发缩略图 / exit-reinit / OSD）**无一复现** wedge，photo 阶段 IMP 序列已**逐行复刻完整**。SDK 流程在所有受测配置下安全。
2. **不是原厂 driver bug**（SDK 自身流程不 wedge）→ **不需要向 T32 原厂提 issue**。
3. **不是 IMP 持久化模式**：`seq-g0-wm` 用了与 singleton **完全相同**的持久 IMP（单 session、photo→record），却 clean。
4. **不是 exit→re-Init**：`seq-split` 证明 fresh-IMP-per-session 可行。
5. **OSD 也排除**：`seq-g0-wm-osd` 复刻 wm OSD engagement，仍 clean。
6. **wedge 必在 wm 的 `IngenicVideo` C++ hal 代码**里——IMP 调用全 rc=0、纯 IMP-call 复现不出来；使 kernel hang 的是 wm C++ 层做了 sample 没做的事。

### 3.1 为何 sample-based reproducer **结构上无法**验证 singleton 假设（关键）

用户怀疑 `SharedVideo` singleton（持久 `IngenicVideo` C++ 对象）是元凶。但 sample reproducer 用的是 `sample_system_init`（C，**没有 singleton**）。它复刻的是 wm 的 **IMP 调用序列**（全 rc=0），而 singleton 的独有之处是它的 **C++ 对象状态**（`~IngenicVideoStream` 的 ref-counted release、持久 `IspOsdManager` 实例、`IngenicVideo::init` 内容）——这些**不在 IMP 调用层面**，sample reproducer 触及不到。

→ **要验证 singleton 假设，必须用一个用「真 singleton」的 standalone C++ harness**（链 wm 的 .so，调 `media::sharedVideo()` + `IngenicVideoStream` 做 photo+record，无 SimPir/scheduler/DB/upload）：
- 若 wedge → singleton/`IngenicVideo` capture 路径本身就是元凶（与 scheduler 无关）。
- 若 clean → scheduler/并发是必需的（不同方向）。

`IngenicVideo::init`（:1255）相对 `sample_system_init` 的额外项（VTS workaround `IMP_ISP_SetSensorRegister`、`IspOsdManager` 创建）已确认，但都非 VPU 触发源特征（sensor timing / OSD 已测）。

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

---

# 续（2026-06-25 当日后续）：singleton harness 演进 + wm 侧二分 + 多进程决定性发现

## 8. singleton harness（用 wm 真 singleton + IVideoStream）— 4 种演进，全 CLEAN

新建 `src/media/video/singleton_harness.cpp`（链 media_recorder，调 `media::sharedVideo()` + `IVideoStream` 做 photo[group0 main + g2 thumb] → record[H264]）。逐版加 wm 特性：

| 演进 | 加的 wm 要素 | 结果 |
|------|------------|------|
| 10-frame record | 基线 | ✅ CLEAN |
| sustained 900-frame | 持续录制（reproducer 已证 raw-IMP 900 帧 clean） | ✅ CLEAN → 非 sustained |
| +4s startup delay | 模拟 wm record 启动（getInfo/fopen/MP4E，H264 跑着无人消费） | ✅ CLEAN → 非 buffer 溢出 |
| +MP4 muxing | fopen/MP4E_open/mp4_h26x_write_init + 每帧 mp4_h26x_write_nal（复刻 VideoRecorder::record） | ✅ CLEAN → 非 MP4 |

→ **singleton + IVideoStream + photo + H264-record + MP4 + sustained + delay 全部复刻，仍 CLEAN**。wedge 不在 IMP-call 层、不在 singleton、不在 IVideoStream、不在 MP4。**必在 wm C++ 层（ImageSnap/VideoRecorder/scheduler/ProcessLifecycle）做了 sample 没做的事**。

## 9. wm 侧二分（Path B：让 cm==1 逼近不宕机的流程）— 3 个 env，仍全 wedge

wm 加了 3 个 env-gated 诊断（默认关，零正常运行影响）：
| env | 作用 | 结果 |
|-----|------|------|
| `HTC_RECORD_NO_THUMBNAIL=1` | record 跳过 captureThumbnail（record_task.cpp:68 已有） | 仍 wedge（fopen 处）→ captureThumbnail 非元凶 |
| `HTC_NO_MEDIA_SCANNER=1` | 跳过 MediaScanner（`cfg.skipMediaScanner`，wm_app 新增） | 仍 wedge → MediaScanner 非元凶 |
| `HTC_CM1_RESET=1` | photo/record 之间 `media::resetSharedVideo()`（→ `IMP_System_Exit` → record re-init，逼近「分开 app」fresh-session-per-capture；SharedVideo 改可 reset + capture_lane 加门） | reset 跑通（log 证 `IMP_System_Exit` + record re-init），**仍 wedge（fopen 处）** → **wedge 非 photo 的 IMP 残留** |

→ **完整 `IMP_System_Exit` 都救不了**。wedge 在 IMP 之外（wm-app 编排/状态，或 kernel driver 里 wm 的 exit 没清干净的东西）。

## 10. 多进程决定性发现（回答「两个独立程序是否宕机」）

**用户疑问**：两个独立 sample 程序（一个拍照、一个录影）在一次启动里先后跑，是否宕机？（用户记忆：不宕机。）

**实测（一次启动里连跑）**：
| 进程 | 结果 |
|------|------|
| `sample-Encoder-jpeg` ×3（photo）+ `sample-Encoder-jpeg-then-video seq` ×1（photo+record）= **4 个 sample IMP 进程** | **全部成功，无 wedge，设备全程存活**（reproducer 作为第 4 个进程跑了完整 photo+record cycle，`system_init rc=0` → DONE） |
| `wm -m 0 HTC_WM_CAMERA_MODE=0`（cm==0 photo，第 5 个进程） | photo 拍到了（thumbnail + desc），**shutdown 时设备挂**（probe 超时） |

**结论（决定性）**：
1. **用户的记忆是对的**：多个独立 **sample** 程序在一次启动里**不宕机，且都成功**。**1-IMP-per-boot 对 sample/IMP driver 是错的** —— IMP driver 完全支持一次启动多个 IMP session。
2. **但 wm 不行**：wm 作为**非首个** IMP 进程，**shutdown（IMP teardown）会挂**。→ **fork/exec 用 wm 进程拆 cm==1 的 photo/record 行不通**（第 2 个 wm 的 shutdown 会挂）。
3. **wedge 确认是 wm 专属**：IMP driver 干净（多 sample 进程都行）、captures 干净（reproducer/harness 复现不出来）。问题在 **wm 自己的 IMP 用法**。
4. **新线索**：wm 的 **IMP teardown（`IngenicVideo::exit`）作为非首个进程会挂** —— 跟 cm==1 wedge 可能同根（都是 wm 的 IMP 生命周期没干净复位）。

## 11. 更新后的根因判断

wedge **必在 wm 的 `IngenicVideo` IMP 生命周期**（init/exit/release）里 —— IMP 调用全 rc=0（hal_trace 证），但**状态没干净复位**（多进程证：wm 第 2 个 session 的 teardown 挂；cm==1 单进程里 photo→record 也挂）。即 **wm 的 IMP release 不彻底**，sample 的彻底。handoff 当初的「framesource reset」方向**可能对**，但在比 framesource 更深的层（wm 的 `IngenicVideo::exit`/release 序列比 sample 的 `sample_system_exit` 少了/错了某步）。

## 12. 下一步方向（新 session 接续）

**主攻：对比 wm 的 `IngenicVideo::init`/`exit` vs sample 的 `sample_system_init`/`sample_system_exit`**，找 wm 的 exit/release 少了或错了哪个 IMP 步骤（导致非首个进程 teardown 挂、可能也导致 cm==1 wedge）。
- wm exit：`src/hal/ingenic/IngenicVideo.cpp:1318` `IngenicVideo::exit()`（[HAL] exit teardown：FS disable → flush → UnBind → DestroyGroup → ISP/System）。
- sample exit：`sdk/samples/libimp-samples/sample-common.c` `sample_system_exit()`（:710）+ `sample_framesource_exit`/`sample_jpeg_exit`/`sample_video_exit`。
- 关键对比点：encoder group/channel 的销毁顺序、ISP 关闭、`IMP_System_Exit` 前是否漏了某个 DisableChn/UnBind/DestroyChn。

**辅证：给 wm 的 shutdown（[HAL] exit teardown）加 `hal_trace`**（现有 hal_trace 只包 configure/start/stop，不包 exit），跑「wm 作为非首个 IMP 进程」复现 shutdown 挂，看最后一条 `-->` 无配对 `<--` = 挂在哪个 IMP 调用。这比 cm==1 wedge 更容易复现（只需 sample 跑一次 + wm 跑一次，同 boot）。

**cm==1 wedge 本身**：等 exit 对比有结论后，用同思路查 cm==1 record 期间 wm 的 release 是否漏步（photo 的 release 没把某 IMP 资源复位给 record）。

## 13. 本会话产出（commit 记录）
- `a1e482a` SharedVideo 单例（上一会话遗留）。
- `3590034` cm==1 诊断（mkdir fix + HTC_HAL_TRACE + reproducer）。
- `a7b484a` reproducer seq-split + OSD variant + singleton-harness 结论。
- `d091ae0` singleton_harness（IVideoStream 隔离测试）。
- `17d1727` singleton_harness 演进（sustained + delay + MP4，全 CLEAN）。
- `e97b402` HTC_NO_MEDIA_SCANNER。
- （本次）SharedVideo 可 reset + capture_lane HTC_CM1_RESET 门。
- 关键样本：`sample-Encoder-jpeg-then-video`（7 mode reproducer）、`singleton_harness`（4 演进）。两个都是干净、可复用的诊断 artifact。
