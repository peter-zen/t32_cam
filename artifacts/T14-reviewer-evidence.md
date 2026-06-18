# T14 reviewer evidence — independent final audit (Phase C-1 app_lifecycle)

Independent re-derivation of every gate. Baseline = `git show HEAD:src/app/main_app.cpp`
(1548 lines, written to `/tmp/HEAD_main_app.cpp`). Target = `src/app/app_lifecycle/`
+ rewired `src/app/main_app.cpp`. All commands re-run by reviewer; no trust on tester claims.

## E0 — Build evidence (re-run by reviewer)

| Build | Cmd | Exit | Artifacts |
|-------|-----|------|-----------|
| PC sim | `cmake -DBUILD_FOR_SIMULATION=ON -B build_sim -S . && cmake --build build_sim -j$(nproc)` | 0 | `build_sim/bin/htc_main_app` + `build_sim/lib/libapp_lifecycle.so` (no TEXTREL) |
| T32 cross | `cmake -DCMAKE_TOOLCHAIN_FILE=toolchain.cmake -B build -S . && cmake --build build -j$(nproc)` | 0 | `build/bin/htc_main_app` + `build/lib/libapp_lifecycle.so` |

Both link clean. Only pre-existing warnings (deprecated `Logger`, unused `SnapImgSize`/`stoi_custom`).
`readelf -d build_sim/lib/libapp_lifecycle.so | grep TEXTREL` → none (PIC correct).

## E1 — Byte-identical moved bodies (PRIMARY)

### E1a. `signalHandler` — HEAD :639-651 vs PL.cpp :140-152
Drop `static` from HEAD, line-slice, `diff` → **IDENTICAL** (safety-critical handler instruction stream preserved). Free fn over file-scope state; no `this`/vtable/pointer-deref. Option A confirmed.

### E1b. `drainSignalPipe` — HEAD :544-554 vs PL.cpp :96-106
`diff` (drop `static`) → **IDENTICAL**.

### E1c. `waitForSignalOrTimeout` — HEAD :560-577 vs PL.cpp :112-132
Sole non-whitespace delta is the documented R3 split:
`performCleanup(sig);`  →  `auto& hook = cleanupHookRef(); if (hook) { hook(sig); }`
Rest byte-identical. **This is the only allowed logic delta.**

### E1d. `shutdown()` — HEAD main_exit :1501-1535 vs PL.cpp :560-605
Statement-for-statement identical (ref-rewrite `rtsp_singleton_used`→`impl_->rtsp_singleton_used`,
`setting_file_path`→`impl_->setting_file_path`, `RtspServer`→`media::RtspServer`):
`TcpEvent::stop → Mdns::stop → rtsp_singleton_used-gated RtspServer::shutdown() → self-pipe close[0,1] → Settings::saveToJsonFile → "Power off" log → (HW) power-hold GPIO LOW → auto_release.release()`.
Additions are cosmetic step-comments + a null-guard on `auto_release` (always set in S7 → behavior-identical).
rtsp gate at SAME position (before self-pipe close, before Settings save, before poweroff). **R1 position PASS.**
Terminal steps (`_exit(0)` / `syncWithMCU+flush+poweroff+while(1)`) correctly stayed in `main_app.cpp` `main_exit:` (:1188-1199).

### E1e. cleanupHook lambda — main_app.cpp :634-672 vs HEAD performCleanup :589-632
Mode-local resets byte-identical (ref-rewritten): `Logger "Processing signal" → daynight DAY → rgbLed LOW → setting_file_path save → mgmtServClient/storageServClient null → SIGTERM power-hold HIGH`.
The transport-teardown lines (`http_server_stop/deinit` + `TcpEventService::stop`, HEAD :611-615) are NOT in the hook — they moved to `shutdown()`. **Behavior-equivalent** (see E3).

## E2 — R1 gate (rtsp_singleton_used)

- `markRtspSingletonUsed` sites in new `main_app.cpp`: exactly **2** — `:974` (inside `if (mobile_rtsp_enabled)` ⊂ CMD_MOBILE; HEAD :1296) and `:1014` (CMD_RTSP_SERVER; HEAD :1336). Same count, same source positions.
- Gate: PL.cpp :571-573 `if (impl_->rtsp_singleton_used) { media::RtspServer::getInstance()->shutdown(); }`, before self-pipe close / Settings save / poweroff.
- Sim assert: `-wm 3 -rtc 1` + SIGTERM → log shows `RtspServer::shutdown: process-level teardown` (R1 FIRED on CMD_MOBILE path). **R1 PASS.**

## E3 — cleanupHook single-invocation (no double-invoke)

- `cleanupHookRef()`/`hook(sig)` invoked ONLY in `waitForSignalOrTimeout` (PL.cpp :126-128), guarded by `if (!already_in_exit_flow)` → fires exactly once.
- `shutdown()` (PL.cpp :560-605) does NOT touch cleanupHook (grep-confirmed).
- Matches HEAD: `performCleanup` was called only from `waitForSignalOrTimeout`; `main_exit` never called it. **No double-invoke.**

### E3b — transport double-stop is behavior-equivalent to HEAD
HEAD signal-path: `performCleanup` stopped http_server + TcpEvent (idempotent), then `main_exit` stopped TcpEvent again (idempotent double-stop).
New signal-path: cleanupHook omits transport (moved to `shutdown()`); `shutdown()` stops TcpEvent + Mdns once. http_server is still torn down inline in the CMD_MOBILE post-loop block (main_app.cpp :1005-1008) and via the mobile loop on the signal path. For non-mobile paths http_server is never started, so no regression. **Equivalent.**

