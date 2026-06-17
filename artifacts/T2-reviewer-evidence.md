---
task_id: T2
node: reviewer
flow: bug
artifact: evidence
---

# T2 Reviewer — Evidence (独立审查)

结论:**passed / success**。双平台独立重编译均 exit 0;teardown 接入正确(在
`Misc::poweroff()/while(1)` 之前、覆盖全部 `goto main_exit` 汇聚路径);SIM 安全;
幂等/可空/无双释放;无明显回归风险。设备侧二次启动回归需用户在 T32 硬件做(PC 跑不动 MIPS)。

## 1. 独立重编译(不依赖 implementer 声明,亲自跑)

### T32 硬件(`build/`,uclibc,既有 toolchain,未 rm)
```
$ cmake --build build -j$(nproc)
[ 92%] Built target media_recorder
[ 92%] Built target media_rtsp
[ 93%] Built target snap_test
[ 94%] Built target htc_daemon_app
[ 94%] Built target htc_media_app
[ 97%] Built target camera_service
[ 99%] Built target http_server
[100%] Built target htc_main_app
=== T32 build exit: 0 ===
```
(后台任务 breglm68s,exit code 0。无 `error:`。)

### PC 模拟(`build_sim/`,BUILD_FOR_SIMULATION=ON,既有目录)
```
$ cmake --build build_sim -j$(nproc)
[ 91%] Built target media_rtsp
[ 92%] Built target snap_test
[ 93%] Built target htc_daemon_app
[ 94%] Built target htc_media_app
[ 97%] Built target camera_service
[ 98%] Built target test_camera_properties
[100%] Built target http_server
[100%] Built target test_http_server
[100%] Built target htc_main_app
=== SIM build exit: 0 ===
```
(后台任务 b46drgepb,exit code 0。无 `error:`。)

**独立验证结论:success** — implementer 的双平台构建声明属实。

## 2. teardown 接入正确性(dispatch 必查 2)

### 2.1 shutdown 在 poweroff/while(1) 之前 — **passed**
`src/app/main_app.cpp`:
- `main_exit:` 标签 = line 1832(所有 `goto main_exit` 的汇聚点,grep 共 40+ 处 goto)
- `if (rtsp_singleton_used) RtspServer::getInstance()->shutdown();` = line 1842-1844
- `Misc::poweroff(); while(1);` = line 1875-1876(在 `#if POWER_MANAGER_ON` 内)
- SIM 末尾 `_exit(0)` = line 1870

shutdown **严格在** freeze/poweroff **之前**执行。单点接入(`main_exit` 标签内)
覆盖 mobile / rtsp-server / performCleanup / 各 start-fail goto 全部退出路径。

### 2.2 rtsp_singleton_used 门控正确 — **passed**
grep 确认 RtspServer 单例仅在 main_app.cpp 两处使用:
- mobile 分支:`rtsp_singleton_used = true` (line 1622) → `getInstance()` (1623/1626/1627/1649)
- rtsp-server 分支:`rtsp_singleton_used = true` (line 1666) → `getInstance()` (1667/1670/1671/1679)
- `RtspServer::getInstance()` 在 main_app.cpp 之外**无任何调用**(grep 全 src 确认)

置位与首次 `getInstance()` 一一对应,**无遗漏退出路径、无误构造单例**(非 RTSP 模式
如 `-wm` 媒体/抓拍不置位,shutdown 不触发,避免误 init HAL)。

### 2.3 shutdown 实现链路 — **passed**
`src/media/rtsp/RtspServer.cpp:214-225`:
- `stop()`(session 级:null-safe,videoSession_ 存在才 stop、rtsp_server 存在才 destroy)
- `deinitialize()`(`deinitialized_` 守卫,首次置 true 后幂等)
  → `uninitVideo()` (line 499-512):先 `videoSession_.reset()`(触发
     `~IngenicVideoStream` DestroyChn/UnBind/DestroyGroup,基于 ref-count 安全释放)
     → 再 `video_->exit()`(`IngenicVideo::exit()` 全局 teardown)

**关键顺序正确**:per-stream 析构在前(清 map),`video_->exit()` 兜底在后(map 已空 → no-op)。

## 3. SIM 安全(dispatch 必查 3)— **passed**
- `RtspServer::initVideo()` (line 449) `video_ = hal::HalProvider::createVideo()`:
  SIM 下返回 `SimVideo`;`SimVideo::exit()` 为 no-op(return true)。shutdown 路径安全。
- `IngenicVideo.cpp` 属硬件 HAL target,SIM 构建不编译进 htc_main_app。
- `IspOsdManager` 仅在 `IngenicVideo::init()`(IngenicVideo.cpp:1153 附近)实例化;
  SIM 用 SimVideo,**不构造 IspOsdManager**,OSD 代码 SIM 下不执行。
- `imp_stub.c` 提供 `IMP_System_UnBind`/`IMP_Encoder_DestroyGroup`/`DestroyChn`/
  `UnRegisterChn`/`Query` no-op stub,SIM 即使走到也无 crash 风险。

## 4. 幂等 / 可空 / 无双重释放(dispatch 必查 4)— **passed**

### 4.1 deinitialized_ 守卫
`deinitialize()`(RtspServer.cpp:198-212)首行 `if (deinitialized_) return;` 然后
`deinitialized_ = true`,重复调用 no-op。`~RtspServer`(line 169-173)也调 deinitialize,
但被守卫短路,无双调用。

