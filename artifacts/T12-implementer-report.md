---
contract: report
contract_version: "1"
task_id: T12
node: implementer
flow: feature
status: success
summary: |
  Phase B-3 RTSP DI lift complete. Created app_workmode::runRtspServerUntilSignal
  in src/app/workmode/RtspWorkMode.{h,cpp} reproducing the CMD_RTSP_SERVER
  register->setPort->start->run-loop->stop sequence with two injected
  capture-less callbacks (keepRunning = !already_in_exit_flow;
  waitForSignal = waitForSignalOrTimeout). CMakeLists.txt now links media_rtsp
  into app_workmode (required; directory-scope include already resolved
  RtspServer.h/Logger.h, explicit ../../media/rtsp added belt-and-suspenders).
  main_app.cpp CMD_RTSP_SERVER block now delegates to the helper; caller keeps
  getConfiguredPort, rtsp_singleton_used=true, and goto main_exit. CMD_MOBILE
  block (mobile_rtsp_enabled path + mDNS/HTTP/TCP teardown) byte-unchanged.
  Both build_sim and T32 build link htc_main_app clean (exit 0); the new symbol
  _ZN12app_workmode24runRtspServerUntilSignalE... is exported from
  libapp_workmode.so on both platforms, proving media_rtsp symbols resolved.
  Behavior-equivalent refactor: helper body is the original sequence 1:1 modulo
  injected callbacks; identical log strings, identical ordering, failure path
  returns false (no stop()), success path calls stop() exactly once.
deliverables:
  - src/app/workmode/RtspWorkMode.h
  - src/app/workmode/RtspWorkMode.cpp
  - src/app/workmode/CMakeLists.txt
  - src/app/main_app.cpp
  - artifacts/T12-implementer-report.md
verification:
  commands:
    - cmake --build build_sim -j$(nproc) --target htc_main_app
    - cmake --build build -j$(nproc) --target htc_main_app
    - grep -nE 'runRtspServerUntilSignal' src/app/main_app.cpp src/app/workmode/RtspWorkMode.cpp
    - grep -nE 'std::to_string|std::stoi' src/app/workmode/RtspWorkMode.cpp
  evidence_ref: src/app/workmode/RtspWorkMode.cpp
state_delta:
  set_task_status: {}
artifact_path: src/app/workmode/RtspWorkMode.cpp
---