## E4 — Ordering (Tier-2)

HEAD: S1-S7 → pipe(:780)+signal(:793) → dispatch(:797 `argc<2`) → S9-S13 → cascade.
New:  `commonStartup`(S1-S7) → `installSignalHandlers`(S8) → dispatch(:522) → `commonStartupPostDispatch`(S9-S13) → cascade.
Signal handlers installed **before dispatch and before any blocking run-loop**, same as HEAD. **Ordering PASS.**
Self-pipe both ends `O_NONBLOCK` (PL.cpp :386,:388). **PASS.**

## E5 — DEVIATION 1 — root `CMakeLists.txt` global PIC flip (`:23-27`)

`set(CMAKE_POSITION_INDEPENDENT_CODE ON)` added. Necessity audit — static libs `app_lifecycle` (SHARED) links:
| Lib | Type | Had PIC? |
|-----|------|----------|
| `http_server` | STATIC | **NO** |
| `event_service` | STATIC | **NO** |
| `discovery_service` | STATIC | **NO** |
| `camera_service` | STATIC | **NO** |
| `storage` | (STATIC) | YES (`set_property`) |
| all others (`common_misc`,`setting`,`env`,`devconf`,`daynight`,`gpio`,`power`,`logger`,`daemon`,`common_time_timezone`,`media_rtsp`) | SHARED | already PIC |

**Necessary on x86_64 sim**: without `-fPIC` the 4 non-PIC static archives cannot be linked into the SHARED `app_lifecycle` (relocation errors). Confirmed: sim build LINKS with the flip; would fail without it.
**Behavior-neutral on T32/MIPS**: MIPS `.so` are already PIC; the flip only changes the relocation model of STATIC archives + adds GOT indirection to executables — negligible, both builds link clean, no size regression observed. No TEXTREL.

**Verdict: ACCEPT the global line** (simplest correct fix; both platforms link; MIPS-neutral). Targeted alternative (`set_property(TARGET http_server event_service discovery_service camera_service PROPERTY POSITION_INDEPENDENT_CODE ON)` in each service CMakeLists) is also valid and more surgical, but the global flip is acceptable for this codebase since no target relies on non-PIC codegen. No FAIL.

## E6 — DEVIATION 2 — pimpl (`impl_`) for non-signal members

Planner specified direct members; implementer used `Impl`. Verified:
- Signal machinery stays file-scope (Option A): `signalHandler`/`drainSignalPipe`/`waitForSignalOrTimeout`/`g_signal_pipe`/`g_pending_signal`/`already_in_exit_flow`/`cleanupHookRef` are anonymous-namespace, NOT in `Impl`. No `g_active_lc` pointer exists (only in forbidding comment :63).
- `Impl` holds only non-signal state: `daynight_switch`, `gpio_rgb_led`, `auto_release` (unique_ptr, constructed in S7), `setting_file_path`, `program_type`, `rtsp_singleton_used`.
- Behavior-equivalent: the AutoRelease reset lambda (PL.cpp :360-370) captures `[&]` and derefs `impl_->daynight_switch`/`impl_->gpio_rgb_led` — `impl_` outlives the lambda use (release() called in shutdown before ~Impl). No leak/lifetime issue (unique_ptr auto-deletes; on HW the process poweroffs before ~Impl anyway, matching HEAD's stack-local `auto_release`).
**pimpl ACCEPT.**

## E7 — Scope

- `git diff --stat HEAD -- src/hal` → **empty** (PIC-owned HAL untouched).
- `grep -rnE 'std::to_string|std::stoi' src/app/app_lifecycle/` → **NONE** (T32 uClibc safe; uses `stoi_custom`/`to_string_custom` in cascade, unchanged).
- src diff scope: only `src/app/CMakeLists.txt` (M), `src/app/main_app.cpp` (M), new `src/app/app_lifecycle/{CMakeLists.txt,ProcessLifecycle.h,ProcessLifecycle.cpp}`. Plus root `CMakeLists.txt` (E5).
- `res/` working-tree changes are sim runtime artifacts (snap test + Settings save), not source edits.

## E8 — Sim smoke (re-run by reviewer)

- `-h` → EXIT=255 (printUsage + return -1; byte-identical to HEAD; `-h` never exited 0).
- `-wm 3 -rtc 1` + SIGTERM → EXIT=0; markers in order: `Processing signal 15` (cleanupHook) → `RtspServer::shutdown: process-level teardown` (R1 fired) → `RtspServer::shutdown: teardown complete` → `Power off From Main function` (shutdown step 5) → `[SIM] Program exit normally` (`_exit(0)`). No crash/abort. **End-to-end signal→hook→R1→shutdown→terminal flow confirmed.**

## Tier-3 device A/B — USER-RUN (mandatory, flagged)

Sim `Misc::poweroff` is a no-op and `_exit(0)` skips the process freeze, so sim cannot surface the R1 symptom (next cold-boot IMP `configure()` hang on stale driver state). User must A/B every sub-mode (`-wm 0..4 -rtc {0,1}`, `-m`, `-rs`) cold-boot vs HEAD on T32 hardware, watching `-m`/`-wm 3` (CMD_MOBILE) and `-rs` (CMD_RTSP_SERVER) for the R1 gate firing before the freeze. **This is the mandatory user-run gate; not driveable from this node.**
