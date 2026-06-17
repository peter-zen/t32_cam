# T2 — Analyst 根因分析（evidence）

status: success
判定：根因落在 **进程退出/teardown 链路**（main_app.cpp 退出流程 + RtspServer 单例析构未执行），
不在 `src/hal/**` 的 IMP 调用本身；修复主要在 `src/app/main_app.cpp` 与 `src/media/rtsp/RtspServer.cpp`。
非 PIC-owned 区域为主，但 OSD region 销毁的补充涉及 `src/hal/ingenic/IspOsdManager.cpp`，那部分属 PIC-owned，需用户书面同意后由 implementer 改。

---

## 1. 卡死点精确定位

日志顺序（第二次启动）：
1. `RTSP initialize: BUILD_FOR_SIMULATION=OFF ...` → `RtspServer.cpp:179`（`initialize()`）
2. `OSDController: setPoolSize(2) done` → `IngenicVideo.cpp:458`（`init()` line 1121）
3. `IspOsdManager: init done` → `IspOsdManager.cpp:28`
4. `IngenicVideo VTS corrected to 0x0690 (1680)` → `IngenicVideo.cpp:1174`（`init()` 末尾）
5. `rtsp config: sensor=0 stream=1 ...` → `RtspServer.cpp:456-465`（`initVideo()`，configure 前）
6. `i264e[info]: profile Main, level 3.1` ← **x264/IMP 编码器内部日志，由 `IMP_Encoder_CreateChn` 触发**
7. **缺失**：`rtsp stream info: ...`（`RtspServer.cpp:472`，INFO）、`IspOsdManager: CreateOsdRgn done`（`IspOsdManager.cpp:122`，INFO）

### `i264e[info]` 的来源与下一步

`initVideo()`（RtspServer.cpp:429-479）在打完 `rtsp config`（line 456-465）后调用 `stream->configure(cfg)`（line 466）。
`IngenicVideoStream::configure()`（IngenicVideo.cpp:894-976）的步骤：
- `IMP_FrameSource_GetChnAttr` / `SetChnAttr`（line 908/928）
- `acquireGroup` → `IMP_Encoder_CreateGroup`（line 933, 945 前的 group）
- `IMP_Encoder_CreateChn(channel_id_, &chn_attr)`（**line 945**）← 这一步让 IMP 内部 H264 编码器初始化，打印 `i264e[info]: profile Main, level 3.1`
- `IMP_Encoder_RegisterChn(group_id_, channel_id_)`（**line 950**）← `i264e[info]` 之后的下一个阻塞 IMP 调用
- `acquireBind` → `IMP_System_Bind`（line 962-968）
- `IspOsdManager::prepare()` → `ensureRegion()` → `IMP_ISP_Tuning_CreateOsdRgn`（line 969-971, IspOsdManager.cpp:108/114）
- 返回 → 回到 `initVideo()` line 470 `getInfo` → line 472 `logRtspStreamInfo("rtsp stream info", ...)`（INFO）

`acquireBind` 失败只 WARNING 不阻断（IngenicVideo.cpp:66，`IMP_System_Bind < 0` 后仍 return true）；
`prepare()` 返回值在 RtspServer.cpp:969-971 **未被检查**，失败也不阻断 configure。
因此若 configure 完整跑完，必然出现 `rtsp stream info`（INFO）。**日志中缺失此行 = configure 未返回 = 卡在 line 950 `IMP_Encoder_RegisterChn`（最可能）或 line 962 `IMP_System_Bind`。**

判定：`i264e[info]` 之后代码仍在执行（非 3d854ab log demote 掩盖——demote 只改了 MediaSession/rtsp.c 的运行时统计日志，在 create_server 之后，与此处无关）。卡死点是 configure 内 `IMP_Encoder_RegisterChn` / `IMP_System_Bind` 在 IMP 全局状态残留下的阻塞/hang。

## 2. 为什么"第二次才复现"——teardown 缺失

### 2.1 第一次进程退出时 `IngenicVideo::exit()` 从未执行

mobile 模式（`-m`）RTSP 启动在 `main_app.cpp:1615-1643`。退出路径：
- SIGTERM/SIGINT → `signalHandler`（`main_app.cpp:925-946`，a205cb7 引入的 self-pipe）仅 `write(g_signal_pipe[1])`，不做事
- 主循环 `waitForSignalOrTimeout`（line 861-878）唤醒 → `performCleanup`（line 882-923）。**`performCleanup` 只做 daynight/RGB/Settings/HTTP/TCP/mgmt/storage，不调用 `RtspServer::stop()`，不调用任何 HAL exit。**
- 主循环退出 → `main_app.cpp:1642` `RtspServer::getInstance()->stop()`
- → `main_exit` 标签 → `main_app.cpp:1857-1858`：`Misc::poweroff(); while(1);`

