# T16 — reviewer evidence (independent final audit, Phase C-3)

Branch `feature/new-workmode`, worktree `.claude/worktrees/new-workmode`.
Audited against `HEAD` (afc8da6, C-2/T15) and pre-C2 (`afc8da6^`, C-1/T14) to
prove the regression fix restores true pre-C2 ordering.

## FOCUS 1 — Regression fix correctness (PRIMARY): PASS

### 1a. The regression was real (introduced in C-2/T15)

Pre-C2 (`git show afc8da6^:src/app/main_app.cpp`) `-wm` dispatch ordering:

```
584: switch (working_mode) { ... command = CMD_MOBILE; (TEST_ONLY) ... }
624: if (!lc.commonStartupPostDispatch(cfg, command)) {   // AFTER the switch
```

So S9-S13 (incl. S11 netif selection) saw the REAL command (e.g. CMD_MOBILE for
`-wm 3`) pre-C2.

C-2 (`git show afc8da6:src/app/main_app.cpp`) moved the switch into
`runWorkMode`, which runs AFTER `commonStartupPostDispatch`. For `-wm 3`,
`command` stayed `CMD_HELP` at S11 — the regression.

### 1b. T16 restores the ordering (TRUE fix, not new behavior)

New `main_app.cpp`:
```
286: command = app_workmode::workModeToCommand(working_mode, ctx);   // BEFORE
291: if (!lc.commonStartupPostDispatch(cfg, command)) {              // AFTER 286
```
New `workmode_app.cpp`:
```
137: command = app_workmode::workModeToCommand(working_mode, ctx);   // BEFORE
140: if (!lc.commonStartupPostDispatch(cfg, command)) {              // AFTER 137
```
`workModeToCommand` for `WORKING_MODE_TEST_ONLY` returns `CMD_MOBILE`
(`WorkModeRunner.cpp:855-862`). So `-wm 3` → CMD_MOBILE reaches S11. Restored.

### 1c. workModeToCommand body byte-identical to the pre-C2 switch

Pre-C2 case bodies (git show afc8da6^) vs `workModeToCommand`
(`WorkModeRunner.cpp:842-879`): SNAP→CMD_SNAP; UPLOAD_ONLY→CONN|DHCP|NTP|UPLOAD
+ `asyncBlink(60)`; TEST_ONLY→`asyncBlink(30)` + CMD_MOBILE; SNAP_UPLOAD;
UVC→CONN|DHCP|RTSP_SERVER. Identical RGB-blink side effects + returns.

### 1d. Invalid-mode CMD_HELP handling — preserved vs C-2/HEAD (the focus-1 nail)

PRIMARY concern: `workModeToCommand default:` returns CMD_HELP (does NOT
sleep/return). Does this still reach the invalid-mode exit path?

`runWorkMode` (`WorkModeRunner.cpp:881-892`):
```
int command = workModeToCommand(mode, ctx);
if (command == CMD_HELP) {
    Logger::log(... "Invalid working mode %d, power off", mode);
    sleep(10);
    return CascadeResult::TerminalExit;     // == pre-C2 default: return -1
}
return runCommands(command, ctx);
```
Both apps map `TerminalExit` → `return -1` (skip the tail)
(`main_app.cpp:354-356`, `workmode_app.cpp:189-191`), matching pre-C2's
`return -1` from inside the dispatch.

Independently re-run (`/tmp` isolated roots, sim):
```
wm99 A (main_app)      rc=255  log: "runWorkMode Invalid working mode 99, power off"
wm99 B (workmode_app)  rc=255  log: "runWorkMode Invalid working mode 99, power off"
normalized app.log:    IDENTICAL (root-tokenized)
```
The invalid-mode path is NOT silently running CMD_HELP; it terminates with
TerminalExit → return -1, exactly as designed. PASS.

### 1e. Observation (Nice-to-have, NOT a T16 regression): invalid-mode now runs S9-S13

Pre-C2 `default: return -1` sat INSIDE the dispatch, BEFORE
`commonStartupPostDispatch` — so `-wm 99` SKIPPED S9-S13 (daemon register,
SD-mount, netif, factory-config, timezone) and bailed immediately.

Post-T16: `workModeToCommand`→CMD_HELP, then `commonStartupPostDispatch(CMD_HELP)`
RUNS fully, THEN `runWorkMode`→TerminalExit→return -1. So `-wm 99` now performs
full S9-S13 startup before exiting.

IMPORTANT: this divergence vs pre-C2 was INTRODUCED BY C-2/T15 (in C-2,
`command` was already CMD_HELP at `commonStartupPostDispatch` for invalid
modes, which also ran fully). T16 does not change C-2's invalid-mode behavior —
it is byte-identical to HEAD. Restoring the pre-C2 startup-skip for invalid
modes would require restructuring the dispatch and is out of T16's (C-3) scope.
Noting for the record; not a T16 blocker.

## FOCUS 2 — Sim A/B equivalence (PRIMARY behavior): PASS

Independently reproduced (isolated SIM_SD_ROOT roots, ANSI stripped, timestamps
+ root paths tokenized):

| case | rc_A(main) | rc_B(workmode) | app.log.norm | proof |
|------|-----------|----------------|--------------|-------|
| wm3 -rtc 1 | 124 | 124 | identical CMD_MOBILE sig | RTSP :8554 + HTTP :8080 + mDNS in BOTH (regression fix live) |
| wm1 -rtc 1 | 0 | 0 | identical | upload path |
| wm99 -rtc 1 | 255 | 255 | identical | invalid-mode TerminalExit |

