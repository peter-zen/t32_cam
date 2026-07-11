# quickSnap 程序规格（quicksnap-app-spec）

> 用途：上电第一个用户程序 `quickSnap` 的行为规格——读 GPIO 定工作模式、读最小 json 配置、时间同步、按模式拍照（≤8M）或跳过，然后用 `fork+execv`（无 shell）拉起 `wm` / `um`。
> 来源：2026-07-09 grill 会话定型（决策见 §11 决策日志）。**真相源 = 本文 + 代码**；本文为 quickSnap 的权威 spec。
>
> **治理规则（重要）**：后续若修改 quickSnap 功能，**必须同步更新本 spec**；若实现与 spec 冲突，**先询问是否更改 spec**，不要默默改实现。关联 [`wm-app-spec.md`](wm-app-spec.md)（下游 work mode）、[`um-app-spec.md`](um-app-spec.md)（下游 user mode）、[`media-app-boot-flow.md`](media-app-boot-flow.md)（前身 `htc_media_app` 的旧流程）。

---

## 0. 背景与定位

- `quickSnap` 是 `htc_media_app`（`src/app/media_app.cpp`）的**原地瘦身重构 + 改名**，不是新建并行二进制。保留已固化的 boot 入口位置、GPIO/WorkMode/ImageSnap 复用、与下游的 handoff 契约。见 [`../decisions/asymmetric-snap-vs-record-design.md`](../decisions/asymmetric-snap-vs-record-design.md)（拍照在前置进程的省电相机决策）。
- **定位**：上电后第一个用户程序；ZL 型号（64MB）上**唯一的拍照者**——`wm` 在 ZL 只跑 `-m 2`(upload)/`-m 3`(heartbeat)，都不拍照、不 init IMP。
- **目标**：尽快出首拍 + 简洁稳定 + 拍完做完整 IMP 释放再拉起下游。

---

## 1. 身份与 CLI

| 项 | 值 |
|----|----|
| binary 名 | `quickSnap`（重构自 `htc_media_app`，源码 `src/app/quick_snap.cpp`） |
| CLI | **无入参**（mode 来自 GPIO，config 来自 quicksnap.json；不沿用旧 `-wm`/`-rtc` handoff） |
| 与旧 app 关系 | **替换** `htc_media_app` 的 boot 入口角色；`htc_media_app` 在 quickSnap 上线后退役 |
| 平台 | **双平台必须编译**：真机（`toolchain.cmake`，进 `build/bin/quickSnap`）+ 仿真（`-DBUILD_FOR_SIMULATION=ON`，进 `build_sim/bin/quickSnap`） |
| 配置文件 | `/config/htc/quicksnap.json`（sim：`./res/quicksnap.json`）—— 绝对路径，**不依赖二进制位置** |

> 旧 `htc_media_app` 的 `startApp("htc_main_app -wm ... -rtc ...")` handoff **作废**。新 handoff 见 §4（`wm -m <2\|3>` 无 `-rtc`，或 `um` 无 flag）。

---

## 2. 配置：`quicksnap.json`（架构 γ —— 单一真相源）

quickSnap **只读 `quicksnap.json`，不读 `setting.json`**。下列 5 个字段以 quicksnap.json 为唯一真相源。

### 2.1 schema

```json
{
  "cameraMode": 0,
  "force_upload": 0,
  "burstNumber": 1,
  "stillSize": 1,
  "timezone": "UTC-8",
  "network": "4g"
}
```

| 字段 | 类型 | quickSnap 用途 | 写者 |
|------|------|---------------|------|
| `cameraMode` | uint8 0..5 | `==2`(仅录影) → 跳过拍照 | um（用户经 app 改） |
| `force_upload` | int 0/1 | `==1` → 覆盖为 upload 模式、不拍照；消费后写回 0 | `Power::requestChangeMode`（um 侧触发） |
| `burstNumber` | uint8 | 连拍张数 | um |
| `stillSize` | uint8（`SnapImgSize[]` 下标） | 出图分辨率；quickSnap **内存里钳到 ≤8M**（下标 ≤ `SNAP_IMG_SIZE_8M`=4） | um |
| `timezone` | string | setenv 用（拍照文件名走本地时区） | provisioning / um |
| `network` | string `"4g"`/`"wifi"` | 上行类型：4G=`htc_net_app --type usb --usb-bringup`，WiFi=`--type wifi`（MCU 读凭据）；缺省/未知 → `"4g"` | provisioning / um |

### 2.2 读写契约（实现要点，必守）

