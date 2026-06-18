---
contract: report
contract_version: "1"
task_id: T16
node: reviewer
flow: feature
status: success
summary: |
  T16 Phase C-3 闭环（新建薄 htc_workmode_app + 修 C2 regression + 共享 syncWithMCU）。
  feature flow 全节点 status=success，C2 audit OK。
  (1) 新增 src/app/workmode_app.cpp：-wm-only 薄 main()，复用 ProcessLifecycle + app_workmode::runWorkMode，
      逐序复刻 main_app 的 -wm 路径（cfg→commonStartup→installSignalHandlers→parse -wm/-rtc→
      workModeToCommand→commonStartupPostDispatch→setCleanupHook→build ctx→runWorkMode→CascadeResult→shutdown tail）。
  (2) **修了 C2(T15) 引入的真 regression**：C2 把 working_mode→command 的 switch 搬进 runWorkMode（在
      commonStartupPostDispatch 之后），导致 -wm 3 给 S11 netif 选择传的是 CMD_HELP 而非 CMD_MOBILE
      （WIFI sku 被掩盖、非 WIFI sku 实际 bail）。本任务把 switch 抽成 app_workmode::workModeToCommand，
      在两个 app 里都在 commonStartupPostDispatch 之前调用，恢复 pre-C2 顺序。reviewer 用 git diff
      afc8da6^ 证实 regression 真实且由 C2 引入；非法 mode 的 CMD_HELP default 仍经 runWorkMode 走
      TerminalExit（-wm 99 双 app rc=255、log 逐字一致）。
  (3) syncWithMCU 从 main_app static 搬进 app_lifecycle（两 app 共用，byte-identical；app_lifecycle 加 mcu 链 + MCU.h）。
  行为保持：reviewer/tester 独立 sim A/B——htc_workmode_app -wm N -rtc M vs htc_main_app 同参，wm0-4×rtc0-1
  共 10 例 + wm99 + badargc，app.log 逐字一致（仅 __func__ 差异）、文件/JSON 等价、rc 一致；-wm 3 双 app 均
  走全 CMD_MOBILE 路径（RTSP:8554+HTTP:8080+mDNS）。sim+T32 双平台两 app build/link 干净；零 std::to_string；
  src/hal/** 未动；workmode_app 仅 -wm、未 spawn（media_app 仍 htc_main_app -wm，C4 未做）。
  两处 benign（非阻塞）：mode 1/3 的 workModeToCommand 被调两次→asyncBlink 两次（GPIO 已幂等，A/B 无差）；
  非法 mode 现会先跑 S9-S13 再 bail（此偏移是 C2 引入、T16 与 HEAD 逐字一致）。
  **关键未决（仅用户可在 T32 验）**：Tier-3 device A/B——htc_workmode_app -wm vs htc_main_app -wm 真机逐子模式
  A/B、确认 clean shutdown + 下次冷启动不卡 IMP configure。**C4 repoint spawn 前必做**。本 closure 把 T16 置为 done。
deliverables:
  - src/app/workmode_app.cpp
  - src/app/workmode/WorkModeRunner.h
  - src/app/workmode/WorkModeRunner.cpp
  - src/app/app_lifecycle/ProcessLifecycle.h
  - src/app/app_lifecycle/ProcessLifecycle.cpp
  - src/app/app_lifecycle/CMakeLists.txt
  - src/app/CMakeLists.txt
  - src/app/main_app.cpp
verification:
  commands:
    - orchestrator audit --flow flows/feature.yaml --task T16 --state orchestration-state.yaml
    - cmake --build build_sim -j$(nproc) --target htc_workmode_app htc_main_app
    - cmake --build build -j$(nproc) --target htc_workmode_app htc_main_app
  evidence_ref: src/app/workmode_app.cpp
state_delta:
  set_risk:
    - "C1-C3 (T14-T16) PC-side verification complete (byte-identical refactors + dual-platform build + sim A/B: htc_workmode_app -wm N -rtc M ≡ htc_main_app -wm across wm0-4×rtc0-1). T16 also fixed a real C2 regression (-wm 3 netif selection). BUT the R1 risk (next cold boot hangs in IMP configure() from shared shutdown/syncWithMCU/CMD_MOBILE-RTSP teardown) is ONLY verifiable on T32. USER-RUN Tier-3 device A/B MANDATORY before C4/T17 repoint: spawn htc_workmode_app -wm vs htc_main_app -wm for every sub-mode (0-4 × rtc 0/1) on real hardware, confirm clean poweroff + next-boot-no-hang. htc_main_app -wm remains the fallback (media_app still spawns it) until C4."
  set_task_status:
    T16: done
artifact_path: src/app/workmode_app.cpp
---
