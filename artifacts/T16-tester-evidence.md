# T16 — tester evidence (Phase C-3: thin htc_workmode_app + C2-regression fix + syncWithMCU shared)

Branch: `feature/new-workmode`. Worktree: `.claude/worktrees/new-workmode`.
A/B harness + normalized A/B logs saved under `artifacts/t16_ab_logs/`.

## Gate 1 — Both apps build+link on both platforms (PASS)

```
cmake --build build_sim -j$(nproc) --target htc_workmode_app htc_main_app   → exit 0
cmake --build build    -j$(nproc) --target htc_workmode_app htc_main_app   → exit 0 (R7)
```
Binaries exist:
```
build_sim/bin/htc_workmode_app   ELF 64-bit x86-64   5591032 B
build_sim/bin/htc_main_app       ELF 64-bit x86-64   5692296 B
build    /bin/htc_workmode_app   ELF 32-bit MIPS32 uClibc (stripped)  20212 B
build    /bin/htc_main_app       ELF 32-bit MIPS32 uClibc (stripped)  28764 B
```
htc_workmode_app target registered in `src/app/CMakeLists.txt` (lines 65, 226 SIM link block, 416 T32 link block, 464 output props).

## Gate 2 — Regression fix: workModeToCommand BEFORE commonStartupPostDispatch (PASS)

main_app.cpp (`src/app/main_app.cpp`):
- line 286: `command = app_workmode::workModeToCommand(working_mode, ctx);`
- line 291: `if (!lc.commonStartupPostDispatch(cfg, command)) {`  ← AFTER 286 ✓

workmode_app.cpp (`src/app/workmode_app.cpp`):
- line 137: `command = app_workmode::workModeToCommand(working_mode, ctx);`
- line 140: `if (!lc.commonStartupPostDispatch(cfg, command)) {`  ← AFTER 137 ✓

`workModeToCommand` for `WORKING_MODE_TEST_ONLY` returns `CMD_MOBILE`
(`src/app/workmode/WorkModeRunner.cpp:855-862`, `command = CMD_MOBILE;`).

`runWorkMode` still calls `workModeToCommand` + `runCommands`
(`src/app/workmode/WorkModeRunner.cpp:883` and `:891`) — cascade unchanged.

Runtime proof `-wm 3` reaches CMD_MOBILE (not CMD_HELP): in BOTH apps the
normalized app.log shows the full CMD_MOBILE signature (RTSP server created on
port 8554, HTTP server, mDNS) for `-wm 3` — see `artifacts/t16_ab_logs/wm3_rtc1_*.norm`
(8 CMD_MOBILE-signature lines each, byte-identical A vs B). Pre-C2 the `-wm 3`
path would have hit S11 with `command=CMD_HELP` and exited immediately with no
RTSP/HTTP setup.

## Gate 3 — Sim A/B equivalence across wm 0-4 × rtc 0-1 (PASS)

Harness: each case runs A=htc_main_app and B=htc_workmode_app in ISOLATED
SIM_SD_ROOT roots; app.log is normalized (ANSI stripped, timestamps → `[TS]`,
per-run SIM_SD_ROOT → `TOKEN_ROOT`, project root → `TOKEN_PROJ`). `-wm` arm
`__func__` is `main` in both binaries, so no func-name normalization is needed.

| case | args | rc_A | rc_B | app.log.norm | written files |
|------|------|------|------|--------------|---------------|
| wm0_rtc0 | -wm 0 -rtc 0 | 0 | 0 | IDENTICAL | SAME (0/0) |
| wm0_rtc1 | -wm 0 -rtc 1 | 0 | 0 | IDENTICAL | SAME (0/0) |
| wm1_rtc0 | -wm 1 -rtc 0 | 0 | 0 | IDENTICAL | SAME (0/0) |
| wm1_rtc1 | -wm 1 -rtc 1 | 0 | 0 | IDENTICAL | SAME (0/0) |
| wm2_rtc0 | -wm 2 -rtc 0 | 0 | 0 | IDENTICAL | SAME (0/0) |
| wm2_rtc1 | -wm 2 -rtc 1 | 0 | 0 | IDENTICAL | SAME (0/0) |
| wm3_rtc0 | -wm 3 -rtc 0 | 124 | 124 | IDENTICAL | SAME (0/0) |
| wm3_rtc1 | -wm 3 -rtc 1 | 124 | 124 | IDENTICAL | SAME (0/0) |
| wm4_rtc0 | -wm 4 -rtc 0 | 0 | 0 | IDENTICAL | SAME (0/0) |
| wm4_rtc1 | -wm 4 -rtc 1 | 0 | 0 | IDENTICAL | SAME (0/0) |

`rc=124` for wm3 is the `timeout 60` SIGTERM while both apps sit in the
CMD_MOBILE `while(ctx.lc.keepRunning())` RTSP loop — both A and B hang
identically and are killed identically. This is A/B equivalence AND live proof
that CMD_MOBILE is reached in both (regression fix verified).

