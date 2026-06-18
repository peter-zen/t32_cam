---
contract: report
contract_version: "1"
task_id: T16
node: tester
flow: feature
status: success
summary: |
  Gate 1 PASS: both apps build+link on both platforms — build_sim (x86-64) and
  build (T32 MIPS32 uClibc, R7) exit 0; build{,_sim}/bin/htc_workmode_app exist.
  Gate 2 PASS (PRIMARY correctness): in BOTH main_app.cpp (line 286 before 291)
  and workmode_app.cpp (line 137 before 140), workModeToCommand() is called
  BEFORE commonStartupPostDispatch(); workModeToCommand(WORKING_MODE_TEST_ONLY)
  returns CMD_MOBILE (WorkModeRunner.cpp:855-862); runWorkMode still calls
  workModeToCommand + runCommands (cascade unchanged); runtime proof: -wm 3
  reaches the full CMD_MOBILE path (RTSP/HTTP/mDNS) in BOTH apps.
  Gate 3 PASS (PRIMARY behavior): Sim A/B equivalence across all 10 cases
  (wm 0-4 × rtc 0-1) — exit codes MATCH and app.log.normalized IDENTICAL for
  every case (wm3 rc=124 in both = CMD_MOBILE RTSP loop, timeout-killed
  identically; wm99 rc=255 both = TerminalExit; badargc -wm 3 rc=255 both =
  invalid-command path, app.log IDENTICAL modulo per-process PID). -s single-
  shot diverges (0 vs 255) by design — out of -wm scope per Gate 5.
  Gate 4 PASS: app_lifecycle::syncWithMCU() body byte-identical to the old
  static (PID/UPID/UPWD/RTC + flush + return true; diff empty modulo a trailing
  blank line); main_app call site updated to app_lifecycle::syncWithMCU() (381);
  app_lifecycle CMake links mcu + includes hardware/mcu + MCU.h.
  Gate 5 PASS: workmode_app is -wm-only (no -s/-u/-m/-rs/-ar/-vr/-grtc/-srtc);
  routes to runWorkMode (not runCommands directly).
  Gate 6 PASS: media_app.cpp:257 still spawns "htc_main_app -wm"; zero
  htc_workmode_app references (C4 not done).
  Gate 7 PASS: src/hal diff empty; zero std::to_string/stoi in workmode_app.cpp
  (uses stoi_custom).
  Gate 8 FLAG (USER-RUN, not closable by tester): Tier-3 T32 device A/B
  (htc_workmode_app -wm vs htc_main_app -wm across all sub-modes) is mandatory
  before C4/T17 repoints the spawn — sim cannot surface the R1 next-boot-hang.
deliverables:
  - artifacts/T16-tester-evidence.md
  - artifacts/T16-tester-report.md
verification:
  commands:
    - cmake --build build_sim -j$(nproc) --target htc_workmode_app htc_main_app
    - cmake --build build -j$(nproc) --target htc_workmode_app htc_main_app
    - git show HEAD:src/app/main_app.cpp
    - grep -nE 'workModeToCommand|commonStartupPostDispatch|htc_workmode_app' src/app/main_app.cpp src/app/workmode_app.cpp src/app/media_app.cpp
    - bash /tmp/t16_ab_harness.sh
  evidence_ref: artifacts/T16-tester-evidence.md
state_delta:
  set_task_status: {}
artifact_path: artifacts/T16-tester-evidence.md
---
