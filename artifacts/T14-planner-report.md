---
contract: report
contract_version: "1"
task_id: T14
node: planner
flow: feature
status: success
summary: |
  C1 design: extract process lifecycle (signal machinery + common startup S1-S16
  + shutdown tail) into a new app_lifecycle lib exporting ProcessLifecycle,
  consumed by htc_main_app (and the future htc_workmode_app in C3). Dispatch
  (:796-895) and command cascade (:1003-1492) stay in main_app, unchanged in
  logic; only scaffolding moves and static refs rewrite to lc. ASYNC-SIGNAL-
  SAFETY VERDICT: adopt option (A) - keep g_signal_pipe/g_pending_signal/
  already_in_exit_flow/drainSignalPipe/waitForSignalOrTimeout/signalHandler/
  performCleanup as anonymous-namespace file-scope state in ProcessLifecycle.cpp
  (NOT class members); signalHandler stays a free fn touching only sig_atomic_t
  store + write(2), zero pointer indirection in signal context = identical
  instruction stream to today. Option (B) (members + g_active_lc pointer deref)
  rejected as pedantically non-async-signal-safe and needlessly riskier on the
  repo's most delicate code. Startup split: commonStartup() runs S1-S8 (env/DB/
  MediaScanner/EasyLogger/DayNight/RGB-LED/AutoRelease/signal-pipe), dispatch
  stays in main() between it and commonStartupPostDispatch() which runs S9-S13
  (Settings/DeviceConfig+ptype/SD-mount+netif/factory/update-config/timezone) -
  this preserves today's exact ordering (signal handlers at :793 before dispatch
  at :796, S9-S13 at :897-998 after dispatch). performCleanup split (R3): mode-
  local resets (daynight/LED/Settings-save/SIGTERM power-hold/mgmt+storage=null,
  :590-609+:616-631) -> caller cleanupHook invoked once from waitForSignal();
  transport teardown (http_server/TcpEvent, :611-615) -> shutdown() only (avoids
  double-stop). rtsp_singleton_used (R1) -> lc.markRtspSingletonUsed() set ONLY
  at :1296 (CMD_MOBILE) and :1336 (CMD_RTSP_SERVER); shutdown() gates
  RtspServer::getInstance()->shutdown() on it (:1504-1512 before-the-freeze gate
  preserved). daynight_switch/gpio_rgb_led/auto_release/setting_file_path/config/
  program_type -> lifecycle members w/ accessors; mgmtServClient/storageServClient/
  mobile_rtsp_enabled/rtsp_audio_enabled/is_rtc_work_well/working_mode -> STAY in
  main_app (move to WorkModeContext in C2). CMake: new
  src/app/app_lifecycle/CMakeLists.txt mirrors src/app/workmode/CMakeLists.txt
  (SHARED, glob *.cpp, LIBRARY_OUTPUT_DIRECTORY build/lib); add_subdirectory
  after src/app/CMakeLists.txt:56; htc_main_app adds app_lifecycle in BOTH sim
  (:104) and hw (:254) link blocks. PUBLIC-deps: common_misc/setting/env/devconf/
  logger/daynight/gpio/power/common_time_timezone/http_server/event_service/
  discovery_service/storage/media_rtsp/daemon/jsoncpp + pthread/rt/gcc/stdc++;
  mcu and common_time_rtc deliberately NOT added (moved code has no RTC/MCU call -
  add only if T32 link fails, per R7). Verification: Tier 1 dual-platform build +
  byte-diff moved bodies vs git show HEAD (only static->lc. rewrites) + sim
  -wm 0..4 -rtc {0,1} + -m smoke vs baseline + SIGTERM cleanup-log tail; Tier 2
  semantic review (handler-before-first-blocking-call, O_NONBLOCK both pipe ends,
  shutdown byte-faithful incl R1 gate, hook not called from shutdown); Tier 3
  T32 device A/B MANDATORY+USER-RUN (subagents cannot drive device) - decisive
  signal = next cold boot must NOT hang in IMP configure(). Risks ranked R1-R8.
  No blocker found that makes C1 riskier than expected; the option-(A) decision
  removes the highest risk (async-signal-safety) by construction.
deliverables:
  - artifacts/T14-planner-full.md
  - artifacts/T14-planner-report.md
verification:
  commands:
    - "grep -nE 'already_in_exit_flow|rtsp_singleton_used|g_signal_pipe|g_pending_signal|drainSignalPipe|waitForSignalOrTimeout|signalHandler|performCleanup' src/app/main_app.cpp"
    - "grep -nE 'static.*(setEnvIfEmpty|normalizePath|getParentPath|parseMediaScannerMode|trimConfigString|getConfiguredPort|syncWithMCU|getCurrentTimeFormatted)' src/app/main_app.cpp"
    - "awk 'NR>=659 && NR<=998' src/app/main_app.cpp | grep -cE 'setEnvIfEmpty|normalizePath|getParentPath|parseMediaScannerMode'"
    - "grep -nE 'define[[:space:]]+(CDS_SENSOR_PIN|RGB_LED_PIN|POWER_HOLD_PIN|IR_LED_PIN|IR_CUT_ENABLE_PIN|IR_CUT_CTRL_PIN)' src/common/Common.h"
    - "grep -nE 'add_subdirectory.workmode|target_link_libraries.htc_main_app|app_workmode' src/app/CMakeLists.txt"
    - "grep -nE 'add_library|target_link_libraries|LIBRARY_OUTPUT_DIRECTORY' src/app/workmode/CMakeLists.txt"
    - "grep -nE 'rtsp_singleton_used|already_in_exit_flow|waitForSignalOrTimeout|daynight_switch|gpio_rgb_led|mobile_rtsp_enabled|mgmtServClient|storageServClient' src/app/main_app.cpp | grep -nE '1[0-9][0-9][0-9]:'"
    - "ls src/app/app_lifecycle/ 2>/dev/null; echo EXIT=$?"
  evidence_ref: artifacts/T14-planner-full.md
state_delta:
  set_task_status: {}
artifact_path: artifacts/T14-planner-full.md
---
