---
contract: report
contract_version: "1"
task_id: T16
node: implementer
flow: feature
status: success
summary: |
  Created thin htc_workmode_app (src/app/workmode_app.cpp) reproducing
  `htc_main_app -wm <mode> -rtc <status>` via app_lifecycle + app_workmode.
  Fixed the C2 regression: factored the switch(mode)->command map (incl. RGB
  asyncBlink side effects) into app_workmode::workModeToCommand(mode, ctx),
  refactored runWorkMode to call it (pure, byte-identical), and call it BEFORE
  commonStartupPostDispatch in BOTH main_app's -wm arm and the new app — so
  `-wm 3` (WORKING_MODE_TEST_ONLY) reaches S11 netif selection with
  command == CMD_MOBILE (restores pre-C2 ordering). Moved syncWithMCU into
  app_lifecycle (free fn app_lifecycle::syncWithMCU) shared by both apps;
  added MCU.h include + hardware/mcu include dir + mcu PUBLIC link to
  app_lifecycle. Both-platform build green; sim A/B identical across
  mode 0..4 x rtc {0,1} + invalid-mode + bad-argc. No T32 extra link deps
  needed (transitives from app_lifecycle + app_workmode covered all). No
  src/hal or media_app touched; no std::to_string/stoi introduced.
deliverables:
  - src/app/workmode_app.cpp
  - src/app/workmode/WorkModeRunner.h
  - src/app/workmode/WorkModeRunner.cpp
  - src/app/app_lifecycle/ProcessLifecycle.h
  - src/app/app_lifecycle/ProcessLifecycle.cpp
  - src/app/app_lifecycle/CMakeLists.txt
  - src/app/CMakeLists.txt
  - src/app/main_app.cpp
  - artifacts/T16-implementer-report.md
verification:
  commands:
    - cmake --build build_sim -j$(nproc) --target htc_workmode_app
    - cmake --build build_sim -j$(nproc) --target htc_main_app
    - cmake --build build -j$(nproc) --target htc_workmode_app
    - cmake --build build -j$(nproc) --target htc_main_app
    - grep -nE 'workModeToCommand|commonStartupPostDispatch' src/app/main_app.cpp src/app/workmode_app.cpp
    - grep -nE 'std::to_string|std::stoi' src/app/workmode_app.cpp
  evidence_ref: src/app/workmode_app.cpp
state_delta:
  set_task_status: {}
artifact_path: src/app/workmode_app.cpp
---

# T16 Implementer Report

## What was built

### Part A — `workModeToCommand` + C2 regression fix
- `WorkModeRunner.{h,cpp}`: added `int app_workmode::workModeToCommand(enum workingMode mode, WorkModeContext& ctx)` — the `switch(mode)→command` map (including `WORKING_MODE_UPLOAD_ONLY`/`WORKING_MODE_TEST_ONLY` RGB `asyncBlink` side effects via `ctx.lc.rgbLed()`) factored verbatim out of `runWorkMode`. `default:` returns `CMD_HELP` (no sleep/TerminalExit — that stays in `runWorkMode`).
- `runWorkMode` refactored to `int command = workModeToCommand(mode, ctx); if (command==CMD_HELP){log+sleep+return TerminalExit;} return runCommands(command, ctx);` — pure refactor, byte-identical behavior.
- `main_app.cpp -wm arm`: `if (is_work_mode_cmd) { command = app_workmode::workModeToCommand(working_mode, ctx); }` inserted BEFORE `lc.commonStartupPostDispatch(cfg, command)`. So `-wm 3` now reaches S11 (ProcessLifecycle netif selection) with `command == CMD_MOBILE`, restoring pre-C2 ordering.

### Part B — `syncWithMCU` moved to `app_lifecycle`
- `ProcessLifecycle.h`: declared free fn `bool app_lifecycle::syncWithMCU();` (touches only DeviceConfig + MCU singletons, no Impl state).
- `ProcessLifecycle.cpp`: added `#include "MCU.h"`; pasted the verbatim body (PID/UPID/UPWD read → DeviceConfig → flush → MCU RTC setDatetime) under `namespace app_lifecycle`.
- `main_app.cpp`: deleted the file-static `syncWithMCU`; HW-tail call site changed to `app_lifecycle::syncWithMCU();`.
- `app_lifecycle/CMakeLists.txt`: added `hardware/mcu` include dir + `mcu` to PUBLIC `target_link_libraries`.

