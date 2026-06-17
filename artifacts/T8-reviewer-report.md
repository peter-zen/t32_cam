---
contract: report
contract_version: "1"
task_id: T8
node: reviewer
flow: feature
status: success
summary: |
  Independent semantic audit of the two Phase A design docs
  (`doc/design/workmode-sdk-architecture.md`,
  `doc/design/workmode-capability-inventory.md`). APPROVE.
  Every material claim re-verified against source:
  (1) all 5 `-wm` sub-mode → command-bitmask maps correct (switch :1166-1195);
  (2) both overlaps confirmed — TEST_ONLY(3) sets CMD_MOBILE @ :1181 same as
  `-m` @ :1130, both enter the CMD_MOBILE block @ :1546; UVC(4) RTSP reaches
  `RtspServer::getInstance()->start()` @ :1679 same as `-rs`;
  (3) all 8 per-capability lib types correct (common_misc/media_snap/
  media_recorder/media_rtsp/network = SHARED; camera_service/discovery_service/
  storage = STATIC); the tester-flagged `storage` vs `storage_service` concern
  is a non-issue — DB capability (DatabaseManager/MetadataDao/MediaScanner)
  is correctly attributed to `storage` (`src/storage/CMakeLists.txt:1-5`),
  not `storage_service`;
  (4) biggest-gap claim fully substantiated — `generateDescInfo` (@ :268) and
  `createDescInfoFile` (@ :453) are app-inline `static` funcs fused to
  Settings/MCU/DeviceConfig + Disk/CRC/Timezone;
  (5) `app_workmode` is enum-only (no execution); `htc_wifi_app` precedent
  grounded; `src/hal/**` PIC-owned per AGENTS.md:142-143.
  Phase-A scope respected: `git status` shows NO `src/` modification.
  Only off-by-N (1-5 line) drift exists in a few line refs, within tolerance,
  no fixes applied. No substantive errors found.
deliverables:
  - artifacts/T8-reviewer-evidence.md
  - artifacts/T8-reviewer-report.md
verification:
  commands:
    - "Read src/app/main_app.cpp:1100-1300 (switch + sub-mode map, :1166-1195)"
    - "Read src/app/main_app.cpp:1540-1700 (CMD_MOBILE :1546, CMD_RTSP_SERVER :1668 blocks)"
    - "Read src/app/main_app.cpp:268-368 (generateDescInfo static, singleton fusion)"
    - "grep -n 'generateDescInfo|createDescInfoFile' src/app/main_app.cpp (:268, :453 static)"
    - "Read src/storage/CMakeLists.txt (add_library(storage ...) STATIC, :1-5) and src/service/storage/CMakeLists.txt (:6 storage_service STATIC — separate)"
    - "Read all 8 capability CMakeLists (common_misc:20, media_snap:21, media_video:18, camera:6, discovery:6, rtsp:33, network:3, workmode:16)"
    - "git status --short (no src/ change)"
    - "grep AGENTS.md src/hal PIC-owned (:142-143); wc -l main_app.cpp = 1894"
  evidence_ref: artifacts/T8-reviewer-evidence.md
state_delta:
  set_task_status: {}
artifact_path: artifacts/T8-reviewer-evidence.md
---
