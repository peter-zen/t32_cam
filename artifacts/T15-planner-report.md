---
contract: report
contract_version: "1"
task_id: T15
node: planner
flow: feature
status: success
summary: |
  C2 design to extract the command-execution cascade from main_app.cpp::main()
  into app_workmode, behavior-preserving, shared by both -wm and single-shot
  paths (no duplication). Executor shape: ONE runCommands(int, WorkModeContext&)
  = the whole if(command & CMD_*) cascade (main_app.cpp:681-1170 today, post-T14)
  verbatim incl. the single-shot-only blocks (they no-op for -wm since -wm never
  sets those bits); ONE runWorkMode(enum workingMode, WorkModeContext&) = the
  verbatim switch(working_mode)->command bitmap map (:584-613, incl. rgbLed
  asyncBlink + invalid-mode handling) + runCommands. htc_main_app -wm calls
  runWorkMode; single-shot flags keep their dispatch (sets command + the -m env
  setenvs which the cascade reads via getenv) then call runCommands. The cascade
  calls 5 -wm-reachable static helpers (processCmdSnap/VideoRecord/
  ConcurrentSnapRecord, getConfiguredPort, getCurrentTimeFormatted) which MUST
  move with it (invisible across TUs). WorkModeContext carries: lc (ref),
  isRtcWorkWell, mobileRtspEnabled, rtspAudioEnabled, argc/argv (only for
  CMD_SET_RTC's argv[2]), mgmtServClient/storageServClient BY REF (cleanupHook
  nulls them, so must share the instance). goto main_exit (~30 sites) -> bare
  return (tail runs unconditionally, matches the single main_exit label); the
  one TerminalExit case = invalid work mode (today's return -1 from main skips
  the tail) -> CascadeResult::{Continue,TerminalExit}, main honors TerminalExit
  with return -1. CMake: app_workmode gains ~20 PUBLIC deps (app_lifecycle,
  common_misc, network, storage, media_snap, media_recorder, audio_recorder,
  http_server, event_service, discovery_service, setting, env, devconf,
  common_time_rtc, common_utils_crc, crc16, power, disk, daynight, jsoncpp,
  civetweb, manifest) + the cascade's include dirs; T32 link is the R7 gate.
  Verification: byte-diff moved bodies vs git show HEAD (only ctx./lc. rewrites),
  dual-platform build, sim -wm 0..4 -rtc{0,1} + -m/-s/-u/-rs/-ar/-vr/-grtc/-srtc
  smoke vs baseline, SIGTERM -wm2/-wm3 cleanup-log diff; Tier-3 device A/B is
  user-run (R1 next-boot-hang + R2 CMD_MOBILE shared by -wm3/-m are NOT
  sim-visible). Flagged plan-vs-brief divergence (brief wins: keep whole cascade).
deliverables:
  - artifacts/T15-planner-full.md
  - artifacts/T15-planner-report.md
verification:
  commands:
    - "grep -nE 'if \\(command & CMD_|goto main_exit|main_exit:|switch \\(working_mode\\)|case WORKING_MODE_' src/app/main_app.cpp"
    - "sed -n '450,467p' src/app/main_app.cpp"
    - "grep -nE 'runRtspServerUntilSignal|markRtspSingletonUsed|keepRunning|waitForSignal|daynight|rgbLed|programType|config\\(\\)' src/app/main_app.cpp"
    - "cat src/app/workmode/CMakeLists.txt"
    - "grep -nE 'add_library|target_link_libraries' src/app/workmode/CMakeLists.txt src/app/app_lifecycle/CMakeLists.txt"
    - "git show HEAD:src/app/main_app.cpp | grep -nE '^static .*(processCmdSnap|processCmdVideoRecord|processCmdConcurrentSnapRecord|getConfiguredPort|getCurrentTimeFormatted)'"
  evidence_ref: artifacts/T15-planner-full.md
state_delta:
  set_task_status: {}
artifact_path: artifacts/T15-planner-full.md
---
