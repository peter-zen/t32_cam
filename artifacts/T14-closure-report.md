---
contract: report
contract_version: "1"
task_id: T14
node: reviewer
flow: feature
status: success
summary: |
  T14 Phase C-1 闭环（抽 app_lifecycle 库 / ProcessLifecycle）。feature flow 全节点
  status=success，C2 audit OK。新增 src/app/app_lifecycle/{ProcessLifecycle.h,cpp,CMakeLists.txt}
  （SHARED），把信号机制（signalHandler/drainSignalPipe/waitForSignalOrTimeout/g_signal_pipe/
  already_in_exit_flow，Option A：匿名命名空间文件域、signalHandler 为自由函数、信号上下文零指针解引用）
  + 公共 startup（S1-S8 commonStartup / S9-S13 commonStartupPostDispatch）+ shutdown 尾（main_exit
  步骤 1-7，含 rtsp_singleton_used 闸门的 RtspServer::shutdown，R1）逐字搬入；htc_main_app 改为
  实例化 ProcessLifecycle 并经 lc. 调用，dispatch + cascade 逻辑不变（仅 lc. 引用改写）。
  行为保持：reviewer/tester 独立 byte-diff vs HEAD 通过——signalHandler/drainSignalPipe IDENTICAL、
  waitForSignalOrTimeout 唯一差异是 performCleanup(sig)→cleanupHook（R3 唯一允许的逻辑拆分：
  transport teardown→shutdown()、mode-local resets→caller cleanupHook）、shutdown() 逐句等同 main_exit；
  sim+T32 双平台 build/link 干净；sim smoke（-wm 0 / -wm 3+SIGTERM）走通 startup→dispatch→cleanupHook→
  R1 闸门 teardown→shutdown；零 std::to_string；src/hal/** 未动。
  两处 implementer 偏差 reviewer 已 ACCEPT：(1) 根 CMakeLists.txt 加全局 CMAKE_POSITION_INDEPENDENT_CODE ON
  （app_lifecycle SHARED 要链 http_server/event_service/discovery_service 等 STATIC 非-PIC 库；行为中性、
  MIPS 本就 PIC、双平台 link 无 TEXTREL；targeted set_property 为可选 nice-to-have）；(2) pimpl(impl_) 装
  非信号成员，信号机制仍文件域（Option A 不变）。
  **关键未决（仅用户可在 T32 真机验证）**：Tier-3 device A/B —— sim 的 Misc::poweroff 是 no-op、_exit(0)
  跳过冻结，暴露不了 R1 症状（下次冷启动卡在 IMP configure()）。用户须对 -m / -wm 3 / -rs 冷启动 A/B vs
  HEAD 真机。本 closure 把 T14 置为 done，并把该 device 验证登记为 risk。
deliverables:
  - src/app/app_lifecycle/ProcessLifecycle.h
  - src/app/app_lifecycle/ProcessLifecycle.cpp
  - src/app/app_lifecycle/CMakeLists.txt
  - src/app/CMakeLists.txt
  - src/app/main_app.cpp
  - CMakeLists.txt
verification:
  commands:
    - orchestrator audit --flow flows/feature.yaml --task T14 --state orchestration-state.yaml
    - cmake --build build_sim -j$(nproc) --target htc_main_app
    - cmake --build build -j$(nproc) --target htc_main_app
  evidence_ref: src/app/app_lifecycle/ProcessLifecycle.cpp
state_delta:
  add_risk:
    - "T14 (Phase C-1) PC-side verification complete (byte-identical refactor + dual-platform build + sim smoke incl. signal→cleanupHook→R1-gated-teardown), but the R1 risk (rtsp_singleton_used HAL-teardown gate drift → next cold boot hangs in IMP configure()) is ONLY verifiable on T32 hardware. USER-RUN Tier-3 device A/B required: cold-boot -m / -wm 3 (CMD_MOBILE) / -rs (CMD_RTSP_SERVER) vs HEAD build/bin/htc_main_app, confirm next boot does NOT hang in IMP configure(). Sim cannot surface this (Misc::poweroff no-op + _exit(0)). Do this before C5 retire; htc_main_app -wm remains the fallback/baseline through C4."
  set_task_status:
    T14: done
artifact_path: src/app/app_lifecycle/ProcessLifecycle.cpp
---
