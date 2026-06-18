---
contract: report
contract_version: "1"
task_id: T13
node: reviewer
flow: feature
status: success
summary: |
  T13 闭环：daynight latent missing-dep 根治。把缺失依赖声明在源头 media_rtsp
  （src/media/rtsp/CMakeLists.txt:36 的 target_link_libraries 加 daynight；daynight 的
  include 目录本就在）。media_rtsp 是 SHARED，PRIVATE 即让 libmedia_rtsp.so NEEDED
  libdaynight.so 并内部解析 DayNightSwitch::*（RtspServer.cpp:587-595 用、未 sim-gate）。
  验证（双平台）：htc_media_app sim（此前 link 失败）+ T32 现 build/link OK；htc_main_app sim
  回归 OK；readelf 证实 build_sim 与 build 的 libmedia_rtsp.so 均 NEEDED libdaynight.so。
  此变更落地了 T12 登记的 daynight risk 推荐动作，并修复了 pre-existing 的 htc_media_app
  链接断裂（非 T9-T12 引入）。与 T10 crc16 同型同治（declare dep at source）。本 closure 把 T13 置为 done。
deliverables:
  - src/media/rtsp/CMakeLists.txt
verification:
  commands:
    - cmake --build build_sim -j$(nproc) --target htc_media_app
    - cmake --build build -j$(nproc) --target htc_media_app
    - cmake --build build_sim -j$(nproc) --target htc_main_app
    - readelf -d build/lib/libmedia_rtsp.so
  evidence_ref: src/media/rtsp/CMakeLists.txt
state_delta:
  set_task_status:
    T13: done
artifact_path: src/media/rtsp/CMakeLists.txt
---
