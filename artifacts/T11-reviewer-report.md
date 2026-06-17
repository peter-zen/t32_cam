---
contract: report
contract_version: "1"
task_id: T11
node: reviewer
flow: feature
status: success
summary: |
  T11 APPROVED. Two behavior-preserving verbatim lifts, independently
  re-verified: (1) NTP wait-loop -> Misc::ntpSyncAndWait in common_misc,
  caller keeps RTC writeback + goto main_exit; (2) mDNS builders +
  getDefault* + file-local trimConfigString -> service:: in discovery_service.
  Misc.cpp-cosmetic judgment CONFIRMED: `diff -w <(git show HEAD:src/common/misc/Misc.cpp)
  src/common/misc/Misc.cpp` yields exactly 3 hunks = `<ctime>` include +
  `Common.h` include + the new `ntpSyncAndWait` function; the existing
  `Misc::ntpSync` body is byte-identical (whitespace-agnostic awk-extraction
  diff = IDENTICAL), so NO hidden logic edit in the spaces->tabs re-indent.
  discovery_service deps sound: MdnsParams.cpp in sources, common_misc + devconf
  added PUBLIC, stays STATIC; no link cycle (common_misc links only
  sdk_stub/system_call+crc16+pthread; devconf links only env — neither links
  discovery_service); common_misc gained NO common_time_rtc dep (boundary
  clean). Moved bodies byte-identical (normalized diff exit 0) for all 5 mDNS
  helpers + the NTP loop modulo allowed signature/namespace/goto->return diffs.
  RTSP genuinely untouched: CMD_RTSP_SERVER block raw-diff empty; CMD_MOBILE
  region raw-diff = exactly 2 lines (the service:: prefixes); no RtspWorkMode
  files. Both builds link the moved symbols (T in libcommon_misc.so +
  libdiscovery_service.a on both platforms; app NEEDED libcommon_misc.so +
  libdevconf.so). src/hal untouched, no singleton decoupling, zero
  std::to_string/stoi in moved bodies, trimConfigString duplication commented.
  No follow-ups for T11. RTSP (Lift #3) correctly deferred to T12.
deliverables:
  - artifacts/T11-reviewer-evidence.md
  - artifacts/T11-reviewer-report.md
verification:
  commands:
    - git show HEAD:src/app/main_app.cpp
    - git show HEAD:src/common/misc/Misc.cpp
    - grep -nE 'common_misc|devconf' src/service/discovery/CMakeLists.txt
    - diff -w <(git show HEAD:src/common/misc/Misc.cpp) src/common/misc/Misc.cpp
  evidence_ref: artifacts/T11-reviewer-evidence.md
state_delta:
  set_task_status: {}
artifact_path: artifacts/T11-reviewer-evidence.md
---