Error-path cases:
| case | args | rc_A | rc_B | app.log.norm |
|------|------|------|------|--------------|
| wm99_rtc1 | -wm 99 -rtc 1 | 255 | 255 | IDENTICAL (runWorkMode invalid-mode default → TerminalExit → return -1) |
| badargc_wm_only | -wm 3 | 255 | 255 | IDENTICAL (PID-stripped; only diff is the per-process PID in "Sending shutdown signal to process <pid>") |

Out-of-scope (single-shot flag, NOT part of `-wm` A/B contract):
| case | args | rc_A | rc_B | note |
|------|------|------|------|------|
| badargc_dash_s | -s | 0 | 255 | main_app handles `-s` (snap, exits 0); workmode_app is `-wm`-only (Gate 5), so `-s` → "Invalid command" → return -1. Divergence is by design. |

Written-files note: sim snap output goes to `./res/` relative to cwd with
hardcoded constant filenames (shared `processCmdSnap`), so identical by
construction; both apps wrote 0 new media/desc files in fresh roots for these
modes (the QUICK_SNAP_INFO_FILE does not exist in a fresh root, so
processCmdSnap's first branch is a no-op).

## Gate 4 — syncWithMCU move byte-identical (PASS)

Old static `syncWithMCU` body from `git show HEAD:src/app/main_app.cpp` (lines
63-95) vs new `app_lifecycle::syncWithMCU()` body
(`src/app/app_lifecycle/ProcessLifecycle.cpp` lines 612-643): diff is empty
modulo a single trailing blank line in the old extraction. PID/UPID/UPWD/RTC
logic, `devconf->flush()`, `return true` — byte-identical.

Call sites updated:
- `src/app/main_app.cpp:381` — `app_lifecycle::syncWithMCU();`
- `src/app/workmode_app.cpp:206` — `app_lifecycle::syncWithMCU();`
- static `syncWithMCU` removed from main_app.cpp (no `static.*syncWithMCU` match).

app_lifecycle CMake (`src/app/app_lifecycle/CMakeLists.txt`):
- `target_link_libraries(app_lifecycle PUBLIC ... mcu ...)` ✓
- `include_directories(... ${CMAKE_CURRENT_SOURCE_DIR}/../hardware/mcu ...)` ✓
- `#include "MCU.h"` in ProcessLifecycle.cpp:35 ✓

## Gate 5 — workmode_app is -wm-only (PASS)

`grep` for single-shot flag dispatch (`-s`/`-u`/`-m`/`-rs`/`-ar`/`-vr`/
`-grtc`/`-srtc` and their long forms) in `src/app/workmode_app.cpp` → NONE.
workmode_app only accepts `-wm`/`--work-mode` + `-rtc`/`--rtc-status`
(argc==5 shape); anything else → "Invalid command" + requestShutdown + sleep(10)
+ return -1 (lines 110-123).
Routes to `runWorkMode` (line 189), NOT `runCommands` directly. ✓

## Gate 6 — New app UNSPAWNED (PASS)

`grep -nE 'htc_main_app -wm|htc_workmode_app' src/app/media_app.cpp`:
```
257:    std::string command = "htc_main_app -wm " + to_string_custom((int)working_mode) + ...;
```
Still `htc_main_app`, no `htc_workmode_app` anywhere. C4 not done. ✓

## Gate 7 — Scope (PASS)

`git diff --stat HEAD -- src/hal` → empty. ✓
`grep -nE 'std::to_string|[^_]stoi\(' src/app/workmode_app.cpp` → only the
header comment line 11 mentioning the uClibc-safety rule; uses `stoi_custom`
(lines 114, 115). No `std::to_string`/`std::stoi` calls. ✓

## Lint / type-check

No project clang-format/clang-tidy/cppcheck config. `-Wall` rebuild of
`workmode_app.cpp` is clean of new warnings: only the project-wide pre-existing
`Logger` deprecation (8 identical occurrences in main_app.cpp too) and a
pre-existing `SnapImgSize defined but not used` in the shared `Common.h`.
No errors, no T16-introduced warnings.

## Gate 8 — Tier-3 device A/B = USER-RUN (FLAG, not a CI gate)

Sim A/B cannot surface the R1 next-boot-hang (IMP/RTSP driver state on real
T32 hardware). The user MUST A/B `htc_workmode_app -wm` vs `htc_main_app -wm`
on a T32 device across all sub-modes (0/1/2/3/4 × rtc 0/1) before C4/T17
repoints media_app's spawn to htc_workmode_app. This is a mandatory
prerequisite for C4 and is NOT closable by this tester node.

## Normalized A/B log samples

Saved under `artifacts/t16_ab_logs/`:
- `wm0_rtc1_{A_main,B_workmode}.norm` — snap path
- `wm3_rtc1_{A_main,B_workmode}.norm` — CMD_MOBILE regression proof (RTSP/HTTP/mDNS in both)
- `wm99_rtc1_{A_main,B_workmode}.norm` — invalid mode → TerminalExit
- `badargc_wm_only_{A_main,B_workmode}.norm` — invalid-command path
- `exit_code_matrix.txt` — full exit-code matrix
