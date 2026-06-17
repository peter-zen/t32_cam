---
task_id: T2
node: implementer
flow: bug
artifact: evidence
---

# T2 Implementer — Evidence

## 1. 改动 diff 摘要（按文件）

### A. 主修复（非 PIC-owned）

**`src/media/rtsp/RtspServer.h`**
- 新增进程级 teardown 入口 `void shutdown();`（:43），注释说明与 session 级 `stop()` 的区别。
- 新增幂等标志 `bool deinitialized_;`（:65）。

**`src/media/rtsp/RtspServer.cpp`**
- 构造函数初始化 `deinitialized_(false)`（:151，置初始化列表首位以匹配声明顺序，消除 -Wreorder）。
- `deinitialize()`（:198）加 `deinitialized_` 守卫，幂等（重复调用 no-op），防止二次 `video_->exit()` 在 `video_` 已 reset 后段错误。
- 新增 `void RtspServer::shutdown()`（:214）：`stop()`（session 级，停 pull 线程 / media session / rtsp server，已 null-safe + 幂等）+ `deinitialize()`（进程级：`videoSession_.reset()` → `~IngenicVideoStream` DestroyChn/UnBind/DestroyGroup；`video_->exit()` → IMP 全局 exit + OSD region destroy）。不把完整 teardown 塞进会话级 `stop()`（仍被 `onSessionClosed` / `~RtspServer` 复用）。

**`src/app/main_app.cpp`**
- 新增全局标志 `static bool rtsp_singleton_used = false;`（:837），避免在未使用 RTSP 的模式下误触发 `getInstance()` 侧构造 + HAL init。
- mobile 分支（:1622）/ rtsp-server 分支（:1666）首次 `getInstance()` 前置位。
- `main_exit:` 标签后（:1842-1843）、`Misc::poweroff(); while(1);`（:1865-1866）之前调用 `RtspServer::getInstance()->shutdown()`。`main_exit` 是 mobile / rtsp-server / performCleanup / 各 start-fail goto 的汇聚点，单点接入覆盖 dispatch 要求的全部退出路径。

### B. PIC-owned 兜底（用户已书面授权，本次允许改 src/hal/**）

**`src/hal/ingenic/IngenicVideo.cpp` `IngenicVideo::exit()`（:1190-1246）**
- 在 `ispOsdMgr_->exit()` / `fsMgr.destroy()` / `IMP_System_Exit()` 之前，基于文件级 static 真相源 `g_bind_ref_count` / `g_group_ref_count` 做兜底 teardown：
  - 遍历 `g_bind_ref_count` 的 key，按 configure 同款 fs/enc cell（DEV_ID_FS / DEV_ID_ENC, groupID=grp, outputID=0）调 `IMP_System_UnBind`，清空 map。
  - 遍历 `g_group_ref_count` 的 key（RTSP H264 下 channel_id == group_id），`IMP_Encoder_Query` → 若 registered 则 `IMP_Encoder_UnRegisterChn`，再 `IMP_Encoder_DestroyChn` + `IMP_Encoder_DestroyGroup`，清空 map。
- 幂等：stream 析构已清则 map 为空 → no-op；可空：未 init 则 ref-count 路径短路。

**`src/hal/ingenic/IspOsdManager.cpp` `exit()`（:90-113）**
- 注释明确 `stop()` 已对 `timeHandle_` / `reservedHandle_` 做 `IMP_ISP_Tuning_DestroyOsdRgn`，覆盖 "prepare 创建 region 但 start 被跳过" 路径。
- 增加 defensive fallback：`stop()` 后若 handle 仍 >= 0，强制 `IMP_ISP_Tuning_DestroyOsdRgn` 再清一次，防跨进程 region 泄漏。

## 2. SIM 安全性

- HAL provider 抽象（`src/hal/HalProvider.cpp`）：`BUILD_FOR_SIMULATION=ON` 时 `createVideo()` → `SimVideo`，`SimVideo::exit()` 返回 true（no-op）。`RtspServer::shutdown()` → `video_->exit()` 在 SIM 下落到 SimVideo::exit()，安全。
- `IspOsdManager` 仅在 `IngenicVideo::init()`（:1153）实例化；SIM 用 SimVideo，不构造 IspOsdManager，OSD 代码在 SIM 下不执行。`IngenicVideo.cpp` 的 IMP 兜底调用在 SIM 下不被链接进 htc_main_app（ IngenicVideo.cpp 属硬件 HAL target，SIM 构建不编译）。
- `imp_stub.c` 提供 `IMP_System_UnBind` / `IMP_Encoder_DestroyGroup` / `DestroyChn` / `UnRegisterChn` / `Query`（:97/196/221/232/557），均为 no-op，SIM 不会 crash。