T32 硬件模式（非 SIM）main 末尾是 `Misc::poweroff(); while(1);`，**main 永不 return**，
因此 Meyers 单例 `static std::shared_ptr<RtspServer> instance`（RtspServer.cpp:144）的析构 `~RtspServer()`（line 168-172 → `deinitialize()` → `uninitVideo()` → `video_->exit()`）**从未执行**。

### 2.2 `RtspServer::stop()` 也不释放 HAL

`RtspServer::stop()`（RtspServer.cpp:209-231）：`videoSession_->stop()` + `uninitAudio()` + `stop_server`/`destroy_server`。
- `IngenicVideoStream::stop()`（IngenicVideo.cpp:1001-1021）仅 `IMP_Encoder_StopRecvPic` + `releaseFrameSource`（DisableChn），**不 DestroyChn、不 UnBind、不 DestroyGroup、不 exit()**。
- `video_->exit()`（`IngenicVideo::exit()`，line 1178-1200：`fsMgr.destroy()`+`IMP_System_Exit`+sensor disable/del+`IMP_ISP_DisableTuning`+`IMP_ISP_Close`+`IMP_Encoder_MultiProcessExit`）**从不被调用**。

### 2.3 残留的硬件资源 → 第二次 init 卡死

第一次退出残留（进程级，IMP SDK 内核驱动状态）：
- encoder channel（`channel_id_`，IngenicVideo.cpp:945 CreateChn 未配对 DestroyChn）
- encoder group（`acquireGroup`，line 933）+ `IMP_System_Bind`（fs→enc，line 962）未解绑
- `IMP_ISP_Tuning_CreateOsdRgn` 创建的 OSD region（IspOsdManager.cpp:108/114）未 DestroyOsdRgn
- `IMP_System_Init` / `IMP_Encoder_MultiProcessInit` / ISP Open 未配对 Exit/Close

第二次新进程启动，`IngenicVideo::init()` 重新 `IMP_Encoder_MultiProcessInit` + `IMP_System_Init` + ISP/sensor setup 能过（因为新进程），但 `configure()` 里 `IMP_Encoder_CreateChn` 后的 `IMP_Encoder_RegisterChn`（line 950）/`IMP_System_Bind`（line 962）遇到 IMP 驱动里残留的 group/channel/bind 状态，**阻塞或失败 hang**，日志在 `i264e[info]` 后静默中断。第一次启动无残留，所以正常。

### 2.4 单例残留（次要）

`IspOsdManager::s_instance`（IspOsdManager.cpp:12）和 `RtspServer::getInstance` 的 `call_once`（RtspServer.cpp:142-148）是进程内单例，进程重启后重置——**不是**跨进程残留源。跨进程残留源是 IMP 驱动内核态状态，由 teardown 缺失导致。进程内 `g_video_init_ref_count`（IngenicVideo.cpp:16）等 ref-count 也是进程内、重启清零，不是根因。

## 3. 近期 commit 对 teardown 的影响

- **a205cb7**（async-signal-safe signal handling）：把 cleanup 从信号处理 worker 线程挪到主线程 `performCleanup`，但 `performCleanup` **不包含** `RtspServer::stop()`/HAL teardown。旧版 worker 线程同样不含 HAL teardown。即：此 commit 没有引入新 teardown 路径，也没有删除已有的；teardown 缺失是既有问题，a205cb7 只是让退出流程更可控（主线程顺序执行）。**但它确认了退出时确实没有 HAL exit 调用点。**
- **6209f88**（RTSP audio init failure）：仅把 `initialize()` 里 audio 失败的 `return false` 改成 `enableAudio_=false`（RtspServer.cpp:190）。与 teardown/卡死无关。
- **3d854ab**（log demote）：只降级 MediaSession 统计、rtsp.c Pull result、mDNS、daynight auto-switch 的日志。**卡死点（configure 内）无任何被降级的日志**，所以"日志没打"=代码没跑到，不是被降级。排除该 commit 干扰。

## 4. 根因（Root cause）

第一次 `htc_main_app -m` 进程经 SIGTERM 退出时，硬件 teardown 链路缺失：
`performCleanup`（main_app.cpp:882-923）和 `RtspServer::stop()`（RtspServer.cpp:209-231）都不调用 `IngenicVideo::exit()`/`~IngenicVideoStream`，
而 main 末尾 `Misc::poweroff(); while(1);`（main_app.cpp:1857-1858）使 RtspServer Meyers 单例析构（`~RtspServer()` → `video_->exit()`）永不执行。
导致 IMP 编码器 channel/group/bind、ISP、OSD region 残留在内核驱动中，第二次进程 `configure()` 的 `IMP_Encoder_RegisterChn`（IngenicVideo.cpp:950）/`IMP_System_Bind`（line 962）阻塞，日志停在 `i264e[info]` 之后。

## 5. 修复方向（Fix direction，给 implementer）

