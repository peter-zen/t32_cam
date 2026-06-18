# T11 tester evidence — Phase B-2 behavior-preserving lifts (NTP + mDNS; RTSP deferred)

Branch `feature/new-workmode`. Baseline for body byte-identity = `git show HEAD:src/app/main_app.cpp`
(1894 lines; HEAD = commit 8df7fe8). Current `src/app/main_app.cpp` = 1559 lines.

Note on baseline: `git diff HEAD` also shows T9 (`manifest`) uncommitted work
(`manifest/Manifest.h` include + `manifest::createDescInfoFile`/`generateDescInfo`
calls). T9 is a closed prior task (`artifacts/T9-closure-report.md`, status: success).
Those lines are NOT T11 changes and are excluded from every gate below; T11 scope is
the NTP + mDNS lifts only (RTSP deferred to T12).

---

## Gate 1 — Both platforms build+link clean

| Command | Result |
|---|---|
| `cmake --build build_sim -j$(nproc) --target htc_main_app` | **PASS** exit 0 — `[100%] Built target htc_main_app` |
| `cmake --build build -j$(nproc) --target htc_main_app` (T32 uClibc) | **PASS** exit 0 — `[100%] Built target htc_main_app` |

Both link `bin/htc_main_app`. Force-rebuilt the two moved TUs (`common_misc/Misc.cpp.o`,
`discovery_service/MdnsParams.cpp.o`) on sim: only pre-existing `-Wdeprecated-declarations`
for `Logger` (project-wide EasyLogger migration; present at HEAD); `MdnsParams.cpp` zero
warnings; no errors.

---

## Gate 2 — NTP moved body byte-identical (behavior gate) — PASS

HEAD CMD_NTP moved body = lines 1418-1448 of `git show HEAD:src/app/main_app.cpp`
(the `Misc::ntpSync(ntp_server)` guard + the `while(wait_time<MAX_WAIT_SECONDS)`
localtime poll through the timeout). New body = `Misc::ntpSyncAndWait` in
`src/common/misc/Misc.cpp:510-547`.

Whitespace-normalized diff (tabs→spaces, strip indent + trailing ws, drop blanks,
map `return false;`→`goto main_exit;`):

```diff
--- /tmp/ntp_HEAD.norm   (HEAD body)
+++ /tmp/ntp_NEW.norm    (ntpSyncAndWait body)
@@ -25,3 +25,5 @@
 if (wait_time >= MAX_WAIT_SECONDS) {
 Logger::log(LogLevel::WARNING, "Timeout waiting for system time synchronization after %d seconds", MAX_WAIT_SECONDS);
 goto main_exit;
+}
+}
```

The only diff = two trailing structural close-braces (the `if(wait_time>=...)` block
close + the function close). **Every logic line is identical.** Allowed diffs present
and only those: signature `static int ...`→`bool Misc::ntpSyncAndWait(...)`, the two
fail/timeout `goto main_exit`→`return false`, indent style spaces→tabs. No other
logic line differs.

Caller (`src/app/main_app.cpp:1110`): `if (!Misc::ntpSyncAndWait(ntp_server)) { goto main_exit; }`
then verbatim RTC writeback at `:1114-1117`:
```cpp
if (is_rtc_work_well) {
    time_t now = time(nullptr);
    struct tm* nowtime = localtime(&now);
    RTC::getInstance()->setTime(*nowtime);
}
```
`ntp_server` build (`ip + ":" + to_string_custom(port)`) + empty-check stay in caller
(main_app.cpp:1101-1109). `common_misc` gained NO `common_time_rtc` dep (clean boundary).

Additional proof (full-file whitespace-agnostic diff of Misc.cpp HEAD vs current):
the ONLY logic additions to the entire `Misc.cpp` are (a) `#include <ctime>`,
(b) `#include "Common.h"`, (c) the `ntpSyncAndWait` function. The existing
`Misc::ntpSync` body is byte-identical (logic) — verified by Python extraction +
normalized diff = "IDENTICAL (no diff)". The large raw `git diff` line count
(262+/191-) is purely cosmetic spaces→tabs re-indentation of pre-existing code,
not logic change.

---

## Gate 3 — mDNS 4 helpers byte-identical — PASS

Extracted from HEAD (`git show HEAD:src/app/main_app.cpp`):
`trimConfigString` (124-140), `getDefaultMdnsInstanceName` (204-222),
`getDefaultMdnsHostName` (224-237), `buildMdnsParams` (239-261),
`isMdnsEnabled` (263-266). Diffed against `src/service/discovery/MdnsParams.cpp`
whitespace-normalized (strip indent, drop blanks, drop `static ` and `service::`
qualifiers per allowed-diffs):

| Helper | Normalized diff result |
|---|---|
| `trimConfigString` | **IDENTICAL** (diff exit 0) |
| `getDefaultMdnsInstanceName` | **IDENTICAL** (diff exit 0) |
| `getDefaultMdnsHostName` | **IDENTICAL** (diff exit 0) |
| `buildMdnsParams` | **IDENTICAL** (diff exit 0) |
| `isMdnsEnabled` | **IDENTICAL** (diff exit 0) |

`buildMdnsParams` body kept `MdnsServiceParams params;` / `kDefaultMdnsDeviceFamily`
without `service::` (now same-namespace) — allowed. `trimConfigString` duplicated as
file-local in `MdnsParams.cpp` anonymous namespace (documented in-code); main_app.cpp
keeps its own copy for the scanner helper.

