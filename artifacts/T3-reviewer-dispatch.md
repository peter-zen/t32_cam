---
contract: dispatch
contract_version: "1"
task_id: T3
node: reviewer
flow: bug
upstream:
  - from: implementer
    artifact: artifacts/T3-implementer-report.md
description: |
  审查 T3 修复:重构 IngenicVideo::exit() teardown 顺序符合 SDK(imp_system.h:130/131)、
  去掉不可靠 Query 门控、退出前 flush 在途帧。独立重编译双平台 + 逐项审查。
acceptance:
  - 独立重编译 build(T32)+ build_sim(SIM)均 exit 0
  - 确认 exit() 顺序符合 imp_system.h:130(UnBind 在 FrameSource Disable 之后)、131(DestroyGroup 在 UnBind 之后)
  - 确认 ~IngenicVideoStream 与 exit() 兜底无双重 destroy、无遗漏;per-stream 资源由析构 erase map、exit 兜底覆盖残留
  - 确认 flush/ReleaseStream/StopRecvPic 配对正确,无 GetStream 泄漏;line ~1082 的 IMP_Encoder_Query 是运行期(合理)还是退出期(应去掉)需确认
  - 确认 SIM 不受影响(IngenicVideo.cpp 不编进 SIM)、对正常启动/第1-2次退出无回归
  - 输出 report card(passed→结束;failed→loopback implementer)
output_contract: report-card@v1
artifact_path: artifacts/T3-reviewer-report.md
pointers:
  files:
    - src/hal/ingenic/IngenicVideo.cpp
    - sdk/include/imp/imp_system.h
  grep:
    - "fsMgr.(disable|destroy)|IMP_System_UnBind|IMP_Encoder_UnRegisterChn|DestroyChn|DestroyGroup"
    - "IMP_Encoder_Query|ReleaseStream|StopRecvPic|IMP_System_Exit|MultiProcessExit"
constraints:
  - 独立验证,不只信 implementer 报告
  - 重编译用既有 build/ 与 build_sim/(别 rm build/)
  - 不 commit/push
---

# Reviewer 任务 (T3)

读 implementer 产物 + diff + analyst 根因,独立审查:
- `artifacts/T3-implementer-report.md`、`artifacts/T3-implementer-evidence.md`
- `artifacts/T3-analyst-evidence.md`(根因)
- diff:`git diff -- src/hal/ingenic/IngenicVideo.cpp`(本次只改这一个文件,+106/-4)

## 必查项
1. **独立重编译**:`cmake --build build -j$(nproc)` + `cmake --build build_sim -j$(nproc)`,两者 exit 0,记进 evidence。
2. **exit() 顺序 SDK 合规**(核心):对照 `sdk/include/imp/imp_system.h:130/131`,确认
   `fsMgr.disable()` → flush → `UnBind` → `UnRegisterChn/DestroyChn/DestroyGroup` → `fsMgr.destroy()` →
   `IMP_System_Exit` → `MultiProcessExit`。重点:UnBind 是否在 FrameSource Disable 之后(130)、
   DestroyGroup 是否在 UnBind 之后(131)。
3. **幂等/无双重 destroy**:`~IngenicVideoStream`(880-899 区)与 exit() 兜底(1230-1280 区)都操作
   `g_bind/g_group` map。确认:析构从 map erase、exit 兜底正常路径 map 已空 no-op、不会对同一
   channel/group 重复 Destroy(IMP 对已释放返回<0 但要确认无副作用/不污染)。`exitCalled_`/`configured_`/
   空指针守卫是否完整。
4. **line ~1082 的 IMP_Encoder_Query**:判断是运行期(start/stop 路径,合理保留)还是退出期(应去掉)。
   implementer 说只去掉退出期的;确认运行期那个保留合理、无残留退出期 Query 门控。
5. **flush 配对**:确认 `IMP_Encoder_GetStream`/`ReleaseStream`/`StopRecvPic` 在退出路径配对,
   无在途帧泄漏到 System_Exit。
6. **SIM 不受影响 + 无回归**:IngenicVideo.cpp 仅 T32 编译;确认 RtspServer 等共享代码 SIM 仍过;
   对正常启动、第1/2次退出路径无副作用(顺序改动不影响首次正常 teardown)。
7. **缺测试**:本仓无 IMP 真实驱动单测(SIM 不可覆盖);给设备侧回归脚本(连跑 N 次 kill+重启)。

## 结论
- 通过 → status=success,next=`:end`,evidence 含 passed/success。
- 实质问题(顺序错/双重 destroy/编译失败/回归) → status=failed,next=implementer,evidence 列清要改什么。
- 小瑕疵不阻断 → status=partial,说明。

## 交付物(必写)
- `artifacts/T3-reviewer-evidence.md`:独立 build 成功 + 逐项审查结论(含 success/passed)。
- `artifacts/T3-reviewer-report.md`:report card(report-card@v1,verification.evidence_ref → evidence,
  artifact_path = 本 report,next 视结论)。deliverables/evidence_ref 必须真实文件路径。

返回给我:≤300 字中文审查结论(passed/failed/partial + 关键发现 + 是否需 loopback)。
