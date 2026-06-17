# T12 — tester evidence

Task: RTSP DI lift (behavior-preserving refactor) — `CMD_RTSP_SERVER` start/run/stop sequence
lifted into `app_workmode::runRtspServerUntilSignal(port, keepRunning, waitForSignal)` in
`src/app/workmode/RtspWorkMode.{h,cpp}`. Signal machinery stays in main_app.

Worktree: `/home/zengping/project/huntcam/code/t32_cam/.claude/worktrees/new-workmode`
Branch: `feature/new-workmode`
Baseline: `git show HEAD:src/app/main_app.cpp` → `/tmp/main_app_orig.cpp`

## Diff summary (git diff --stat HEAD)

```
 orchestration-state.yaml        |  8 +++++++-
 src/app/main_app.cpp            | 19 ++++---------------
 src/app/workmode/CMakeLists.txt |  3 ++-
 3 files changed, 13 insertions(+), 17 deletions(-)
```
New untracked: `src/app/workmode/RtspWorkMode.{h,cpp}`.

## Gate 1 — Both platforms build+link clean

SIM:
```
$ cmake --build build_sim -j$(nproc) --target htc_main_app
[ 92%] Built target media_rtsp
[ 93%] Built target app_workmode
[100%] Built target htc_main_app
SIM_BUILD_EXIT=0
```

T32 (cross, uClibc):
```
$ cmake --build build -j$(nproc) --target htc_main_app
[ 92%] Built target media_rtsp
[ 92%] Built target app_workmode
[100%] Built target htc_main_app
T32_BUILD_EXIT=0
```

Linkage — `libapp_workmode.so` NEEDED `libmedia_rtsp.so` on BOTH:
```
$ readelf -d build/lib/libapp_workmode.so | grep media_rtsp
 0x00000001 (NEEDED)                     Shared library: [libmedia_rtsp.so]
$ readelf -d build_sim/lib/libapp_workmode.so | grep media_rtsp
 0x0000000000000001 (NEEDED)             Shared library: [libmedia_rtsp.so]
```
CMakeLists.txt change (verified):
```diff
+        ${CMAKE_CURRENT_SOURCE_DIR}/../../media/rtsp
-target_link_libraries(app_workmode PUBLIC mcu gpio logger)
+target_link_libraries(app_workmode PUBLIC mcu gpio logger media_rtsp)
```

## Gate 2 — Semantic equivalence (PRIMARY)

ORIGINAL block (`git show HEAD:src/app/main_app.cpp` @1334-1352):
```cpp
if (command & CMD_RTSP_SERVER) {
    //auto wifi_ssid = ...   <- 4 lines of DEAD commented-out code
    //auto wifi_pwd = ...
    //Misc::connectWifi(wifi_ssid, wifi_pwd);
    //Misc::startDHCP();
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

NEW helper body (`src/app/workmode/RtspWorkMode.cpp`):
```cpp
bool runRtspServerUntilSignal(uint16_t rtsp_port, ..., waitForSignal) {
    media::RtspServer::getInstance()->registerOnsessionClosedCallback([]() {
        Logger::log(LogLevel::INFO, "RTSP session closed, waiting for new connection...");
    });
    media::RtspServer::getInstance()->setPort(static_cast<int>(rtsp_port));
    if (!media::RtspServer::getInstance()->start()) {
        Logger::log(LogLevel::ERROR, "Failed to start RTSP server");
        return false;  // caller does goto main_exit
    }
    /* RTSP 服务器持续运行，等待退出信号 */
    while (keepRunning()) {
        waitForSignal(1000);
    }
    media::RtspServer::getInstance()->stop();
    return true;
}
```

Step-by-step equivalence:
| Step | ORIGINAL | NEW helper | Match |
|------|----------|-----------|-------|
| registerOnsessionClosedCallback lambda | yes | yes | OK |
| session-closed log string | "RTSP session closed, waiting for new connection..." | identical | OK |
| setPort(static_cast<int>(rtsp_port)) | yes | yes | OK |
| start(); fail → log "Failed to start RTSP server" | yes | yes | OK |
| NO stop() on start() failure | yes (`goto main_exit`) | yes (`return false`) | OK |
| run-loop `while(!already_in_exit_flow){(void)waitForSignalOrTimeout(1000);}` | yes | `while(keepRunning()){waitForSignal(1000);}` with `keepRunning=[]{return !already_in_exit_flow;}` and `waitForSignal=[](int ms){(void)waitForSignalOrTimeout(ms);}` | SEMANTICALLY IDENTICAL |
| stop() exactly once on success path | yes | yes | OK |
| Chinese run-loop comment | yes | preserved | OK |

Dead code removed (NOT behavior): 4 commented-out lines (`//auto wifi_ssid/pwd`, `//Misc::connectWifi/startDHCP`). No executable statement lost.

