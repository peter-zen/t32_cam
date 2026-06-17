---
contract: dispatch
contract_version: "1"
task_id: T2
node: reviewer
flow: bug
upstream:
  - from: implementer
    artifact: artifacts/T2-implementer-report.md
description: |
  审查 T2 修复(进程退出时显式 teardown IMP 硬件资源)。独立重编译双平台,
  审查正确性/SIM 安全/幂等可空/回归风险/缺测试。
acceptance:
  - 独立重编译 build(T32)+ build_sim(SIM)均 exit 0,佐证 implementer 的构建声明
  - 确认 teardown 确实在 `Misc::poweroff()/while(1)` 之前、在所有用到 RtspServer 的退出路径
    (mobile / rtsp-server / 以及 -wm 等其它模式)被调用;`rtsp_singleton_used` 门控正确
  - 确认 SIM 路径 teardown 安全(SimVideo::exit no-op,不走 IngenicVideo)
  - 确认 IngenicVideo::exit() / IspOsdManager 兜底幂等、可空、不会与 ~IngenicVideoStream 双重释放
  - 评估对"正常(第一次)启动"及其它运行模式的回归风险
  - 输出 report card(passed → flow 结束;tests_failed → loopback 打回 implementer)
output_contract: report-card@v1
artifact_path: artifacts/T2-reviewer-report.md
pointers:
  files:
    - src/media/rtsp/RtspServer.h
    - src/media/rtsp/RtspServer.cpp
    - src/app/main_app.cpp
    - src/hal/ingenic/IngenicVideo.cpp
    - src/hal/ingenic/IspOsdManager.cpp
  grep:
    - "shutdown|deinitialized_|rtsp_singleton_used"
    - "Misc::poweroff|while\\(1\\)|main_exit"
    - "IngenicVideo::exit|IMP_System_UnBind|DestroyChn|DestroyGroup"
    - "DestroyOsdRgn|exitCalled_"
constraints:
  - 独立验证,不要只信 implementer 的报告
  - 重编译用既有 build/ 与 build_sim/(别 rm build/)
  - 不 commit / 不 push
---

# Reviewer 任务 (T2)

读 implementer 产物 + diff,独立审查:
- `artifacts/T2-implementer-report.md`、`artifacts/T2-implementer-evidence.md`
- `artifacts/T2-analyst-evidence.md`(根因,对照)
- diff:`git diff -- src/`(5 个文件:RtspServer.{h,cpp}、main_app.cpp、IngenicVideo.cpp、IspOsdManager.cpp)

## 必查项
1. **独立重编译**(别只信报告):
   - `cmake --build build -j$(nproc)`(T32)
   - `cmake --build build_sim -j$(nproc)`(SIM;不存在则先 `cmake -DBUILD_FOR_SIMULATION=ON -B build_sim -S .`)
   - 两者都要 exit 0。把成功输出记进 evidence。
2. **teardown 接入正确性**:确认 `RtspServer::getInstance()->shutdown()` 在 `main_exit`、
   `Misc::poweroff()/while(1)` **之前**调用;`rtsp_singleton_used` 标志在所有用到 RtspServer 的模式
   (mobile / rtsp-server / `-wm` 等)被正确置位,不会在非 RTSP 模式误构造单例、也不会漏掉某条退出路径。
3. **SIM 安全**:grep `SimVideo`/`BUILD_FOR_SIMULATION`,确认 SIM 下 `video_->exit()` 落到
   `SimVideo::exit()`(no-op),根本不构造 IngenicVideo;IspOsdManager 在 SIM 下不触发。
4. **幂等/可空/无双重释放**:`shutdown()`→`stop()`+`deinitialize()`;`deinitialize()` 有 `deinitialized_` 守卫;
   `IngenicVideo::exit()` 的兜底 destroy 与 `~IngenicVideoStream` 的 destroy 不会双重释放
   (ref-count / 已 destroy 标志守卫);`IspOsdManager::exit()` DestroyOsdRgn 幂等可空。
5. **回归风险**:对正常(第一次)启动、`-wm`、rtsp-server 模式有无副作用?shutdown() 是否可能
   在运行中误触发?stop() 被会话级路径复用是否被改动行为?
6. **缺测试**:本仓有无 teardown/exit 的单测框架可挂?若有,建议补;若无(嵌入式硬件路径难单测),
   说明并给出设备侧回归脚本。

## 结论
- 通过 → status=success,`next: :end`,并在 evidence 写 "passed"/"success"。
- 发现问题(正确性 bug/编译失败/回归风险实质)→ status=failed,`next: implementer`(loopback),
  evidence 里列清要 implementer 改什么。
- 介于二者(小瑕疵不阻断)→ status=partial,说明。

## 交付物(必写)
- `artifacts/T2-reviewer-evidence.md`:独立 build 成功输出 + 逐项审查结论(含 success/passed 字样)。
- `artifacts/T2-reviewer-report.md`:report card(report-card@v1,verification.evidence_ref → evidence,
  artifact_path = 本 report,next 视结论)。

返回给我:≤300 字中文审查结论(passed/failed/partial + 关键发现 + 是否需 loopback)。
