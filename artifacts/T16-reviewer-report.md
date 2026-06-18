---
contract: report
contract_version: "1"
task_id: T16
node: reviewer
flow: feature
status: success
summary: |
  APPROVE. Independent audit confirms the C-2 regression fix is a TRUE restoration
  of pre-C2 ordering: `workModeToCommand(mode)` is called BEFORE
  `commonStartupPostDispatch` in BOTH main_app.cpp:286 and workmode_app.cpp:137,
  so `-wm 3` (WORKING_MODE_TEST_ONLY) → CMD_MOBILE reaches S11 netif selection
  (pre-C2 behavior, proven vs `git show afc8da6^:src/app/main_app.cpp`). Verified
  live: both apps reach RTSP:8554 + HTTP:8080 + mDNS for `-wm 3` with rc=124
  (timeout in the CMD_MOBILE loop), byte-identical normalized app.log. Sim A/B
  byte-identical across wm0-4 × rtc0-1 (10 cases) + wm99 invalid mode + badargc,
  independently reproduced for wm1/wm3/wm99. `workModeToCommand` case bodies +
  RGB asyncBlink side effects byte-identical to the pre-C2 switch; `runWorkMode`
  = workModeToCommand + runCommands, cascade unchanged. Invalid-mode CMD_HELP
  handling correctly preserved: `runWorkMode` sleeps(10) + returns TerminalExit,
  both apps map to `return -1` (skip tail) — matching C-2/HEAD (NOT silently
  running CMD_HELP). `syncWithMCU` moved byte-identically into app_lifecycle
  (PID/UPID/UPWD/flush/RTC), both apps call it, app_lifecycle links `mcu` +
  includes MCU.h. workmode_app mirrors main_app's -wm arm block-for-block
  (commonStartup→installSignalHandlers→-wm validate→ctx→workModeToCommand→
  commonStartupPostDispatch→setCleanupHook→runWorkMode→exit tail). Scope clean:
  src/hal untouched, workmode_app is -wm-only (no single-shot flags), new app
  UNSPAWNED (media_app.cpp:257 still htc_main_app -wm), zero std::to_string/stoi
  (uClibc-safe), both apps build clean on sim + T32 cross. Two benign
  observations noted (invalid-mode now runs S9-S13 before bailing — introduced
  by C-2/T15, not T16; asyncBlink called twice for modes 1/3 — idempotent, no
  observable effect). MANDATORY: Tier-3 device A/B (next-boot-no-hang) is a
  user-run prerequisite before C4/T17 — sim cannot surface the R1 HAL/IMP
  teardown risk.
deliverables:
  - artifacts/T16-reviewer-evidence.md
  - artifacts/T16-reviewer-report.md
verification:
  commands:
    - git show afc8da6^:src/app/main_app.cpp
    - git show afc8da6:src/app/main_app.cpp
    - grep -nE 'workModeToCommand|runWorkMode|CMD_HELP|TerminalExit|syncWithMCU|commonStartupPostDispatch' src/app/workmode/WorkModeRunner.cpp src/app/main_app.cpp src/app/workmode_app.cpp src/app/app_lifecycle/ProcessLifecycle.cpp
    - cmake --build build_sim --target htc_workmode_app htc_main_app
    - cmake --build build --target htc_workmode_app htc_main_app
    - diff artifacts/t16_ab_logs/wm3_rtc1_A_main.norm artifacts/t16_ab_logs/wm3_rtc1_B_workmode.norm
  evidence_ref: artifacts/T16-reviewer-evidence.md
state_delta:
  set_task_status: {}
artifact_path: artifacts/T16-reviewer-evidence.md
---