- **`force_upload` 消费后 read-modify-write 回 0**：先 load 整个 json、置 `force_upload=0`、再 dump 全部字段——**不能盲覆盖**，否则踩掉 um 写入的 `cameraMode`/`burstNumber`/`stillSize`。
- **`stillSize` 钳制只在内存、绝不写回**：与 `force_upload` 相反——`stillSize` 钳到 ≤8M 仅为本次拍摄，写回会把整个产品永久降到 8M。
- **首启兜底**：quicksnap.json 不存在（出厂首启 / um 尚未写过）时用安全默认：`cameraMode=0 / burstNumber=1 / stillSize=SNAP_IMG_SIZE_4M(=1) / force_upload=0 / timezone` 空。
- **路径**：`/config/htc/quicksnap.json`（与 `env.ini`/`config.ini`/`setting.json` 同目录）；sim 下 `./res/quicksnap.json`。

### 2.3 γ 闭环项（非 quickSnap 阻塞，归 wm/um 迁移）

quickSnap 只读 quicksnap.json 成立的前提是这 5 个字段不再有"第二个真相源"。下列迁移须在 wm/um 侧完成：

1. **wm 的 `cameraMode` 读点迁到 quicksnap.json**：现 `capture_lane.cpp:23`、`WorkModeRunner.cpp:399,491` 从 `Settings`(setting.json) 读，须改读 quicksnap.json，否则 quickSnap 与 wm 对 cameraMode 看两个值 → 撕裂隐式 handoff。
2. **`Settings` 序列化的影子副本**：`Settings` 结构体仍带这 4 字段，`saveToJsonFile` 会写回 setting.json。须从序列化摘除，或明确 setting.json 里这份是"死数据无人读"。
3. **um 承担 quicksnap.json 的写入**（用户经 app 改 cameraMode/burst/stillSize、`Power` 改 force_upload）—— um 侧迁移项。

---

## 3. 模式与拍照决策（mode router）

GPIO **暂沿用旧 `workingMode` 枚举**（2 根 pin `PC(9)/PC(8)` = 4 组合）；后续可改为 GPIO 直接对照三模式，避免旧枚举映射（留 follow-up）。

| GPIO（旧枚举） | 新语义 | quickSnap 拍照？ | spawn 目标 |
|---|---|---|---|
| `SNAP_ONLY`(0) | work（仅拍） | ✅ | **不 spawn** → 关机 |
| `SNAP_UPLOAD`(1) | work（拍+传） | ✅ | `wm -m 2` |
| `UPLOAD_ONLY`(2) | **heartbeat** | ❌ | `wm -m 3` |
| `TEST_ONLY`(3) | **user** | ❌ | `um` |
| （任意）+ `force_upload==1` | force upload（覆盖） | ❌ | `wm -m 2` + 写回 force_upload=0 |

**拍照门控** = `mode ∈ {SNAP_ONLY, SNAP_UPLOAD} && cameraMode ≠ 2 && force_upload == 0`。

- `daemon`（旧 `htc_daemon_app`）**不再由 quickSnap 拉起**。
- heartbeat（`UPLOAD_ONLY`）= **单次**发心跳让服务器知在线后关机（「周期性」= MCU 周期唤醒，每次唤醒 = 1 boot = 1 心跳；**无补传**——lean 无卡无重传）；走 `wm -m 3`（见 §10、[`wm-app-spec.md`](wm-app-spec.md) §2.1）。
- user（`TEST_ONLY`）= RTSP 预览（不拍不录），走 `um`。

---

## 4. IMP 生命周期（ZL：1-IMP-per-boot）

**关键结论**：ZL 上 `wm -m 2/-m 3` **不 init IMP**（upload/heartbeat 用网络栈，不碰 camera）→ 每个 boot **最多一个 IMP 进程** → "第 2 个 IMP 进程"在 ZL 不存在。

| 模式 | IMP 使用 | wedge |
|---|---|---|
| work（SNAP_ONLY/SNAP_UPLOAD） | quickSnap snap 用 IMP（唯一） | 1 IMP/boot |
| heartbeat（UPLOAD_ONLY） | quickSnap 不碰、wm -m 3 不碰 | 0 IMP/boot |
| user（TEST_ONLY） | quickSnap 不碰、um 是首个 IMP | 1 IMP/boot（um 首进程） |