wm3 CMD_MOBILE signature present in both A and B logs:
`Creating RTSP server ... Server started on port 8554`, `HTTP server started on
port 8080`, `Started mDNS service _t32cam._tcp.local`. rc=124 = timeout-killed
in the `while(ctx.lc.keepRunning())` RTSP loop (identical hang in both).

`-s` divergence (main→0, workmode→255) is BY DESIGN — workmode_app is -wm-only
(Gate 5). Not part of the `-wm` A/B contract.

Tester's 10-case matrix (`artifacts/t16_ab_logs/exit_code_matrix.txt`) all
MATCH; spot-checked normalized logs (`wm3_rtc1`, `wm99_rtc1`, `badargc_wm_only`)
are byte-identical A vs B. Confirmed.

## FOCUS 3 — workmode_app mirrors main_app's -wm path: PASS

Block-by-block sequence comparison (line markers above): commonStartup →
installSignalHandlers → -wm arg-validation (argc==5 + -rtc/--rtc-status, else
requestShutdown+sleep(10)+return-1) → WorkModeContext → workModeToCommand →
commonStartupPostDispatch → setCleanupHook (verbatim lambda: daynight DAY,
rgbLed LOW, settings save, client null, SIGTERM power-hold) → config alias →
runWorkMode(TerminalExit→return -1) → workmode_exit/main_exit → shutdown(ShutdownContext)
→ sim `_exit(0)` / HW `syncWithMCU + config->flush + Misc::poweroff + while(1)`.

No logic divergence from main_app's -wm arm. Single-shot dispatch arm
(`-w/-d/-s/-m/-rs/...`) correctly absent (workmode_app is -wm-only).

## FOCUS 4 — syncWithMCU move byte-identical: PASS

Old static (`git show afc8da6^:src/app/main_app.cpp:108-149`) vs new
`app_lifecycle::syncWithMCU()` (`ProcessLifecycle.cpp:611-643`): PID/UPID/UPWD
reads → devconf->set, `devconf->flush()`, RTC `mcu->setDatetime(localtime)`,
`return true`. Byte-identical body.

Call sites: `main_app.cpp:381` + `workmode_app.cpp:206` both call
`app_lifecycle::syncWithMCU()`. Static removed from main_app (grep clean).
app_lifecycle CMake: `#include "MCU.h"` (ProcessLifecycle.cpp:35),
`target_link_libraries(... mcu ...)` (CMakeLists.txt:45),
`include_directories(... hardware/mcu ...)` (CMakeLists.txt:21). Links resolve.

## FOCUS 5 — runWorkMode refactor behavior-preserving: PASS

`runWorkMode = workModeToCommand + (CMD_HELP? sleep+TerminalExit : runCommands)`
(`WorkModeRunner.cpp:881-892`). `runCommands` cascade (`WorkModeRunner.cpp:337-840`)
unchanged from C-2/T15. No double-execution of cascade steps; no skipped step.

Observation (Nice-to-have, benign): for valid modes 1/3, `workModeToCommand` is
called TWICE (main_app:286 pre-dispatch + runWorkMode:883 inside the cascade),
so `asyncBlink(60|30)` is invoked twice. Verified idempotent:
`GPIO::asyncBlink` (`GPIO.cpp:339-342`) — if `m_blinkActive`, just updates
in-loop params (identical values) and returns; no new thread, no flicker.
Pre-C2 called the switch exactly once. Benign cosmetic divergence; the A/B
logs are byte-identical so it has no observable effect.

## FOCUS 6 — Scope / hygiene: PASS

- `git diff --stat HEAD -- src/hal` → empty. No HAL touched.
- workmode_app is -wm-only: no single-shot flags (`-s/-u/-m/-rs/-ar/-vr/-grtc/-srtc`).
- New app UNSPAWNED: `media_app.cpp:257` still `"htc_main_app -wm " + ...`.
- Zero `std::to_string`/`std::stoi` in workmode_app.cpp (uses `stoi_custom`,
  uClibc-safe). Only match is the header comment stating the rule.
- Both apps build clean on BOTH platforms (independently rebuilt):
  - sim: `cmake --build build_sim --target htc_workmode_app htc_main_app` → exit 0
  - T32: `cmake --build build    --target htc_workmode_app htc_main_app` → exit 0
    (ELF 32-bit MIPS32, both binaries present in build/bin/)

## FOCUS 7 — R1 / Tier-3 device A/B: MANDATORY USER-RUN BEFORE C4 (FLAG)

Sim A/B cannot surface the R1 next-boot-no-hang risk (IMP/RTSP driver state on
real T32 hardware). T16 touches the run-loop + HAL-teardown path indirectly
(via shared `shutdown()` + `syncWithMCU` + the CMD_MOBILE RTSP start/stop
loop). The user MUST run device A/B: `htc_workmode_app -wm N -rtc M` vs
`htc_main_app -wm N -rtc M` across all sub-modes (0/1/2/3/4 × rtc 0/1) on a T32
device, verifying clean shutdown + next-boot-no-hang, BEFORE C4/T17 repoints
`media_app`'s spawn to `htc_workmode_app`. Not closable by this reviewer node.

## Verdict

All sound. `status: success`. The regression fix is a TRUE restoration of
pre-C2 ordering (switch-set-command before commonStartupPostDispatch), verified
live for `-wm 3`→CMD_MOBILE in both apps. syncWithMCU move byte-identical.
workmode_app mirrors main_app's -wm arm. Invalid-mode CMD_HELP handling
correctly reaches TerminalExit→return -1 (preserved vs C-2/HEAD). Tier-3 device
A/B flagged mandatory user-run before C4.
