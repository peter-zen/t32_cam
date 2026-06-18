# T12 Reviewer Evidence

Task: T12 (Phase B-3) — lift CMD_RTSP_SERVER start/run/stop into `app_workmode::runRtspServerUntilSignal`.
Node: reviewer. Branch: `feature/new-workmode`.

All citations are against the worktree at
`/home/zengping/project/huntcam/code/t32_cam/.claude/worktrees/new-workmode`.

## 1. Semantic equivalence (original vs lifted)

Original CMD_RTSP_SERVER block extracted via `git show HEAD:src/app/main_app.cpp`
(HEAD lines 1333–1353):

```
1333: if (command & CMD_RTSP_SERVER) {
1334:     //auto wifi_ssid = config->get(INI_SECTION_DEVICE, INI_KEY_CSSID, "");
1335:     //auto wifi_pwd  = config->get(INI_SECTION_DEVICE, INI_KEY_CPWD, "");
1336:     //Misc::connectWifi(wifi_ssid, wifi_pwd);
1337:     //Misc::startDHCP();
1338:     uint16_t rtsp_port = getConfiguredPort(config, INI_SECTION_MDNS, INI_KEY_MDNS_RTSP_PORT, DEFAULT_RTSP_PORT);
1339:     rtsp_singleton_used = true;
1340:     RtspServer::getInstance()->registerOnsessionClosedCallback([]() {
1341:         Logger::log(LogLevel::INFO, "RTSP session closed, waiting for new connection...");
1342:     });
1343:     RtspServer::getInstance()->setPort(static_cast<int>(rtsp_port));
1344:     if (!RtspServer::getInstance()->start()) {
1345:         Logger::log(LogLevel::ERROR, "Failed to start RTSP server");
1346:         goto main_exit;
1347:     }
1348:     /* RTSP 服务器持续运行，等待退出信号 */
1349:     while (!already_in_exit_flow) {
1350:         (void)waitForSignalOrTimeout(1000);
1351:     }
1352:     RtspServer::getInstance()->stop();
1353: }
```

New caller (`src/app/main_app.cpp` current, 1334–1341) + helper
(`src/app/workmode/RtspWorkMode.cpp` 9–26):

```
caller:
1334: if (command & CMD_RTSP_SERVER) {
1335:     uint16_t rtsp_port = getConfiguredPort(config, INI_SECTION_MDNS, INI_KEY_MDNS_RTSP_PORT, DEFAULT_RTSP_PORT);
1336:     rtsp_singleton_used = true;
1337:     if (!app_workmode::runRtspServerUntilSignal(rtsp_port,
1338:             []{ return !already_in_exit_flow; },
1339:             [](int ms){ (void)waitForSignalOrTimeout(ms); })) {
1340:         goto main_exit;
1341:     }

helper:
 9: bool runRtspServerUntilSignal(uint16_t rtsp_port,
10:                               std::function<bool()> keepRunning,
11:                               std::function<void(int)> waitForSignal) {
12:     media::RtspServer::getInstance()->registerOnsessionClosedCallback([]() {
13:         Logger::log(LogLevel::INFO, "RTSP session closed, waiting for new connection...");
14:     });
15:     media::RtspServer::getInstance()->setPort(static_cast<int>(rtsp_port));
16:     if (!media::RtspServer::getInstance()->start()) {
17:         Logger::log(LogLevel::ERROR, "Failed to start RTSP server");
18:         return false;  // caller does goto main_exit
19:     }
20:     /* RTSP 服务器持续运行，等待退出信号 */
21:     while (keepRunning()) {
22:         waitForSignal(1000);
23:     }
24:     media::RtspServer::getInstance()->stop();
25:     return true;
26: }
```

Equivalence matrix:

| step | original | new | match |
|---|---|---|---|
| port resolution | caller `getConfiguredPort` | caller line 1335 | yes |
| `rtsp_singleton_used = true` | caller line 1339 | caller line 1336 (set BEFORE helper) | yes |
| registerOnsessionClosedCallback | line 1340 | helper line 12 | yes |
| INFO log bytes | `"RTSP session closed, waiting for new connection..."` | identical | yes |
| setPort | line 1343 `static_cast<int>` | helper line 15 `static_cast<int>` | yes |
| start-fail → ERROR log + NO stop() | `goto main_exit` (no stop) | `return false` → caller `goto main_exit` (no stop) | yes |
| run-loop comment | line 1348 | helper line 20 (same UTF-8) | yes |
| run-loop body | `while (!already_in_exit_flow) { (void)waitForSignalOrTimeout(1000); }` | `while (keepRunning()) { waitForSignal(1000); }` w/ injected lambdas | yes |
| stop() once on clean exit | line 1352 | helper line 24 | yes |

`media::RtspServer` == main_app's `RtspServer`: `src/app/main_app.cpp:71` has
`using namespace media;`, so the bare `RtspServer` resolves to `media::RtspServer`,
the same symbol the helper qualifies explicitly. `media::RtspServer` API used
(`registerOnsessionClosedCallback`, `setPort`, `start`, `stop`, `getInstance`)
all declared in `src/media/rtsp/RtspServer.h` (lines 25, 30, 31, 32, and stop on
the singleton). `getInstance()` returns `std::shared_ptr<RtspServer>`; `->` usage
is correct.

## 2. The 4 removed "dead-comment" lines

Removed HEAD lines 1334–1337:
```
//auto wifi_ssid = config->get(INI_SECTION_DEVICE, INI_KEY_CSSID, "");
//auto wifi_pwd = config->get(INI_SECTION_DEVICE, INI_KEY_CPWD, "");
//Misc::connectWifi(wifi_ssid, wifi_pwd);
//Misc::startDHCP();
```
All four begin with `//` — commented-out, non-executable. No executable
statement was dropped. Verdict: clean removal.

