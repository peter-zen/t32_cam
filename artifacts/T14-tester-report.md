---
contract: report
contract_version: "1"
task_id: T14
node: tester
flow: feature
status: success
summary: |
  Gate1 PASS — build_sim htc_main_app exit 0 (libapp_lifecycle.so 1ee116b6..., 2524896B); build (T32 cross, R7) htc_main_app exit 0 (libapp_lifecycle.so c1afe579..., 386956B); both .so exist.
  Gate2 PASS — PRIMARY byte-diff: signalHandler IDENTICAL (only static->void wrap); drainSignalPipe IDENTICAL (whitespace-normalized); waitForSignalOrTimeout's ONLY delta is performCleanup(sig)->cleanupHookRef()(sig) (the documented R3 split); performCleanup mode-local resets (cleanupHook) byte-identical modulo ref rewrites + the transport-teardown lines that moved to shutdown(); main_exit tail statements byte-identical (rtsp_singleton_used-gated RtspServer::shutdown, self-pipe close, Settings save, power-hold GPIO, auto_release.release with a null-guard) with terminal _exit/poweroff correctly staying in main_app; S1-S8 + S9-S13 verbatim modulo member/cfg rewrites. The performCleanup->hook split is the ONLY logic delta.
  Gate3 PASS — cascade (RTC->CMD_AUTH/UPLOAD) byte-identical vs HEAD after applying the documented ref rewrites (daynight_switch->lc.daynight(), gpio_rgb_led->lc.rgbLed(), rtsp_singleton_used=true->lc.markRtspSingletonUsed(), while(!already_in_exit_flow)->while(lc.keepRunning()), waitForSignalOrTimeout->lc.waitForSignal); RTSP DI lambdas capture &lc; no other cascade logic line changed.
  Gate4 PASS — Option A confirmed: signalHandler is a free fn over file-scope sig_atomic_t+int[2]+bool state, body is ONLY sig_atomic_t store + write(2) + already_in_exit_flow guard; NO this/impl_/pointer deref; g_active_lc appears ONLY in the comment forbidding Option B.
  Gate5 PASS — R1 gate: markRtspSingletonUsed called at exactly 2 sites (main_app :974 CMD_MOBILE/mobile_rtsp_enabled, :1014 CMD_RTSP_SERVER) matching HEAD's 2 original sites (:1296,:1336); shutdown() gates RtspServer::getInstance()->shutdown() on impl_->rtsp_singleton_used at PL.cpp :571-572.
  Gate6 PASS — sim smoke: -wm 0 -rtc 1 exits 0 (Power off From Main + SIM Program exit normally); -wm 3 -rtc 1 + SIGTERM exits 0 with full signal path logged (Processing signal 15 -> RtspServer::shutdown teardown -> teardown complete -> Power off -> Program exit normally) proving the cleanupHook runs AND the R1 gate fires on the CMD_MOBILE path. -h returns -1 (pre-existing, byte-identical to HEAD; the gate's "exits 0" was imprecise).
  Gate7 PASS — git diff --stat src/hal empty; zero std::to_string/stoi in ProcessLifecycle.cpp; src/ scope = only main_app.cpp + app/CMakeLists.txt + new app_lifecycle/; daynight/gpio/auto_release are pImpl members not globals.
  Tier-3 device A/B (cold-boot IMP configure() hang) is USER-RUN — sim Misc::poweroff is a no-op and _exit(0) skips the freeze so sim cannot surface the R1 symptom; user must A/B -m/-wm 3 (CMD_MOBILE) and -rs (CMD_RTSP_SERVER) on the T32 device.
  NOTE for reviewer (not a fail): root CMakeLists.txt:27 set(CMAKE_POSITION_INDEPENDENT_CODE ON) — one-line global build flag outside the app_lifecycle scope; required for dual-platform SHARED-lib link, relocation-model-only (behavior-neutral), MIPS already used PIC; rollback = delete the line.
deliverables:
  - artifacts/T14-tester-evidence.md
  - artifacts/T14-tester-report.md
verification:
  commands:
    - cmake --build build_sim -j$(nproc) --target htc_main_app
    - cmake --build build -j$(nproc) --target htc_main_app
    - git show HEAD:src/app/main_app.cpp
    - grep -nE 'markRtspSingletonUsed|signalHandler|g_active_lc' src/app/app_lifecycle/ProcessLifecycle.cpp src/app/main_app.cpp
    - grep -rnE 'std::to_string|std::stoi' src/app/app_lifecycle/
    - git diff --stat HEAD -- src/hal
  evidence_ref: artifacts/T14-tester-evidence.md
state_delta:
  set_task_status: {}
artifact_path: artifacts/T14-tester-evidence.md
---

# T14 tester report — Phase C-1 app_lifecycle extraction

**Status: SUCCESS** — all 7 gates PASS. Behavior-preserving refactor verified.

## Gate results

| Gate | Result | Key evidence |
|------|--------|--------------|
| 1 Build/link (both platforms) | PASS | build_sim + build (T32 R7) both exit 0; libapp_lifecycle.so present on both (md5s in evidence) |
| 2 Moved bodies byte-identical (PRIMARY) | PASS | signalHandler/drain/waitFor identical; only delta = performCleanup(sig)->cleanupHook (R3 split); main_exit tail + S1-S13 verbatim modulo member/cfg rewrites |
| 3 Cascade logic unchanged | PASS | only deltas are the documented lc. ref rewrites + &lc DI lambda captures |
| 4 Option A async-signal-safety | PASS | signalHandler = free fn over file-scope state, no pointer deref, no g_active_lc |
| 5 R1 gate | PASS | markRtspSingletonUsed at exactly 2 sites (CMD_MOBILE + CMD_RTSP_SERVER); gate present in shutdown() |
| 6 Sim smoke | PASS | -wm 0 exits 0; -wm 3+SIGTERM logs full signal path + R1 gate fire, exits 0 |
| 7 Scope | PASS | src/hal empty; no std::to_string/stoi; scope = main_app + app_lifecycle only |

## Single intentional logic delta (R3 split)
`performCleanup(sig)` in `waitForSignalOrTimeout` → invoke caller-registered `cleanupHook` (mode-local resets live in the hook; transport teardown lives in `shutdown()`). This is the ONLY logic change; confirmed by byte-diff. Everything else is wrapping + ref rewrites.

## User-run (Tier-3 device A/B)
Sim cannot surface the R1 cold-boot IMP configure() hang symptom (sim poweroff is a no-op, `_exit(0)` skips the freeze). User must A/B `-m`/`-wm 3` (CMD_MOBILE) and `-rs` (CMD_RTSP_SERVER) on T32 hardware vs HEAD.

## Reviewer flag
Root `CMakeLists.txt:27` `set(CMAKE_POSITION_INDEPENDENT_CODE ON)` — global build flag outside the `src/app/app_lifecycle/` scope. Required for the SHARED-lib link (sim static deps needed PIC); behavior-neutral (relocation model only); MIPS already used PIC. Not a failure per the dispatch instruction.
