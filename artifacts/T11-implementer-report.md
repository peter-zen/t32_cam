---
contract: report
contract_version: "1"
task_id: T11
node: implementer
flow: feature
status: success
summary: |
  Phase B-2 Lift #1 (NTP wait-loop -> Misc::ntpSyncAndWait in common_misc) and
  Lift #2 (mDNS builders -> service::buildMdnsParams/isMdnsEnabled in
  discovery_service) done as verbatim moves. Moved bodies are byte-identical to
  HEAD modulo namespace/signature and the ntpSync-fail/timeout goto->return
  boundary. Lift #3 (RTSP) intentionally DEFERRED to T12: CMD_RTSP_SERVER block
  and CMD_MOBILE RTSP/HTTP/TCP teardown left fully untouched; no RtspWorkMode
  files created. trimConfigString duplicated (file-local) into MdnsParams.cpp;
  main_app keeps its own copy (scanner helper) plus getConfiguredPort (http/
  rtsp ports). Both-platform build links clean: build_sim htc_main_app exit 0,
  build htc_main_app (T32 uClibc toolchain) exit 0. Zero std::to_string/stoi
  in either moved body (grep empty). CAMERA_VERSION confirmed as top-level
  add_definitions -> propagates to discovery_service automatically (no action).
state_delta:
  set_task_status: {}
deliverables:
  - src/common/misc/Misc.h
  - src/common/misc/Misc.cpp
  - src/common/misc/CMakeLists.txt
  - src/service/discovery/MdnsParams.h
  - src/service/discovery/MdnsParams.cpp
  - src/service/discovery/CMakeLists.txt
  - src/app/main_app.cpp
  - artifacts/T11-implementer-report.md
  - artifacts/T11-implementer-full.md
verification:
  commands:
    - cmake --build build_sim -j$(nproc) --target htc_main_app
    - cmake --build build -j$(nproc) --target htc_main_app
    - grep -nE 'std::to_string|std::stoi' src/common/misc/Misc.cpp src/service/discovery/MdnsParams.cpp
    - grep -nE 'ntpSyncAndWait|service::buildMdnsParams' src/app/main_app.cpp
  evidence_ref: src/service/discovery/MdnsParams.cpp
artifact_path: src/service/discovery/MdnsParams.cpp
---

# T11 implementer report — Phase B-2 (NTP + mDNS verbatim lifts; RTSP deferred to T12)

## What was done

### Lift #1 — NTP wait-loop -> `common_misc`
- `src/common/misc/Misc.h`: added `static bool ntpSyncAndWait(const std::string& ntp_server);`
  next to the existing `ntpSync` decl.
- `src/common/misc/Misc.cpp`: added `Misc::ntpSyncAndWait` body = verbatim the
  CMD_NTP loop from `main_app.cpp` HEAD (`Misc::ntpSync(ntp_server)` guard +
  the `while (wait_time < MAX_WAIT_SECONDS)` localtime poll until
  `tm_year + YEAR_OFFSET > YEAR_MIN`, MAX_WAIT_SECONDS=30, CHECK_INTERVAL=2).
  On ntpSync failure or timeout -> `return false`; on sync -> `return true`.
  The only control-flow change vs HEAD: `goto main_exit` on ntpSync-fail /
  timeout becomes `return false` (the caller does `goto main_exit`), exactly
  as the planner specified. RTC writeback stays in the caller.
- Added includes: `#include <ctime>`, `#include "Common.h"` (for
  YEAR_MIN/YEAR_OFFSET/MONTH_OFFSET). `<unistd.h>` was already present.
- `src/common/misc/CMakeLists.txt`: added
  `${CMAKE_CURRENT_SOURCE_DIR}/..` to `include_directories` so bare
  `Common.h` (lives in `src/common/`) resolves. No new link dep
  (common_misc is the home lib; `Misc.cpp` already compiled in).
- `src/app/main_app.cpp` CMD_NTP block: replaced the inlined
  ntpSync + wait-loop with
  `if (!Misc::ntpSyncAndWait(ntp_server)) { goto main_exit; }`, then kept the
  RTC writeback verbatim (recomputing `time`/`localtime` locally — the helper
  returns bool, not the `tm*`, so the caller recomputes for `RTC::setTime`).
  The `ntp_server` string build (`ip + ":" + to_string_custom(port)`) and the
  empty-check both stay in the caller.

### Lift #2 — mDNS builders -> `discovery_service`
- `src/service/discovery/MdnsParams.h` (new): in `namespace service`, declares
  `MdnsServiceParams buildMdnsParams(...)` and `bool isMdnsEnabled(...)`.
  Forward-declares `class DeviceConfig;` in the header.
- `src/service/discovery/MdnsParams.cpp` (new): verbatim move of
  `trimConfigString` (as a file-local copy inside an anonymous-namespace),
  `getDefaultMdnsInstanceName`, `getDefaultMdnsHostName`, `buildMdnsParams`,
  `isMdnsEnabled`, wrapped in `namespace service`. The 3 internal helpers
  (`trimConfigString`, `getDefaultMdnsInstanceName`, `getDefaultMdnsHostName`)
  are TU-private inside `namespace service { namespace { ... } }`. Bodies are
  byte-identical to HEAD modulo: `service::` qualifiers dropped on the
  `MdnsServiceParams` return type and on `kDefaultMdnsDeviceFamily`
  (now same-namespace), and the `static` linkage keywords retained.
  Includes: `MdnsParams.h`, `MdnsService.h`, `MdnsTxtRecord.h`,
  `DeviceConfig.h` (real include), `misc/Misc.h`, `Common.h`, `Logger.h`,
  `<cctype>`, `<string>`. `CAMERA_VERSION` is a top-level `add_definitions`
  (`CMakeLists.txt:47-48`), so it propagates to `discovery_service`
  automatically — confirmed, no action needed.
