# T11 — Phase B-2 Plan: three behavior-preserving lifts out of `src/app/main_app.cpp`

Task T11, node=planner, flow=feature. Branch `feature/new-workmode`.
Scope: **verbatim-move-first** lifts (byte-identical bodies) into existing
SDK/capability libs, exactly mirroring the T9 (`manifest`) pattern. Dual-platform
build required (T32 uClibc: zero `std::to_string`/`stoi`; use existing
`to_string_custom`/`snprintf`). No logic changes. No `src/hal/**` touches.
No CMD_MOBILE touches. No singleton decoupling.

All facts below were verified by reading the repo (evidence commands in the
report card `verification.commands`). **Line numbers are current post-T9
(main_app.cpp = 1649 lines; HEAD baseline for the diff = 1894 lines, i.e.
the un-lifted file).**

---

## 0. Verified ground truth (evidence-backed)

### Lift #1 — NTP wait-loop (CMD_NTP, `main_app.cpp:1164-1209`)
Block reads `INI_KEY_NTP_IP`/`INI_KEY_NTP_PORT`, builds
`ntp_server_ip + ":" + to_string_custom(ntp_server_port)`, calls
`Misc::ntpSync(ntp_server)`, then loops `while (wait_time < MAX_WAIT_SECONDS)`
polling `localtime()` until `tm_year + YEAR_OFFSET > YEAR_MIN` (every 2s,
max 30s), then writes RTC `if (is_rtc_work_well) RTC::getInstance()->setTime(*nowtime)`.

Key facts:
- `Misc::ntpSync` already in `common_misc` (`src/common/misc/Misc.h:36`,
  impl `src/common/misc/Misc.cpp:494`). The lift adds a sibling helper
  in the SAME lib (no new dep).
- `YEAR_MIN=2000`/`YEAR_OFFSET=1900`/`MONTH_OFFSET=1` all in
  `src/common/Common.h:208-210`.
- `to_string_custom` is header-only in
  `src/common/utils/string/StringConvert.h:87/96/110` — include-dir only.
- `RTC::setTime(const struct tm&)` is in `common_time_rtc`
  (`src/common/time/rtc/RTC.h:25`).
- POSIX: `time`, `localtime`, `sleep` (`<ctime>`, `<unistd.h>`).
- `is_rtc_work_well` is a `main()` local bool (`main_app.cpp:719`), and RTC
  writeback uses it — **this is the boundary decision** (see §1).

### Lift #2 — mDNS params builders (`main_app.cpp:164-225`)
Four `static` file-local helpers form a cluster:
- `getDefaultMdnsInstanceName` (`:164-181`) — uses `trimConfigString` + INI macros.
- `getDefaultMdnsHostName` (`:184-196`) — uses `trimConfigString` + INI macros.
- `buildMdnsParams` (`:199-220`) — uses both above + `trimConfigString` +
  `Misc::getMACAddress` + `service::MdnsServiceParams`/`MdnsTxtPayload`/
  `kDefaultMdnsDeviceFamily` + `CAMERA_VERSION`.
- `isMdnsEnabled` (`:223-226`) — `config->get(INI_SECTION_MDNS, INI_KEY_MDNS_ENABLE, 1) != 0`.

CRITICAL dependency: all four depend on `trimConfigString` (`:84-99`), a
`static` file-local helper. `trimConfigString` is ALSO used by the scanner-mode
helper at `:144` (`std::string mode = trimConfigString(rawMode);`), which is
**NOT** part of this lift. So `trimConfigString` cannot simply be deleted from
main_app; it must either (a) be duplicated into the receiving lib as a
file-local static there (clean — each TU keeps its own), or (b) be promoted to
a shared header. **Recommend (a): copy `trimConfigString` verbatim as a
file-local `static` in the receiving .cpp** — keeps the move verbatim, no
behavior change, no header proliferation, mirrors how T9 kept
`getFileCreationTime` file-local in `Manifest.cpp`.

