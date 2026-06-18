# T11 reviewer evidence — Phase B-2 behavior-preserving lifts (NTP + mDNS; RTSP deferred to T12)

Branch `feature/new-workmode`. Independent re-verification of the 7 review-focus
items. Baselines: `git show HEAD:src/app/main_app.cpp` (1894 lines, HEAD=8df7fe8),
`git show HEAD:src/common/misc/Misc.cpp` (644 lines). Current `src/app/main_app.cpp`
= 1559 lines, `src/common/misc/Misc.cpp` = 684 lines. T9 (`manifest`) uncommitted
work present in `git diff HEAD` is excluded — it is a prior closed task, not T11.

---

## 1. Byte-identical moves (whitespace-normalized, re-confirmed)

### NTP wait-loop -> `Misc::ntpSyncAndWait`

HEAD CMD_NTP loop = `/tmp/main_app_HEAD.cpp` lines 1418-1448
(`Misc::ntpSync(ntp_server)` guard + `while(wait_time<MAX_WAIT_SECONDS)`
localtime poll). New body = `src/common/misc/Misc.cpp:513-546` (`Misc::ntpSyncAndWait`).

Whitespace-agnostic diff (`diff -w`) confirms the ONLY differences are the
signature line, `goto main_exit` -> `return false` (2 sites: ntpSync-fail +
timeout), and indent (spaces -> tabs). Every logic line matches HEAD. Constant
values preserved: `MAX_WAIT_SECONDS=30`, `CHECK_INTERVAL=2`,
`tm_year + YEAR_OFFSET > YEAR_MIN` break condition.

Caller (`src/app/main_app.cpp:1110`):
`if (!Misc::ntpSyncAndWait(ntp_server)) { goto main_exit; }` then RTC writeback
at `:1114-1117` (`time/localtime` re-read locally -> `RTC::getInstance()->setTime`).
`ntp_server` build + empty-check stay in caller (`:1101-1109`).

Behavioral note (non-regression): the caller now re-reads `time`/`localtime`
for RTC writeback instead of reusing the loop's `nowtime`. Semantically
equivalent — the helper returns true only after the year-check passed, so the
re-read yields a valid time. `localtime` null-safety pattern is IDENTICAL in
HEAD and the new function (neither null-checks; pre-existing codebase pattern).

### mDNS 4 helpers + `trimConfigString` -> `service::` in `MdnsParams.cpp`

Normalized (strip leading ws, drop blanks, drop `static` and `service::`
qualifiers per allowed-diffs) per-function diffs vs HEAD:

| Helper (HEAD range -> MdnsParams.cpp range) | Normalized diff |
|---|---|
| `trimConfigString` (124-141 -> 20-37) | IDENTICAL (diff exit 0) |
| `getDefaultMdnsInstanceName` (204-222 -> 39-57) | IDENTICAL |
| `getDefaultMdnsHostName` (224-237 -> 59-72) | IDENTICAL |
| `buildMdnsParams` (239-261 -> 76-98) | IDENTICAL |
| `isMdnsEnabled` (263-266 -> 100-103) | IDENTICAL |

`buildMdnsParams` keeps `MdnsServiceParams params;` and `kDefaultMdnsDeviceFamily`
without `service::` (now same-namespace) — allowed. All string literals, INI
keys, defaults (`"T32Camera"`, `"t32cam"`, `"_t32cam._tcp"`, `"T32"`, `"ready"`)
and the `Misc::getMACAddress` call match HEAD byte-for-byte.

---

## 2. `Misc.cpp` re-indentation is COSMETIC ONLY (the key risk)

`diff -w <(git show HEAD:src/common/misc/Misc.cpp) src/common/misc/Misc.cpp`
produces EXACTLY 3 hunks, no more:

```
10a11    > #include <ctime>
16a18    > #include "Common.h"
504a507,544
  > 	return true;
  > }
  >
  > bool Misc::ntpSyncAndWait(const std::string& ntp_server) { ... }   (38-line insertion)
```