- `src/service/discovery/CMakeLists.txt`: added `MdnsParams.cpp` to
  `DISCOVERY_SOURCES`; added `common_misc` + `devconf` to
  `target_link_libraries(... PUBLIC ...)`; added include dirs
  `../../common`, `../../common/misc`, `../../config/devconf`, `../../logger`
  (so bare `Common.h` / `misc/Misc.h` / `DeviceConfig.h` / `Logger.h`
  resolve and bodies stay byte-identical). `discovery_service` left STATIC.
- `src/app/main_app.cpp`: deleted the 4 statics
  (`getDefaultMdnsInstanceName`, `getDefaultMdnsHostName`, `buildMdnsParams`,
  `isMdnsEnabled`). **KEPT** `trimConfigString` (still used by the scanner
  helper `parseMediaScannerMode`). **KEPT** `getConfiguredPort` (generic;
  still used for http_port + rtsp_port in CMD_MOBILE and CMD_RTSP_SERVER).
  At the CMD_MOBILE mDNS call sites prefixed `service::isMdnsEnabled` /
  `service::buildMdnsParams`. Added `#include "MdnsParams.h"`.

## Why these changes satisfy the plan

- Both lifts are additive/verbatim: the moved bodies match HEAD logic
  byte-for-byte (verified by whitespace-stripped per-function diff vs
  `git show HEAD:src/app/main_app.cpp`). The only edits to moved bodies are
  namespace wrap / `service::` qualifier drop / signature prefix, exactly
  the allowed-diffs in the planner §3.
- NTP boundary clean: RTC writeback stays in caller; `common_misc` did NOT
  gain a `common_time_rtc` dep (planner §1 Lift #1).
- mDNS home = `discovery_service` (not `app_workmode`); `devconf` +
  `common_misc` added as PUBLIC deps (correct direction; neither links back
  to `discovery_service` -> no cycle). `discovery_service` stays STATIC
  (planner §1 Lift #2).
- `trimConfigString` duplication is intentional and documented (two TUs hold
  identical file-local copies) — mirrors T9's `getFileCreationTime`.
- RTSP untouched: CMD_RTSP_SERVER block (register/setPort/start/run-loop/stop)
  and CMD_MOBILE RTSP/HTTP/TCP teardown are verbatim vs HEAD; no
  `RtspWorkMode` files created. Lift #3 = T12 (planner §1 Lift #3 Option B,
  PM split confirmed by dispatch).

## Verification results

| Check | Result |
|---|---|
| `cmake --build build_sim -j$(nproc) --target htc_main_app` | exit 0 (only pre-existing deprecation warnings) |
| `cmake --build build -j$(nproc) --target htc_main_app` (T32 uClibc) | exit 0 (links `bin/htc_main_app`; flattens .so symlinks) |
| `grep -nE 'std::to_string\|std::stoi' Misc.cpp MdnsParams.cpp` | empty (both files) |
| `grep -nE 'ntpSyncAndWait'` | decl (Misc.h:41) + def (Misc.cpp:510) + 1 call (main_app.cpp:1110) |
| `grep -nE 'service::(buildMdnsParams\|isMdnsEnabled)' main_app.cpp` | both CMD_MOBILE call sites (main_app.cpp:1264-1265) |
| `trimConfigString` retained in main_app.cpp | yes (def :85, used :145) |
| `getConfiguredPort` retained in main_app.cpp | yes (def :153; used :1230/:1231/:1338) |
| moved-body byte-identity vs HEAD | logic-identical (whitespace-stripped diff empty) for buildMdnsParams / isMdnsEnabled / NTP loop; helpers match modulo `static` + namespace |
| RTSP block untouched | CMD_RTSP_SERVER (main_app.cpp:1333-1352) + CMD_MOBILE RTSP teardown verbatim; no RtspWorkMode files |

Build evidence: `MdnsParams.cpp.o` present in both
`build_sim/src/service/discovery/CMakeFiles/discovery_service.dir/` and
`build/.../discovery_service.dir/`; `bin/htc_main_app` produced in both
build dirs.

## Remaining concerns / handoff to reviewer

- The moved NTP loop uses tabs for indentation (matches `Misc.cpp`'s
  prevailing style); HEAD main_app.cpp used spaces. The reviewer's
  whitespace-normalized diff will be clean; a raw `diff -u` will show
  indent noise only.
- `trimConfigString` now exists in two TUs (main_app.cpp + MdnsParams.cpp).
  Both are file-local; intentional, documented in `MdnsParams.cpp`. A future
  refactor could promote it to a shared header, but that is out of T11 scope.
- RTSP (Lift #3) is deferred to T12 as planned; the CMD_RTSP_SERVER run-loop
  still depends on main_app-private `already_in_exit_flow` /
  `waitForSignalOrTimeout` / `performCleanup`, which T12 must abstract first.
- No `src/hal/**` diff. No CMD_MOBILE RTSP/HTTP/TCP teardown diff.
