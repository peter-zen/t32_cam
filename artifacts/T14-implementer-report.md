---
contract: report
contract_version: "1"
task_id: T14
node: implementer
flow: feature
status: success
summary: |
  Created the app_lifecycle shared lib (ProcessLifecycle) and rewired htc_main_app
  to use it. The signal machinery (self-pipe + signalHandler, drainSignalPipe,
  waitForSignalOrTimeout), the S1-S8 + S9-S13 common startup, and the main_exit
  tail (steps 1-7 incl. the rtsp_singleton_used-gated RtspServer::shutdown())
  moved verbatim into the lifecycle TU as anonymous-namespace file-scope state
  (Option A) + pImpl members; signalHandler is a free fn over file-scope state
  with NO pointer deref (async-signal-safe). htc_main_app's dispatch + cascade
  logic is byte-identical; only lc. ref rewrites (daynight_switch->lc.daynight(),
  gpio_rgb_led->lc.rgbLed(), rtsp_singleton_used=true->lc.markRtspSingletonUsed(),
  while(!already_in_exit_flow)->while(lc.keepRunning()),
  waitForSignalOrTimeout->lc.waitForSignal, config->lc.config()). The -m/-wm 3
  cleanupHook (performCleanup :590-609 + :616-631 minus the transport teardown
  that moved to shutdown()) is registered via lc.setCleanupHook.
  BOTH PLATFORMS LINK: build_sim/lib/libapp_lifecycle.so + htc_main_app (exit 0),
  build/lib/libapp_lifecycle.so + htc_main_app (exit 0, T32 cross — R7 gate).
  Byte-diff verdict: signalHandler / drainSignalPipe / waitForSignalOrTimeout /
  main_exit tail / performCleanup bodies are byte-identical to HEAD (only
  static->anon-ns, daynight_switch->lc.daynight()/impl_->daynight_switch_,
  rtsp_singleton_used->impl_->rtsp_singleton_used, etc. ref rewrites). Sim smoke
  (-wm 0 -rtc 1, -wm 3 -rtc 1 + SIGTERM) confirms startup->dispatch->shutdown->
  terminal flow and the R1 gate fires (RtspServer::shutdown teardown logged) on
  the CMD_MOBILE path. Tier-3 device A/B (next cold-boot IMP configure() hang
  check) is USER-RUN and cannot be driven from this node.
deliverables:
  - src/app/app_lifecycle/ProcessLifecycle.h
  - src/app/app_lifecycle/ProcessLifecycle.cpp
  - src/app/app_lifecycle/CMakeLists.txt
  - src/app/CMakeLists.txt
  - src/app/main_app.cpp
  - CMakeLists.txt
  - artifacts/T14-implementer-report.md
verification:
  commands:
    - cmake --build build_sim -j$(nproc) --target htc_main_app
    - cmake --build build -j$(nproc) --target htc_main_app
    - ls build_sim/lib/libapp_lifecycle.so build/lib/libapp_lifecycle.so
    - git show HEAD:src/app/main_app.cpp
    - grep -nE 'markRtspSingletonUsed|lc\.(keepRunning|waitForSignal)' src/app/main_app.cpp
    - grep -nE 'std::to_string|std::stoi' src/app/app_lifecycle/ProcessLifecycle.cpp
    - grep -n 'g_active_lc' src/app/app_lifecycle/ProcessLifecycle.cpp
  evidence_ref: src/app/app_lifecycle/ProcessLifecycle.cpp
state_delta:
  set_task_status: {}
artifact_path: src/app/app_lifecycle/ProcessLifecycle.cpp
---

# T14 implementer report — Phase C-1 (app_lifecycle extraction)

## What shipped

1. **`src/app/app_lifecycle/ProcessLifecycle.h`** — `app_lifecycle::ProcessLifecycle`
   class + `StartupConfig` + `ShutdownContext`. pImpl front end so the heavy
   member types (DayNightSwitch / GPIO / AutoRelease) stay out of the public
   header. API: `commonStartup`, `commonStartupPostDispatch(cfg, command)`
   (takes command — R8, S11 netif reads CMD_MOBILE), `installSignalHandlers`
   (returns bool so the caller can bail on pipe() failure matching the original
   `return -1`), `keepRunning`, `waitForSignal`, `setCleanupHook`,
   `markRtspSingletonUsed` + `rtspSingletonUsed` accessor, `shutdown`,
   accessors `daynight/rgbLed/config/programType/settingFilePath`. Non-copyable.