Call site: `main_app.cpp:1367` `auto mdns_params = buildMdnsParams(config, interface_name, ip_address, http_port, rtsp_port);` (inside CMD_MOBILE).
`isMdnsEnabled(config)` called at `:1366`.

Symbols/macros needed by the moved bodies:
- `INI_SECTION_MDNS`/`INI_KEY_MDNS_*`/`INI_SECTION_BOOT`/`INI_KEY_PMODEL`/
  `INI_KEY_PNAME`/`INI_SECTION_DEVICE`/`INI_KEY_PID` — all in
  `src/common/Common.h:13-74`.
- `CAMERA_VERSION` — defined where? `grep` shows it's used at `:215`; need
  to confirm it's a build-definition or `Common.h` macro. **ACTION for
  implementer: confirm `CAMERA_VERSION` origin before lift** (likely a
  `add_definition` in top-level CMake or `Common.h`). If it is a compile
  definition, the moved .cpp will see it identically (same target compile flags).
- `service::MdnsServiceParams`/`MdnsTxtPayload`/`kDefaultMdnsDeviceFamily` —
  in `discovery_service` (`src/service/discovery/MdnsService.h`,
  `MdnsTxtRecord.h:10/14`).

**NOTE per dispatch:** `getConfiguredPort` (`:152-163`) is a GENERIC
port-from-config helper, also used for non-mDNS ports
(http_port `:1320`, rtsp port `:1428`). **Do NOT move it.** It stays in
main_app.cpp (still used by CMD_MOBILE and CMD_RTSP_SERVER after the lifts).

### Lift #3 — RTSP server run-loop (CMD_RTSP_SERVER, `main_app.cpp:1424-1444`)
Block:
```cpp
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
```

**CRITICAL BLOCKER for a verbatim move of the whole block:**
- `already_in_exit_flow` is a file-scope `static bool` (`:584`) in main_app.cpp.
- `waitForSignalOrTimeout` is a file-scope `static int(int)` (`:622`) that
  reads the self-pipe `g_signal_pipe`, sets `already_in_exit_flow`, and calls
  `performCleanup(sig)` (`:642`+, which does the whole HAL/HTTP/mDNS/mgmt teardown).
- `rtsp_singleton_used` is a file-scope `static bool` (`:592`) read later at
  `main_exit` to drive process-level HAL teardown.

These three are **main_app.cpp translation-unit-private** and deeply entangled
with the process signal/cleanup machinery. A function in `app_workmode`
**cannot** call `waitForSignalOrTimeout` or read/write `already_in_exit_flow`
without first promoting that machinery to a shared header — which is a
**refactor with behavior risk**, explicitly out of scope for a "verbatim move".

`RtspServer` API (`src/media/rtsp/RtspServer.h`):
`static std::shared_ptr<RtspServer> getInstance()`,
`static void registerOnsessionClosedCallback(std::function<void()>)`,
`void setPort(int)`, `bool start()`, `bool stop()`.

---

## 1. Per-lift design

### Lift #1 — `Misc::ntpSyncAndWait` into `common_misc` (RECOMMEND: bundle in T11)

**API (new, `src/common/misc/Misc.h`):**
```cpp
// Synchronize via ntpd, then block until system time is valid (year > YEAR_MIN)
// or MAX_WAIT_SECONDS elapses. Returns true if time became valid, false on
// ntpSync failure OR timeout. RTC writeback is left to the CALLER
// (keeps common_misc free of a common_time_rtc dependency).
static bool ntpSyncAndWait(const std::string& ntp_server);
```

