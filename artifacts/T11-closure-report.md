---
contract: report
contract_version: "1"
task_id: T11
node: reviewer
flow: feature
status: success
summary: |
  T11 Phase B-2 闭环（NTP + mDNS 两处 verbatim 下沉；RTSP 留 T12）。feature flow 全节点
  status=success，C2 audit OK。交付：Misc::ntpSyncAndWait 进 common_misc（RTC 回写留 caller，
  不引入 common_time_rtc 依赖）；buildMdnsParams/isMdnsEnabled（+ file-local trimConfigString/
  getDefault*）进 discovery_service 新增 MdnsParams.{h,cpp}，discovery_service CMake 加 common_misc+
  devconf PUBLIC 依赖（已核实无环）；main_app.cpp 删 4 static、2 处 mDNS 调用加 service:: 前缀、
  保留 trimConfigString+getConfiguredPort。RTSP/CMD_RTSP_SERVER/CMD_MOBILE teardown 原样未动（T12）。
  行为保持：reviewer/tester 独立 whitespace-normalized byte-identical diff vs HEAD 通过；Misc.cpp 的
  spaces→tabs 重排经 diff -w + awk 抽取证实纯排版、既有 ntpSync 体零改动；sim+T32 双平台 link 干净；
  零 std::to_string；src/hal/** 未动。本 closure 把 T11 置为 done。
deliverables:
  - src/common/misc/Misc.h
  - src/common/misc/Misc.cpp
  - src/common/misc/CMakeLists.txt
  - src/service/discovery/MdnsParams.h
  - src/service/discovery/MdnsParams.cpp
  - src/service/discovery/CMakeLists.txt
  - src/app/main_app.cpp
verification:
  commands:
    - orchestrator audit --flow flows/feature.yaml --task T11 --state orchestration-state.yaml
    - cmake --build build_sim -j$(nproc) --target htc_main_app
    - cmake --build build -j$(nproc) --target htc_main_app
  evidence_ref: src/service/discovery/MdnsParams.cpp
state_delta:
  set_task_status:
    T11: done
artifact_path: src/service/discovery/MdnsParams.cpp
---