- **cm==1 / ISP re-enable wedge**：[[wm-cm1-wedge-isp-reenable-crash]] 记录的 wedge 是"非首个 IMP 进程 re-enable sensor 时 kernel ISP OOPS"，**存疑、under investigation**。在 ZL 目标架构里其前提（第 2 个 IMP 进程）不成立，故**不列为 quickSnap 风险**；若未来 wm/um 出现别的 IMP 使用，另查。
- **user 模式下 quickSnap 完全不 init IMP**（无 snap 可拍，跳过整条 ImageSnap）—— 最快、最省电；um 侧为 RTSP 自行 init IMP（um 是首进程）。

### 4.1 work 模式的 IMP 释放序列（Q5 决策）

用 `ImageSnap`（≤8M HW scaler 路径）拍完，**必须干净退出 IMP**：

1. `ImageSnap::snap()` → 产出 `/tmp/media/<ts>/*.JPG` + `info.json`。
2. `~ImageSnap()`（析构 → stream `stop()` + `DestroyChn`，channel 级释放）。
3. **`HalProvider::resetSharedVideo()`** → 触发 `~IngenicVideo → exit() → IMP_System_Exit`。

> 现状 `ImageSnap.cpp:222-224` 注释：ImageSnap **故意不调** `exit()`/`IMP_System_Exit`，因为它要和 VideoRecorder/preview **同进程共享** `sharedVideo()` 单例（同进程内 exit→re-init 才 wedge）。**该理由对 quickSnap 不成立**（quickSnap 是独立 one-shot，不与谁共享）——故 quickSnap 末尾**必须显式 `resetSharedVideo()` 补上干净退出**，达到与 SDK `sample` 同级的干净度。
>
> ⚠️ **验证点**：`resetSharedVideo()` 在 `~ImageSnap` 之后调用是否干净（channel 已 DestroyChn，理论上安全，但需实机确认无 double-free / 重入）。

---

## 5. 拍照路径（≤8M，HW scaler）

- 用 `ImageSnap`，`stillSize` **钳到 ≤ `SNAP_IMG_SIZE_4M`（2560×1440，下标 1）= sensor-native**。两维均不超 sensor → `isLargeImage=false` → 走 HW encoder 路径（只需 JPEG CH12，`ImageSnap.cpp:124`）。
- **>4M（8M…）任一维超 sensor-native → `isLargeImage=true` → strip 路径**（`snap_large_internal`）。strip 需 sensor framesource **CH0**（`ImageSnap.cpp:471` `IMP_FrameSource_EnableChn(0)`），但 quickSnap photo-only HAL（`residentMode=0`）只建 CH12+CH14、**不建 CH0**（selective-preBind 省 ~1.84MB，`IngenicVideo.cpp:1141`）→ `EnableChn(0)` 必败 → snap 失败。故 quickSnap 精简 HAL 结构上**不支持 >4M**；doSnap clamp（`quick_snap.cpp` stillSize clamp）从源头杜绝 strip。注：旧版 spec 写「≤8M 走 HW 路径」有误——8M(3840×2160)>sensor-native 同样走 strip。
- 产出落 `/tmp/media/`（`QUICK_SNAP_DIR` 常量改名 `/tmp/quick_snap/`→`/tmp/media/`，`app.h`）。
- `info.json` manifest（`{files:[..], dir:"<ts>"}`）写给 **wm `-m 2` 直接读取并造 desc 上传**——**不搬移到 SD**（wm lean 模式无卡可跑；见 [`wm-app-spec.md`](wm-app-spec.md) §2.1）。旧 `processCmdSnap` 的 /tmp→SD 搬移路径**作废**。
- **成片即终片**：SNAP_ONLY/SNAP_UPLOAD 模式下 quickSnap 出的片就是最终成片（wm `-m 2` 直接从 /tmp 上传、不补拍），故 ≤8M 封顶是产品取舍（高清留给后续）。
- **不入库（no DB）**：quickSnap 出片即终片、wm 直传，**不入 `media_file.db`**（无 playback/相册索引需求）。`ImageSnap` 是**纯拍照类**——只写 JPEG，不调 `addMedia`（2026-07-10 把 addMedia 从 `ImageSnap` 移出，归还调用方；wm `snap_task` / 主 app `CameraServiceT32::takePhoto` 各自入库）。quickSnap 全程**不 init `DatabaseManager`**，`addMedia` 路径在 quickSnap 不存在。
- **CH2 缩略图全关（两层都关，省内存）**：quickSnap 不要缩略图。两层——① HAL preBind 的 group2/CH14（320×180 JPEG 通道，`IngenicVideo::buildResidentChannels`）由 `HalVideoConfig.withThumb=false` 门控不建；② `ImageSnap` 的 `thumbStream_`（`setThumbnailEnabled(false)`）不创建。只关一层（②）省不下主体内存——group2 编码器通道仍 idle 占着，故两层都必须关。
- **HAL channel 配置走显式 API（env 退役）**：quickSnap 在决定拍照后（`if(willSnap)` 内、`doSnap` 前）调 `hal::HalProvider::start(HalVideoConfig{residentMode:0, withThumb:false})` 声明本进程的常驻通道配置。旧的 `HTC_HAL_RESIDENT_MODE` env **全面退役**（`buildResidentChannels` 改读 `HalVideoConfig` 成员，不再 `getenv`）。`sharedVideo()` 仍 lazy 构造，但用 `start()` 设的 config（未调则默认 `{-1, true}` = um/legacy 全建）——这让 cm==1 的 `resetSharedVideo`→re-init 自动用同 config，且 um/main_app/test 零改动继承默认。

