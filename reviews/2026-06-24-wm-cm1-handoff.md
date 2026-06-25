# Handoff: wm app cm==1 (photo+record) wedge 修复 — 2026-06-24

> 上一会话 context 满，本文自包含，新会话读完即可继续。**目标**：修 wm app 的 cm==1（photo+record）wedge。
> 配套必读：[`wm-app-spec.md`](../doc/knowledge/specs/wm-app-spec.md)（权威 spec）、[`2026-06-24-wm-slice2-capture.md`](2026-06-24-wm-slice2-capture.md)（Slice 2 + cm==1 诊断）、[`2026-06-24-wm-slice1-m2.md`](2026-06-24-wm-slice1-m2.md)、[`2026-06-23-time-chain-hw-verification.md`](2026-06-23-time-chain-hw-verification.md)。

## 1. 总目标

新建独立 binary `wm`（丢弃 crash 不收敛的 `htc_workmode_app`），重组 Phase-1 已验证模块。`wm -m 0`(CAPTURE_ONLY) / `-m 1`(CAPTURE+UPLOAD) / `-m 2`(UPLOAD_ONLY)。当前卡在 **cm==1（photo+record 组合）wedge**。

## 2. 已完成 + 已验证（GREEN，勿回退）

- **wm app 主体**：`src/app/wm_app.cpp` + `src/app/workmode/{wm_time,wm_scheduler,snap_task,capture_lane}.{h,cpp}` + CMake `wm` target（双平台）。
- **时间链**：`time_test` 7/7 GREEN（`acquireTimeChain`/`writebackMcuTime` 已 lift 进 wm）。
- **HW 矩阵**（`tests/host/test_wm.py`，1-boot-1-case）：m2-upload / m0-photo / m0-record / m1-photo / m1-record **5 PASSED**。**wm 最大风险（IMP 捕获 crash）已排除**。
- **已 commit**：`1df6b1c`（wm + stabilization，已 push 到 `origin/merge_develop_simu`）。
- **本会话 cm==1 debug 期间改动（未 commit）**：
  - `src/media/video/SharedVideo.{h,cpp}`（新，进程级 IngenicVideo 单例，VideoRecorder+ImageSnap 共享）。
  - `src/media/snap/ImageSnap.cpp`（改用 `media::sharedVideo()`，不再 createVideo/`exit()`）。
  - `src/media/video/VideoRecorder.cpp`（用 `media::sharedVideo()`，删 file-static）。
  - `src/media/video/CMakeLists.txt`（SharedVideo.cpp 入 media_recorder + link daynight）、`src/common/misc/CMakeLists.txt`（link jsoncpp）、`src/media/snap/CMakeLists.txt`（PRIVATE link media_recorder + include）。
  - `src/app/workmode/wm_scheduler.cpp`（oneShot 改为首次触发后立即 mask）。
  - `.claude/CLAUDE.md`（hal/ 归属：PIC → 项目维护者）。
  - 这些都是**真实改进**（cm==1 从「立即 wedge」进展到「record started 后 wedge」；build 修复）。**待 commit**。

## 3. cm==1 wedge 根因（已诊断到 hal/ 层，definitive）

**现象**：`wm -m 0 HTC_WM_CAMERA_MODE=1`（photo+record）→ photo 完成 → record start 成功（`record started duration=30s`）→ **record 过程中 kernel wedge**（设备 shell 无响应，需硬断电）。

**关键事实**（新会话勿重证）：
- ImageSnap（photo, JPEG）encoder channel = `12 + sensor*3 + stream`（CH0 JPEG → **chn 12**）；VideoRecorder（record, H264）encoder channel = `sensor*3 + stream`（CH0 H264 → **chn 0**）。**encoder channel 不同**（不是 encoder 冲突）。
- 两者**共享 framesource group 0**（CH0 主通道）。
- 释放机制相同：`~IngenicVideoStream`（`src/hal/ingenic/IngenicVideo.cpp:881`）→ StopRecvPic → `releaseFrameSource`(DisableChn, ref-counted) → UnRegisterChn → releaseBind(UnBind) → DestroyChn → releaseGroup。**看起来彻底**。
- **VideoRecorder H264→H264 重复 work**（`test_record_repeat`：1 进程 3 段 GREEN）→ 同 payload 释放干净。
- **ImageSnap JPEG → VideoRecorder H264 wedge**（record 过程中）→ **JPEG→H264 payload 切换**，release 没把 framesource group 0 完全重置给 H264。
- `daynight_switch` 是 no-op（排除 ISP daynight）。
- sample-Encoder-video 单独也 fail（24MB pool，其 own config 太大）—— 红鲱鱼，不代表 IMP 不支持顺序。

**结论**：根因在 **`src/hal/ingenic/IngenicVideo.cpp`** 的 JPEG→H264 framesource 释放/重置间隙（hal/ 现归项目维护者，可改，需讨论批准）。**用户坚信 in-process 顺序 photo+record 应可行**（CH0 拍照→正确释放→CH0 应干净→CH0 录影应正常）—— wedge = release bug，不是 fundamental 限制。

## 4. 已批准的修复计划（4 步，新会话从 Step 1 继续）

