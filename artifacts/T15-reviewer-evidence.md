# T15 — Reviewer Evidence (Phase C-2: extract `-wm` execution into WorkModeRunner)

Worktree: `.claude/worktrees/new-workmode` (branch `feature/new-workmode`).
HEAD under audit: `6cb5b5a` (T14) + uncommitted T15 implementer changes.
Baseline: `git show HEAD:src/app/main_app.cpp` (1200 lines) → `/tmp/main_app_HEAD.cpp`.

This is an INDEPENDENT re-verification of the tester's gates. Every byte-identity
claim below was reproduced by extracting the HEAD ranges and the WorkModeRunner
ranges into temp files, applying the allowed substitutions, and `diff -wB`.

## Independent gate results

| # | Gate | Result | How verified |
|---|------|--------|--------------|
| 1 / R7 | Sim + T32 build+link `htc_main_app` | PASS | `cmake --build build_sim -j6 --target htc_main_app` → exit 0; `cmake --build build -j6 --target htc_main_app` → exit 0 (only 2 pre-existing `-Wunused-function` warnings for `normalizePath`/`parseIniFile`, both inherited from HEAD). |
| 2a | 5 helpers byte-identical (static→anon-ns only) | PASS | Extracted HEAD helpers + WMR helpers, stripped leading `static `, `diff -wB` empty for all 5 (H1..H5 IDENTICAL). |
| 2b | `if(command & CMD_*)` cascade byte-identical under allowed deltas | PASS | HEAD 681-1170 ↔ WMR 347-839. After applying `is_rtc_work_well→ctx.isRtcWorkWell`, `mobile_rtsp_enabled→ctx.mobileRtspEnabled`, `mgmtServClient/storageServClient→ctx.*`, `lc.→ctx.lc.`, `[&lc]→[&ctx]`, `argv[2]→ctx.argv[2]`, `goto main_exit;→return CascadeResult::Continue;`, `diff -wB` = ONLY the trailing `return CascadeResult::Continue;` (normal-completion fall-through to the old `main_exit:` label). ZERO other logic delta. |
| 2c | `switch(working_mode)` byte-identical under allowed deltas | PASS | HEAD 585-613 ↔ WMR 845-874. After `working_mode→mode`, `lc.→ctx.lc.`, `return -1;→return CascadeResult::TerminalExit;`, `diff -wB` empty (body identical). |
| 2d | R1 run-loop rewiring (CMD_MOBILE/CMD_RTSP_SERVER) exact | PASS | HEAD cascade ALREADY used `lc.markRtspSingletonUsed()` / `lc.keepRunning()` / `lc.waitForSignal()` (lines 974, 993-994, 1014, 1016-1017) — these lifecycle methods existed pre-C2 (Phase B-3/T12). C2's only change is `lc.→ctx.lc.` + `[&lc]→[&ctx]`; the loop bodies are byte-identical (covered by 2b). |
| 3 | goto→CascadeResult outcome-preservation (sim smoke A/B) | PASS | S1 `-wm 0`→exit 0+"exit normally" (tail via Continue); S2 `-wm 99`→exit 255(-1)+"Invalid working mode"+tail SKIPPED (TerminalExit→`return -1`); S3 `-grtc`→exit 0+"get RTC time error"+"exit normally" (Continue→tail). |
| 4 | All modes route | PASS | `-wm`→`runWorkMode` (main_app:376); all 14 single-shot flags (`-w/-d/-s/-qs/-ar/-vr/-a/-hb/-u/-m/-n/-rs/-grtc/-srtc`) set `command` in dispatch (236-280) → `runCommands` (main_app:380). |
| 5 / ODR | CMD_* single-sourced, no duplicate `#define` | PASS | `#define CMD_*` lives ONLY in WorkModeRunner.h:14-27; `grep -rnE '#define CMD_(CONN_NET|...)' src/` finds zero other definitions. main_app gets them via `#include "WorkModeRunner.h"` (main_app:31). |
| 6 / R3 | cleanupHook by-ref shares ctx instances | PASS | `mgmtServClient`/`storageServClient` are main()-locals (main_app:185-186); ctx holds them as `shared_ptr<...>&` (WorkModeRunner.h:43-44), bound at construction (main_app:307-309); cascade WRITES `ctx.mgmtServClient=make_shared<...>` (WMR:695) and `ctx.storageServClient=...` (WMR:717) through the ref; cleanupHook captures `&mgmtServClient,&storageServClient` (main_app:323) and nulls them (main_app:345-346) — SAME instances. `lc` is by-ref; outlives runCommands (main()-local, main_app:216). |
| 7a | `src/hal` untouched | PASS | `git diff --stat HEAD -- src/hal` empty. |
| 7b | No `std::to_string`/`stoi` in WorkModeRunner.cpp | PASS | grep empty; uses `to_string_custom`/`stoi_custom` (portable per T32-uclibc constraint). |
| 7c | Inherited latent `config->flush()` null-deref is pre-existing | PASS (pre-existing) | HEAD `main_exit` tail (line 1193) calls `config->flush()`; HEAD `config` is null on the early-goto path exactly as in C2. Byte-identical to HEAD — NOT introduced by C2. Currently unreachable (`commonStartupPostDispatch` cannot fail today, per comment main_app:219). |
| Tier-3 | Device A/B = USER-RUN (R1 next-boot-hang not surfacable on sim) | FLAG | Mandatory user-run before C4: every `-wm` sub-mode (0-4) + `-m/-s/-u/-rs` on T32, confirm clean next-boot. |