**Body (`src/common/misc/Misc.cpp`, verbatim copy of the loop body from
main_app.cpp:1172-1204):**
- `if (!Misc::ntpSync(ntp_server)) { Logger::log(ERROR, "ntp sync error"); return false; }`
- `const int MAX_WAIT_SECONDS = 30; const int CHECK_INTERVAL = 2; int wait_time = 0; struct tm* nowtime = nullptr;`
- `while (wait_time < MAX_WAIT_SECONDS) { ... localtime ... if (nowtime->tm_year + YEAR_OFFSET > YEAR_MIN) { Logger::log(INFO, "System time synchronized: ..."); break; } Logger::log(INFO, "Waiting ..."); sleep(CHECK_INTERVAL); wait_time += CHECK_INTERVAL; }`
- `if (wait_time >= MAX_WAIT_SECONDS) { Logger::log(WARNING, "Timeout waiting ... %d seconds", MAX_WAIT_SECONDS); return false; }`
- `return true;` (and expose `nowtime`? NO — RTC writeback stays in caller;
  the caller recomputes `time/localtime` for `RTC::setTime` after a true
  return, OR — preferred to keep byte-identical logs — the helper returns the
  captured `nowtime` is NOT possible without changing the signature to
  out-param, which is a behavior-neutral API choice).

**Caller rewrite (`main_app.cpp:1172-1208` → ~7 lines):**
```cpp
if (!Misc::ntpSyncAndWait(ntp_server)) {
    goto main_exit;
}
// RTC writeback stays here (verbatim):
if (is_rtc_work_well) {
    time_t now = time(nullptr);
    struct tm* nowtime = localtime(&now);
    RTC::getInstance()->setTime(*nowtime);
}
```

**Justification for RTC-writeback-stays-in-caller:** the dispatch explicitly
flags this as the preferred boundary (avoids adding `common_time_rtc` as a dep
of `common_misc`). Verified: `common_misc/CMakeLists.txt` does NOT link
`common_time_rtc` today, and adding it would be a new transitive edge.
Keeping the RTC line in the caller means the caller already has
`is_rtc_work_well`, `RTC.h`, and `<ctime>` — zero new deps anywhere.

**NOTE on log byte-identity:** the original loop logs `"System time
synchronized: %d-%02d-%02d %02d:%02d:%02d"` with the captured `nowtime`.
Because the helper now returns `bool` and the caller recomputes time for RTC,
the "synchronized" log stays inside the helper (uses its own `nowtime`) —
byte-identical. The caller's RTC writeback does NOT log, so no log change.
The only acceptable diff vs HEAD for this block: the `ntpSync` error/timeout
`goto main_exit` becomes `return false` in the helper + caller `goto main_exit`
(net behavior identical: same exit path, same logs).

**Includes the moved body needs (added to `Misc.cpp`):**
`<ctime>` (time/localtime — likely already present), `<unistd.h>` (sleep —
already present), `Common.h` (YEAR_MIN/YEAR_OFFSET/MONTH_OFFSET — verify
already included; add if not), `Logger.h` (already). `Misc.h` declares the
new static method.

**CMake:** `common_misc/CMakeLists.txt` — **no change** (all deps already
present: it's the home lib; `Misc.cpp` already compiled in). Header-only
`StringConvert.h`/`Common.h` via existing include dirs.

**Risk: LOW.** Pure leaf-helper extraction within its own lib; no new linkage;
RTC boundary is clean.

---

### Lift #2 — mDNS builders into `discovery_service` (RECOMMEND: bundle in T11)

**Chosen home: `discovery_service`** (NOT `app_workmode`). Rationale:
- `buildMdnsParams`/`isMdnsEnabled` produce `service::MdnsServiceParams` and
  read `service::kDefaultMdnsDeviceFamily` — both are **already** defined in
  `discovery_service` (`MdnsService.h`, `MdnsTxtRecord.h`). The builders are
  domain-owned by the discovery layer (they construct that layer's own data
  type). Putting them in `app_workmode` would make `app_workmode` depend on
  `discovery_service`'s internal param struct — inverted ownership.
- The dispatch notes `discovery_service` is "low-coupling (only
  tinysvcmdns+easylogger)". Adding `DeviceConfig` + `common_misc` deps is the
  **correct** direction: discovery params inherently come from device config +
  MAC address; that coupling belongs IN discovery, not in the work-mode
  orchestration seam. (`app_workmode` is being grown for the RTSP orchestration
  in Lift #3; piling config→mdns-param construction on it as well would muddy
  its "work-mode orchestration" role. mDNS param construction is not a
  work-mode concern.)
