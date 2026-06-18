---
contract: report
contract_version: "1"
task_id: T14
node: reviewer
flow: feature
status: success
summary: |
  APPROVE. Independent audit confirms the Phase C-1 app_lifecycle extraction is
  byte-identical to HEAD modulo the single documented performCleanup→hook split.
  signalHandler / drainSignalPipe are verbatim IDENTICAL (diff-clean); the
  waitForSignalOrTimeout delta is only performCleanup(sig)→cleanup-hook (the one
  allowed logic delta); shutdown() is statement-for-statement identical to HEAD
  main_exit :1501-1535 with the rtsp_singleton_used gate at the SAME position
  (R1 PASS). cleanupHook is invoked only from waitForSignalOrTimeout under the
  already_in_exit_flow one-shot guard, never from shutdown() (no double-invoke).
  R1 gate: markRtspSingletonUsed at exactly the 2 original sites (CMD_MOBILE
  :974, CMD_RTSP_SERVER :1014); sim SIGTERM smoke shows the gate firing on the
  CMD_MOBILE path. Ordering (signal handlers before dispatch/run-loop), self-pipe
  O_NONBLOCK both ends, Option A async-signal-safety (no g_active_lc pointer),
  src/hal untouched, zero std::to_string/stoi — all PASS. Both platforms build
  + link clean (sim + T32 cross), no TEXTREL.
  DEVIATION 1 (root CMakeLists global set(CMAKE_POSITION_INDEPENDENT_CODE ON)):
  ACCEPT — necessary (http_server/event_service/discovery_service/camera_service
  are STATIC without PIC and must link into the SHARED app_lifecycle on x86_64
  sim) and behavior-neutral on T32/MIPS (already PIC; both builds link). A
  targeted set_property alternative on the 4 service libs is equally valid.
  DEVIATION 2 (pimpl impl_ for non-signal members): ACCEPT — signal machinery
  stays file-scope (Option A); Impl holds only daynight/gpio/auto_release/
  setting_file_path/program_type/rtsp_singleton_used; behavior-equivalent, no
  leak/lifetime issue. Tier-3 device A/B (cold-boot IMP configure() hang on
  -m/-wm 3/-rs) is the mandatory USER-RUN gate — sim cannot surface the R1
  symptom (poweroff is a no-op, _exit skips the freeze).
deliverables:
  - artifacts/T14-reviewer-evidence.md
  - artifacts/T14-reviewer-report.md
verification:
  commands:
    - git show HEAD:src/app/main_app.cpp
    - cmake -DBUILD_FOR_SIMULATION=ON -B build_sim -S . && cmake --build build_sim -j$(nproc)
    - cmake -DCMAKE_TOOLCHAIN_FILE=toolchain.cmake -B build -S . && cmake --build build -j$(nproc)
    - git diff HEAD -- CMakeLists.txt
    - grep -nE 'POSITION_INDEPENDENT_CODE' src/service/*/CMakeLists.txt src/storage/CMakeLists.txt
    - grep -nE 'signalHandler|cleanupHook|markRtspSingletonUsed' src/app/app_lifecycle/ProcessLifecycle.cpp
    - readelf -d build_sim/lib/libapp_lifecycle.so
  evidence_ref: artifacts/T14-reviewer-evidence.md
state_delta:
  set_task_status: {}
artifact_path: artifacts/T14-reviewer-evidence.md
---

# T14 reviewer report card — APPROVE

## Verdict: APPROVE (status: success)

## Findings by severity

### Blocker (must-fix before merge)
- None.

### Substantive (would loopback) — all verified CLEAN
- Hidden logic edit in moved bodies: **NONE.** `signalHandler` and `drainSignalPipe` are `diff`-clean identical to HEAD; `waitForSignalOrTimeout`'s only delta is the documented `performCleanup(sig)`→cleanup-hook split; `shutdown()` matches HEAD main_exit :1501-1535 statement-for-statement.
- R1 gate broken: **NO.** `markRtspSingletonUsed` at exactly the 2 original sites; gate fires before self-pipe close / Settings save / poweroff; sim SIGTERM smoke confirms firing on CMD_MOBILE.
- cleanupHook double-invoke: **NO.** Invoked only in `waitForSignalOrTimeout` under the `!already_in_exit_flow` one-shot guard; `shutdown()` does not touch it.
- Non-behavior-neutral PIC side effect on T32: **NONE.** Both sim + T32 builds link clean; no TEXTREL; MIPS already PIC.

### Nice-to-have (non-blocking)
- The global `CMakeLists.txt` PIC flip is the broadest correct fix. A more surgical alternative — `set_property(TARGET http_server event_service discovery_service camera_service PROPERTY POSITION_INDEPENDENT_CODE ON)` inside each service `CMakeLists.txt` — would limit the relocation-model change to exactly the 4 non-PIC static libs that need it. Acceptable either way; not required to change.
- `ProcessLifecycle` ctor retains a dead `g_constructed` debug flag (PL.cpp :251-257) that intentionally does not assert. Cosmetic; harmless. Could be removed or actually asserted in a future debug pass.

## PIC-deviation verdict
**ACCEPT the global line** (status: success). It is necessary (4 STATIC deps lack PIC and must link into SHARED app_lifecycle on x86_64 sim) and behavior-neutral on T32/MIPS. Targeted-fix alternative noted as nice-to-have. No rollback required.

## Tier-2 semantic review results
| Check | Result |
|-------|--------|
| Byte-identical moved bodies (only performCleanup split) | PASS |
| R1 gate position + 2 mark sites + sim firing | PASS |
| cleanupHook single-invoke, not in shutdown() | PASS |
| Ordering: handlers before dispatch/run-loop | PASS |
| self-pipe O_NONBLOCK both ends | PASS |
| Option A async-signal-safety (no g_active_lc deref) | PASS |
| shutdown() tail byte-faithful to :1501-1535 | PASS |
| src/hal untouched; no std::to_string/stoi | PASS |
| Both platforms build+link (sim + T32 cross) | PASS |

## Tier-3 — mandatory USER-RUN gate (flagged)
Sim cannot surface the R1 symptom (`Misc::poweroff` no-op, `_exit(0)` skips freeze). User must A/B cold-boot every sub-mode (`-wm 0..4 -rtc {0,1}`, `-m`, `-rs`) vs HEAD on T32 hardware, watching `-m`/`-wm 3` (CMD_MOBILE) and `-rs` (CMD_RTSP_SERVER) for the R1 gate firing before the freeze (next-boot IMP `configure()` hang on stale driver state). **Not driveable from this node.**