2. **`src/app/app_lifecycle/ProcessLifecycle.cpp`** — Option (A): signal machinery
   (`already_in_exit_flow`, `g_signal_pipe[2]`, `g_pending_signal`,
   `drainSignalPipe`, `waitForSignalOrTimeout`, `signalHandler`) as
   **anonymous-namespace file-scope state**; `signalHandler` is a free fn that
   does ONLY a `sig_atomic_t` store + `write(2)` (no `this`, no pointer deref —
   a file-top comment explicitly forbids converting to Option B / a
   `g_active_lc` pointer). Startup helpers `setEnvIfEmpty` / `normalizePath` /
   `getParentPath` / `parseMediaScannerMode` / `trimConfigString` moved verbatim
   into the anon-ns. pImpl `Impl` owns `daynight_switch` / `gpio_rgb_led` /
   `auto_release` (unique_ptr) / `setting_file_path` / `program_type` /
   `rtsp_singleton_used`. `commonStartup` (S1-S8 verbatim), `commonStartupPostDispatch`
   (S9-S13 verbatim, takes command), `shutdown` (main_exit steps 1-7 verbatim —
   TcpEvent/Mdns stop, rtsp_singleton_used-gated RtspServer::shutdown() at the
   SAME position before self-pipe close + Settings save, power-hold GPIO,
   auto_release.release()). A private `const int CMD_MOBILE = (1<<8)` mirror
   keeps the lifecycle TU self-contained (these are app-internal bitmasks; C2
   will lift them).