---

## Gate 4 — Caller correctness in main_app.cpp — PASS

- `grep -nE 'Misc::ntpSyncAndWait' src/app/main_app.cpp` → exactly **1** call:
  `1110:        if (!Misc::ntpSyncAndWait(ntp_server)) {` (in CMD_NTP).
- `grep -nE 'service::(buildMdnsParams|isMdnsEnabled)' src/app/main_app.cpp` → the **2**
  CMD_MOBILE call sites:
  - `1264: if (service::isMdnsEnabled(config)) {`
  - `1265: auto mdns_params = service::buildMdnsParams(config, interface_name, ip_address, http_port, rtsp_port);`
- RTC writeback present after the `ntpSyncAndWait` call (main_app.cpp:1114-1117, see Gate 2).

---

## Gate 5 — RTSP UNTOUCHED (deferred to T12) — PASS

**CMD_RTSP_SERVER block byte-identical.** Raw `diff` of HEAD block
(`git show HEAD:src/app/main_app.cpp` lines 1668-1688) vs current
(`src/app/main_app.cpp` lines 1333-1353):

```
$ diff <(sed -n '1668,1688p' /tmp/main_app_HEAD.cpp) <(sed -n '1333,1353p' src/app/main_app.cpp)
$ echo $?
0
```
Empty diff. The register/setPort/start + `while(!already_in_exit_flow){waitForSignalOrTimeout(1000)}`
run-loop + `RtspServer::getInstance()->stop()` are all verbatim vs HEAD.

**CMD_MOBILE region byte-identical except the two `service::` prefixes.** Raw `diff` of
HEAD CMD_MOBILE block (1546-1667) vs current (1211-1332):

```diff
54,55c54,55
<         if (isMdnsEnabled(config)) {
<             auto mdns_params = buildMdnsParams(config, interface_name, ip_address, http_port, rtsp_port);
---
>         if (service::isMdnsEnabled(config)) {
>             auto mdns_params = service::buildMdnsParams(config, interface_name, ip_address, http_port, rtsp_port);
```
Only these 2 lines differ — exactly the mDNS call prefixes. The CMD_MOBILE RTSP/HTTP/TCP
teardown (`mobile_rtsp_enabled`, `RtspServer::getInstance()`, `http_server_stop/deinit`,
`TcpEventService::stop`, `MdnsService::stop`, run-loop) is byte-identical to HEAD.

**No `RtspWorkMode.*` files exist:**
```
$ ls src/app/workmode/
CMakeLists.txt  WorkMode.cpp  WorkMode.h
$ find . -name 'RtspWorkMode*' -not -path './build*/*'   # empty
```
No changes to `RtspServer::` call sequences anywhere.

---

## Gate 6 — Retained helpers — PASS

- `trimConfigString`: def `src/app/main_app.cpp:85`, used `:145` (scanner helper).
- `getConfiguredPort`: def `src/app/main_app.cpp:153`, used `:1230`/`:1231` (http+rtsp
  ports, CMD_MOBILE) / `:1338` (CMD_RTSP_SERVER).

---

## Gate 7 — Scope — PASS

- `git diff --stat HEAD -- src/hal` → **empty**. No `src/hal/**` touches.
- No singleton decoupling: NTP helper uses no singleton; mDNS `buildMdnsParams` calls
  `Misc::getMACAddress` whose singleton (`::getInstance()`) stays inside `Misc.cpp`.
  `grep getInstance src/common/misc/Misc.cpp src/service/discovery/MdnsParams.cpp` →
  none in the moved bodies.

---

## Gate 8 — T32 uClibc — PASS

```
$ grep -nE 'std::to_string|std::stoi|std::stoul' src/common/misc/Misc.cpp src/service/discovery/MdnsParams.cpp
(empty)
```
No `std::to_string`/`stoi`/`stoul` in either moved body. NTP uses `to_string_custom`
(kept in caller); mDNS/RTSP bodies use no string conversion. T32 link is clean
(Gate 1b).

---

## CMake wiring (supporting evidence)

- `src/common/misc/Misc.h:41`: `static bool ntpSyncAndWait(const std::string& ntp_server);`
  (doc comment 39-40 re: RTC-writeback-stays-in-caller boundary).
- `src/common/misc/CMakeLists.txt`: added `${CMAKE_CURRENT_SOURCE_DIR}/..` to
  `include_directories` (bare `Common.h` resolves). No new link dep (home lib).
- `src/service/discovery/CMakeLists.txt`: `MdnsParams.cpp` in `DISCOVERY_SOURCES`;
  `common_misc` + `devconf` added to `target_link_libraries(... PUBLIC ...)`;
  include dirs added (common, common/misc, config/devconf, logger). Stays STATIC.
- `CAMERA_VERSION` = top-level `add_definitions` (root `CMakeLists.txt:47-48`),
  propagates to `discovery_service` automatically (planner risk #2 resolved; T32
  link clean proves it).

---

## Conclusion

All 8 gates PASS. T11 is two clean verbatim lifts (NTP wait-loop → `common_misc`,
mDNS builders → `discovery_service`) with byte-identical moved bodies modulo the
allowed signature/namespace/`goto`→`return` diffs. RTSP is fully deferred to T12:
CMD_RTSP_SERVER block and CMD_MOBILE RTSP/HTTP/TCP teardown are byte-identical to
HEAD, no `RtspWorkMode` files created. Dual-platform build links clean.
