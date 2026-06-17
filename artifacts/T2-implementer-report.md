---
contract: report
contract_version: "1"
task_id: T2
node: implementer
flow: bug
status: success
summary: |
  实现 T2 进程级 IMP teardown。新增 RtspServer::shutdown()（session 级 stop + 进程级
  deinitialize=videoSession_.reset→~IngenicVideoStream DestroyChn/UnBind/DestroyGroup +
  video_->exit），并在 main_app.cpp 的 main_exit 标签后、Misc::poweroff()/while(1) 之前
  调用（覆盖 mobile/rtsp-server/performCleanup 全部退出路径，用 rtsp_singleton_used 标志
  避免非 RTSP 模式误构造单例）。PIC-owned 兜底：IngenicVideo::exit() 基于
  g_bind/g_group_ref_count 兜底 UnBind/DestroyChn/DestroyGroup；IspOsdManager::exit()
  defensive DestroyOsdRgn。teardown 幂等(deinitialized_/exitCalled_/空 map no-op)且可空
  (守卫 + SIM 走 SimVideo::exit no-op)。双平台 build 通过(T32 build/ exit 0 + SIM build_sim exit 0)。
deliverables:
  - src/media/rtsp/RtspServer.h
  - src/media/rtsp/RtspServer.cpp
  - src/app/main_app.cpp
  - src/hal/ingenic/IngenicVideo.cpp
  - src/hal/ingenic/IspOsdManager.cpp
  - artifacts/T2-implementer-evidence.md
  - artifacts/T2-implementer-report.md
verification:
  commands:
    - cmake --build build -j$(nproc)
    - cmake --build build_sim -j$(nproc)
    - grep -n "void RtspServer::shutdown\|deinitialized_\|void RtspServer::deinitialize" src/media/rtsp/RtspServer.cpp src/media/rtsp/RtspServer.h
    - grep -n "rtsp_singleton_used\|RtspServer::getInstance()->shutdown" src/app/main_app.cpp
    - grep -n "fallback UnBind\|IMP_Encoder_UnRegisterChn\|IMP_Encoder_DestroyChn\|IMP_Encoder_DestroyGroup\|g_bind_ref_count.clear\|g_group_ref_count.clear" src/hal/ingenic/IngenicVideo.cpp
    - grep -n "IMP_ISP_Tuning_DestroyOsdRgn" src/hal/ingenic/IspOsdManager.cpp
  evidence_ref: artifacts/T2-implementer-evidence.md
state_delta:
  set_task_status: {}
  add_decision:
    - key: T2-teardown-entry
      value: "进程级 teardown 入口 = RtspServer::shutdown()（stop + deinitialize），在 main_app.cpp main_exit 标签后、Misc::poweroff()/while(1) 前调用，用 rtsp_singleton_used 标志门控，不依赖被 while(1) 跳过的单例析构。"
    - key: T2-hal-fallback
      value: "IngenicVideo::exit() 兜底：基于 g_bind_ref_count/g_group_ref_count 显式 IMP_System_UnBind/IMP_Encoder_UnRegisterChn/DestroyChn/DestroyGroup；IspOsdManager::exit() defensive DestroyOsdRgn。幂等且可空。"
  add_risk:
    - key: T2-hw-regression-unverified
      severity: medium
      description: "双平台编译已过，但设备侧二次启动回归(SG=连续两次 -m 不卡死、退出日志见 teardown 标志)尚未在 T32 硬件验证，留给用户。"
artifact_path: artifacts/T2-implementer-report.md
next: reviewer
---

# T2 Implementer Report

## 改动一句话
在进程退出（`Misc::poweroff()/while(1)` 之前）显式调 `RtspServer::shutdown()` 做 IMP 全量 teardown，并给 `IngenicVideo::exit()`/`IspOsdManager::exit()` 加兜底 destroy，解决第二次启动 `configure()` 卡在 `IMP_Encoder_RegisterChn` 的根因。

## 接入点
- `src/app/main_app.cpp` `main_exit:` 后（:1842-1843）：`if (rtsp_singleton_used) RtspServer::getInstance()->shutdown();`。`main_exit` 是所有退出路径汇聚点，单点覆盖 mobile / rtsp-server / performCleanup / start-fail。
- `rtsp_singleton_used` 标志在 mobile(:1622)/rtsp-server(:1666) 首次 `getInstance()` 前置位，避免非 RTSP 模式误触发 HAL init。

## 幂等 / 可空 / SIM 安全
- `deinitialize()` 加 `deinitialized_` 守卫；`IngenicVideo::exit()` 有 `exitCalled_` + ref-count；兜底遍历基于 map，空则 no-op。
- SIM：`video_->exit()` 落到 `SimVideo::exit()`（return true）；IspOsdManager 仅硬件 HAL 构造，SIM 不执行；imp_stub 提供对应 IMP no-op stub。
- 双平台 build：T32 `build/` exit 0、SIM `build_sim/` exit 0，无编译 error。

## 遗留
- 设备侧二次启动回归未验证（PC 不能跑 MIPS），留用户在硬件做。
- 未 git commit / push（未授权）。

详见 `artifacts/T2-implementer-evidence.md`。