### 5.1 文件/目录命名规则（始终时间戳，不区分 rtcOk）

- **目录** = `/tmp/media/YYYYMMDD_HHMMSS/`；**文件** = `<YYYYMMDD_HHMMSS>_<n>.JPG`（`<n>` = 1..burstNumber）。`<ts>` 来自 `getCurrentTimeFormatted()`（`time()`+`localtime`，本进程时区）。
- **`rtcOk` 不影响命名**：rtc 同步失败（RTC+MCU 都不可信，时钟 `<2026`）时，**仍原样用当前（不可信）系统时间**命名，**不另设 "pic" 兜底名**。理由：
  - 命名必须统一为 `^\d{8}_\d{6}(_\d+)?$`，wm 的兜底扫描 / SD 续传（`isTimestampDir`，`wm_sweep.cpp`）才认得；非时间戳名（旧 "pic"）会被兜底/续传略过。
  - T22「上传失败 SD 落卡」后，tmpfs 工作目录会被 `persistStrandedTmpDir` 落到 SD；若名非时间戳，下次 boot SD-resume 认不出 → **永久 stranding**。统一时间戳命名从源头消除该 gap（根因见 [`workmode-m2-workunit-handoff.md`](../../../doc/design/workmode-m2-workunit-handoff.md) §4 rtc-fail 注）。
  - **刻意不 clamp** 时间到 plausible 下限：wm `syncWithMCU` 仅在系统时间 `≥2026`（可信）时才写 MCU；若 quickSnap 把时钟 clamp 成假 plausible，wm 在 NTP 失败时会把假时间固化进 MCU，污染最后已知好时间。保持时钟 implausible → wm 可信门关闭 → MCU 安全。（`snap_test` 的 `ensurePlausibleClock` clamp 是独立测试工具、不进 MCU 写回链，不可照搬。）
- `rtcOk` 仍由 `syncSystemTime()` 求值并打 log（标识本次嵌入的时间戳是否可信），但**不再用于命名分支**。

---

## 6. 调度下游（fork+execv，无 shell）

**硬要求**（T32 no-fork-shell 规范，用户 2026-07-08 指令）：quickSnap 新代码**禁止** `system()`/`popen()`/`Misc::syscall(命令串)`/`system_call(命令串)`——T32 MemFree 紧张，fork 一个 sh + 复制页表即 OOM（2026-07-08 `wm -m 1` 因 `system("mount | grep ...")` 被 OOM kill 实证）。旧 `startApp` 即此违规，挂 P1 待清。

- **机制**：`fork() + execv()`（不经 sh，省 sh 进程 0.5–1MB）。src/ 无现成 helper（P2 未落地）→ quickSnap 自写极小 `spawn(path, argv)`。
- **顺序**：snap → **完整释放（§4.1 IMP_System_Exit + free buffer + close file）→ 再 fork**。fork 时 quickSnap 已 lean，加上上电首进程 MemFree 最大 → OOM 风险最低的 fork 时机。可选 `vfork()+execv()`（不复制页表，更保险；子进程只能立即 execv/_exit）。
- **handoff 契约**（不再是旧 `-wm -rtc`）：
  - work/heartbeat → `wm -m <2|3>`（**无 `-rtc`**，wm 自跑时间链，见 [`wm-app-spec.md`](wm-app-spec.md) §6）。
  - user → `um`（无 flag）。
  - quickSnap 的 RTC 同步结果**不传**下游，wm/um 各自管时间。

---

## 7. 启动顺序（不可乱）