1. **Step 1 — instrument hal/（无害，仅日志）**：在 `IngenicVideoStream::configure/release/start`（`src/hal/ingenic/IngenicVideo.cpp`）每个 IMP 调用（SetChnAttr/CreateChn/Bind/DisableChn/DestroyChn/EnableChn/PollingStream）前后加 rc 日志。跑 cm==1 → wedge → **reset → 读 app.log**（`/mnt/sdcard/logs/app.log`，SD 上跨 reset 存活）→ 最后一条成功日志 = wedge 点。** pinpoint 缺失的 IMP 重置调用**。
2. **Step 2 — 修（same-payload-safe）**：根据 pinpoint，在 release 加 **clean framesource reset**（确保下次 configure 干净启动）。设计上对同 payload 无害（photo→photo / record→record 本就 re-configure + 已 GREEN）。
3. **Step 3 — 验证（回归保障）**：修后重跑**全矩阵** —— photo→photo（multi-photo）、record→record（`test_record_repeat`）**必须仍 GREEN**；cm==1（photo→record）变 GREEN。这证明对同 payload 无影响。
4. **Step 4 — cm==1 = 聚合 task（用户设计方向）**：把 `CaptureLane` cm==1 改成**一个不可分割的 atomic task**（触发 → 依序 photo 再 record，isBusy 在两者期间都 true），而非两个顺序 task 调用。让 scheduler 的 task 逻辑更清晰。

## 5. 关键文件 + 事实（新会话速查）

| 文件 | 作用 / 关键行 |
|------|--------------|
| `src/hal/ingenic/IngenicVideo.cpp` | **cm==1 根因所在**。`IngenicVideoStream::configure`(:905, channel_id JPEG=12+/H264=group)、`~IngenicVideoStream`(:881, release)、`releaseFrameSource`(:114, ref-counted DisableChn)、`IngenicVideo::exit`(:1189, IMP_System_Exit)。**现可改（讨论批准）**。 |
| `src/media/video/SharedVideo.{h,cpp}` | 进程级 IngenicVideo 单例（新）。单 TU 定义（不能 inline header，跨 .so 会多实例）。 |
| `src/media/snap/ImageSnap.cpp` | photo。`SNAP_STREAM_ID=0`（ImageSnap.h:15），`initialize`(:108) 用 sharedVideo，`deinitialize`(:209) 不 exit。`daynight_switch`(:648) no-op。 |
| `src/media/video/VideoRecorder.cpp` | record。`VIDEO_STREAM_ID=0`（VideoRecorder.h:21），`initVideo`(:1007) 用 sharedVideo，`HTC_RECORD_STREAM_ID=1` 可改 stream(:1019)。`uninitVideo`(:1197) 不 exit。 |
| `src/service/camera/CameraRecorder.cpp:316` | `releaseVideoResources`（repeated-record 的 release，注释说 exit 是**stale**，实际走 ~VideoRecorder 不 exit）。 |
| `src/app/workmode/capture_lane.cpp` | cm==1 路由：`snap_->trigger()`（sync）→ `record_->trigger()`（async）。**Step 4 要改成聚合 task**。 |
| `src/app/workmode/snap_task.cpp` | photo task（sync ImageSnap::snap + saveThumbnail + createDescInfoFile + enqueue）。 |
| `tests/host/test_wm.py` | 矩阵。m0-both/m1-both 现为 `xfail`（cm==1 wedge）；修后应 un-xfail。 |

**设备 / devtest 约束**：
- T32 经 `tools/devctl/devctl`（broker 拥 `/dev/ttyUSB0`）。`devctl bringup`（SD→WiFi→NFS noac）→ `devctl run --timeout N "<cmd>"`。
- **1-wm-per-boot**：每个 IMP-using wm run 必须 fresh boot（用户硬断电 reset；soft-reboot 断 WiFi）。
- **app.log**（`/mnt/sdcard/logs/app.log`）跨 reset 存活（SD 上，wm 启动时 truncate）—— wedge 后 reset + bringup 读它 = wedge 点。serial.log 跨 run 不可靠。
- build host `192.168.0.206`（company env），HW 交叉编译 `./script/build_t32@206.sh`（或 `export PATH=toolchain/.../bin:$PATH; /usr/bin/cmake -DCMAKE_TOOLCHAIN_FILE=toolchain.cmake -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -B build -S .; cmake --build build --target wm`）。
- **thin-loader md5 陷阱**：wm 可执行 md5 不变，改在 `libapp_workmode.so`/`libmedia_snap.so`/`libmedia_recorder.so`——验 `.so` md5（host `build/lib/` == 设备 `/mnt/huntcam/lib/`）。
- 双平台编译：sim（`cmake -DBUILD_FOR_SIMULATION=ON -B build_sim -S .`）+ HW。

## 6. 待决策 / 注意

- **hal/ 修改需讨论批准**（CLAUDE.md 已更新归属，但仍要 propose→approve）。
- **同 payload 回归**（Step 3）是硬保障——别让 cm==1 fix 破坏已 GREEN 的 photo→photo / record→record。
- `htc_workmode_app` 与新 `wm` 并存（未退役）。
- cm==1 之外，wm 全功能已 GREEN + committed。

## 7. 新会话第一步

1. 读本文 + `wm-app-spec.md` + `2026-06-24-wm-slice2-capture.md`。
2. 用户 reset 设备后，执行 **Step 1**（hal/ instrument）→ 跑 cm==1 → 读 app.log → pinpoint wedge IMP 调用。
3. 据 pinpoint 提 **Step 2 fix 方案**（same-payload-safe）→ 用户批准 → 实现 → Step 3 验证 → Step 4 cm==1 聚合 task。