- Counter-consideration acknowledged: this widens `discovery_service`'s dep
  set. Accepted — it's the semantically correct home, and the deps are
  read-only config + MAC (both already linked into `htc_main_app`).

**API (new, `src/service/discovery/MdnsParams.h`):**
```cpp
#pragma once
#include "MdnsService.h"
#include <memory>
#include <string>

class DeviceConfig;  // fwd

namespace service {

// Build MdnsServiceParams from DeviceConfig + interface/IP/ports.
// (Verbatim move of main_app.cpp buildMdnsParams; file-local
//  trimConfigString/getDefaultMdnsInstanceName/getDefaultMdnsHostName
//  move with it as anonymous-namespace helpers.)
MdnsServiceParams buildMdnsParams(const std::shared_ptr<DeviceConfig>& config,
                                  const std::string& interface_name,
                                  const std::string& ip_address,
                                  uint16_t ctrl_port,
                                  uint16_t rtsp_port);

bool isMdnsEnabled(const std::shared_ptr<DeviceConfig>& config);

} // namespace service
```
(`DeviceConfig` is included as a real header in the .cpp — fwd-decl in the
header keeps the include surface small; mirror T9's `Settings.h` real-include
in .cpp.)

**Body (`src/service/discovery/MdnsParams.cpp`):**
- File-local `static std::string trimConfigString(const std::string&)` —
  **verbatim copy** of main_app.cpp:84-99 (the body incl. the
  `"..."`-strip tail at `:97+`; read full body before copy).
- `getDefaultMdnsInstanceName`/`getDefaultMdnsHostName` — verbatim (`:164-196`).
- `buildMdnsParams`/`isMdnsEnabled` — verbatim, wrapped in `namespace service`.
- All bodies unchanged except namespace wrapping.

