---
contract: report
contract_version: "1"
task_id: T12
node: planner
flow: feature
status: success
summary: |
  T12 = Phase B-3: lift the full CMD_RTSP_SERVER start/run/stop sequence
  (main_app.cpp:1333-1353) into a new app_workmode::runRtspServerUntilSignal(...)
  via dependency injection (T11 Option B). The run-loop's two dependencies on
  main_app's TU-private signal machinery are injected as capture-less lambdas
  (keepRunning = !already_in_exit_flow; waitForSignal = waitForSignalOrTimeout),
  so the signal machinery (already_in_exit_flow, waitForSignalOrTimeout,
  g_signal_pipe, performCleanup) is NOT moved. Behavior-identical refactor: the
  helper body is the original sequence verbatim modulo the two injected
  callbacks; register/setPort/start/run-loop/stop ordering, log strings, the
  start()-failure goto main_exit path, and the rtsp_singleton_used caller-side
  gate are all preserved. Impacted files: NEW src/app/workmode/RtspWorkMode.{h,cpp};
  EDIT src/app/workmode/CMakeLists.txt (REQUIRED: add media_rtsp to
  target_link_libraries; optional: explicit ../../media/rtsp include dir —
  already satisfied by app directory scope per app_workmode.dir/flags.make);
  EDIT src/app/main_app.cpp (rewrite CMD_RTSP_SERVER block + #include
  RtspWorkMode.h). CMD_MOBILE path, getConfiguredPort, rtsp_singleton_used, and
  src/hal/** all untouched. T11 did NOT implement its Option A, so T12 lifts the
  verbatim original directly. DI (not signal-machinery promotion) is confirmed
  as the right approach: it converts T11's HIGH-risk promotion into a LOW-risk
  behavior-preserving refactor.
deliverables:
  - artifacts/T12-planner-full.md
  - artifacts/T12-planner-report.md
verification:
  commands:
    - grep -nE 'CMD_RTSP_SERVER|rtsp_singleton_used|already_in_exit_flow|waitForSignalOrTimeout|performCleanup|g_signal_pipe|getConfiguredPort' src/app/main_app.cpp
    - grep -nE 'getInstance|registerOnsessionClosedCallback|setPort|bool start|bool stop' src/media/rtsp/RtspServer.h
    - grep -nE 'add_library|target_link_libraries|include_directories' src/app/workmode/CMakeLists.txt
    - find build_sim -name flags.make -path '*workmode*' -exec grep -nE 'media/rtsp|/logger' {} \;
    - grep -nE 'Lift #3|Option A|Option B|defer|T12|HIGH' artifacts/T11-planner-full.md
  evidence_ref: artifacts/T12-planner-full.md
state_delta:
  set_task_status: {}
artifact_path: artifacts/T12-planner-full.md
---
