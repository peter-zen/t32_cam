---
contract: report
contract_version: "1"
task_id: T12
node: planner
flow: feature
status: success
summary: |
  T12 = Phase B-3: lift the full CMD_RTSP_SERVER start/run/stop sequence into
  app_workmode via dependency injection (T11 Option B). DI keeps main_app's
  TU-private signal machinery (already_in_exit_flow, waitForSignalOrTimeout,
  g_signal_pipe, performCleanup) in place — only the RTSP sequence moves.
  Behavior-identical refactor (not a verbatim move): helper body is the
  original sequence modulo two injected callbacks that wrap the exact
  existing statics; ordering/log/control-flow unchanged.
deliverables:
  - artifacts/T12-planner-full.md
  - artifacts/T12-planner-report.md
verification:
  commands:
    - grep -nE 'CMD_RTSP_SERVER|rtsp_singleton_used|already_in_exit_flow|waitForSignalOrTimeout|performCleanup|g_signal_pipe|getConfiguredPort' src/app/main_app.cpp
    - grep -nE 'getInstance|registerOnsessionClosedCallback|setPort|bool start|bool stop' src/media/rtsp/RtspServer.h
    - cat src/app/workmode/CMakeLists.txt
    - grep -nE 'add_library|target_link_libraries' src/app/workmode/CMakeLists.txt
    - find build_sim -name flags.make -path '*workmode*' -exec cat {} \;
    - grep -nE 'Lift #3|Option A|Option B|defer|T12' artifacts/T11-planner-full.md
  evidence_ref: artifacts/T12-planner-full.md
state_delta:
  set_task_status: {}
artifact_path: artifacts/T12-planner-full.md
---

# T12 Planner — Phase B-3: full CMD_RTSP_SERVER lift into app_workmode (DI)

## 0. Evidence baseline (read from the worktree)

All line numbers verified against the working tree at branch `feature/new-workmode`.

### 0.1 CMD_RTSP_SERVER block — the code to lift
`src/app/main_app.cpp:1333-1353` (the `if (command & CMD_RTSP_SERVER)` scope):

```cpp
if (command & CMD_RTSP_SERVER) {
    uint16_t rtsp_port = getConfiguredPort(config, INI_SECTION_MDNS, INI_KEY_MDNS_RTSP_PORT, DEFAULT_RTSP_PORT);
    rtsp_singleton_used = true;
    RtspServer::getInstance()->registerOnsessionClosedCallback([]() {
        Logger::log(LogLevel::INFO, "RTSP session closed, waiting for new connection...");
    });
    RtspServer::getInstance()->setPort(static_cast<int>(rtsp_port));
    if (!RtspServer::getInstance()->start()) {
        Logger::log(LogLevel::ERROR, "Failed to start RTSP server");
        goto main_exit;
    }
    /* RTSP 服务器持续运行，等待退出信号 */
    while (!already_in_exit_flow) {
        (void)waitForSignalOrTimeout(1000);
    }
    RtspServer::getInstance()->stop();
}
```

### 0.2 Signal machinery it depends on (stays in main_app — DI does not move it)
- `already_in_exit_flow` — `static bool` at `main_app.cpp:521`.
- `g_signal_pipe` — `static int[2]` at `:536`.
- `waitForSignalOrTimeout(int)` — `static` at `:559`. Polls the self-pipe; on signal sets `already_in_exit_flow` + calls `performCleanup`.
- `performCleanup(int)` — `static`, forward-decl `:539`, defined `:588`.
- `rtsp_singleton_used` — `static bool` at `:529`. Read at `main_exit` (`:1521`) to gate `RtspServer::getInstance()->shutdown()` (process-level HAL teardown).
- `getConfiguredPort(...)` — `static` at `:153`. Generic; stays caller-side.

### 0.3 RtspServer API (`src/media/rtsp/RtspServer.h`)
- `:25`  `static std::shared_ptr<RtspServer> getInstance();`  — returns `shared_ptr`, so `->` deref works.
- `:30`  `static void registerOnsessionClosedCallback(std::function<void(void)> callback);`  — **static**; original calls it via `getInstance()->` (allowed; resolves to static). Helper will mirror original: `RtspServer::getInstance()->registerOnsessionClosedCallback(...)`.
- `:31`  `void setPort(int port);`
- `:32`  `bool start();`
- `:33`  `bool stop();`

