---
contract: report
contract_version: "1"
task_id: T8
node: planner
flow: feature
status: success
summary: |
  Phase A (docs + inventory only, no src/ changes). Validated the user thesis
  against source: the "SDK layer" already exists as 8 capability libs
  (common_misc/media_snap/media_recorder/discovery_service/media_rtsp/storage
  /network + camera_service); the real gap is app-inline orchestration fused
  into the 1894-line main_app.cpp. Verified the -wm sub-mode→command-bitmask
  dispatch (main_app.cpp:1158-1202) and ordered execution (:1310-1838),
  flagging the two overlaps: TEST_ONLY(3)≡CMD_MOBILE (same as -m), and the
  UVC(4) RTSP portion≡CMD_RTSP_SERVER (same as -rs). Identified the single
  biggest extraction target: generateDescInfo/createDescInfoFile (:268,:453)
  are static funcs fused to Settings/MCU/DeviceConfig singletons — the only
  capability with NO lib today. Proposed layering (HAL → capability libs →
  new mode-orchestration seam in the existing enum-only app_workmode lib →
  thin apps) plus a behavior-preserving A/B/C roadmap, and a doc outline for
  the two design docs the implementer will write.
deliverables:
  - artifacts/T8-planner-full.md
  - artifacts/T8-planner-report.md
verification:
  commands:
    - "grep -n 'WORKING_MODE_SNAP_ONLY\\|WORKING_MODE_UVC\\|enum workingMode' src/app/workmode/WorkMode.h"
    - "sed -n '1109,1202p' src/app/main_app.cpp   # -wm dispatch + command-bitmap map"
    - "grep -n 'command & CMD_\\|processCmd\\|generateDescInfo\\|createDescInfoFile\\|MdnsService\\|RtspServer\\|connectWifi\\|startDHCP\\|ntpSync' src/app/main_app.cpp"
    - "grep -nE 'add_library' src/common/misc/CMakeLists.txt src/network/CMakeLists.txt src/media/snap/CMakeLists.txt src/media/video/CMakeLists.txt src/media/rtsp/CMakeLists.txt src/service/discovery/CMakeLists.txt src/service/camera/CMakeLists.txt src/storage/CMakeLists.txt src/app/workmode/CMakeLists.txt"
    - "find /home/zengping/project/huntcam/code/t32_cam/build -name 'libstorage*'   # .a => STATIC"
    - "sed -n '257p' src/app/media_app.cpp   # spawn htc_main_app -wm"
    - "sed -n '268,360p' src/app/main_app.cpp   # generateDescInfo fused to singletons"
  evidence_ref: artifacts/T8-planner-full.md
state_delta:
  set_task_status: {}
artifact_path: artifacts/T8-planner-full.md
---
