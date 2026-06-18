# T15 — Tester Evidence (Phase C-2: extract `-wm` execution into WorkModeRunner)

Worktree: `.claude/worktrees/new-workmode` (branch `feature/new-workmode`)
HEAD under test: `6cb5b5a` (T14) + uncommitted T15 implementer changes.
Comparison baseline: `git show HEAD:src/app/main_app.cpp` (1200 lines).

## Gate table

| # | Gate | Result | Evidence |
|---|------|--------|----------|
| 1a | Sim build+link `htc_main_app` | PASS | `cmake --build build_sim -j6 --target htc_main_app` → exit 0; `[100%] Built target htc_main_app`. Binary md5 `4f0aa34ff4bf7290d7b68eb98e308428`. |
| 1b / R7 | T32 cross build+link `htc_main_app` | PASS | `cmake --build build -j6 --target htc_main_app` → exit 0; `[100%] Built target htc_main_app` (links `libapp_workmode.so` transitively). |
| 2a | 5 helpers byte-identical (static→anon-ns only) | PASS | whitespace-normalized diff empty after stripping leading `static`. |
| 2b | `if(command & CMD_*)` cascade byte-identical under allowed deltas | PASS | normalized diff = only a cosmetic `//RTC` comment + the final `return CascadeResult::Continue;` (replaces implicit fall-through to `main_exit:`). No logic-line delta. |
| 2c | `switch(working_mode)` byte-identical under allowed deltas | PASS | normalized diff empty (`working_mode`→`mode`, `lc.`→`ctx.lc.`, `return -1`→`return CascadeResult::TerminalExit`, brace indent from nesting). |
| 3 | 3 outcomes preserved (sim smoke A/B vs HEAD) | PASS | S1 valid `-wm 0`: exit 0 + "exit normally" (both). S2 invalid `-wm 99`: exit 255 + "Invalid working mode" + tail SKIPPED (both). S3 `-grtc`: exit 0 + "get RTC time error" + "exit normally" (both). |
| 4 | All modes route correctly | PASS | `runWorkMode` in `-wm` branch (main_app:376); `runCommands` in single-shot path (main_app:380). Single-shot dispatch (235-284) still sets CMD_* bits. |
| 5 | CMD_* accessible / single-sourced | PASS | `#define CMD_*` lives in WorkModeRunner.h:14-27; main_app includes `WorkModeRunner.h` (main_app:31). Single-shot dispatch uses them (main_app:176,236-280). |
| 6 | cleanupHook by-ref shares ctx instances | PASS | main()-locals `mgmtServClient`/`storageServClient` (main_app:185-186); ctx binds them via `std::shared_ptr<...>&` fields (WorkModeRunner.h:43-44, ctx built main_app:307-309); cleanupHook captures `&mgmtServClient, &storageServClient` (main_app:323) and nulls them (main_app:345-346) — SAME instances. |
| 7a | `src/hal` untouched | PASS | `git diff --stat HEAD -- src/hal` empty. |
| 7b | No `std::to_string`/`stoi` in WorkModeRunner.cpp | PASS | grep empty. Uses `to_string_custom`/`stoi_custom` (portable, from StringConvert.h). |
| Tier-3 | Device A/B = USER-RUN (R1 next-boot-hang not surfacable on sim) | FLAG | User must A/B every `-wm` sub-mode (0-4) + `-m`/`-s`/`-u`/`-rs` on T32 before C4. |

## Gate 2 — byte-identity diffs (PRIMARY)

### 2a. The 5 helpers — IDENTICAL
HEAD helper bodies extracted from `git show HEAD:src/app/main_app.cpp`:
- `getCurrentTimeFormatted` HEAD:74-83 ↔ WMR:71-80
- `getConfiguredPort` HEAD:96-106 ↔ WMR:82-92
- `processCmdSnap` HEAD:190-259 ↔ WMR:94-163
- `processCmdVideoRecord` HEAD:261-358 ↔ WMR:165-262
- `processCmdConcurrentSnapRecord` HEAD:360-427 ↔ WMR:264-331

Command: extract each range, strip leading `static ` (allowed static→anon-ns delta),
then `diff -wB head_norm wmr_norm`:
```
RESULT: 5 helpers IDENTICAL (byte-identical modulo static->anon-ns)
```

### 2b. The `if(command & CMD_*)` cascade — IDENTICAL under allowed deltas
HEAD cascade = main_app_HEAD.cpp:681-1170. WMR cascade = WorkModeRunner.cpp:347-839
(after the `config`/`program_type` local-alias setup at 344-345).
Applied allowed substitutions (`is_rtc_work_well`→`ctx.isRtcWorkWell`,
`mobile_rtsp_enabled`→`ctx.mobileRtspEnabled`, `mgmtServClient`/`storageServClient`→`ctx.*`,
`lc.`→`ctx.lc.`, `daynight_switch`/`gpio_rgb_led`→`ctx.lc.daynight()`/`ctx.lc.rgbLed()`,
`rtsp_singleton_used=true`→`ctx.lc.markRtspSingletonUsed()`,
`while(!already_in_exit_flow)`→`while(ctx.lc.keepRunning())`,
`waitForSignalOrTimeout(`→`ctx.lc.waitForSignal(`, `[&lc]`→`[&ctx]`,
`argv[2]`→`ctx.argv[2]`, `goto main_exit;`→`return CascadeResult::Continue;`),
then `diff -wB`:
```
0a1
>     //RTC                <- cosmetic comment, non-logic
490a492,493
> 
>     return CascadeResult::Continue;   <- replaces implicit fall-through to main_exit:
```
No other logic-line delta. `config`/`program_type` kept as cascade-local aliases
(WMR:344-345) so the moved body stays byte-identical.