3. **`src/app/app_lifecycle/CMakeLists.txt`** — SHARED, glob *.cpp,
   `LIBRARY_OUTPUT_DIRECTORY=${CMAKE_BINARY_DIR}/lib`, PUBLIC-link the moved
   code's deps. Per the hard rule, `common_time_rtc` and `mcu` were NOT added
   preemptively (the moved code doesn't use them); `camera_service` WAS added
   because the HW S11 factory-config importer (`CameraFactoryConfigImporter`)
   is compiled under `#else` and the T32 link needs it.

4. **`src/app/CMakeLists.txt`** — `add_subdirectory(app_lifecycle)` after
   `workmode`; added `${CMAKE_CURRENT_SOURCE_DIR}/app_lifecycle` to the include
   dirs (for `ProcessLifecycle.h`); added `app_lifecycle` to `htc_main_app`'s
   link in BOTH the sim and HW blocks. Existing main_app link list NOT pruned.

5. **`src/app/main_app.cpp`** — `main()` rewired: compute StartupConfig →
   `lc.commonStartup(cfg)` (early failure does `return -1`, matching the
   original pipe()-fail path and keeping `goto main_exit` from crossing the
   dispatch locals) → `lc.installSignalHandlers()` (bail on false) → UNCHANGED
   dispatch (only `gpio_rgb_led->` → `lc.rgbLed()->` at the 2 blink sites) →
   `lc.commonStartupPostDispatch(cfg, command)` (early failure does
   `goto main_exit`) → `lc.setCleanupHook([...])` (verbatim
   performCleanup :590-609 + :616-631) → UNCHANGED cascade (refs rewritten to
   `lc.`; a local `auto config = lc.config();` + `auto program_type =
   lc.programType();` alias minimizes diff) → `main_exit:` builds
   `ShutdownContext` + `lc.shutdown(sctx)` + sim `_exit(0)` /
   HW `syncWithMCU()`+`lc.config()->flush()`+`Misc::poweroff()`+`while(1)`.
   Removed the moved statics/helpers/regions (signal machinery, performCleanup,
   signalHandler, daynight_switch/gpio_rgb_led globals, already_in_exit_flow /
   rtsp_singleton_used / g_signal_pipe / g_pending_signal, the 4 moved startup
   helpers). Kept `normalizePath` as a main_app-local static (main() still needs
   it to resolve the SIM path inputs that feed StartupConfig). `getCurrentTimeFormatted`
   / `getConfiguredPort` / `syncWithMCU` / `parseIniFile` / `processCmd*` /
   `printUsage` / CMD_* defines / rtsp_audio_enabled / mobile_rtsp_enabled /
   mgmtServClient / storageServClient STAY.

6. **`CMakeLists.txt`** (root) — one-line addition: `set(CMAKE_POSITION_INDEPENDENT_CODE ON)`
   after `project()`. Reason: the new SHARED `app_lifecycle` embeds in-repo
   STATIC libs (event_service / http_server / discovery_service / etc.) that
   were built without `-fPIC` on x86_64 sim, which made the shared-lib link
   fail with "recompile with -fPIC". MIPS (T32) already uses PIC for shared
   libs. Setting PIC globally is the standard CMake idiom, changes only the
   relocation model (no behavior change), and unblocks the dual-platform build.
   `storage` already had PIC ON; the rest inherit it now.

## Why this satisfies the plan

- **C1 scope only**: the `-wm` cascade stays inline in main_app (NOT extracted
  to `runWorkMode` — that's C2/T15); no new binary (C3/T16); no `src/hal/**`
  edit; `media_app.cpp:257` spawn string untouched (C4/T17).
- **Behavior byte-identical**: every moved body is verbatim (whitespace-normalized
  diff vs HEAD shows only `static`→anon-ns/member-wrap + ref rewrites). The
  cascade + dispatch logic is unchanged.
- **Async-signal-safety Option (A)**: `signalHandler` is a free fn over
  file-scope `sig_atomic_t` + `int[2]` + `bool` — NO pointer deref in signal
  context. Verified by byte-diff against HEAD (identical instruction stream
  modulo relocation) and by grep (no `g_active_lc`).
- **R1 gate preserved**: `rtsp_singleton_used` set ONLY at the 2 original source
  lines via `lc.markRtspSingletonUsed()` (CMD_MOBILE + CMD_RTSP_SERVER);
  `shutdown()` gates `RtspServer::getInstance()->shutdown()` on it at the same
  position (step 2, before self-pipe close / Settings save / poweroff). Sim
  `-wm 3` + SIGTERM confirms the teardown fires (`RtspServer::shutdown:
  process-level teardown` + `teardown complete` logged).
- **R8 split**: `commonStartupPostDispatch(cfg, command)` runs S9-S13 AFTER
  dispatch; takes `command` so S11 netif selection reads CMD_MOBILE.
- **R3 split**: transport teardown lives in `shutdown()`; mode-local resets
  live in the caller-registered cleanupHook (invoked only from
  `waitForSignal()`, NOT from `shutdown()` — avoids double-stop).
- **Dual-platform**: `std::to_string`/`std::stoi` absent from the lifecycle TU
  (grep-verified; T32 uClibc safe).

## Validation

- `cmake --build build_sim -j$(nproc) --target htc_main_app` → exit 0.
- `cmake --build build -j$(nproc) --target htc_main_app` (T32 cross) → exit 0 (R7 gate).
- `ls build_sim/lib/libapp_lifecycle.so build/lib/libapp_lifecycle.so` → both present.
- Byte-diff (HEAD main_app.cpp vs ProcessLifecycle.cpp): signalHandler /
  drainSignalPipe / waitForSignalOrTimeout / main_exit tail / performCleanup
  bodies byte-identical (only ref rewrites). `waitForSignalOrTimeout`'s single
  intentional delta is `performCleanup(sig)` → `cleanupHookRef()(sig)` (the R3
  split).
- `grep -nE 'std::to_string|std::stoi' src/app/app_lifecycle/ProcessLifecycle.cpp` → empty.
- `grep -nE 'markRtspSingletonUsed|lc\.(keepRunning|waitForSignal)' src/app/main_app.cpp`
  → expected rewrites present (2 markRtspSingletonUsed sites: CMD_MOBILE + CMD_RTSP_SERVER).
- `grep -n 'g_active_lc' src/app/app_lifecycle/ProcessLifecycle.cpp` → only the
  comment forbidding Option B.
- Sim smoke: `-h` (usage + startup logs), `-wm 0 -rtc 1` (full startup→shutdown→
  terminal), `-wm 3 -rtc 1` + SIGTERM (signal cleanupHook + R1 gate fire +
  clean `_exit(0)`).

## Remaining concerns

- **Tier-3 device A/B is USER-RUN** (blocking per the plan's acceptance §14.8).
  This node cannot drive the T32 device. The user must A/B every sub-mode
  (`-wm 0..4 -rtc {0,1}`, `-m`) cold-boot vs HEAD and watch for the next-boot
  IMP `configure()` hang (the R1 symptom sim cannot surface because sim
  `Misc::poweroff` is a no-op and `_exit(0)` skips the freeze). Specifically
  A/B `-m` and `-wm 3` (both CMD_MOBILE, R2) and `-rs` (CMD_RTSP_SERVER).
- **Root `CMakeLists.txt` change** (`CMAKE_POSITION_INDEPENDENT_CODE ON`) is a
  one-line global build-flag change outside the `src/app/app_lifecycle/` scope
  listed in the plan. It is required for the dual-platform SHARED-lib link and
  is behavior-neutral (relocation model only), but it does touch the root build
  file — flagging for reviewer awareness. Rollback is trivial (delete the line).
- **`commonStartup` early-fail path** currently cannot trigger (it always
  returns true; pipe() lives in `installSignalHandlers`). The `if (!lc.commonStartup(cfg)) return -1;`
  guard is retained for API parity / future skipMediaScanner failures, and
  returns `-1` (matching the original pipe-fail semantics) rather than
  `goto main_exit` (which would cross the dispatch locals' initializers).