## 3. 双平台 build 成功输出

### T32 硬件（`build/`，uclibc，toolchain 已配好）
```
$ cmake --build build -j$(nproc)
[ 92%] Built target media_rtsp
[100%] Built target htc_main_app
=== exit code: 0 ===
```
（完整尾部：`[100%] Linking CXX executable ../../bin/htc_main_app` → `Done flattening symlinks` → `Built target htc_main_app`。无 `error:`。）

### PC 模拟（`build_sim/`，BUILD_FOR_SIMULATION=ON）
```
$ cmake --build build_sim -j$(nproc)
[ 89%] Built target media_recorder
[ 91%] Built target media_rtsp
[ 92%] Built target htc_daemon_app
[ 93%] Built target htc_media_app
[ 94%] Built target snap_test
[ 97%] Built target camera_service
[ 98%] Built target test_camera_properties
[100%] Built target http_server
[100%] Built target test_http_server
[100%] Built target htc_main_app
=== exit: 0 ===
```
（无编译 `error:`；grep 命中的 "error" 均为源码 Logger 字符串字面量，非编译错误。）

## 4. grep 静态确认（退出路径调用新 teardown + 兜底存在）

```
=== 1. RtspServer shutdown() 声明+定义 ===
src/media/rtsp/RtspServer.h:43:        void shutdown();
src/media/rtsp/RtspServer.h:65:        bool deinitialized_;
src/media/rtsp/RtspServer.cpp:151:    : deinitialized_(false)
src/media/rtsp/RtspServer.cpp:198:void RtspServer::deinitialize()
src/media/rtsp/RtspServer.cpp:200:    if (deinitialized_) {
src/media/rtsp/RtspServer.cpp:214:void RtspServer::shutdown()

=== 2. main_exit 调用 shutdown + rtsp_singleton_used 标志 ===
src/app/main_app.cpp:837:static bool rtsp_singleton_used = false;
src/app/main_app.cpp:1622:            rtsp_singleton_used = true;        # mobile
src/app/main_app.cpp:1666:        rtsp_singleton_used = true;           # rtsp-server
src/app/main_app.cpp:1842:    if (rtsp_singleton_used) {
src/app/main_app.cpp:1843:        RtspServer::getInstance()->shutdown();  # 在 poweroff/while(1) 前

=== 3. IngenicVideo::exit 兜底 destroy ===
src/hal/ingenic/IngenicVideo.cpp:1210: [HAL] exit: fallback UnBind(group=%d)
src/hal/ingenic/IngenicVideo.cpp:1211: IMP_System_UnBind(&fs_cell, &enc_cell);
src/hal/ingenic/IngenicVideo.cpp:1215: g_bind_ref_count.clear();
src/hal/ingenic/IngenicVideo.cpp:1229: [HAL] exit: fallback destroy chn/group
src/hal/ingenic/IngenicVideo.cpp:1233: IMP_Encoder_UnRegisterChn(chn);
src/hal/ingenic/IngenicVideo.cpp:1235: IMP_Encoder_DestroyChn(chn);
src/hal/ingenic/IngenicVideo.cpp:1236: IMP_Encoder_DestroyGroup(grp);
src/hal/ingenic/IngenicVideo.cpp:1240: g_group_ref_count.clear();

=== 4. IspOsdManager exit 兜底 DestroyOsdRgn ===
src/hal/ingenic/IspOsdManager.cpp:68:  IMP_ISP_Tuning_DestroyOsdRgn(0, timeHandle_);   # stop() 内
src/hal/ingenic/IspOsdManager.cpp:80:  IMP_ISP_Tuning_DestroyOsdRgn(0, reservedHandle_); # stop() 内
src/hal/ingenic/IspOsdManager.cpp:104: IMP_ISP_Tuning_DestroyOsdRgn(0, timeHandle_);     # defensive fallback
src/hal/ingenic/IspOsdManager.cpp:108: IMP_ISP_Tuning_DestroyOsdRgn(0, reservedHandle_); # defensive fallback
```

## 5. 未做（按 dispatch 约束）

- 未运行 T32 硬件二进制（PC 跑不动 MIPS；设备二次启动验证留给用户）。
- 未 git commit / push / rollback（用户未授权）。
