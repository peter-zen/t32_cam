---
contract: report
contract_version: "1"
task_id: T8
node: implementer
flow: feature
status: success
summary: |
  Phase A 文档产出。创建了两份设计文档：
  (1) doc/design/workmode-sdk-architecture.md — 方向文档（问题陈述 + 论点校准 +
  Mermaid 分层图 + A/B/C 路线图 + 7 条约束 + 未决问题）。
  (2) doc/design/workmode-capability-inventory.md — 参考清单（`-wm` 子模式表 + 两处重叠
  callout + 8 项能力 SDK 盘点 + 最大缺口 callout + network 依赖汇聚点注记）。
  并在 doc/knowledge/working-set.md 的活跃任务区新增一行指向架构文档。
  所有 file:line 引用都已对源码逐一核实并修正了 planner 的若干偏移：
  分发解析 1163-1164（非 1158-1161）；SNAP_ONLY/UPLOAD_ONLY/TEST_ONLY/SNAP_UPLOAD/UVC
  case 行 1167/1170/1176/1184/1187；-m 分发 1129/1130，-rs 分发 1145；
  CMD_MOBILE 块 1546、RTSP start 1635/1679、stop 1657/1687、shutdown 1857；
  CMD_AUTH/UPLOAD 1690、上传循环 1716-1840；NTP 等待循环 1418-1450；
  DB init 1003、MediaScanner 1022、EasyLogger 1025-1054、DayNight 1059；
  Settings load 1217、DeviceConfig 1219、SD mount 1224/1237；spawn 点 media_app.cpp:257。
  库类型核实：storage=STATIC（libstorage.a 产物存在）、network SHARED(L3)、
  common_misc SHARED(L20)、media_snap SHARED(L21)、media_rtsp SHARED(L33)、
  discovery_service STATIC(L6)、media_recorder SHARED(L18)+camera_service STATIC(L6)、
  app_workmode SHARED(L16)。
  Mermaid 分层图（apps→编排缝→能力库→HAL）已按 planner §4 的 ASCII 版转换加入架构文档。
  两处重叠（TEST_ONLY(3)≡-m、UVC(4) RTSP≡-rs）与「最大缺口 generateDescInfo」callout 均显式保留。
deliverables:
  - doc/design/workmode-sdk-architecture.md
  - doc/design/workmode-capability-inventory.md
  - doc/knowledge/working-set.md
  - artifacts/T8-implementer-report.md
verification:
  commands:
    - test -f doc/design/workmode-sdk-architecture.md && test -f doc/design/workmode-capability-inventory.md
    - grep -n "WORKING_MODE_SNAP_ONLY" src/app/main_app.cpp                  # cited dispatch: case at :1167
    - grep -n "htc_main_app -wm" src/app/media_app.cpp                         # cited spawn point at :257
    - grep -n "generateDescInfo\|createDescInfoFile" src/app/main_app.cpp      # cited biggest-gap target at :268 / :453
    - grep -n "command & CMD_MOBILE\|command & CMD_RTSP_SERVER" src/app/main_app.cpp  # overlap exec blocks :1546 / :1668
    - grep -n "add_library" src/storage/CMakeLists.txt src/network/CMakeLists.txt src/service/discovery/CMakeLists.txt  # lib types
    - find /home/zengping/project/huntcam/code/t32_cam/build -name 'libstorage.a'      # STATIC proof
    - grep -c "mermaid" doc/design/workmode-sdk-architecture.md                # layering diagram present
    - grep -n "TEST_ONLY(3)" doc/design/workmode-capability-inventory.md       # overlap callout present
    - grep -n "workmode-sdk-architecture.md" doc/knowledge/working-set.md      # discoverability pointer present
  evidence_ref: doc/design/workmode-capability-inventory.md
state_delta:
  set_task_status: {}
artifact_path: doc/design/workmode-capability-inventory.md
---
