---
contract: report
contract_version: "1"
task_id: T15
node: reviewer
flow: feature
status: success
summary: |
  T15 Phase C-2 闭环（-wm 执行抽进 app_workmode）。feature flow 全节点 status=success，
  C2 audit OK。新增 src/app/workmode/WorkModeRunner.{h,cpp}：app_workmode::runCommands(command,ctx)
  = 整条 if(command&CMD_*) 级联（含 GET_RTC/SET_RTC/AUDIO/VIDEO/HEARTBEAT 单发块，-wm 不触发天然 no-op）
  + runWorkMode(mode,ctx) = switch(mode)→command 映射 + runCommands；5 个 -wm 可达 static helper
  （processCmdSnap/Video/Concurrent、getConfiguredPort、getCurrentTimeFormatted）一并搬入；WorkModeContext
  持 ProcessLifecycle& + isRtcWorkWell/mobileRtspEnabled/rtspAudioEnabled/argc,argv + mgmtServClient/
  storageServClient 按引用（与 cleanupHook 共享同一实例）。main_app：-wm→runWorkMode，单发 flag→runCommands；
  goto main_exit→return CascadeResult::Continue（main 无条件走 main_exit 尾，等同原单一 label），非法 mode
  default→TerminalExit（main return -1 跳尾）。
  行为保持：reviewer/tester 独立 byte-diff vs HEAD——级联唯一差异是 trailing return Continue（替代原隐式
  fall-through）+ ctx./lc. 引用改写 + goto→return；switch 仅 default→TerminalExit + 缩进；5 helper IDENTICAL；
  goto→CascadeResult 三结局逐一核对（30 goto + 1 正常→Continue→走尾；1 非法 mode→TerminalExit→跳尾）。
  CMD_* 单一定义在 WorkModeRunner.h（无 ODR 冲突）；cleanupHook by-ref 核实；sim+T32 双平台 build/link
  干净（app_workmode 链 ~24 个 PUBLIC 依赖，T32 R7 通过）；sim smoke 三结局复现；零 std::to_string；
  src/hal/** 未动。（注：commonStartupPostDispatch 失败时 config->flush() 空指针是 HEAD 既有、非 C2 引入。）
  **关键未决（仅用户可在 T32 验）**：Tier-3 device A/B 现为 C1+C2 累积——sim 暴露不了 R1（下次冷启动卡
  IMP configure）。用户须在 C4 repoint 前对每个 -wm 子模式(0-4 × rtc 0/1)+单发 -m/-s/-u/-rs 真机 A/B vs HEAD。
  本 closure 把 T15 置为 done。
deliverables:
  - src/app/workmode/WorkModeRunner.h
  - src/app/workmode/WorkModeRunner.cpp
  - src/app/workmode/CMakeLists.txt
  - src/app/main_app.cpp
verification:
  commands:
    - orchestrator audit --flow flows/feature.yaml --task T15 --state orchestration-state.yaml
    - cmake --build build_sim -j$(nproc) --target htc_main_app
    - cmake --build build -j$(nproc) --target htc_main_app
  evidence_ref: src/app/workmode/WorkModeRunner.cpp
state_delta:
  set_task_status:
    T15: done
artifact_path: src/app/workmode/WorkModeRunner.cpp
---
