---
contract: report
contract_version: "1"
task_id: T2
node: reviewer
flow: bug
status: success
summary: |
  T2 进程级 IMP teardown 修复审查 passed。独立重编译双平台均 exit 0(T32 build/
  + SIM build_sim),佐证 implementer 构建声明。teardown 接入正确:shutdown() 在
  main_exit 标签内、Misc::poweroff()/while(1) 之前,用 rtsp_singleton_used 门控
  (仅 mobile/rtsp-server 置位,与全 src 仅有的两处 RtspServer::getInstance 一一对应),
  覆盖全部 goto main_exit 汇聚退出路径。SIM 安全(SimVideo::exit no-op,
  IngenicVideo.cpp 不编译进 SIM,IspOsdManager SIM 不构造)。幂等/可空/无双释放:
  deinitialized_ 守卫;~IngenicVideoStream 与 IngenicVideo::exit() 兜底共用同一份
  g_bind/g_group_ref_count map(stream 析构 erase → exit 兜底 no-op);
  IspOsdManager::exit() defensive fallback 幂等可空。无明显回归(stop() 行为未改、
  非 RTSP 模式不触发)。设备侧二次启动回归留用户(PC 跑不动 MIPS),静态链路正确,
  不阻断。
deliverables:
  - artifacts/T2-reviewer-evidence.md
  - artifacts/T2-reviewer-report.md
verification:
  commands:
    - cmake --build build -j$(nproc)
    - cmake --build build_sim -j$(nproc)
    - grep -n "main_exit:\|rtsp_singleton_used\|getInstance()->shutdown\|Misc::poweroff\|while(1)" src/app/main_app.cpp
    - grep -n "void RtspServer::shutdown\|deinitialized_\|videoSession_.reset\|video_->exit" src/media/rtsp/RtspServer.cpp
    - grep -n "fallback\|g_bind_ref_count.clear\|g_group_ref_count.clear\|IMP_Encoder_DestroyGroup" src/hal/ingenic/IngenicVideo.cpp
  evidence_ref: artifacts/T2-reviewer-evidence.md
state_delta:
  set_task_status:
    T2: done
  add_decision:
    - key: T2-review-verdict
      value: "passed。双平台独立编译 exit 0;teardown 在 poweroff/while(1) 前、rtsp_singleton_used 门控覆盖全部退出路径;SIM 安全;幂等无双释放;无回归。"
    - key: T2-hw-regression-pending
      value: "设备侧连续两次(建议 5 次)htc_main_app -m --force-day + SIGTERM 回归未验证(PC 跑不动 MIPS),留用户在 T32 执行;验收标准见 analyst evidence §7 + reviewer evidence §6。"
  add_risk:
    - key: T2-hw-regression-unverified
      severity: medium
      description: "静态链路 + 双平台编译已过,但 T32 硬件真实 IMP 驱动下第二次启动是否不再卡在 configure() 尚未在设备验证,留给用户做设备侧回归。"
artifact_path: artifacts/T2-reviewer-report.md
next: ":end"
---

# T2 Reviewer Report

## 结论:passed / success

### 独立验证(亲自跑,非引用 implementer)
- T32 `build/` 重编译:exit 0(后台 breglm68s)
- SIM `build_sim/` 重编译:exit 0(后台 b46drgepb)
- implementer 的双平台构建声明属实。

### 必查项逐条结论
1. teardown 接入:**passed** — shutdown() 在 main_exit 标签内(line 1842-1844)、
   `Misc::poweroff()/while(1)`(line 1875-1876)之前;`rtsp_singleton_used` 门控正确
   (仅 mobile:1622 + rtsp-server:1666 置位,与全 src 仅有的两处 getInstance 一一对应,
   非 RTSP 模式不误构造单例);main_exit 是全部 goto 汇聚点,单点覆盖所有退出路径。
2. SIM 安全:**passed** — SimVideo::exit() no-op;IngenicVideo.cpp 不编译进 SIM target;
   IspOsdManager 仅 IngenicVideo::init 实例化,SIM 不构造;imp_stub no-op。
3. 幂等/可空/无双释放:**passed** — deinitialized_ 守卫;~IngenicVideoStream 与 exit()
   兜底共用 g_bind/g_group_ref_count map(erase 后 no-op);exitCalled_/ref-count 守卫;
   IspOsdManager::exit defensive fallback 幂等可空(handle 初始 -1)。
4. 回归风险:**passed(低风险)** — stop() 行为未改仍被 session 复用;运行期不触发 shutdown;
   非 RTSP 模式不置位门控。
5. 缺测试:无合适单测挂载点(IMP 真实驱动路径 SIM 不可覆盖);已给设备侧回归脚本。

### 遗留(非阻断)
- 设备侧二次启动回归未验证(PC 跑不动 MIPS),留用户在 T32 做连续 5 次 kill+重启回归。

详见 `artifacts/T2-reviewer-evidence.md`。