- Hunk 1: `<ctime>` include added.
- Hunk 2: `Common.h` include added.
- Hunk 3: 38-line insertion = close of the PRECEDING `Misc::ntpSync` function
  (`return true; }`, shown as context for the insertion point) + the new
  `Misc::ntpSyncAndWait` body. The preceding function's close is unchanged;
  it appears in the diff only as anchoring context for where the new function
  is inserted.

Function-extraction proof that existing code was NOT altered:
```
$ awk '/^bool Misc::ntpSync\(const std::string& ntp_server\)/,/^}/' HEAD > a
$ awk '/^bool Misc::ntpSync\(const std::string& ntp_server\)/,/^}/' current > b
$ diff -w a b   && echo IDENTICAL
IDENTICAL
```
Existing `Misc::ntpSync` body is byte-identical (whitespace-agnostic). No stray
logic edit is hidden in the spaces->tabs re-indent. The large raw
`git diff HEAD -- src/common/misc/Misc.cpp` line count is purely cosmetic.

`Misc.h` diff (`diff -w` HEAD vs current): ONLY the new
`static bool ntpSyncAndWait(const std::string& ntp_server);` decl + a 4-line
doc comment (the boundary note re: RTC-writeback-stays-in-caller). No existing
decl altered.

---

## 3. `discovery_service` new deps sound + no link cycle

`src/service/discovery/CMakeLists.txt`:
- `MdnsParams.cpp` in `DISCOVERY_SOURCES` (line 4).
- `common_misc` + `devconf` added to `target_link_libraries(... PUBLIC ...)`
  (lines 21-22), alongside pre-existing `easylogger` + `tinysvcmdns`.
- Include dirs added: `../../common`, `../../common/misc`, `../../config/devconf`,
  `../../logger` (lines 11-14) so bare `Common.h`/`misc/Misc.h`/`DeviceConfig.h`/
  `Logger.h` resolve and moved bodies stay byte-identical.
- `add_library(discovery_service STATIC ...)` — stays STATIC (line 7).

No link cycle (verified by reading the upstream CMakeLists):
- `src/common/misc/CMakeLists.txt`: `common_misc` is SHARED, links only
  `sdk_stub|system_call` + `crc16` + `pthread`. Does NOT link `discovery_service`.
- `src/config/devconf/CMakeLists.txt`: `devconf` is SHARED, links only `env`.
  Does NOT link `discovery_service`.

Direction `discovery_service -> common_misc + devconf` is one-way. No cycle.

`common_misc` NEEDED list (build_sim `readelf -d`): `libstdc++.so.6`,
`libgcc_s.so.1`, `libc.so.6` ONLY — confirms `common_misc` did NOT gain a
`common_time_rtc` dep (NTP boundary clean; RTC writeback correctly in caller).

---

## 4. RTSP genuinely untouched

- No `RtspWorkMode.*` files: `find src -name 'RtspWorkMode*'` = empty.
  `ls src/app/workmode/` = `CMakeLists.txt WorkMode.cpp WorkMode.h` only.

- CMD_RTSP_SERVER block: raw `diff` of HEAD `:1668-1688` vs current `:1333-1353`
  = EMPTY (byte-identical). The register/setPort/start +
  `while(!already_in_exit_flow){waitForSignalOrTimeout(1000)}` run-loop +
  `RtspServer::getInstance()->stop()` are verbatim vs HEAD.