### 4.2 IngenicVideo::exit 兜底 vs ~IngenicVideoStream 无双释放 — **passed**
真相源是文件级 static map:`g_bind_ref_count`(line 55)、`g_group_ref_count`(line 19)。
- `~IngenicVideoStream`(IngenicVideo.cpp:880-891):`configured_` 守卫;调
  `releaseBind`(line 73-86:map 有才 UnBind + erase)、`releaseGroup`(line 37-50:
  map 有且 ref 减到 0 才 DestroyGroup + erase)。析构后对应 key 从 map 移除。
- `IngenicVideo::exit()` 兜底(line 1191-1242):遍历**同一份 map 的剩余 key**。
  正常退出时 stream 析构已清空 map → 兜底 no-op。即使有残留,DestroyChn 前先
  `IMP_Encoder_Query` + `st.registered` 才 UnRegister,且 `chn == grp`(H264 payload
  路径,configure line 899-901 `channel_id_ = group_id_`)假设成立。
- `exitCalled_` + `g_video_init_ref_count`(line 1179-1184):exit 本身幂等 + ref-count。

**无双释放**:map erase 保证 stream 析构与 exit 兜底对同一 group/bind 互斥。

### 4.3 IspOsdManager::exit 幂等可空 — **passed**
`exit()`(IspOsdManager.cpp:90-113):先 `stop()`(line 59-88,handle>=0 才 DestroyOsdRgn
并置 -1),再 defensive fallback(handle 仍>=0 再清一次并置 -1)。`timeHandle_`/
`reservedHandle_` 初始 -1(构造函数 line 15),未创建则全 no-op。重复 exit 安全
(s_instance 已置 nullptr + handle 已 -1)。

## 5. 回归风险(dispatch 必查 5)— **passed(低风险)**

- **第一次启动 / 正常运行**:`rtsp_singleton_used` 默认 false,运行期不触发 shutdown;
  shutdown 仅在 `main_exit` 退出流程单次调用,不会在运行中误触发。
- **`stop()` 行为未被改**:仍 null-safe + 幂等,被 `onSessionClosed`(回调)/
  `~RtspServer`(析构)复用。新增的 `shutdown()` 是独立进程级入口,不污染 session 级语义。
- **`-wm` 等非 RTSP 模式**:不置位 `rtsp_singleton_used`,shutdown 不调用,
  不误构造 RtspServer 单例 / 不误 init HAL。无副作用。
- **mobile/rtsp-server 已有的 `stop()` 调用**(line 1649/1679):仍在,shutdown 内
  会再调一次 stop,但 stop 幂等(null-safe,rtsp_server 已 nullptr),无害。

## 6. 缺测试(dispatch 必查 6)

- 本仓 `build_sim/bin/` 有 `snap_test`/`test_camera_properties`/`test_http_server`/
  `test_mcu_service` 等单测,但 teardown/exit 走真实 IMP 内核驱动(嵌入式硬件路径),
  **SIM 用 imp_stub no-op,无法覆盖真实残留场景**。无合适的单测挂载点。
- 设备侧回归脚本(留用户在 T32 执行):
  ```
  # 修复前 bug:第二次卡在 i264e[info] 之后;修复后应通过。
  cd /mnt/huntcam
  for i in 1 2 3 4 5; do
    LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH ./bin/htc_main_app -m --force-day &
    PID=$!
    sleep 12   # 等待 "RTSP server started on port"
    kill -TERM $PID
    wait $PID 2>/dev/null
    sleep 2
  done
  # 验收:每次启动日志均出现 "rtsp stream info" + "RTSP server started on port"
  # 且退出日志出现 "RtspServer::shutdown: teardown complete" + "[HAL] exit:" 标志
  ```
  pass 标准(analyst evidence §7):连续 5 次 kill+重启不卡死,第 N 次启动能拉到流。

## 7. 遗留观察(非阻断)

- `IngenicVideo::exit()` 兜底遍历全部 group key 统一 destroy:单例进程级 teardown 场景
  下,正常退出时所有 stream 已析构、map 为空(no-op);即便存在非 RTSP 共享 group
  (如 media_recorder 也用 group_id 0),destroy 已配对资源正是兜底意图。无实质风险。
- 设备侧回归未验证(PC 不能跑 MIPS),作为已知风险留给用户,不阻断 merge(静态链路正确)。

## 8. 证据指针

- `src/media/rtsp/RtspServer.cpp:142-148`(单例 call_once)、`:150-160`(构造)、
  `:169-173`(~RtspServer)、`:198-212`(deinitialize + 守卫)、`:214-225`(shutdown)、
  `:227-249`(stop,null-safe)、`:499-512`(uninitVideo:videoSession_.reset→video_->exit)
- `src/app/main_app.cpp:832-837`(rtsp_singleton_used 声明)、`:1622/1666`(置位)、
  `:1832`(main_exit 标签)、`:1842-1844`(shutdown 调用,在 poweroff/while(1) 前)、
  `:1875-1876`(Misc::poweroff/while(1))、`:1870`(SIM _exit(0))
- `src/hal/ingenic/IngenicVideo.cpp:880-891`(~IngenicVideoStream)、`:894-975`(configure,
  channel_id==group_id for H264)、`:1178-1258`(exit 兜底 + 顺序:兜底→OSD→fsMgr→
  IMP_System_Exit→sensor→ISP→Encoder_MultiProcessExit)
- `src/hal/ingenic/IspOsdManager.cpp:59-88`(stop)、`:90-113`(exit defensive fallback)