### 0.4 app_workmode build (`src/app/workmode/CMakeLists.txt`)
- `file(GLOB SOURCE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")` — new TU auto-picked.
- `add_library(app_workmode SHARED ${SOURCE_FILES})`.
- `target_link_libraries(app_workmode PUBLIC mcu gpio logger)` — does NOT currently link `media_rtsp`. This is the one real link gap.
- `include_directories(...)` lists `${CMAKE_CURRENT_SOURCE_DIR}/../logger` etc. (these resolve under `src/app/workmode` → `src/app/logger`, which does NOT exist as a standalone dir).

### 0.5 DECISIVE include-path evidence (`build_sim/.../app_workmode.dir/flags.make`)
The compiled `app_workmode` TU already receives the full directory-scope include set from `src/app/CMakeLists.txt`'s `include_directories()`. The captured `CXX_INCLUDES` for `app_workmode` contains BOTH:
- `-I.../src/app/../logger`  → resolves to `src/logger` (Logger.h found)
- `-I.../src/app/../media/rtsp` → resolves to `src/media/rtsp` (RtspServer.h found)

Conclusion: **`RtspWorkMode.cpp` will resolve `#include "RtspServer.h"` and `#include "Logger.h"` with ZERO include-directories edits** — directory-scope inheritance already covers both. The only CMake edit that is strictly required is adding `media_rtsp` to `target_link_libraries` (linker symbol resolution for the `RtspServer` member functions). An explicit `include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../media/rtsp)` is optional belt-and-suspenders (harmless, redundant) — recommended to ADD anyway for defense against future CMake-scope changes (see Risks §6).

### 0.6 Prior-task state (T9/T11)
- `artifacts/T11-planner-full.md` §1 "Lift #3" (`:81`, `:298`) analyzed two options:
  - **Option A** (`:323`, IN SCOPE for T11): lift only register+setPort+start into a `bool` helper; leave run-loop + stop verbatim in caller.
  - **Option B** (`:367`, DEFERRED to T12): move the whole sequence including run-loop; flagged HIGH risk IF done by promoting the signal machinery to a shared header.
- **T12 = Option B done via dependency injection** — the full lift, but instead of promoting the signal machinery (the HIGH-risk path), the run-loop's two dependencies on main_app-private statics are injected as callbacks. This is the PM steer; it converts a HIGH-risk promotion into a LOW-risk behavior-preserving refactor.
- **T11 did NOT implement Option A** — `grep` for `startRtspServer`/`runRtspServerUntilSignal`/`app_workmode` in `main_app.cpp` and `workmode/*.{h,cpp}` returns nothing. The CMD_RTSP_SERVER block at `:1333-1353` is still the original inline form. Therefore T12 lifts the verbatim original directly; there is no prior partial lift to reconcile.

## 1. Goals / Non-goals

### Goals
1. Lift the full CMD_RTSP_SERVER sequence (register → setPort → start → run-loop → stop) out of `main_app.cpp` into a new `app_workmode::runRtspServerUntilSignal(...)` helper in `src/app/workmode/RtspWorkMode.{h,cpp}`.
2. Achieve the lift via dependency injection: the helper takes `rtsp_port` + two caller-injected callbacks (`keepRunning()`, `waitForSignal(int)`) so the TU-private signal machinery never leaves `main_app.cpp`.
3. Behavior-identical to the original `:1333-1353` block (refactor, not verbatim move — see §4).
4. Dual-platform: T32 uClibc + x86 sim. Zero `std::to_string`/`stoi`.

### Non-goals
1. Do NOT promote/move `already_in_exit_flow`, `waitForSignalOrTimeout`, `g_signal_pipe`, `performCleanup`, or any signal machinery out of `main_app.cpp`. DI keeps them TU-private.
2. Do NOT touch the CMD_MOBILE RTSP path (`main_app.cpp:1294-1330`) — its RTSP is fused with mDNS/HTTP/TCP teardown.
3. Do NOT move `getConfiguredPort` or `rtsp_singleton_used` — both stay caller-side; the caller keeps setting `rtsp_singleton_used = true;` and keeps resolving the port.
4. Do NOT touch `src/hal/**`.
5. Do NOT touch the existing `WorkMode.{h,cpp}` enum/reader.
6. No behavior change, no new features, no log-string edits.

