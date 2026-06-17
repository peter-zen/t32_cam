---
contract: dispatch
contract_version: "1"
task_id: T2
node: implementer
flow: bug
upstream:
  - from: analyst
    artifact: artifacts/T2-analyst-report.md
description: |
  修复 T2:进程退出时显式 teardown IMP 硬件资源,使第二次启动不再卡死。
  用户已书面授权两部分:主修复(非 PIC)+ PIC-owned 健壮性兜底。
acceptance:
  - RtspServer 在进程退出路径(mobile / rtsp-server / performCleanup)显式调用
    process-level teardown,到达 IngenicVideo::exit()(不依赖被 while(1) 跳过的单例析构)
  - IngenicVideo::exit() 兜底显式 destroy encoder channel / UnBind / DestroyGroup
    (不只依赖 ~IngenicVideoStream)
  - IspOsdManager 退出路径兜底 DestroyOsdRgn
  - SIM 构建(BUILD_FOR_SIMULATION=ON,SIM HAL)同样安全:teardown 在 SimVideo/SimAudio
    上是 no-op 或安全调用,不 crash
  - 两个平台都编译通过(build/ T32 + build_sim/ PC)
  - 不 commit、不 push(用户未授权 git 操作)
  - 输出 report card + 双平台构建证据
output_contract: report-card@v1
artifact_path: artifacts/T2-implementer-report.md
pointers:
  files:
    - src/media/rtsp/RtspServer.cpp
    - src/media/rtsp/RtspServer.h
    - src/app/main_app.cpp
    - src/hal/ingenic/IngenicVideo.cpp
    - src/hal/ingenic/IngenicVideo.h
    - src/hal/ingenic/IspOsdManager.cpp
    - src/hal/ingenic/IspOsdManager.h
  grep:
    - "deinitialize|uninitVideo|void stop|~RtspServer"
    - "Misc::poweroff|while(1)|performCleanup"
    - "IngenicVideo::exit|IMP_Encoder_MultiProcessExit|IMP_System_Exit"
    - "DestroyOsdRgn|IspOsdManager::exit"
constraints:
  - 双平台必须编译通过(T32 + SIM);SIM 走 SimVideo/SimAudio/imp_stub,teardown 要 SIM-safe
  - 不 commit / 不 push / 不 rollback(用户未授权 git)
  - src/hal/** 改动本次已获用户书面授权(仅限本任务的 teardown 兜底)
  - 改动要最小化、贴合既有代码风格;teardown 要幂等(多次调用安全)与可空(未 init 时安全)
---

# Implementer 任务 (T2)

读 `artifacts/T2-analyst-evidence.md`(完整根因 + file:line 指针)再动手。

## 要改的两部分(用户已授权两部分)

### A. 主修复(非 PIC-owned)
1. **`src/media/rtsp/RtspServer.{h,cpp}`**:确保有一个进程级 teardown 入口
   (analyst 建议 `shutdown()`,或复用已有的 `deinitialize()`/`uninitVideo()`)。
   内部做:`stop()` + `videoSession_.reset()`(→ ~IngenicVideoStream DestroyChn/UnBind/DestroyGroup)
   + `video_->exit()`。注意 `stop()` 现被 onSessionClosed/~RtspServer 复用,**不要**把完整
   teardown 塞进会话级 `stop()`;区分"会话级 stop"与"进程级 shutdown"。
   - 参考:`~RtspServer`(RtspServer.cpp:168-172)→ `deinitialize()` 已是这条 teardown 路径,
     尽量复用,别另造重复逻辑。
2. **`src/app/main_app.cpp`**:在 mobile 分支主循环退出后(:1641-1643)、rtsp-server 模式
   (:1653-1672)、以及 `performCleanup`(:882-923)里,在 `Misc::poweroff(); while(1);`
   (:1857-1858)**之前**调用上述 process-level teardown。保证:进程要进 while(1)/poweroff 前,
   IMP 资源已 exit。

### B. PIC-owned 兜底(用户已书面授权)
3. **`src/hal/ingenic/IngenicVideo.cpp`** `IngenicVideo::exit()`(:1178-1200):
   显式对已注册 channel 做 `IMP_Encoder_UnRegisterChn` / `IMP_Encoder_DestroyChn` /
   `IMP_System_UnBind` / `IMP_Encoder_DestroyGroup`(兜底,即使 stream 析构未触发也能清理)。
   幂等、可空。
4. **`src/hal/ingenic/IspOsdManager.cpp`** `exit()`(:90-97)/相关路径:
   兜底 `IMP_ISP_Tuning_DestroyOsdRgn`,覆盖 `stop()` 已 stop 但 region 未 destroy 的路径。

### SIM 安全
- SIM 走 `SimVideo`/`SimAudio`/`imp_stub`(不是 IngenicVideo)。RtspServer 的 teardown 入口
  在 SIM 下落到 SimVideo 的等价 exit(应为 no-op 或安全)。确认 SIM 编译 + 不 crash。
  先 grep `SimVideo`/`BUILD_FOR_SIMULATION` 在 RtspServer / HAL provider 选择处,搞清抽象。

## 验证(必须做,写入证据)
1. 双平台编译:
   - `cmake --build build -j$(nproc)`(T32,uclibc,toolchain 已配好,**不要 rm build/**)
   - 若 `build_sim/` 未配:`cmake -DBUILD_FOR_SIMULATION=ON -B build_sim -S .`,再
     `cmake --build build_sim -j$(nproc)`
2. 静态确认:`grep` 退出路径确实调用了新 teardown;`grep` 确认 `IngenicVideo::exit`/OSD 兜底存在。
3. 把双平台 build 的成功输出(含 "exit 0"/"success"/"0 errors" 字样)和 grep 结果
   写进 `artifacts/T2-implementer-evidence.md`。
- **不要**尝试运行 T32 硬件二进制(PC 跑不动 MIPS;设备侧二次启动验证由用户在硬件上做)。

## 交付物(必写文件)
- `artifacts/T2-implementer-evidence.md`:改动 diff 摘要 + 双平台 build 成功输出 + grep 证据。
  内含 "success"/"exit 0" 字样。
- `artifacts/T2-implementer-report.md`:report card(frontmatter report-card@v1,
  status ∈ success/partial/failed/blocked,verification.evidence_ref 指向上面 evidence 文件,
  artifact_path = 本 report)。`deliverables` 用文件路径指针。`next` 建议 reviewer。
- 改动的源码文件本身(在工作树里,**不要 git commit**)。

返回给我:≤300 字中文摘要(改了哪些文件、teardown 怎么接、双平台 build 是否通过、有无遗留风险)。