## Gate 2 — byte-identity diffs (PRIMARY), reproduced independently

### goto-conversion accounting
- HEAD cascade range (681-1170): **30** `goto main_exit;`.
- HEAD switch range (584-613): **0** `goto main_exit;`.
- WMR `runCommands` body (337-840): **31** `return CascadeResult::Continue;` = 30 ex-gotos + 1 normal-completion fall-through (line 839). Exact.
- WMR `runWorkMode` (842-877): **1** `return CascadeResult::TerminalExit;` (the invalid-mode `default`). Exact.
- `grep -nE '\bgoto\b' src/app/workmode/WorkModeRunner.cpp` → ZERO (only a comment mention).
- main_app.cpp retains ONE `goto main_exit;` (line 314, the `commonStartupPostDispatch` failure path) + the `main_exit:` label (line 383) — this is the pre-existing early-fail path, NOT part of the cascade, correctly left in main_app.

### Cascade normalized diff (the load-bearing evidence)
Applied allowed substitutions to HEAD 681-1170, then `diff -wB` vs WMR 347-839:
```
490a491,492
> 
>     return CascadeResult::Continue;
```
That single hunk is the normal-completion fall-through (HEAD's cascade fell off the end into `main_exit:`; WMR makes it explicit `return Continue`). ZERO other logic-line delta.

### Switch normalized diff
HEAD 585-613 vs WMR 845-874, after `working_mode→mode`, `lc.→ctx.lc.`, `return -1→TerminalExit`:
```
0a1
>     switch (mode) {     <- bracket I excluded from the HEAD extract; body identical
```

### 5 helpers — IDENTICAL (static→anon-ns only)
`diff -wB` empty for all of: `getCurrentTimeFormatted` (HEAD 74-83 ↔ WMR 71-80), `getConfiguredPort` (HEAD 96-106 ↔ WMR 82-92), `processCmdSnap` (HEAD 190-259 ↔ WMR 94-163), `processCmdVideoRecord` (HEAD 261-358 ↔ WMR 165-262), `processCmdConcurrentSnapRecord` (HEAD 360-427 ↔ WMR 264-331).

## goto→CascadeResult outcome-preservation verdict

HEAD `main()` had exactly ONE `main_exit:` label reached by: (a) normal cascade completion, (b) any in-cascade `goto main_exit`, (c) the `commonStartupPostDispatch`-fail `goto main_exit`. The invalid-mode `default` did `return -1` from main (tail skipped).

C2 mapping:
- (a) normal completion → `runCommands` returns Continue → main falls through to `main_exit:` tail. SAME.
- (b) in-cascade goto (30 sites) → `runCommands` returns Continue → main runs tail. SAME (tail reached).
- (c) `commonStartupPostDispatch`-fail → still `goto main_exit` in main_app (line 314) → tail. SAME.
- invalid `-wm` → `runWorkMode` default returns TerminalExit → main does `return -1` (line 377) → tail SKIPPED. SAME as HEAD's `return -1`.

No outcome changed. The tail-reach semantics are preserved exactly (normal→tail; any goto→tail; invalid-mode→skip tail). VERDICT: outcome-preserving.

## Gate 6 — by-ref evidence
```
main_app:185  std::shared_ptr<MgmtServClient>   mgmtServClient   = nullptr;
main_app:186  std::shared_ptr<StorageServClient> storageServClient = nullptr;
main_app:307  app_workmode::WorkModeContext ctx{lc, ..., mgmtServClient, storageServClient};
main_app:323  lc.setCleanupHook([&lc, &mgmtServClient, &storageServClient](int sig) {
main_app:345      mgmtServClient = nullptr;     # nulls SAME instances
main_app:346      storageServClient = nullptr;
WMR.h:43      std::shared_ptr<network::MgmtServClient>&   mgmtServClient;   # BY REF
WMR.h:44      std::shared_ptr<network::StorageServClient>& storageServClient;
WMR:695       ctx.mgmtServClient = std::make_shared<MgmtServClient>(...);   # writes via ref
WMR:717       ctx.storageServClient = ctx.mgmtServClient->newStorageServClient();
```

## Sim smoke A/B (T15 build, reproduced)
```
S1  build_sim/bin/htc_main_app -wm 0 -rtc 1   → exit 0  + "[SIM] Program exit normally"      (tail)
S2  build_sim/bin/htc_main_app -wm 99 -rtc 1  → exit 255 + "runWorkMode Invalid working mode 99, power off"  (tail SKIPPED)
S3  build_sim/bin/htc_main_app -grtc          → exit 0  + "get RTC time error" + "[SIM] Program exit normally"
```
S2's only delta vs HEAD is `__func__` = `runWorkMode` (was `main`) — unavoidable, switch moved into a named fn; same `%d` mode, same power-off, same return code.

## CMake (R7) spot-check
- `app_workmode` CMakeLists declares ~24 PUBLIC deps (app_lifecycle, common_misc, network, storage, media_snap, media_recorder, audio_recorder, http_server, event_service, discovery_service, setting, env, devconf, common_time_rtc, common_utils_crc, crc16, power, disk, daynight, jsoncpp, civetweb, manifest + pthread/rt/gcc/stdc++). All map to symbols the cascade/5-helpers actually reference.
- One include dir `../service/camera` added — NEEDED: `#include "CameraRecorder.h"` (WMR:21) resolves to `src/service/camera/CameraRecorder.h`; `processCmdVideoRecord` uses `service::camera::CameraRecorder/RecordError/RecordOptions/RecordResult/RecordingPostProcess`.
- Both-platform link clean (see gate 1). No missing dep, no T32 break.

## Commands run
```
git show HEAD:src/app/main_app.cpp > /tmp/main_app_HEAD.cpp
cmake --build build_sim -j6 --target htc_main_app     # exit 0
cmake --build build    -j6 --target htc_main_app      # exit 0
# cascade/switch/helpers extraction + normalized diff -wB (see Gate 2)
LD_LIBRARY_PATH=build_sim/lib timeout 15 build_sim/bin/htc_main_app -wm 0 -rtc 1    # S1
LD_LIBRARY_PATH=build_sim/lib timeout 15 build_sim/bin/htc_main_app -wm 99 -rtc 1   # S2
LD_LIBRARY_PATH=build_sim/lib timeout 15 build_sim/bin/htc_main_app -grtc           # S3
grep -nE 'goto main_exit|CascadeResult|runCommands|runWorkMode|mgmtServClient|storageServClient|#define CMD_' ...
grep -rnE '#define CMD_(CONN_NET|DHCP|SNAP|MOBILE|RTSP_SERVER|NTP|GET_RTC|SET_RTC|UPLOAD|AUTH|HEARTBEAT|AUDIO_RECORD|VIDEO_RECORD|HELP)' src/   # only WorkModeRunner.h
git diff --stat HEAD -- src/     # main_app.cpp + workmode/CMakeLists.txt only
git diff --stat HEAD -- src/hal  # empty
```

## Scope
- Modified: `src/app/main_app.cpp` (-838/+109 net, the extraction), `src/app/workmode/CMakeLists.txt` (+deps/include).
- New (untracked): `src/app/workmode/WorkModeRunner.{cpp,h}`.
- `src/hal/**` untouched (gate 7a).
- The `res/*` working-tree modifications are runtime artifacts from sim smoke (snap JPG/JSON, config.sim.ini/setting.json) — not source.

## Nits (non-blocking, inherited)
- `parseIniFile` (main_app:97) is unused dead code; `normalizePath` is sim-only so it warns on T32. Both PRE-EXISTING in HEAD (lines 87, 143). Not introduced by C2; leave for a separate hygiene pass.
- Inherited latent `config->flush()` null-deref on the (currently unreachable) `commonStartupPostDispatch`-fail path — byte-identical to HEAD.

## Tier-3 device A/B — USER-RUN (mandatory before C4)
Sim cannot surface R1 (the next-boot-hang that motivated this refactor series). Before C4 the user MUST A/B on T32:
- every `-wm` sub-mode: `-wm 0`, `-wm 1`, `-wm 2`, `-wm 3`, `-wm 4` (each with `-rtc 0` and `-rtc 1`);
- single-shot flags: `-m`, `-s`, `-u`, `-rs`;
- confirm the device boots cleanly the NEXT power-cycle after each run (R1 signal).