**Includes the moved body needs (`MdnsParams.cpp`):**
`MdnsService.h` (params struct — in same dir), `MdnsTxtRecord.h` (kDefault*
+ MdnsTxtPayload), `DeviceConfig.h` (real include —
`src/config/devconf/DeviceConfig.h`), `Misc.h`
(`Misc::getMACAddress` — `src/common/misc/Misc.h`), `Common.h`
(INI_SECTION_*/INI_KEY_*), `Logger.h`, `<cctype>` (std::isspace in
trimConfigString), `<string>`. `CAMERA_VERSION` — see §0 caveat (confirm
origin; if it's a compile-def it propagates automatically).

**CMake (`src/service/discovery/CMakeLists.txt`):**
- Add `MdnsParams.cpp` to `DISCOVERY_SOURCES`.
- `target_link_libraries(discovery_service PUBLIC ...)` add `common_misc`
  and `devconf`. Keep existing `easylogger`, `tinysvcmdns`. (Adding
  `common_misc`/`devconf` as PUBLIC propagates to `htc_main_app` which
  already links both — no harm.)
- `target_include_directories` add `${CMAKE_CURRENT_SOURCE_DIR}/../../common`
  (Common.h), `${CMAKE_CURRENT_SOURCE_DIR}/../../common/misc`,
  `${CMAKE_CURRENT_SOURCE_DIR}/../../config/devconf`,
  `${CMAKE_CURRENT_SOURCE_DIR}/../../logger`.
- Note: `discovery_service` is currently **STATIC**. That's fine — the new
  .cpp compiles into the same static archive. (T9 made `manifest` SHARED to
  match sibling convention, but `discovery_service` is already STATIC and
  changing its linkage is out of scope / behavior-affecting for the .so set.
  Leave it STATIC.)

**Caller rewrite (`main_app.cpp:1366-1372`):**
```cpp
if (service::isMdnsEnabled(config)) {
    auto mdns_params = service::buildMdnsParams(config, interface_name, ip_address, http_port, rtsp_port);
    if (!service::MdnsService::getInstance()->start(mdns_params)) { ... }
}
```
(prefix `service::` on the two calls; delete the four statics `:164-226` from
main_app; keep `trimConfigString` at `:84` in main_app because the scanner
helper at `:144` still uses it).

**main_app.cpp includes:** add `#include "MdnsParams.h"` (discovery dir is
already in `include_directories` — `src/app/CMakeLists.txt` lists
`${CMAKE_CURRENT_SOURCE_DIR}/../service/discovery`).

**Risk: MEDIUM-LOW.** New transitive deps for `discovery_service` (devconf,
common_misc) — verify no circular link (devconf/common_misc do NOT link
discovery_service — confirmed by reading their CMake scope; `common_misc`
links sdk_stub/system_call+crc16+pthread; `devconf` is config-only). The
`trimConfigString` duplication is the one place two TUs will hold identical
file-local copies — acceptable and behavior-neutral.

---

### Lift #3 — RTSP run-loop into `app_workmode` (RECOMMEND: **SPLIT INTO T12**)

**Why this lift cannot be a clean verbatim move in T11:**

The CMD_RTSP_SERVER run-loop is structurally fused with main_app's
translation-unit-private signal machinery:
- `while (!already_in_exit_flow) { (void)waitForSignalOrTimeout(1000); }` —
  both symbols are `static` at file scope (`:584`, `:622`) and
  `waitForSignalOrTimeout` calls `performCleanup(sig)` (`:642`), which does
  the **entire process teardown** (daynight, RGB LED, settings save, HTTP
  stop, TCP event stop, mgmt/storage client clear, RTSP HAL teardown...).
- `rtsp_singleton_used = true` is read at `main_exit` to gate process-level
  HAL teardown.

To move the **whole block** (register + setPort + start + loop + stop) into
`app_workmode::runRtspServerUntilSignal(uint16_t)`, the run-loop's
`already_in_exit_flow`/`waitForSignalOrTimeout`/`performCleanup` trichotomy
must first be abstracted (e.g. inject a `std::function<int(int)> pollFn` or
promote the signal machinery to a shared header). That abstraction is a
**behavior-affecting refactor** (signal handling, cleanup ordering) —
exactly what the dispatch says to DEFER, and exactly the kind of change that
breaks the "byte-identical diff" guarantee that gates T11.

**Two options for T11 (pick A; recommend PM split B into T12):**

**Option A (IN SCOPE for T11 — verbatim, safe):** lift ONLY the
non-run-loop part of CMD_RTSP_SERVER into a helper that does register +
setPort + start, returns `bool`, and leaves the run-loop + stop in the caller.
```cpp
// app_workmode (new header src/app/workmode/RtspWorkMode.h)
namespace app_workmode {
// Register the standard session-closed logger callback, set port, start the
// RTSP singleton. Returns false if start() fails (caller does goto main_exit).
// Caller MUST set rtsp_singleton_used = true and run its own signal loop +
// RtspServer::getInstance()->stop() (the signal/exit machinery is
// main-app-private and intentionally not moved here).
bool startRtspServer(uint16_t rtsp_port);
}
```
Body (verbatim of `:1429-1438` minus the loop):
```cpp
bool startRtspServer(uint16_t rtsp_port) {
    RtspServer::getInstance()->registerOnsessionClosedCallback([]() {
        Logger::log(LogLevel::INFO, "RTSP session closed, waiting for new connection...");
    });
    RtspServer::getInstance()->setPort(static_cast<int>(rtsp_port));
    if (!RtspServer::getInstance()->start()) {
        Logger::log(LogLevel::ERROR, "Failed to start RTSP server");
        return false;
    }
    return true;
}
```
Caller rewrite (`main_app.cpp:1424-1444`):
```cpp
uint16_t rtsp_port = getConfiguredPort(config, INI_SECTION_MDNS, INI_KEY_MDNS_RTSP_PORT, DEFAULT_RTSP_PORT);
rtsp_singleton_used = true;
if (!app_workmode::startRtspServer(rtsp_port)) {
    goto main_exit;
}
while (!already_in_exit_flow) {
    (void)waitForSignalOrTimeout(1000);
}
RtspServer::getInstance()->stop();
```
The run-loop + stop stay verbatim in the caller; only register+setPort+start
moves. **`goto main_exit` propagation:** helper returns `false`, caller does
`goto main_exit` — identical control flow, byte-identical logs.

**Option B (DEFER to T12 — full run-loop move):** move the whole
`while(!already_in_exit_flow){waitForSignalOrTimeout(1000)}` + stop into the
helper. Requires first abstracting the signal machinery (`already_in_exit_flow`
flag + `waitForSignalOrTimeout` + `performCleanup`) — propose a follow-up
`signal/ExitFlowController` (or a `std::function<int(int)>` injected poll
callback) promoted out of main_app.cpp. This is a real refactor with signal/
cleanup-ordering risk; it deserves its own task (T12) with its own
golden-test (currently none exists, so it must be developed). Do NOT bundle.

**`app_workmode` growth (for Option A):**
- New files: `src/app/workmode/RtspWorkMode.h`, `src/app/workmode/RtspWorkMode.cpp`.
  (The existing `WorkMode.h/.cpp` is enum + GPIO/MCU mode-read — leave it
  untouched; add a sibling TU. `file(GLOB *.cpp)` in the workmode CMakeLists
  will pick up `RtspWorkMode.cpp` automatically.)
- `src/app/workmode/CMakeLists.txt`:
  - `include_directories` add
    `${CMAKE_CURRENT_SOURCE_DIR}/../../media/rtsp` (RtspServer.h),
    `${CMAKE_CURRENT_SOURCE_DIR}/../../logger` (already there).
  - `target_link_libraries(app_workmode PUBLIC mcu gpio logger)` → add
    `media_rtsp`. (Currently app_workmode links mcu+gpio+logger; RTSP needs
    `media_rtsp`. **T10 lesson: declare the dep at the source that uses it.**
    `htc_main_app` already links `media_rtsp`, so the transitive add is
    harmless but REQUIRED for app_workmode to compile/link its new TU.)

**Includes `RtspWorkMode.cpp` needs:** `RtspServer.h`, `Logger.h`,
`RtspWorkMode.h`. (No config/INI deps — `getConfiguredPort` stays in the
caller, so the helper takes a raw `uint16_t`.)

**Risk (Option A): LOW.** No signal machinery touched. No new transitive deps
beyond `media_rtsp` (already in htc_main_app). The run-loop semantics
(`already_in_exit_flow` + `waitForSignalOrTimeout(1000)`) are unchanged
because they stay in the caller.

**Risk (Option B / T12): HIGH** — see above; do not bundle.

---

## 2. Full change list (Option A for #3)

### New files
- `src/service/discovery/MdnsParams.h` — `namespace service`:
  `buildMdnsParams`, `isMdnsEnabled` decls (+ `DeviceConfig` fwd).
- `src/service/discovery/MdnsParams.cpp` — verbatim bodies (4 helpers) +
  file-local `trimConfigString` copy, wrapped `namespace service`.
- `src/app/workmode/RtspWorkMode.h` — `namespace app_workmode { bool startRtspServer(uint16_t); }`.
- `src/app/workmode/RtspWorkMode.cpp` — verbatim register+setPort+start body.

### Edited files
- `src/common/misc/Misc.h` — add `static bool ntpSyncAndWait(const std::string&);`.
- `src/common/misc/Misc.cpp` — add `ntpSyncAndWait` body (verbatim loop) +
  ensure `<ctime>`/`<unistd.h>`/`Common.h` included.
- `src/service/discovery/CMakeLists.txt` — add `MdnsParams.cpp` to sources;
  add `common_misc`+`devconf` to `target_link_libraries PUBLIC`; add include
  dirs (common, common/misc, config/devconf, logger).
- `src/app/workmode/CMakeLists.txt` — add `media/rtsp` include dir; add
  `media_rtsp` to `target_link_libraries PUBLIC`.
- `src/app/main_app.cpp`:
  - delete `getDefaultMdnsInstanceName`/`getDefaultMdnsHostName`/
    `buildMdnsParams`/`isMdnsEnabled` (`:164-226`). **Keep `trimConfigString`
    (`:84-99`)** — still used by scanner-mode helper `:144`. **Keep
    `getConfiguredPort` (`:152-163`)** — generic, used for http+rtsp ports.
  - delete NTP wait-loop body (`:1172-1204`); replace with
    `Misc::ntpSyncAndWait` call + RTC writeback (verbatim).
  - CMD_MOBILE RTSP/mDNS call sites: prefix `service::isMdnsEnabled` /
    `service::buildMdnsParams` (`:1366-1367`). **Do NOT touch the rest of
    CMD_MOBILE** (`:1384-1420` RTSP+HTTP+TCP teardown stays byte-identical).
  - CMD_RTSP_SERVER (`:1424-1444`): replace register+setPort+start with
    `app_workmode::startRtspServer(rtsp_port)`; keep run-loop + stop verbatim.
  - add includes: `#include "MdnsParams.h"`, `#include "RtspWorkMode.h"`.

### No CMake edits to `src/app/CMakeLists.txt` expected
`htc_main_app` already links `common_misc`, `discovery_service`,
`app_workmode`, `media_rtsp`, `devconf` on both platforms. The lifts reuse
existing link edges. (Verify only: discovery_service's new PUBLIC deps
propagate — they do, transitively.)

---

## 3. Behavior-preservation verification (per lift, mirroring T9)

For each moved body, the implementer must show a **byte-identical diff** of
the moved body vs `git show HEAD:src/app/main_app.cpp` (HEAD = pre-T11,
1894-line file). Allowed differences ONLY:
- signature prefix (`Misc::`, `service::`, `app_workmode::`, namespace wrap),
- `goto main_exit` → `return false` + caller `goto main_exit` (NTP + RTSP),
- the `nowtime` recomputation for RTC writeback staying in caller (NTP).

Verification commands:
```
# Per moved body: normalize (strip namespace prefix + signature) and diff vs HEAD
git show HEAD:src/app/main_app.cpp > /tmp/main_app_HEAD.cpp
# NTP loop body diff
diff <(sed -n '1172,1204p' /tmp/main_app_HEAD.cpp) <(awk '/ntpSyncAndWait/,/return true;/' src/common/misc/Misc.cpp)
# mDNS builders
diff <(sed -n '164,226p' /tmp/main_app_HEAD.cpp) <(sed -n '/MdnsServiceParams service::buildMdnsParams/,/^}/p' src/service/discovery/MdnsParams.cpp)
# RTSP start
diff <(sed -n '1429,1438p' /tmp/main_app_HEAD.cpp) <(sed -n '/bool app_workmode::startRtspServer/,/^}/p' src/app/workmode/RtspWorkMode.cpp)
# CMD_MOBILE untouched (must be empty diff over :1310-1420)
diff <(sed -n '1310,1420p' /tmp/main_app_HEAD.cpp) <(sed -n '1310,1420p' src/app/main_app.cpp)
```
Plus dual-platform build (exactly T9's gate):
```
cmake --build build_sim -j$(nproc) --target htc_main_app   # exit 0
cmake --build build -j$(nproc) --target htc_main_app        # exit 0 (toolchain.cmake/T32)
```
And for RTSP specifically: confirm the CMD_MOBILE region
(`main_app.cpp:1310-1420` post-edit) is **byte-identical** to HEAD (only the
two `service::` prefixes at the mDNS calls change, which are outside the RTSP
teardown lines `:1384-1420`).

---

## 4. Risks (ranked)

1. **RTSP run-loop `goto`/signal entanglement (HIGH if Option B attempted).**
   Mitigation: do Option A only in T11; split B to T12. (See §1 Lift #3.)
2. **`CAMERA_VERSION` origin unknown** (used in `buildMdnsParams` `:215`).
   If it's a `target_compile_definition` on `htc_main_app` only, the moved
   `MdnsParams.cpp` in `discovery_service` will NOT see it → compile error.
   Mitigation: implementer MUST `grep -rn CAMERA_VERSION` in CMake +
   `Common.h` first; if it's app-scoped, add it to `discovery_service`
   compile defs OR (preferred) pass it as a parameter / read from a shared
   header. **Flag to implementer as a pre-flight check.**
3. **`trimConfigString` duplication** (main_app keeps one for scanner helper,
   discovery gets one). Two TUs hold identical file-local copies —
   behavior-neutral, but a future maintainer may diverge them. Accepted for a
   verbatim move; note in a code comment.
4. **`discovery_service` new transitive deps** (`devconf`, `common_misc`).
   Verify no link cycle (confirmed: neither links `discovery_service`).
5. **T32 uClibc `std::to_string`/`stoi`** — moved bodies use neither (NTP uses
   `to_string_custom` which stays in caller; mDNS/RTSP bodies use no string
   conversion). Verify no stray `std::to_string` sneaks in during the move.
6. **`rtsp_singleton_used` stays in caller** — the helper does NOT set it.
   Caller MUST keep `rtsp_singleton_used = true;` before calling
   `startRtspServer` (otherwise process-level HAL teardown at main_exit is
   skipped). Documented in the helper's comment.

---

## 5. Rollback

All changes are additive files + localized main_app.cpp edits on branch
`feature/new-workmode`. Rollback = `git checkout src/app/main_app.cpp
src/app/workmode/ src/service/discovery/ src/common/misc/ && rm` the 4 new
files. No state/migration. The lifts are independent: NTP (#1) and mDNS (#2)
can land even if RTSP (#3) is deferred.

---

## 6. Recommendation to PM

- **Bundle #1 (NTP) + #2 (mDNS) in T11** — both are clean verbatim moves,
  low risk, independent of each other.
- **Split #3 (RTSP) into T12** — the run-loop move (Option B) requires
  abstracting main_app's signal/cleanup machinery first; bundling it risks
  breaking the byte-identical gate that defines this phase. Do Option A
  (start-only helper) in T11 if a partial RTSP lift is wanted now; defer
  the full run-loop move + signal abstraction to T12 with a golden test.

---

## 7. Acceptance criteria

- [ ] `build_sim` + `build` both produce `htc_main_app` with exit 0.
- [ ] NTP moved body byte-identical to HEAD (modulo signature/`goto`→`return`).
- [ ] mDNS 4 helpers moved byte-identical to HEAD (modulo `service::` wrap).
- [ ] `trimConfigString` retained in main_app.cpp (scanner helper still compiles).
- [ ] `getConfiguredPort` retained in main_app.cpp (http/rtsp port calls intact).
- [ ] CMD_MOBILE region (`:1310-1420`) byte-identical to HEAD except the two
      `service::` prefixes at the mDNS call sites.
- [ ] CMD_RTSP_SERVER run-loop (`while(!already_in_exit_flow)`) + `stop()`
      remain in main_app.cpp (Option A) — OR fully deferred (T12).
- [ ] No `std::to_string`/`stoi` in any moved body.
- [ ] No `src/hal/**` diff.
- [ ] Report card + full archive written; C1 validate passes.
