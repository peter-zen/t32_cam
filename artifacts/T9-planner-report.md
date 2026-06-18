---
contract: report
contract_version: "1"
task_id: T9
node: planner
flow: feature
status: success
summary: |
  Verbatim-move-first extraction of generateDescInfo/createDescInfoFile
  (main_app.cpp:268-471) plus the file-local helper getFileCreationTime
  (main_app.cpp:83-122, used ONLY by generateDescInfo) into a new SHARED
  library `manifest` at src/manifest/, namespaced `manifest::`, with
  signatures kept identical so call sites change only by prefix.
  Library is SHARED (matches disk/mcu/app_workmode convention; uniform .so
  set, deployment/flatten-symlinks flow unchanged). Public header
  manifest/Manifest.h exposes only the two functions; getFileCreationTime
  stays a file-local static in Manifest.cpp. Behavior-preservation gate =
  (a) both platforms build+link clean, (b) reviewer byte-identical diff of
  the moved bodies vs HEAD, (c) sim produces a valid desc JSON (golden diff
  if a pre-move baseline is captured). Key risks: T32 uClibc has no
  std::to_string/stoi (body already uses to_string_custom exclusively;
  reviewer must grep-confirm zero std::to_string hits); USER_CONFIG_WPWS
  is never defined so the #else branch is the live path (no special
  handling, MCU.h covers both); link common_utils_crc (not crc16) for
  CRC::calculate_crc16; add manifest to BOTH sim+hw app link blocks.
  Singleton decoupling is OUT OF SCOPE -> tracked as T9-followup.
deliverables:
  - artifacts/T9-planner-full.md
  - artifacts/T9-planner-report.md
verification:
  commands:
    - "grep -nE 'generateDescInfo|createDescInfoFile' src/app/main_app.cpp   # call sites at :268,:453,:456,:609,:621,:716,:790"
    - "grep -rn 'getFileCreationTime' src/ --include=*.cpp --include=*.h | grep -v main_app.cpp   # empty => only def is the static at main_app.cpp:83-122, safe to move"
    - "grep -nE 'YEAR_OFFSET|MONTH_OFFSET|DISK_PATHNAME|EC_SUCCESS|EC_OPEN_FILE_FAILED|PTYPE_USB_DONGLE' src/common/Common.h   # all constants live in Common.h"
    - "grep -rn 'USER_CONFIG_WPWS' --include=*.txt --include=*.cmake --include=*.h --include=*.cpp . | grep -v main_app.cpp   # empty => never add_definition'd, #else is live path"
    - "sed -n '83,122p' src/app/main_app.cpp   # getFileCreationTime static body to move verbatim"
    - "sed -n '268,471p' src/app/main_app.cpp  # generateDescInfo + createDescInfoFile bodies to move verbatim"
    - "cat src/app/workmode/CMakeLists.txt     # SHARED-lib convention + structure to mirror for manifest"
    - "grep -nE 'common_utils_crc|crc16|app_workmode' src/app/CMakeLists.txt   # link-list shape + where to insert manifest (sim ~:135, hw ~:288)"
  evidence_ref: artifacts/T9-planner-full.md
state_delta:
  set_task_status: {}
artifact_path: artifacts/T9-planner-full.md
---
