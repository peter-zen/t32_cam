---
contract: report
contract_version: "1"
task_id: T12
node: reviewer
flow: feature
status: success
summary: |
  T12 Phase B-3 闭环（RTSP start/run/stop 经 DI 下沉进 app_workmode）。feature flow 全节点
  status=success，C2 audit OK。新增 src/app/workmode/RtspWorkMode.{h,cpp}（namespace app_workmode，
  runRtspServerUntilSignal(port, keepRunning, waitForSignal)），app_workmode 链 media_rtsp；
  main_app CMD_RTSP_SERVER 块改为委托 + 注入两无捕获 lambda（!already_in_exit_flow /
  waitForSignalOrTimeout），保留 getConfiguredPort/rtsp_singleton_used/goto main_exit。
  行为保持（重构非 verbatim）：reviewer/tester 独立复核——helper 体是原序列 1:1 映射
  （register→setPort→start→run-loop→stop，日志串字节相同，start 失败 return false 不 stop、
  成功 stop 一次）；删掉的 4 行是 dead 注释 wifi 代码、无可执行语句丢失；CMD_MOBILE 块
  byte-identical（md5 核对）；信号机制 already_in_exit_flow/waitForSignalOrTimeout/performCleanup/
  g_signal_pipe 仍 static 留 main_app；rtsp_singleton_used 闸门不变；sim+T32 双平台 htc_main_app +
  libapp_workmode（NEEDED libmedia_rtsp.so）link 干净；零 std::to_string；src/hal/** 未动。
  **Phase B 完成**（T9/T10/T11/T12 全 done）。
  另：reviewer 发现一处 PRE-EXISTING（非 T12 引入）同类 latent missing-dep，已登记 risk
  （见 add_risk）：media_rtsp 用 DayNightSwitch 却不链 daynight，htc_media_app sim 链接失败。
deliverables:
  - src/app/workmode/RtspWorkMode.h
  - src/app/workmode/RtspWorkMode.cpp
  - src/app/workmode/CMakeLists.txt
  - src/app/main_app.cpp
verification:
  commands:
    - orchestrator audit --flow flows/feature.yaml --task T12 --state orchestration-state.yaml
    - cmake --build build_sim -j$(nproc) --target htc_main_app
    - cmake --build build -j$(nproc) --target htc_main_app
    - readelf -d build/lib/libapp_workmode.so
  evidence_ref: src/app/workmode/RtspWorkMode.cpp
state_delta:
  add_risk:
    - "media_rtsp (src/media/rtsp/CMakeLists.txt:36) uses DayNightSwitch (RtspServer.cpp:587-595, NOT BUILD_FOR_SIMULATION-gated) but does not link the daynight target. Latent missing-dep masked because htc_main_app links daynight directly; surfaces as a sim (and likely T32) link failure for htc_media_app (undefined DayNightSwitch::* refs). Same class as the crc16/T10 issue. Fix: target_link_libraries(media_rtsp PRIVATE ... daynight) at the source. Pre-existing, not introduced by T9-T12."
  set_task_status:
    T12: done
artifact_path: src/app/workmode/RtspWorkMode.cpp
---