```
1. 记录启动时间戳
2. set POWER_HOLD_PIN（PC(8)）OUTPUT HIGH（保持供电）—— ⚠️ 见 §9.1 oddity
3. 读 GPIO → mode（WorkMode::getWorkingMode，暂沿用旧枚举）
4. 读 quicksnap.json（5 字段，首启兜底）
5. set timezone（从 quicksnap.json，setenv）
6. 时间同步：app_lifecycle::syncSystemTime()（RTC 优先 + MCU 兜底）—— 仅供本进程文件名用
7. 判 force_upload：==1 → effective_mode=UPLOAD(wm -m 2)，跳过 snap
8. 拍照门控通过 → ImageSnap snap(≤8M) → /tmp/media/ + info.json
9. ~ImageSnap + HalProvider::resetSharedVideo()（IMP_System_Exit，仅 work 模式走过 IMP 时）
10. 若 force_upload 消费过 → read-modify-write 回 force_upload=0
10.7. **同步上行准备**（仅 SNAP_UPLOAD / UPLOAD_ONLY 模式）：按 quicksnap.json `network` 字段
      spawn `htc_net_app`——`"4g"`(默认) → `--type usb --usb-bringup`；`"wifi"` → `--type wifi`
      （不带凭据，MCU 读 UPID/UPWD）。退出码 !=0 → 中止不 spawn wm；sim 下 `#ifdef BUILD_FOR_SIMULATION`
      bypass（无 HW）。未知 network 值降级 "4g" + 警告。
11. fork+execv 目标（释放之后）：SNAP_ONLY→不spawn / SNAP_UPLOAD→wm -m 2 / UPLOAD_ONLY→wm -m 3 / TEST_ONLY→um / force_upload→wm -m 2
12. exit
```

---

## 8. 复用（无否决即默认）

| 模块 | 复用源 | 用途 |
|------|--------|------|
| 时间同步 | `app_lifecycle::syncSystemTime()`（`ProcessLifecycle.cpp`） | 本进程时间戳（结果不传下游） |
| GPIO / mode | `WorkMode::getWorkingMode()`、`GPIO` 类 | 读模式脚 + power hold |
| 拍照 | `ImageSnap`（`src/media/snap/`） | ≤8M HW scaler |
| IMP 释放 | `HalProvider::resetSharedVideo()`（`HalProvider.cpp:69`） | 干净 IMP_System_Exit |
| 目录创建 | `createDirectory`（递归 mkdir(2)，无 shell） | /tmp/media |
| sim | 镜像 `media_app` 的 sim 处理（GPIO/IMP stub） | 双平台编译 |

---

## 9. 风险与关注点

### 9.1 ⚠️ `PC(8)` 一脚两用（既有 oddity，需澄清）

`Common.h:187` `WORKING_MODE_CHECK_PIN_1 = PC(8)` 与 `:191` `POWER_HOLD_PIN = PC(8)` **是同一根 pin**。media_app 先把 PC8 设 OUTPUT HIGH（保持供电），再用 PC9/PC8 读模式 → PC8 被驱动，读出来恒为 HIGH。这是**既有行为**（非 quickSnap 引入），但与"简洁稳定"目标有张力。需判断：板级 mux？还是历史 bug？是否在 quickSnap 里一并澄清。（暂列待确认，不阻塞主干。）

### 9.2 ⚠️ `/tmp` tmpfs 大小

照片落 `/tmp/media/`（RAM）。ZL 64MB 下需验证 `/tmp` 挂载上限 + burst 上限，避免暂存照片（每张 ≤8M JPEG ~1–3MB × burst）撑爆 RAM。ADR 说 media_app 阶段 SD 可能未 mount，故走 /tmp 是有意的——保留，但须量上限。

### 9.3 cm==1 wedge —— 存疑，非 quickSnap 风险

见 §4。ZL 目标架构 1-IMP-per-boot，wedge 前提不成立。仅留 §4.1 干净退出作卫生；若未来 wm/um 出现 IMP 使用另查。**不下"第 2 个 IMP 进程必炸"的死结论。**

### 9.4 γ 闭环依赖

quickSnap 只读 quicksnap.json 的正确性依赖 §2.3 的 wm/um 迁移完成。迁移未完成前，cameraMode 存在 quickSnap(读 quicksnap.json) 与 wm(读 setting.json) 双源 divergence 风险。

### 9.5 T20 4G 同步准备（spawnAndWait htc_net_app）

**T20-RV-sim-bypass-masks-regression (medium)**：sim 下 `#ifdef BUILD_FOR_SIMULATION` bypass 4G 步骤，若真机 4G 逻辑有 bug，sim 测不出（sim 根本不跑 htc_net_app）。缓解：真机回归必跑（拔/插 dongle 两场景）；sim bypass 仅编译期生效，真机 `#else` 完整 gate。

