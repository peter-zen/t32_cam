---
contract: report
contract_version: "1"
task_id: T2
node: analyst
flow: bug
status: success
summary: 第二次启动卡在 i264e[info] 之后，根因是第一次进程经 SIGTERM 退出时 IMP 硬件资源(encoder channel/group/bind/ISP/OSD region)未被 teardown——performCleanup 与 RtspServer::stop() 均不调用 IngenicVideo::exit()，而 main 末尾 Misc::poweroff()+while(1) 使 RtspServer 单例析构永不执行，残留导致第二次 configure() 的 IMP_Encoder_RegisterChn/IMP_System_Bind 阻塞。
deliverables:
  - artifacts/T2-analyst-evidence.md
  - artifacts/T2-analyst-report.md
verification:
  commands:
    - grep -n "i264e\|CreateChn\|RegisterChn\|IMP_System_Bind\|configure: success" src/hal/ingenic/IngenicVideo.cpp
    - grep -n "rtsp stream info\|rtsp config\|RTSP server started" src/media/rtsp/RtspServer.cpp
    - grep -n "poweroff\|while(1)\|performCleanup\|RtspServer::getInstance()->stop\|main_exit" src/app/main_app.cpp
    - git show a205cb7 -- src/app/main_app.cpp | grep -n "performCleanup\|video_->exit\|RtspServer"
    - git show 3d854ab --stat
  evidence_ref: artifacts/T2-analyst-evidence.md
state_delta:
  set_task_status: {}
  add_decision:
    - key: T2-root-cause
      value: "teardown 缺失：第一次 SIGTERM 退出时 performCleanup(main_app.cpp:882-923) 与 RtspServer::stop()(RtspServer.cpp:209-231) 均不调用 IngenicVideo::exit()，main 末尾 Misc::poweroff()+while(1)(main_app.cpp:1857-1858) 使 RtspServer 单例析构永不执行；IMP encoder channel/group/bind/ISP/OSD region 残留，第二次 configure() 卡在 IngenicVideo.cpp:950/962。"
    - key: T2-fix-locus
      value: "主修复非 PIC-owned(src/app/main_app.cpp + src/media/rtsp/RtspServer.cpp)：给 RtspServer 加 shutdown()/或让 stop() 触发 video_->exit()，在退出路径显式 teardown。PIC-owned 补充(src/hal/ingenic/IngenicVideo.cpp exit + IspOsdManager.cpp exit 兜底 destroy)需用户书面同意。"
  add_risk:
    - key: T2-hal-teardown-bypass
      severity: high
      description: "T32 硬件模式 main 末尾 Misc::poweroff()+while(1) 跳过所有 C++ 静态对象析构，任何依赖单例析构释放硬件资源的 HAL 模块都有跨重启残留风险(IngenicVideo/ISP/OSD/encoder)。"
artifact_path: artifacts/T2-analyst-report.md
next: implementer
---

# T2 Analyst Report — 给 implementer

## 根因（一句话）
第一次 `-m` 进程经 SIGTERM 退出时**不释放 IMP 硬件**，残留 encoder channel/group/bind/OSD region，第二次启动 `configure()` 在 `IMP_Encoder_RegisterChn`/`IMP_System_Bind` 阻塞，日志停在 `i264e[info]` 之后。

## 卡死点
- `i264e[info]: profile Main, level 3.1` 来自 `IngenicVideo.cpp:945 IMP_Encoder_CreateChn`。
- 下一步 `IngenicVideo.cpp:950 IMP_Encoder_RegisterChn` / `:962 IMP_System_Bind` 是阻塞点。
- 证据：`configure` 成功必然打 INFO `rtsp stream info`(RtspServer.cpp:472) 与 `CreateOsdRgn done`(IspOsdManager.cpp:122)，第二次日志二者均缺失 → configure 未返回。
- 排除 3d854ab(log demote) 干扰：被降级的日志全在 create_server 之后，与卡死点无关。

## teardown 缺失链路
1. `performCleanup`(main_app.cpp:882-923) 无 HAL teardown。
2. `RtspServer::stop()`(RtspServer.cpp:209-231) 只 StopRecvPic+releaseFrameSource，不 exit()。
3. main 末尾 `Misc::poweroff(); while(1);`(main_app.cpp:1857-1858) → RtspServer Meyers 单例析构(RtspServer.cpp:144,168) 永不执行 → `IngenicVideo::exit()`(IngenicVideo.cpp:1178-1200) 永不调用。
- 结论：第一次退出后 IMP 驱动内核态残留；第一次启动无残留所以正常。

## 修复方向
**主（非 PIC-owned）**：给 `RtspServer` 加 `shutdown()`（或让 `stop()` 在进程退出场景触发 teardown），内部 `stop()` + `videoSession_.reset()`(→`~IngenicVideoStream` DestroyChn/UnBind/DestroyGroup) + `video_->exit()`；在 mobile(main_app.cpp:1641-1643) 与 rtsp-server(:1671) 退出路径、以及 `performCleanup` 内调用。不依赖被 `while(1)` 跳过的单例析构。
**PIC-owned 补充（需用户书面同意）**：`IngenicVideo::exit()` 显式兜底 destroy encoder channel/UnBind/DestroyGroup；`IspOsdManager::exit` 兜底 DestroyOsdRgn。

## 触及 src/hal/**
- 主修复不触及（`src/app` + `src/media/rtsp`）。
- 健壮性补充触及 `src/hal/ingenic/{IngenicVideo.cpp,IspOsdManager.cpp}`（PIC-owned，需用户同意；不做主修复也能解决卡死）。

## 复现 / 回归
- 复现：T32 硬件，连续两次 `htc_main_app -m --force-day`，中间 SIGTERM 退出；第二次停在 i264e[info]。
- 回归 pass：第二次启动出现 `rtsp stream info` + `RTSP server started on port`；退出日志出现 `IspOsdManager: exit done`/exit 路径标志；连续 5 次 kill+重启不卡死。

详见 `artifacts/T2-analyst-evidence.md`。