主修复（**非 PIC-owned**，`src/app` + `src/media/rtsp`）：
1. **在进程退出前显式 teardown HAL**。在 `main_app.cpp` mobile 分支主循环退出后、`main_exit` 前（或 `performCleanup` 内），确保 `RtspServer::getInstance()` 的 video 资源被释放到 `IngenicVideo::exit()`。最干净的做法：给 `RtspServer` 增加 `void shutdown()`，内部 `stop()` + 显式 reset `videoSession_`（触发 `~IngenicVideoStream` DestroyChn/UnBind/DestroyGroup）+ `video_->exit()`（`IngenicVideo::exit()`），并在 mobile/rtsp-server 退出路径（main_app.cpp:1641-1643, 1671）调用它。这样不依赖单例析构（被 `while(1)` 跳过）。
2. 或者：让 `RtspServer::stop()` 在 mobile/常驻场景也 `uninitVideo()`（即 `deinitialize()` 的等价动作），而不是只 `videoSession_->stop()`。注意 stop() 当前还被 `onSessionClosed`/`~RtspServer` 复用，需区分"会话级 stop"与"进程级 teardown"。

PIC-owned 补充（**`src/hal/ingenic/`，需用户书面同意**）：
3. `IngenicVideo::exit()`（line 1178-1200）当前不显式 destroy encoder channel/UnBind/DestroyGroup（依赖 `~IngenicVideoStream`）。建议在 `exit()` 中对已注册 channel 做兜底 `IMP_Encoder_UnRegisterChn`/`IMP_Encoder_DestroyChn`/`IMP_System_UnBind`/`IMP_Encoder_DestroyGroup`，保证即使 stream 析构未触发也能清理；并确保 `IspOsdManager::exit()`（IspOsdManager.cpp:90-97）DestroyOsdRgn 在 `stop()` 已 stop 但 region 未 destroy 的路径也覆盖。

建议优先级：1（或 2）是根因修复，3 是健壮性兜底。若用户暂不动 hal，仅做 1 即可解决"第二次卡死"。

## 6. 复现条件（Reproduction）

- 设备：T32 硬件（`BUILD_FOR_SIMULATION=OFF`，`build/` 产物，NFS 挂载到 `/mnt/huntcam`）。
- 步骤：
  1. 第一次：`./bin/htc_main_app -m --force-day` → 等到 "RTSP server started on port ..." 出现 → 发 `SIGTERM`/`SIGINT`（或 kill）让进程经 `performCleanup` → `Misc::poweroff` 路径退出。
  2. 第二次：同命令重启 → 日志停在 `i264e[info]: profile Main, level 3.1`，不出现 "RTSP server started on port"。
- 不复现：PC 模拟（`BUILD_FOR_SIMULATION=ON`，走 `imp_stub.c`，无真实 IMP 驱动残留）；或第一次正常 `return`（单例析构执行，但 T32 模式 main 不 return，所以正常 return 只在 SIM）。

## 7. 回归验证（Regression test）

pass / 验收标准（设备侧）：
1. 连续两次 `./bin/htc_main_app -m --force-day`，每次中间用 SIGTERM 退出；第二次日志必须出现 `rtsp stream info: ...`（RtspServer.cpp:472）和 `RTSP server started on port <N>`（RtspServer.cpp:380）。
2. 退出日志应出现 `IngenicVideo::exit()` 路径的标志（如 `IMP_Encoder_MultiProcessExit` 相关或 `IspOsdManager: exit done`，IspOsdManager.cpp:96），证明 teardown 执行。
3. 用 `netstat`/RTSP 客户端连第二次启动的端口能拉到流（非阻塞）。
4. 静态：`grep` 确认 mobile/rtsp-server 退出路径调用了新的 `shutdown()`/`stop()+exit()`。
5. 长稳：连续 5 次 kill+重启均不卡死（排除偶发）。

## 8. 证据指针（Evidence refs）

- `src/media/rtsp/RtspServer.cpp:142-148`（单例 call_once）、`:168-172`（~RtspServer）、`:174-207`（initialize/deinitialize）、`:209-231`（stop，无 HAL exit）、`:429-479`（initVideo → configure → getInfo line 472）、`:456-465`（rtsp config 日志）
- `src/hal/ingenic/IngenicVideo.cpp:894-976`（configure，i264e 来自 line 945 CreateChn，卡死点 line 950/962）、`:1115-1177`（init）、`:1178-1200`（exit，未在退出路径调用）、`:1001-1021`（stream stop 不 destroy）
- `src/hal/ingenic/IspOsdManager.cpp:90-97`（exit）、`:99-124`（ensureRegion/CreateOsdRgn）
- `src/app/main_app.cpp:882-923`（performCleanup 无 HAL teardown）、`:1615-1650`（mobile RTSP 启动/退出）、`:1653-1672`（rtsp-server 模式）、`:1824-1862`（main_exit → poweroff + while(1)）
- git: `a205cb7`（signal handling）、`6209f88`（audio init，无关）、`3d854ab`（log demote，无关，已排除）