### Part C — `src/app/workmode_app.cpp`
Thin `main()` reproducing the `-wm` path verbatim (S1 StartupConfig sim/HW block, S1-S8 + signal install, `-wm`/`-rtc` arg validation with the same poweroff/sleep/return-1 on bad args, WorkModeContext build, `command = workModeToCommand(...)` BEFORE post-dispatch, S9-S13, verbatim cleanupHook lambda, `runWorkMode` with TerminalExit→return -1, shutdown tail with `app_lifecycle::syncWithMCU()` + `config->flush()` + `#if POWER_MANAGER_ON` guard). Includes its own `normalizePath` copy (file-local, like main_app).

### Part D — CMake
`src/app/CMakeLists.txt`: WORKMODE_APP_SOURCES glob, `add_executable(htc_workmode_app ...)`, sim link block (app_workmode/app_lifecycle/common_misc/setting/env/devconf/power/daynight/gpio/mcu/logger/jsoncpp/sdk_stub), HW link block (same + system_call/imp/alog), output-dir property → `build[_sim]/bin/htc_workmode_app`.

## Verification results

### Tier 1 — both-platform build (exit 0)
- `cmake --build build_sim -j$(nproc) --target htc_workmode_app` → Built (deprecation warnings only, same as main_app).
- `cmake --build build_sim -j$(nproc) --target htc_main_app` → Built (refactor regression-free).
- `cmake --build build -j$(nproc) --target htc_workmode_app` → Built (`app_lifecycle` rebuilt to absorb `syncWithMCU`; no undefined symbols; `build/bin/htc_workmode_app` = 20212 bytes).
- `cmake --build build -j$(nproc) --target htc_main_app` → Built (regression-free; flatten-symlinks POST_BUILD ran).
- Binaries present: `build_sim/bin/htc_workmode_app` (5.59 MB), `build/bin/htc_workmode_app` (20 KB).

### Tier 1 — regression-fix confirmation
`workModeToCommand` is called BEFORE `commonStartupPostDispatch` in BOTH apps:
- `main_app.cpp`: workModeToCommand at line 286, commonStartupPostDispatch at line 291.
- `workmode_app.cpp`: workModeToCommand at line 137, commonStartupPostDispatch at line 140.
So `-wm 3` reaches S11 with `command == CMD_MOBILE` (not CMD_HELP).

### Tier 1 — sim A/B vs `htc_main_app -wm`
Per `(mode, rtc)` in `{0,1,2,3,4} × {0,1}`, ran baseline `htc_main_app` vs new `htc_workmode_app` in isolated SIM_SD_ROOT, diffed normalized `app.log` + produced files:
- Exit codes: identical for all 10 (0 for non-blocking modes; 124 = timeout for mode 3 which blocks in `waitForSignal`, same for both).
- Log diff lines: 0 for all 10.
- Produced-file diff: 0 for all 10.
- `-wm 99 -rtc 1`: both exit 255 (-1), both log `runWorkMode Invalid working mode 99, power off`, tail skipped (TerminalExit).
- `-wm 0` (bad argc): both exit 255, both log `main Invalid command -wm, power off`.

### uClibc safety
`grep -nE 'std::to_string|std::stoi' src/app/workmode_app.cpp` → empty (only a comment mention). Uses `stoi_custom`.

## T32 link deps added
None beyond the planned floor. The transitive PUBLIC deps of `app_lifecycle` + `app_workmode` resolved every undefined symbol; the explicit `system_call`/`imp`/`alog` floor sufficed. No extra libs appended.

## Scope discipline
- `src/hal/**`: untouched (git diff confirms).
- `media_app.cpp`: untouched (C4/T17).
- Cascade `runCommands` body: untouched (still line 337); only `runWorkMode` refactored to delegate.
- `POWER_MANAGER_ON=0` guard kept verbatim in the new app's tail.

## Tier 3 — device A/B (USER-RUN, deferred)
The Tier-3 T32 device A/B (per-sub-mode spawn + next-boot-no-hang check) is a mandatory gate before C4/T17 but is user-run and not part of this implementer node's exit. Flagged for the user/tester: the new app is UNSPAWNED in C3 (media_app still spawns `htc_main_app -wm`), so the A/B must be driven manually. Pay special attention to mode 3 on a non-WIFI sku (exercises the §3 netif-selection fix).

## Remaining concerns
- The pre-existing `Logger` deprecation warnings appear in the new app exactly as in main_app (not introduced here; `Logger::log` is the established API in this TU family).
- `MCU.h` include in main_app.cpp is now only referenced by comments (left in place to minimize churn; harmless and was present before).
- Device next-boot-no-hang (R1) cannot be exercised on sim — Tier-3 user-run is the decisive gate.
