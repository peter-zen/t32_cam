---
contract: report
contract_version: "1"
task_id: T16
node: planner
flow: feature
status: success
summary: |
  C3 (T16) design for a thin htc_workmode_app whose main() reproduces htc_main_app -wm.
  The new src/app/workmode_app.cpp is pure -wm orchestration: S1 path-inputs (sim vs HW)
  -> ProcessLifecycle.commonStartup + installSignalHandlers -> -wm arg validation
  (argc==5/-rtc gate, else requestShutdown+sleep+return -1) -> build WorkModeContext ->
  commonStartupPostDispatch(cfg, command) -> setCleanupHook (verbatim lambda) -> runWorkMode
  (TerminalExit => return -1, skip tail) -> shutdown tail (ShutdownContext + sim _exit /
  HW syncWithMCU + config->flush + POWER_MANAGER_ON poweroff). CMake mirrors htc_wifi_app
  (glob, add_executable, sim+HW link blocks, output-dir property).

  KEY DECISION 1 (command for commonStartupPostDispatch): the mode->command switch moved
  into app_workmode::runWorkMode in C2, so post-C2 main_app passes CMD_HELP (not CMD_MOBILE)
  to S11 netif selection for -wm 3 -- a latent regression on non-WIFI skus (ProcessLifecycle.cpp:445
  reads command==CMD_MOBILE). Fix: extract app_workmode::workModeToCommand(mode, ctx) (the switch,
  verbatim, incl. RGB asyncBlink side effects; default returns CMD_HELP), refactor runWorkMode to
  call it (pure, byte-identical), and have the new app call it BEFORE commonStartupPostDispatch.
  Recommends also fixing main_app's -wm arm the same way (flagged for reviewer).

  KEY DECISION 2 (syncWithMCU): move main_app.cpp:62-94 static into app_lifecycle::syncWithMCU()
  (free fn), both apps call it. Requires adding #include MCU.h + mcu to app_lifecycle's PUBLIC
  link (verified NOT currently present).

  Risks: the command ordering (highest), syncWithMCU placement, T32 link-dep minimal-set, new app
  unspawned until C4 (Tier-3 device A/B is user-run and mandatory before C4), __func__ cosmetic
  log delta, POWER_MANAGER_ON=0 global. Verification: Tier-1 sim A/B per mode 0..4 x rtc{0,1} +
  invalid/bad-argc, Tier-2 semantic review, Tier-3 T32 device A/B per sub-mode + next-boot-no-hang.
deliverables:
  - artifacts/T16-planner-full.md
  - artifacts/T16-planner-report.md
verification:
  commands:
    - "grep -nE 'commonStartupPostDispatch|runWorkMode|is_work_mode_cmd|command = CMD_HELP|syncWithMCU|POWER_MANAGER_ON' /home/zengping/project/huntcam/code/t32_cam/.claude/worktrees/new-workmode/src/app/main_app.cpp"
    - "grep -nE 'command == CMD_MOBILE|setNetworkInterfaceName|PTYPE_WIFI' /home/zengping/project/huntcam/code/t32_cam/.claude/worktrees/new-workmode/src/app/app_lifecycle/ProcessLifecycle.cpp"
    - "grep -nE 'CascadeResult runWorkMode|switch \\(mode\\)|WORKING_MODE_TEST_ONLY|CMD_MOBILE|return CascadeResult::TerminalExit' /home/zengping/project/huntcam/code/t32_cam/.claude/worktrees/new-workmode/src/app/workmode/WorkModeRunner.cpp"
    - "grep -nE 'workModeToCommand|runWorkMode|CascadeResult|CMD_MOBILE|struct WorkModeContext' /home/zengping/project/huntcam/code/t32_cam/.claude/worktrees/new-workmode/src/app/workmode/WorkModeRunner.h"
    - "grep -nE 'htc_wifi_app|add_executable|target_link_libraries|RUNTIME_OUTPUT_DIRECTORY|add_subdirectory' /home/zengping/project/huntcam/code/t32_cam/.claude/worktrees/new-workmode/src/app/CMakeLists.txt"
    - "grep -nE 'PUBLIC|target_link_libraries|mcu|LIBRARY_OUTPUT_DIRECTORY' /home/zengping/project/huntcam/code/t32_cam/.claude/worktrees/new-workmode/src/app/app_lifecycle/CMakeLists.txt"
    - "git show HEAD~2:src/app/main_app.cpp | grep -nE 'switch \\(working_mode\\)|WORKING_MODE_TEST_ONLY|command = CMD_MOBILE|goto main_exit' | head -20"
    - "grep -nE 'POWER_MANAGER_ON|add_definitions' /home/zengping/project/huntcam/code/t32_cam/.claude/worktrees/new-workmode/CMakeLists.txt"
    - "grep -nE 'stoi_custom|header-only|USE_UCLIBC' /home/zengping/project/huntcam/code/t32_cam/.claude/worktrees/new-workmode/src/common/utils/string/StringConvert.h /home/zengping/project/huntcam/code/t32_cam/.claude/worktrees/new-workmode/src/common/utils/string/CMakeLists.txt"
    - "grep -nE 'WIFI_IFNAME|ETH_IFNAME|PTYPE_|NETIF_NAME' /home/zengping/project/huntcam/code/t32_cam/.claude/worktrees/new-workmode/src/app/app.h"
  evidence_ref: artifacts/T16-planner-full.md
state_delta:
  set_task_status: {}
artifact_path: artifacts/T16-planner-full.md
---