- CMD_MOBILE region: raw `diff` of HEAD `:1546-1667` vs current `:1211-1332` =
  EXACTLY 2 lines differ (the mDNS prefixes):
  ```
  54,55c54,55
  <         if (isMdnsEnabled(config)) {
  <             auto mdns_params = buildMdnsParams(config, interface_name, ip_address, http_port, rtsp_port);
  ---
  >         if (service::isMdnsEnabled(config)) {
  >             auto mdns_params = service::buildMdnsParams(config, interface_name, ip_address, http_port, rtsp_port);
  ```
  The CMD_MOBILE RTSP/HTTP/TCP teardown (`mobile_rtsp_enabled`,
  `RtspServer::getInstance()` register/start/stop, `http_server_stop`/`deinit`,
  `TcpEventService::stop`, `MdnsService::stop`, run-loop) is byte-identical
  to HEAD. Lift #3 (RTSP) is fully deferred to T12.

---

## 5. Retained helpers + caller correctness

- `Misc::ntpSyncAndWait`: exactly 1 call site (`src/app/main_app.cpp:1110`),
  followed by RTC writeback (`:1114-1117`). Declared `Misc.h:41` next to
  existing `ntpSync` (`:36`).
- `service::buildMdnsParams`/`isMdnsEnabled`: exactly 2 call sites
  (`main_app.cpp:1264-1265`, both CMD_MOBILE).
- `trimConfigString` retained in main_app: def `:85`, used `:145` (scanner
  helper `parseMediaScannerMode`). Duplicated as file-local anonymous-namespace
  copy in `MdnsParams.cpp:20` with an in-code NOTE documenting the intentional
  duplication.
- `getConfiguredPort` retained in main_app: def `:153`; used `:1230`/`:1231`
  (http+rtsp ports, CMD_MOBILE) and `:1338` (CMD_RTSP_SERVER). Not moved
  (generic; correct).

---

## 6. Both builds link the right symbols

`MdnsParams.cpp.o` is compiled into `libdiscovery_service.a` in BOTH
`build/` and `build_sim/`. `nm -C` confirms the moved symbols are DEFINED
(`T`) in `MdnsParams.cpp.o`:
```
T service::isMdnsEnabled(std::shared_ptr<DeviceConfig> const&)
T service::buildMdnsParams(std::shared_ptr<DeviceConfig> const&, ..., unsigned short, unsigned short)
```
`Misc::ntpSyncAndWait` is DEFINED (`T`) in `libcommon_misc.so` on BOTH
platforms:
```
build_sim/lib/libcommon_misc.so: 0000000000006bd0 T Misc::ntpSyncAndWait(...)
build/lib/libcommon_misc.so:     0000556c T Misc::ntpSyncAndWait(...)
```
The app links `libcommon_misc.so` + `libdevconf.so` as NEEDED (transitively via
`discovery_service`'s PUBLIC deps) — proven by `readelf -d build_sim/bin/htc_main_app`.
Both binaries produced (`build/bin/htc_main_app`, `build_sim/bin/htc_main_app`).
Tester Gate 1 (both builds exit 0) stands.

---

## 7. Scope / hygiene

- `git diff --stat HEAD -- src/hal` = EMPTY. No `src/hal/**` touches.
- No singleton decoupling: `ntpSyncAndWait` uses no singleton;
  `buildMdnsParams` calls `Misc::getMACAddress` whose singleton
  (`::getInstance()`) stays inside `Misc.cpp`.
- Zero `std::to_string`/`stoi`/`stoul` in either moved body
  (`grep -nE 'std::to_string|std::stoi|std::stoul' Misc.cpp MdnsParams.cpp`
  = empty). NTP uses `to_string_custom` (kept in caller); mDNS bodies use no
  string conversion. T32 uClibc-safe.
- `trimConfigString` duplication is intentional + commented (in-code NOTE in
  `MdnsParams.cpp:17-19`). Mirrors T9's `getFileCreationTime` pattern.

---

## Verdict

All 7 review-focus items pass. The Misc.cpp re-indentation is COSMETIC ONLY
(only `<ctime>` + `Common.h` includes + the new `ntpSyncAndWait` function were
added; the existing `Misc::ntpSync` body is byte-identical). The discovery_service
deps are sound and one-way (no link cycle). RTSP is genuinely untouched. Both
platforms link the moved symbols clean. status: success.