## 2. Impacted files

| File | Change type | Scope |
|------|-------------|-------|
| `src/app/workmode/RtspWorkMode.h` | NEW | Declare `namespace app_workmode { bool runRtspServerUntilSignal(uint16_t, std::function<bool()>, std::function<void(int)>); }` |
| `src/app/workmode/RtspWorkMode.cpp` | NEW | Implement the sequence using injected callbacks. |
| `src/app/workmode/CMakeLists.txt` | EDIT | (a) Add `media_rtsp` to `target_link_libraries(app_workmode PUBLIC ...)` — REQUIRED. (b) Add `include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../media/rtsp)` — optional/redundant (already satisfied by directory scope, see §0.5) but recommended for robustness. |
| `src/app/main_app.cpp` | EDIT | Rewrite CMD_RTSP_SERVER block (`:1333-1353`) to call the helper with two DI lambdas; add `#include "RtspWorkMode.h"`. |

No other files touched. `htc_main_app` already links `media_rtsp` (`src/app/CMakeLists.txt:111`) so the transitive add via `app_workmode` is harmless but necessary for `app_workmode` itself to link.

## 3. Plan (concrete, code-level)

### 3.1 `src/app/workmode/RtspWorkMode.h` (NEW)
```cpp
#pragma once

#include <cstdint>
#include <functional>

// Lifted RTSP server start/run/stop sequence for the CMD_RTSP_SERVER work mode.
//
// main_app's signal/exit-flow machinery (already_in_exit_flow,
// waitForSignalOrTimeout, g_signal_pipe, performCleanup) is TU-private and
// intentionally NOT moved. The run-loop's two dependencies on that machinery
// are injected by the caller so the helper stays decoupled.
namespace app_workmode {

// Register the session-closed callback, set the port, and start the RTSP
// singleton, then block on the caller-supplied wait until keepRunning()
// returns false, then stop().
//
//   rtsp_port       — caller-resolved (main_app::getConfiguredPort).
//   keepRunning     — returns true while the run-loop should continue
//                     (caller wraps its private !already_in_exit_flow).
//   waitForSignal   — blocks up to the given ms for a signal
//                     (caller wraps its private waitForSignalOrTimeout).
//
// Returns false if start() fails (caller does `goto main_exit`), true after a
// clean run-loop exit and stop().
//
// NOTE: the helper does NOT set rtsp_singleton_used — that gate (read at
// main_exit to drive RtspServer::getInstance()->shutdown()) stays caller-side.
bool runRtspServerUntilSignal(uint16_t rtsp_port,
                              std::function<bool()> keepRunning,
                              std::function<void(int)> waitForSignal);

}  // namespace app_workmode
```

### 3.2 `src/app/workmode/RtspWorkMode.cpp` (NEW)
Includes: `RtspWorkMode.h`, `RtspServer.h`, `Logger.h`, `<functional>` (transitively from header, but explicit for clarity). All three headers resolve via directory-scope includes (see §0.5).

```cpp
#include "RtspWorkMode.h"
#include "RtspServer.h"
#include "Logger.h"

namespace app_workmode {

bool runRtspServerUntilSignal(uint16_t rtsp_port,
                              std::function<bool()> keepRunning,
                              std::function<void(int)> waitForSignal) {
    RtspServer::getInstance()->registerOnsessionClosedCallback([]() {
        Logger::log(LogLevel::INFO, "RTSP session closed, waiting for new connection...");
    });
    RtspServer::getInstance()->setPort(static_cast<int>(rtsp_port));
    if (!RtspServer::getInstance()->start()) {
        Logger::log(LogLevel::ERROR, "Failed to start RTSP server");
        return false;  // caller does goto main_exit
    }
    /* RTSP 服务器持续运行，等待退出信号 */
    while (keepRunning()) {
        waitForSignal(1000);
    }
    RtspServer::getInstance()->stop();
    return true;
}

}  // namespace app_workmode
```