`RtspServer` symbol identity: helper qualifies `media::RtspServer`; main_app uses unqualified
`RtspServer` via `using namespace media;` (main_app.cpp:71). `src/media/rtsp/RtspServer.h:20`
declares `namespace media { class RtspServer { static std::shared_ptr<RtspServer> getInstance(); ... }; }`.
SAME symbol. Expected.

## Gate 3 — Caller wiring

```
$ grep -n 'runRtspServerUntilSignal' src/app/main_app.cpp
1337:        if (!app_workmode::runRtspServerUntilSignal(rtsp_port,
```
1 call. Caller block (main_app.cpp:1334-1342):
```cpp
if (command & CMD_RTSP_SERVER) {
    uint16_t rtsp_port = getConfiguredPort(config, INI_SECTION_MDNS, INI_KEY_MDNS_RTSP_PORT, DEFAULT_RTSP_PORT);
    rtsp_singleton_used = true;                                  // set BEFORE call, caller-side
    if (!app_workmode::runRtspServerUntilSignal(rtsp_port,
            []{ return !already_in_exit_flow; },                 // keepRunning (capture-less)
            [](int ms){ (void)waitForSignalOrTimeout(ms); })) {  // waitForSignal (capture-less)
        goto main_exit;                                          // on return false
    }
}
```
- lambdas are capture-less ✓
- `rtsp_singleton_used = true` BEFORE call, in caller ✓
- `getConfiguredPort(...)` resolves port in caller (not helper) ✓
- `goto main_exit` on `return false` ✓

## Gate 4 — Helper does NOT set rtsp_singleton_used

```
$ grep -n 'rtsp_singleton_used' src/app/workmode/RtspWorkMode.cpp
(empty; exit=1)
```
Helper has zero references. Singleton gate stays caller-side (read at main_exit:1510 → `RtspServer::getInstance()->shutdown()`).

## Gate 5 — CMD_MOBILE untouched

CMD_MOBILE block (mDNS/HTTP/TCP teardown + its RTSP) byte-diffed:
```
$ sed -n '1211,1332p' /tmp/main_app_orig.cpp > /tmp/orig_mobile.txt      # 122 lines
$ sed -n '1212,1333p' .../main_app.cpp                > /tmp/new_mobile.txt
$ diff /tmp/orig_mobile.txt /tmp/new_mobile.txt
(no output)
CMD_MOBILE block BYTE-IDENTICAL (122 lines)
```
The only main_app.cpp changes are: +1 `#include "RtspWorkMode.h"`, and the CMD_RTSP_SERVER block replacement.

## Gate 6 — Signal machinery untouched

```
$ git diff HEAD -- src/app/main_app.cpp | grep -E '^[-+].*static'
-        RtspServer::getInstance()->setPort(static_cast<int>(rtsp_port));
```
The single `static` hit is the existing `static_cast<int>` in a REMOVED line — no `static` machinery line was changed. All five TU-private symbols still `static` at original locations:
```
522:static bool already_in_exit_flow = false;
537:static int g_signal_pipe[2] = {-1, -1};
540:static void performCleanup(int sig);  // forward decl
560:static int waitForSignalOrTimeout(int timeoutMs) {
589:static void performCleanup(int sig) {
```

## Gate 7 — Scope/hygiene

- `git diff --stat HEAD -- src/hal` → empty (HAL untouched) ✓
- `std::to_string`/`stoi` in RtspWorkMode.cpp → none (grep exit=1) ✓ (uclibc-safe)
- `src/app/workmode/WorkMode.{h,cpp}` → `git diff --stat HEAD` empty (untouched) ✓

## Result
All 7 gates PASS. Behavior-preserving lift confirmed; no executable statement lost (only dead
commented-out code removed), CMD_MOBILE byte-identical, signal machinery unchanged, media_rtsp
linked into libapp_workmode.so on both platforms.