**T20-RV-usb-model-default-EC20 (medium)**：`--usb-model` 不传，走默认 EC20（`net_app.cpp:93`）。真机若 EC200A/EG800K/RG255AA，`setModel` 用错型号（`net_app.cpp:434`）→ AT 指令分支错 → 退出码 3 → quickSnap 中止。表象：4G 总是失败。缓解：真机回归时确认 dongle 型号；若非 EC20，在 `prepareNetwork4g` argv 加 `--usb-model`。

**T20-RV-binary-missing (medium)**：`htc_net_app` binary 不存在或路径错（NFS 未挂、build 未产出）→ execv 失败 → 退出码 127 → 中止。缓解：`Misc::getExecutablePath()` 定位（同 wm/um 已验证）；退出码 127 + 日志 "execv failed" 可定位。

**T20-RV-waitpid-mem-footprint (low-medium)**：waitpid 期间 quickSnap 阻塞，两进程并存。缓解：Step 9 已 `resetSharedVideo`（IMP_System_Exit）→ fork 时 quickSnap 已 lean，且 htc_net_app 是 fork+execv 不经 sh，比 system() 省进程。留真机 MemFree 验证。

**T20-RV-4g-blocks-heartbeat (low)**：UPLOAD_ONLY（heartbeat）也前置 4G，若 dongle 未插 → heartbeat 发不出 → 中止。符合当前"4G 失败=中止"决策；若产品需"尽力而为"，另开任务。

**T20-RV-no-timeout (low)**：`spawnAndWait` 无超时，若 htc_net_app 卡死（驱动 hang），quickSnap 永久阻塞。缓解：htc_net_app 是 connect-once-then-exit 前台工具，有内部重试上限；若真机观测到 hang，后续加 `alarm()`/`SIGALRM`。

### 9.6 T21 配置驱动上行选择（network 字段）

**T21-RV-wifi-needs-mcu-creds (medium)**：WiFi 模式依赖 MCU UPID/UPWD 已配。
htc_net_app wifi 在 --ssid 空 + MCU 读空 → ABORT exit 6（net_app.cpp:247-254）。表象：
"WiFi prepare failed (exit=6)"。缓解：测试设备先配 WiFi 凭据到 MCU（经 um 或 htc_mcu_api_test
writeUPID/writeUPWD）；或临时改 htc_net_app 传 --ssid/--pwd（仅测试）。

**T21-RV-unknown-fallback-masks-misconfig (medium)**：未知 network 值降级 "4g" + 警告。
若用户意图写 "wifi" 但误拼（如 "Wifi"/"WIFI"/"wi-fi"），静默降级 4G → 测试期 4G 仍失败
（无 dongle）→ quickSnap 仍中止。缓解：日志 WARNING 含原值；测试期查日志确认 network 值。

**T21-RV-wifi-dhcp-slow (low-medium)**：WiFi DHCP 可能比 4G 慢或不稳定（取决于 AP）。
spawnAndWait 无超时（T20 决策），DHCP 卡住 → quickSnap 阻塞。缓解：同 T20-RV-no-timeout；
真机观测到 hang 再加 alarm()。

**T21-RV-save-drops-network-on-force-upload (low)**：force_upload 写回时（save() 重建 root），
若 save 不补塞 network，json 里 network 字段会丢。缓解：save() 加 `root["network"] = network`
（见 planner A.4 方案 1）。已纳入实现。

---

## 10. 外部依赖

- **wm 侧新增 `-m 3`**（heartbeat 档：**单次** sendHeartbeat → 关机；无补传）。wm 的实现项，quickSnap 只 spawn `wm -m 3`。
- **wm `-m 2`/`-m 3` lean 启动**（无卡 / 无 DB / 无重传；m2 直读 `/tmp/media/` 造 desc 上传、不经 SD）：wm 侧实现项，见 [`wm-app-spec.md`](wm-app-spec.md) §2.1。quickSnap 的 handoff 契约依赖它。
- **`QUICK_SNAP_DIR` 常量改名** `/tmp/quick_snap/` → `/tmp/media/`（`app.h`）；wm `-m 2` 读同一 `/tmp/media/` 目录（**不再经 `processCmdSnap`**），同步改。
- **`fork+execv` helper**：src/ 无现成，quickSnap 自写。

---

## 11. 决策日志（grill 2026-07-09）