## 3. DI soundness

Injected capture-less lambdas (caller lines 1338–1339):
- `[]{ return !already_in_exit_flow; }` — no captures; reads file-scope
  `static bool already_in_exit_flow` (main_app.cpp:522).
- `[](int ms){ (void)waitForSignalOrTimeout(ms); }` — no captures; calls
  file-scope `static int waitForSignalOrTimeout(int)` (main_app.cpp:560).

Substituted into the helper loop `while (keepRunning()) { waitForSignal(1000); }`
this reproduces EXACTLY `while (!already_in_exit_flow) { (void)waitForSignalOrTimeout(1000); }`.
`std::function` is a type-erased callable; the loop invokes them with identical
call semantics, no behavioral change. Both lambdas refer only to TU-private
statics, so no capture is needed or possible-to-omit incorrectly.

## 4. `rtsp_singleton_used` gate

- Set to `true` in caller at line 1336 BEFORE the helper call.
- Helper does NOT touch `rtsp_singleton_used` (header NOTE at RtspWorkMode.h:27–28
  documents this; confirmed by grep — no occurrence in RtspWorkMode.cpp).
- Still read at `main_exit` (current main_app.cpp:1510–1511):
  `if (rtsp_singleton_used) { RtspServer::getInstance()->shutdown(); }`.
- Helper calls `stop()` (session-level), NOT `shutdown()` (process-level HAL
  teardown). `shutdown()` remains caller-driven by `rtsp_singleton_used` at
  main_exit — unchanged. Gate intact.

## 5. `app_workmode` growth

`git diff HEAD -- src/app/workmode/CMakeLists.txt`:
- added include dir `${CMAKE_CURRENT_SOURCE_DIR}/../../media/rtsp`
- `target_link_libraries(app_workmode PUBLIC mcu gpio logger media_rtsp)`
  (media_rtsp added PUBLIC).
- `RtspWorkMode.cpp` picked up by existing `file(GLOB *.cpp)`.
- `WorkMode.{h,cpp}` untouched (`git diff HEAD --stat` empty for them).

`readelf -d` NEEDED `libmedia_rtsp.so` on both platforms:
- T32 `build/lib/libapp_workmode.so`: `NEEDED Shared library: [libmedia_rtsp.so]`
- sim `build_sim/lib/libapp_workmode.so`: `NEEDED Shared library: [libmedia_rtsp.so]`

`htc_main_app` NEEDED on both platforms includes `libapp_workmode.so` AND
`libmedia_rtsp.so`.

## 6. Untouched confirmations

- CMD_MOBILE mobile-rtsp region: byte-identical HEAD vs current
  (md5 `b3cd545f853a13707173d930a930326b` for the `if (mobile_rtsp_enabled) {…}`
  block; only a +1 line shift from the added include above it).
- Signal machinery still `static` in main_app: `already_in_exit_flow` (522),
  `rtsp_singleton_used` (530), `g_signal_pipe` (537), `performCleanup` fwd (540),
  `waitForSignalOrTimeout` (560), `performCleanup` def (589).
- `src/hal/**` diff empty.
- Zero `std::to_string`/`stoi` in changed/new files.

## 7. Build verification

- T32 cross build (`cmake --build build -j$(nproc)`): 100%, all targets built
  incl. `htc_main_app`, `htc_media_app`, `snap_test`. Only pre-existing warnings
  in `snap_test.cpp`/`media_app.cpp` (unrelated: `SnapImgSize` unused,
  `stoi_custom` unused).
- PC sim build (`cmake --build build_sim -j$(nproc)`): `htc_main_app` and
  `libapp_workmode.so` build clean (100% when targeting `htc_main_app`).
  NOTE: `htc_media_app` link FAILS on sim with pre-existing `DayNightSwitch`
  undefined references from `libmedia_rtsp.so` — this is NOT caused by T12.

## 8. Pre-existing sim build issue (NOT a T12 regression)

`htc_media_app` sim link failure:
```
/usr/bin/ld: ../../lib/libmedia_rtsp.so: undefined reference to `DayNightSwitch::controlIRCut(DayNightState)'
... (10 DayNightSwitch symbols)
```
Root cause (independent of T12):
- `src/media/rtsp/RtspServer.cpp:17` `#include "DayNightSwitch.h"` and
  `:589 auto daynight_controller = DayNightSwitch::getInstance();` (NOT gated
  by `BUILD_FOR_SIMULATION`).
- `src/media/rtsp/CMakeLists.txt:16` adds the daynight include dir but line 36
  `target_link_libraries(media_rtsp PRIVATE media_base media_fifo audio_recorder smolrtsp smolrtsp-libevent event atomic)`
  does NOT link the `daynight` target (which exists at
  `src/hardware/daynight/CMakeLists.txt:20`).
- T32 build passes because the symbols resolve via the T32 link path; sim
  `htc_media_app` needs them statically and fails.
- T12 changed only `app_workmode/CMakeLists.txt` (adds media_rtsp as a DEPENDENT,
  not a provider) and `main_app.cpp`. It neither introduced the DayNightSwitch
  reference nor removed the daynight link. `libapp_workmode.so` itself links
  cleanly on sim.

Follow-up (out of T12 scope, recommend a separate task): add `daynight` to
`media_rtsp`'s `target_link_libraries` or gate the `RtspServer.cpp:589` call
behind `BUILD_FOR_SIMULATION`. This is a latent sim-only breakage that predates
T12 (traceable to `a9770cb fix(rtsp): teardown IMP HAL on exit before poweroff`
which added the DayNightSwitch use to RtspServer.cpp).