Mapping to the original block (`main_app.cpp:1333-1353`):
| Original line | In helper | Notes |
|---|---|---|
| `rtsp_port = getConfiguredPort(...)` | parameter | caller-side (NON-goal #3) |
| `rtsp_singleton_used = true;` | NOT in helper | caller-side (NON-goal #3) |
| `registerOnsessionClosedCallback(...)` | first stmt | identical lambda + log string |
| `setPort(...)` | second stmt | identical |
| `if (!start()) { log; goto main_exit; }` | `if (!start()) { log; return false; }` | caller turns `false` into `goto main_exit` |
| `while (!already_in_exit_flow) { (void)waitForSignalOrTimeout(1000); }` | `while (keepRunning()) { waitForSignal(1000); }` | DI; `(void)` dropped (void-returning cb) |
| `RtspServer::getInstance()->stop();` | last stmt | identical |

### 3.3 `src/app/workmode/CMakeLists.txt` (EDIT)
```cmake
# Find source files in the current directory
file(GLOB SOURCE_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
 )

# Include current directory headers
include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../common
        ${CMAKE_CURRENT_SOURCE_DIR}/../logger
        ${CMAKE_CURRENT_SOURCE_DIR}/../../media/rtsp   # NEW — RtspServer.h (also satisfied by app dir scope; added for robustness)
        ${CMAKE_CURRENT_SOURCE_DIR}/../hardware/gpio
        ${CMAKE_CURRENT_SOURCE_DIR}/../hardware/mcu
)

# Create library
add_library(app_workmode SHARED ${SOURCE_FILES})

target_link_libraries(app_workmode PUBLIC mcu gpio logger media_rtsp)   # NEW: media_rtsp

# Set the output directory for the library
set_target_properties(app_workmode PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
```
Two edits:
1. `target_link_libraries`: add `media_rtsp` — **REQUIRED** (linker symbols for `RtspServer` member fns). `htc_main_app` already links it, so no new transitive dep at the executable level.
2. `include_directories`: add `${CMAKE_CURRENT_SOURCE_DIR}/../../media/rtsp`. From `src/app/workmode`, `../../media/rtsp` = `src/media/rtsp`. **Redundant** with the directory-scope include (§0.5 proves `app_workmode.dir/flags.make` already has `-I .../src/app/../media/rtsp`) but harmless and self-documenting. Recommended to keep it so the TU is robust if workmode is ever built outside the `src/app` scope.

### 3.4 `src/app/main_app.cpp` (EDIT)
Add include near the existing workmode/rtsp includes (after `#include "RtspServer.h"` at `:31`):
```cpp
#include "RtspWorkMode.h"
```
Rewrite the CMD_RTSP_SERVER block (`:1333-1353`) to:
```cpp
if (command & CMD_RTSP_SERVER) {
    uint16_t rtsp_port = getConfiguredPort(config, INI_SECTION_MDNS, INI_KEY_MDNS_RTSP_PORT, DEFAULT_RTSP_PORT);
    rtsp_singleton_used = true;
    if (!app_workmode::runRtspServerUntilSignal(
            rtsp_port,
            []{ return !already_in_exit_flow; },
            [](int ms){ (void)waitForSignalOrTimeout(ms); })) {
        goto main_exit;
    }
}
```
Caller retains: port resolution, `rtsp_singleton_used = true;`, and `goto main_exit`. The two lambdas capture nothing — they call file-scope statics directly (`already_in_exit_flow`, `waitForSignalOrTimeout`), so no capture is needed and the `std::function` wrappers are zero-state.

`#include "RtspWorkMode.h"` resolves via the `src/app/workmode` directory-scope include already present in `src/app/CMakeLists.txt:32` (`${CMAKE_CURRENT_SOURCE_DIR}/workmode`).

## 4. Behavior-equivalence argument (refactor, not verbatim move)

T9/T11's byte-identical gate does NOT strictly apply here because the run-loop body is restructured behind `std::function`. This is a REFACTOR; equivalence is argued structurally:

(a) **Helper body is the original sequence verbatim, modulo the injected callbacks.** Every statement in `RtspWorkMode.cpp` has a 1:1 counterpart in the original `:1333-1353` (see the mapping table in §3.2). The session-closed log string is byte-identical (`"RTSP session closed, waiting for new connection..."`), the `ERROR` log on start failure is byte-identical (`"Failed to start RTSP server"`), the port cast (`static_cast<int>(rtsp_port)`) and the `1000` ms wait timeout are identical.

(b) **The two injected lambdas wrap the exact existing statics.**
- `keepRunning()` = `[]{ return !already_in_exit_flow; }` — identical predicate to the original `while (!already_in_exit_flow)`.
- `waitForSignal(1000)` = `[](int ms){ (void)waitForSignalOrTimeout(ms); }` — calls the same function with the same `1000` argument; the original `(void)` cast (discarding the returned signal number) is preserved by making the callback `void`-returning.

(c) **Ordering / log / control-flow unchanged.**
- register → setPort → start → run-loop → stop: same order.
- On `start()` failure: log ERROR then bail to `goto main_exit` (helper `return false` → caller `goto main_exit`). `stop()` is NOT called on the failure path in either version (identical).
- On clean exit (signal flips `already_in_exit_flow`): `stop()` runs once, then the block falls through exactly as before.

(d) **`std::function` does not alter loop semantics.** `std::function` is a type-erased callable wrapper; invoking it is a single indirect call per iteration. It does not re-order, cache, or suppress the loop body. Both lambdas are stateless (no captures), so there is no lifetime/aliasing concern. The wait timeout (`1000`) is passed by value. Behavior of `poll(2)` inside `waitForSignalOrTimeout`, the self-pipe drain, and `performCleanup` on signal are all unchanged because they are reached through the exact same function.

(e) **Side effects outside the block are unchanged.** `rtsp_singleton_used` is set by the caller both before and after this change (the helper does not touch it), so the `main_exit` gate at `:1521` (`if (rtsp_singleton_used) RtspServer::getInstance()->shutdown();`) behaves identically. The singleton instance is the same process-global (`RtspServer::getInstance()`), so `start()/stop()/shutdown()` act on the same object in the same order.

Net: the only structural change is that the run-loop and `start()` live behind injected callbacks + a `bool` return. Observable behavior (log output, RTSP lifecycle, signal-driven exit, process teardown) is identical.

## 5. Verification

### 5.1 Build (dual-platform)
```bash
# PC sim
cmake -DBUILD_FOR_SIMULATION=ON -B build_sim -S . && cmake --build build_sim -j$(nproc)
# T32 cross
cmake -DCMAKE_TOOLCHAIN_FILE=toolchain.cmake -B build -S . && cmake --build build -j$(nproc)
```
Both must compile and link with zero new warnings. Specifically watch for: unresolved `RtspServer` symbols when linking `libapp_workmode.so` (would mean `media_rtsp` not added) — this is the #1 integration failure mode.

### 5.2 Reviewer semantic-equivalence check
Reviewer confirms §4 (a)–(e) by diffing the original `:1333-1353` against the helper + caller rewrite, verifying byte-identical log strings, identical ordering, and that the failure path does not call `stop()` while the success path calls `stop()` exactly once.

### 5.3 `std::function` sanity (optional reasoning check)
Confirm both DI lambdas have empty capture lists (`[]{ ... }` and `[](int){ ... }`), so the `std::function` instances hold no state and cannot dangle. No heap allocation is observable from the loop.

### 5.4 Runtime (optional, device/sim)
Boot `htc_main_app` into the RTSP work mode, connect an RTSP client, observe the "RTSP session closed" log on disconnect, send SIGINT and confirm clean `stop()` + `main_exit` shutdown path (same as pre-T12).

## 6. Risks

| # | Risk | Likelihood | Impact | Mitigation |
|---|------|-----------|--------|------------|
| R1 | `libapp_workmode.so` fails to link with unresolved `RtspServer::*` symbols (forgot `media_rtsp` in `target_link_libraries`). | Med | Build break | CMake edit §3.3 #1 is REQUIRED; §5.1 build catches it immediately. |
| R2 | `RtspServer.h` not found in `RtspWorkMode.cpp`. | Very low | Build break | §0.5 proves directory-scope include already resolves it; §3.3 #2 adds an explicit redundant include for robustness. |
| R3 | `std::function` misbehaves on T32 uClibc. | Very low | Runtime | `std::function` is std lib (NOT the banned `std::to_string`/`stoi` per project memory). Already used in `RtspServer.h:30` (`std::function<void(void)>`), so it is proven on this toolchain. |
| R4 | Helper accidentally sets `rtsp_singleton_used`, breaking the `main_exit` shutdown gate. | Low | Process teardown regression | Header comment + §3.2 explicitly exclude it; caller keeps the assignment. Reviewer checks. |
| R5 | Future CMake-scope change removes the directory-level include that currently satisfies `RtspServer.h`/`Logger.h` for `app_workmode`. | Low | Latent build break | §3.3 #2 adds an explicit `${CMAKE_CURRENT_SOURCE_DIR}/../../media/rtsp` so the TU is self-sufficient regardless of parent scope. |
| R6 | DI lambdas accidentally capture and dangle across the run-loop. | Very low | Runtime UAF | Both lambdas written capture-less (`[]`); no capture needed since they call file-scope statics. |
| R7 | Scope creep into CMD_MOBILE RTSP path. | Low | Regression in unrelated mode | NON-goal #2; reviewer verifies only `:1333-1353` changed, `:1294-1330` untouched. |

No new transitive dependencies are introduced beyond `media_rtsp` (already consumed by `htc_main_app`). No `src/hal/**` changes.

### Rollback
Pure additive/replace in 4 files; revert is a single `git checkout -- src/app/main_app.cpp src/app/workmode/CMakeLists.txt && rm src/app/workmode/RtspWorkMode.{h,cpp}`. No schema, config, or persistent-state changes.

## 7. Acceptance criteria
1. `build_sim` and `build` both compile + link `libapp_workmode.so` and `htc_main_app` cleanly.
2. `src/app/workmode/RtspWorkMode.{h,cpp}` exist; `runRtspServerUntilSignal` declared under `namespace app_workmode` with the exact `(uint16_t, std::function<bool()>, std::function<void(int)>)` signature.
3. `src/app/workmode/CMakeLists.txt` links `media_rtsp` into `app_workmode`.
4. `main_app.cpp` CMD_RTSP_SERVER block calls the helper with the two capture-less DI lambdas; `rtsp_singleton_used = true;`, `getConfiguredPort`, and `goto main_exit` remain caller-side.
5. CMD_MOBILE block (`:1294-1330`) is byte-unchanged.
6. No file under `src/hal/**` modified.
7. No `std::to_string`/`stoi` introduced anywhere in the new code.

## 8. Test strategy
- **Build gate (primary):** §5.1 — dual-platform clean build is the main acceptance signal for a behavior-preserving refactor.
- **Semantic-equivalence review:** §5.2 — reviewer carries the equivalence argument; this is a refactor, not new behavior, so no new unit tests are strictly required.
- **No golden harness:** T11/T9 used byte-identical gates; T12 is a refactor (not verbatim move) so the gate is reviewer semantic equivalence (§4), not a byte diff.
- **Optional runtime smoke (device or sim):** §5.4 — RTSP connect/disconnect + SIGINT shutdown, confirming log strings and shutdown path match pre-T12.
- **Regression guard:** CMD_MOBILE path untouched (acceptance #5); reviewer greps to confirm no stray edits.

## 9. Planner opinion on DI vs promoting signal machinery

DI is the RIGHT approach for T12. Promoting `already_in_exit_flow`/`waitForSignalOrTimeout`/`g_signal_pipe`/`performCleanup` into a shared header — which is what Option B as originally framed ("requires first abstracting the signal machinery") would demand — is exactly the HIGH-risk path T11 flagged (`artifacts/T11-planner-full.md:400`, "Risk (Option B / T12): HIGH"). Reasons DI wins:
1. **No signal-machinery promotion.** The self-pipe + `poll` + `performCleanup` interplay is subtle (signal-handler-safe `write(2)`, non-blocking pipe, main-thread cleanup) and intentionally TU-private. DI leaves it 100% in place.
2. **Minimal coupling surface.** The helper depends on exactly two abstract behaviors (`keepRunning()`, `waitForSignal(int)`), not on C functions or globals. The caller adapts its existing statics to that interface with two trivial capture-less lambdas.
3. **Testability/reuse.** The helper can now be exercised with synthetic callbacks in a future harness without spinning up the real signal pipe — something impossible if it directly referenced the statics.
4. **`std::function` is already proven on this toolchain** (`RtspServer.h:30` uses it), neutralizing the only real DI-specific concern.

No reservations: proceed with DI as specified.