| # | 决策点 | 结论 |
|---|--------|------|
| 1 | 改旧 app vs 新建 vs 重命名 | **B：原地重构 `htc_media_app` → `quickSnap`**，不新建并行二进制 |
| 2 | 静态二进制 | **暂搁置**；"少加载 .so"的真正杠杆是砍依赖面（取决于拍照路径），非 `-static` |
| 3 | 配置架构 | **γ：quicksnap.json 为 5 字段唯一真相源**，quickSnap 只读它、不读 setting.json |
| 4 | force_upload 归属 | 进 quicksnap.json（um/Power 写，quickSnap 读+写回 0） |
| 5 | quicksnap.json 路径 | **绝对路径** `/config/htc/quicksnap.json`（sim `./res/quicksnap.json`），不绑二进制位置 |
| 6 | 拍照路径 | **≤8M HW scaler**，不走 strip/SIMD；`stillSize` 内存钳到 ≤8M、不写回 |
| 7 | IMP 释放 | **ImageSnap + 末尾 `resetSharedVideo()`** 强制 IMP_System_Exit（方案 1） |
| 8 | 调度机制 | **fork+execv（无 shell）**，禁 system_call；daemon 不再拉起 |
| 9 | 调度目标 | work→`wm -m 2`、heartbeat→`wm -m 3`、user→`um`；SNAP_ONLY 不 spawn |
| 10 | 模式分类 | work / heartbeat(=UPLOAD_ONLY) / user(=TEST_ONLY)，GPIO 暂沿用旧枚举 |
| 11 | wedge 定性 | **存疑、非 quickSnap 风险**（ZL 1-IMP-per-boot，前提不成立） |
| 12 | user 模式 IMP | quickSnap **不 init IMP**（um 自行 init 作首进程） |
| 13 | handoff 契约 | 不再 `-wm -rtc`；`wm -m <2\|3>` 无 -rtc，`um` 无 flag；RTC 结果不传下游 |
| 14 | m2 上传契约（续） | wm `-m 2` **直读 `/tmp/media/` 造 desc 上传、不搬移 SD**（lean 无卡/无 DB/无重传）；`processCmdSnap` 搬移路径作废。详见 [`wm-app-spec.md`](wm-app-spec.md) §2.1 / §12.2 |

### 11.1 决策日志（grill 2026-07-10 续：addMedia 归属 + CH2 全关 + HAL 配置架构）

| # | 决策点 | 结论 |
|---|--------|------|
| 15 | addMedia 归属 | **方案 C**：`ImageSnap` 改为纯拍照类（只写 JPEG），`addMedia` 从 `ImageSnap` 的 3 个 internal 站点移除，归还调用方。wm `snap_task` + 主 app `CameraServiceT32::takePhoto` 各自 `addMedia`；`CameraServiceT32:411` capture-to-memory 顺带修了"索引后立即 remove 的孤儿 DB 行"latent bug；`media_app`（退役）/ `WorkModeRunner:159`（test fallback）/ `CameraServiceSim` 不动。 |
| 16 | quickSnap 入库 | **不入库**——quickSnap 全程不 init `DatabaseManager`；§5 的 no-DB 契约靠 addMedia 移出 `ImageSnap` 落实（不再是"调了注定失败"的假 WARNING）。 |
| 17 | CH2 缩略图 | **两层全关**：① HAL `buildResidentChannels` 的 group2/CH14 由 `withThumb=false` 门控；② `ImageSnap::setThumbnailEnabled(false)`。只关②省不下主体内存（group2 编码器通道仍 idle 占内存），故两层都必须关。 |
| 18 | HAL 配置架构 | **env 全面退役**：新 `struct HalVideoConfig{residentMode, withThumb}`，boot 入口调 `HalProvider::start(cfg)` 显式声明；`buildResidentChannels` 读成员不再 `getenv("HTC_HAL_RESIDENT_MODE")`。`sharedVideo()` 保留 lazy 构造但用 start 设的 config（未调默认 `{-1,true}`）——cm==1 reset→re-init 自动复用 config，um/main_app/test 零改动。**未走 pure-accessor**：main_app 是 per-command lazy init IMP（非相机命令不能 init IMP），pure-accessor 要么逐命令埋 start（易漏）要么 early-init（行为变化），代价不值。 |
| 19 | 本次迁移面 | quickSnap + wm 调 `start(cfg)`（仅非默认 config 的入口）；um/main_app/snap_test/singleton_harness 继承默认，零改动；capture_lane cm==1 无需改（reset 后 sharedVideo 自动 re-init）。`src/hal/**` 改动经 maintainer 批准（hal 已转 maintainer）。 |

