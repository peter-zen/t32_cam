---
contract: report
contract_version: "1"
task_id: T11
node: planner
flow: feature
status: success
summary: |
  Three behavior-preserving lifts planned, each a verbatim move mirroring T9.
  #1 NTP wait-loop -> Misc::ntpSyncAndWait in common_misc (LOW risk; RTC
  writeback stays in caller to avoid adding common_time_rtc dep; goto main_exit
  becomes return false + caller goto). #2 mDNS builders buildMdnsParams +
  isMdnsEnabled (+ the two getDefault* helpers + a copied file-local
  trimConfigString) -> discovery_service namespace service (MEDIUM-LOW; adds
  devconf+common_misc deps to discovery_service, semantically correct home;
  trimConfigString kept in main_app for the scanner helper at :144;
  getConfiguredPort NOT moved - generic). #3 RTSP run-loop -> app_workmode:
  the run-loop while(!already_in_exit_flow){waitForSignalOrTimeout(1000)} is
  FUSED with main_app TU-private signal machinery (already_in_exit_flow :584,
  waitForSignalOrTimeout :622, performCleanup :642) so a full verbatim move is
  IMPOSSIBLE without a signal-abstraction refactor (behavior risk). Plan gives
  Option A (move only register+setPort+start as startRtspServer returning bool;
  caller keeps loop+stop+goto main_exit) as the safe T11 scope, and flags
  Option B (full run-loop move) for split into T12. Behavior-preservation gate:
  byte-identical diff of each moved body vs git show HEAD:src/app/main_app.cpp
  (1894-line baseline) + dual-platform build + CMD_MOBILE region byte-identical
  except the two service:: prefixes. Pre-flight risk: CAMERA_VERSION origin
  (used at :215) must be confirmed by implementer before the mDNS move.
deliverables:
  - artifacts/T11-planner-full.md
  - artifacts/T11-planner-report.md
verification:
  commands:
    - grep -nE 'CMD_NTP|ntpSync|MAX_WAIT_SECONDS|YEAR_MIN|tm_year' src/app/main_app.cpp
    - sed -n '1164,1209p' src/app/main_app.cpp
    - grep -nE 'buildMdnsParams|isMdnsEnabled|getDefaultMdnsInstanceName|getDefaultMdnsHostName|trimConfigString|getConfiguredPort' src/app/main_app.cpp
    - sed -n '84,99p;164,226p' src/app/main_app.cpp
    - sed -n '1423,1444p' src/app/main_app.cpp
    - grep -nE 'already_in_exit_flow|waitForSignalOrTimeout|performCleanup|rtsp_singleton_used|g_signal_pipe' src/app/main_app.cpp
    - sed -n '584,645p' src/app/main_app.cpp
    - cat src/app/workmode/CMakeLists.txt
    - cat src/service/discovery/CMakeLists.txt
    - cat src/common/misc/CMakeLists.txt
    - grep -nE 'static bool ntpSync|getMACAddress' src/common/misc/Misc.h
    - grep -nE 'YEAR_MIN|YEAR_OFFSET|MONTH_OFFSET|INI_KEY_MDNS|INI_SECTION_MDNS|DEFAULT_RTSP_PORT' src/common/Common.h
    - grep -rnE 'kDefaultMdnsDeviceFamily|MdnsServiceParams|MdnsTxtPayload' src/service/discovery
    - grep -rnE 'registerOnsessionClosedCallback|setPort|bool start|bool stop' src/media/rtsp/RtspServer.h
    - git show HEAD:src/app/main_app.cpp | wc -l
    - grep -nE 'manifest|discovery_service|app_workmode|common_misc|media_rtsp' src/app/CMakeLists.txt
  evidence_ref: artifacts/T11-planner-full.md
state_delta:
  set_task_status: {}
artifact_path: artifacts/T11-planner-full.md
---