### 2c. The `switch(working_mode)` — IDENTICAL under allowed deltas
HEAD switch = main_app_HEAD.cpp:584-613 ↔ WMR runWorkMode = WorkModeRunner.cpp:845-874.
Applied `working_mode`→`mode`, `lc.`→`ctx.lc.`, `return -1;`→`return CascadeResult::TerminalExit;`,
then `diff -wB`:
```
RESULT: switch IDENTICAL under allowed deltas
```

## Gate 3 — sim smoke A/B (current vs HEAD)

Run on `build_sim/bin/htc_main_app` with `LD_LIBRARY_PATH=build_sim/lib`. Each `timeout 30`.

### S1 — `-wm 0 -rtc 1` (SNAP_ONLY, valid → Continue → tail runs)
| Build | exit | tail marker |
|-------|------|-------------|
| HEAD  | 0    | `[SIM] Program exit normally` (line 77) |
| T15   | 0    | `[SIM] Program exit normally` (line 77) |
Both reach the shutdown tail. PASS.

### S2 — `-wm 99 -rtc 1` (invalid → TerminalExit → `return -1` → tail SKIPPED)
| Build | exit | invalid-mode log | "exit normally"? |
|-------|------|------------------|------------------|
| HEAD  | 255 (-1) | `main Invalid working mode 99, power off` | NO |
| T15   | 255 (-1) | `runWorkMode Invalid working mode 99, power off` | NO |
Only delta: `__func__` changed `main`→`runWorkMode` (unavoidable, switch moved into a named fn).
Same message format, same `%d` mode, same power-off, same return code, tail correctly SKIPPED. PASS.

### S3 — `-grtc` (single-shot GET_RTC → runCommands → Continue → tail)
| Build | exit | markers |
|-------|------|---------|
| HEAD  | 0    | `get RTC time error` + `[SIM] Program exit normally` |
| T15   | 0    | `get RTC time error` + `[SIM] Program exit normally` |
runCommands reaches the tail via Continue. PASS.

## Gate 4/5/6 grep evidence

```
$ grep -nE 'runCommands|runWorkMode|CascadeResult' src/app/main_app.cpp
376:        if (app_workmode::runWorkMode(working_mode, ctx) == app_workmode::CascadeResult::TerminalExit) {
377:            return -1;   // invalid -wm mode: skip the tail (matches today's `return -1`)
380:        (void)app_workmode::runCommands(command, ctx);

$ grep -nE '#define CMD_' src/app/workmode/WorkModeRunner.h   # 14-27, single-source
$ grep -n 'WorkModeRunner.h' src/app/main_app.cpp            # 31 (include)

$ grep -n 'mgmtServClient\|storageServClient' src/app/main_app.cpp
185:    std::shared_ptr<MgmtServClient>   mgmtServClient   = nullptr;
186:    std::shared_ptr<StorageServClient> storageServClient = nullptr;
309:                                      mgmtServClient, storageServClient};   # ctx binds by ref
323:    lc.setCleanupHook([&lc, &mgmtServClient, &storageServClient](int sig) {
345:        mgmtServClient = nullptr;        # hook nulls SAME instances
346:        storageServClient = nullptr;
```

## Scope

`git diff --stat HEAD -- src/`:
```
 src/app/main_app.cpp            | 871 ++--------------------------------------
 src/app/workmode/CMakeLists.txt |  76 +++-
 2 files changed, 109 insertions(+), 838 deletions(-)
```
`git diff --stat HEAD -- src/hal` → empty (gate 7a).

New (untracked) source files: `src/app/workmode/WorkModeRunner.{cpp,h}`.
The `res/*` modifications are runtime artifacts from the sim smoke runs (snap JPG/JSON,
config.sim.ini/setting.json touched by the sim) — not source changes.

## Commands run

```
cmake --build build_sim -j6 --target htc_main_app        # exit 0
cmake --build build    -j6 --target htc_main_app         # exit 0 (R7)
git show HEAD:src/app/main_app.cpp > /tmp/main_app_HEAD.cpp
# byte-identity extractions + normalized diffs (see Gate 2 above)
timeout 30 build_sim/bin/htc_main_app -wm 0 -rtc 1       # S1
timeout 30 build_sim/bin/htc_main_app -wm 99 -rtc 1      # S2
timeout 30 build_sim/bin/htc_main_app -grtc              # S3
# (same 3 on HEAD binary for A/B)
grep -nE 'runCommands|runWorkMode|CascadeResult|CMD_|mgmtServClient|storageServClient' ...
git diff --stat HEAD -- src/hal     # empty
git diff --stat HEAD -- src/        # main_app.cpp + CMakeLists.txt only
```

## Tier-3 device A/B — USER-RUN (flag, not blocking)

Sim cannot surface R1 (the next-boot-hang regression that motivated this refactor
series). Before C4 the user MUST A/B on T32 hardware:
- every `-wm` sub-mode: `-wm 0`, `-wm 1`, `-wm 2`, `-wm 3`, `-wm 4` (each with `-rtc 0` and `-rtc 1`);
- single-shot flags: `-m`, `-s`, `-u`, `-rs`;
- confirm the device boots cleanly the *next* power-cycle after each run (R1 signal).
