---
contract: report
contract_version: "1"
task_id: T12
node: reviewer
flow: feature
status: success
summary: |
  APPROVE. T12 lifts the CMD_RTSP_SERVER start/run/stop sequence into
  app_workmode::runRtspServerUntilSignal(port, keepRunning, waitForSignal) with
  full semantic equivalence to the original inline block. Independently
  re-extracted the HEAD block (main_app.cpp:1333-1353) and compared against the
  helper (RtspWorkMode.cpp:9-26) + caller (main_app.cpp:1334-1341): the
  register->setPort->start->run-loop->stop sequence is preserved, both log
  strings are byte-identical, start-failure returns false with NO stop() (caller
  goto main_exit unchanged), stop() runs exactly once on clean exit. media::RtspServer
  is the same symbol as main_app's RtspServer (main_app.cpp:71 `using namespace media`).

  Dead-comment judgment: the 4 removed lines (HEAD 1334-1337) are all //-prefixed
  comments (commented-out wifi_ssid/wifi_pwd/connectWifi/startDHCP) — confirmed
  non-executable; zero executable statements dropped.

  DI soundness: the two injected capture-less lambdas, substituted into
  `while(keepRunning()){waitForSignal(1000);}`, reproduce EXACTLY the original
  `while(!already_in_exit_flow){(void)waitForSignalOrTimeout(1000);}`. std::function
  introduces no behavioral change. rtsp_singleton_used gate intact (set true in
  caller before the call; helper does not touch it; still read at main_exit to
  drive shutdown(); helper calls stop() not shutdown()). app_workmode links
  media_rtsp PUBLIC; libapp_workmode.so NEEDED libmedia_rtsp.so on both platforms
  (readelf). CMD_MOBILE byte-identical (md5 match). src/hal diff empty. Zero
  std::to_string/stoi. T32 cross build 100%; htc_main_app sim build 100%.

  Phase B completion: T12 is the last Phase B piece — T9/T10/T11/T12 all done.

  Follow-up (NOT a T12 regression, out of scope): pre-existing sim-only link
  failure in htc_media_app caused by libmedia_rtsp.so referencing DayNightSwitch
  symbols (RtspServer.cpp:589, not BUILD_FOR_SIMULATION-gated) while
  src/media/rtsp/CMakeLists.txt does not link the daynight target. Predates T12
  (from a9770cb). Recommend a separate task to add daynight to media_rtsp's
  target_link_libraries. Does not block T12 — T12's own artifacts (libapp_workmode.so,
  htc_main_app) link cleanly on both platforms.
deliverables:
  - artifacts/T12-reviewer-evidence.md
  - artifacts/T12-reviewer-report.md
verification:
  commands:
    - git show HEAD:src/app/main_app.cpp
    - readelf -d build/lib/libapp_workmode.so
    - readelf -d build_sim/lib/libapp_workmode.so
    - grep -nE 'runRtspServerUntilSignal|rtsp_singleton_used|shutdown' src/app/main_app.cpp src/app/workmode/RtspWorkMode.cpp
    - cmake --build build -j$(nproc)
    - cmake --build build_sim --target htc_main_app
  evidence_ref: artifacts/T12-reviewer-evidence.md
state_delta:
  set_task_status: {}
artifact_path: artifacts/T12-reviewer-evidence.md
---
