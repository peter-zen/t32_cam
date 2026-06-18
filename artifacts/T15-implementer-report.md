---
contract: report
contract_version: "1"
task_id: T15
node: implementer
flow: feature
status: success
summary: |
  Phase C-2 done. app_workmode::runCommands (the whole if(command & CMD_*) cascade)
  and runWorkMode (the -wm switch→bitmap map) extracted verbatim from
  src/app/main_app.cpp into new src/app/workmode/WorkModeRunner.{h,cpp}. The 5
  -wm-reachable static helpers (getCurrentTimeFormatted, getConfiguredPort,
  processCmdSnap, processCmdVideoRecord, processCmdConcurrentSnapRecord) moved
  byte-identical into WorkModeRunner.cpp's anon namespace. main_app rewired:
  -wm branch calls runWorkMode (TerminalExit→return -1, skip tail; else fall
  through to the main_exit tail), single-shot flags call runCommands (Continue,
  tail runs unconditionally). The main_exit tail + cleanupHook stay in main_app;
  mgmtServClient/storageServClient/mobile_rtsp_enabled/rtsp_audio_enabled became
  main()-locals passed into WorkModeContext (clients by ref so the cleanupHook
  nulls the same instances). CMD_* defines single-sourced in WorkModeRunner.h.
  app_workmode CMakeLists declares the cascade's real deps PUBLIC (app_lifecycle,
  common_misc, network, storage, media_snap, media_recorder, audio_recorder,
  http_server, event_service, discovery_service, setting, env, devconf,
  common_time_rtc, common_utils_crc, crc16, power, disk, daynight, jsoncpp,
  civetweb, manifest) + service/camera include dir. Both-platform build links
  clean (sim exit 0, T32 exit 0 — no extra deps needed beyond the declared set).
  Byte-diff vs HEAD: cascade/switch/helpers show ONLY the allowed ctx./lc.
  rewrites + goto main_exit→return Continue + argv[2]→ctx.argv[2] + the DI lambda
  [&lc]→[&ctx] capture + default→return TerminalExit; zero logic change. 5
  helpers IDENTICAL to HEAD. Sim smoke confirms 3 original outcomes (normal
  completion→tail; in-cascade ex-goto→tail; invalid -wm→TerminalExit, tail
  skipped). Tier-3 device A/B NOT run (user-run; R4 — decisive gate before C4).
deliverables:
  - src/app/workmode/WorkModeRunner.h
  - src/app/workmode/WorkModeRunner.cpp
  - src/app/workmode/CMakeLists.txt
  - src/app/main_app.cpp
  - artifacts/T15-implementer-report.md
verification:
  commands:
    - cmake --build build_sim -j$(nproc) --target htc_main_app
    - cmake --build build -j$(nproc) --target htc_main_app
    - git show HEAD:src/app/main_app.cpp
    - grep -nE 'runCommands|runWorkMode|CascadeResult' src/app/main_app.cpp
    - grep -nE 'std::to_string|std::stoi' src/app/workmode/WorkModeRunner.cpp
  evidence_ref: src/app/workmode/WorkModeRunner.cpp
state_delta:
  set_task_status: {}
artifact_path: src/app/workmode/WorkModeRunner.cpp
---