### 11.2 决策日志（2026-07-10 续：T20 4G 同步准备）

| # | 决策点 | 结论 |
|---|--------|------|
| 20 | 4G 同步时机 | SNAP_UPLOAD/UPLOAD_ONLY 在 spawn wm 之前，同步 `spawnAndWait htc_net_app --type usb --usb-bringup`；SNAP_ONLY/TEST_ONLY 不加 4G。 |
| 21 | 4G 失败语义 | 退出码 !=0 → **中止不 spawn wm**，记日志含退出码含义 `[2=driver 3=connect 4=dhcp 6=arg]`；quickSnap return 1。 |
| 22 | sim bypass | **`#ifdef BUILD_FOR_SIMULATION`** 包整个 `prepareNetwork4g` 函数体的 sim 分支（直接 return true），不 spawn htc_net_app。真机 `#else` 完整 gate，编译期隔离。 |
| 23 | --usb-model | **不传**，走 EC20 默认（`net_app.cpp:93`）。型号是硬件事实，不应硬编码在 boot 程序；真机若非 EC20，由 provisioning 配置。 |
| 24 | spawnAndWait vs 复用 spawn | **新增独立函数**（不合并）——语义不同（wait vs fire-and-forget），wm/um 需要不等待的语义。 |
| 25 | CMake link 依赖 | **不加** network/system_call（4G 全在 htc_net_app 子进程，quickSnap 只用 fork/waitpid/libc）。 |
| 26 | UPLOAD_ONLY heartbeat 4G | **也加 4G 前置**（heartbeat 要网络发心跳包），失败同样中止。符合用户决策。 |

### 11.3 决策日志（2026-07-10 续：T21 配置驱动上行选择）

| # | 决策点 | 结论 |
|---|--------|------|
| 27 | 上行字段名 | `network`，取值 `"4g"`/`"wifi"`。 |
| 28 | 默认值 | 缺省/未知 → `"4g"`（保持产品现状/向后兼容）。 |
| 29 | WiFi 凭据 | quickSnap **不传** `--ssid/--pwd`；htc_net_app wifi 从 MCU 读（net_app.cpp:212-217）。 |
| 30 | 失败语义 | 任意上行模式 exit !=0 → quickSnap 中止（同 T20 决策 21）。 |
| 31 | network 写回 | save() 补塞 network 字段（防 force_upload 写回丢字段）；normalize 直接改 cfg.network（接受 force_upload 写回时大写/误拼值被降级，频次极低）。 |
| 32 | sim bypass | 同 T20：`#ifdef BUILD_FOR_SIMULATION` bypass，network 值不影响 sim 行为。 |

---

## 12. 不在范围 / 后续

- **静态二进制**：若要回到"尽量少加载 .so"，杠杆是砍链接面（hal_audio/daynight/app_workmode 全家桶等死重量），且取决于拍照路径；非字面 `-static`（IMP 静态链接有风险）。
- **γ 闭环迁移**（§2.3）：wm cameraMode 读点迁 quicksnap.json；Settings 序列化摘 4 字段影子；um 承担 quicksnap.json 写入。
- **GPIO 直接对照三模式**（避免旧枚举映射）：后续重构。
- **`PC(8)` 一脚两用** 澄清（§9.1）。
- **`htc_media_app` 退役时点**：quickSnap 上线、handoff 切到 `wm -m`/`um` 后退役。
- **`/tmp` tmpfs 上限 + burst 上限实测**（§9.2）。

---

## 13. 关联文档

- [`wm-app-spec.md`](wm-app-spec.md) — 下游 work mode（`wm -m 2/-m 3`；须新增 `-m 3` heartbeat）
- [`um-app-spec.md`](um-app-spec.md) — 下游 user mode（`um`，RTSP 预览）
- [`media-app-boot-flow.md`](media-app-boot-flow.md) — 前身 `htc_media_app` 旧流程（对照）
- [`../decisions/asymmetric-snap-vs-record-design.md`](../decisions/asymmetric-snap-vs-record-design.md) — 拍照在前置进程的省电相机决策（quickSnap 存在的依据）
- [`../decisions/workmode-usermode-process-split.md`](../decisions/workmode-usermode-process-split.md) — wm/um 拆分 ADR
- [`main-app-mode-behavior.md`](main-app-mode-behavior.md) — 旧 app 模式行为（旧 `-wm` 体系对照）
