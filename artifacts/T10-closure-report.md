---
contract: report
contract_version: "1"
task_id: T10
node: reviewer
flow: feature
status: success
summary: |
  T10 闭环：crc16 隐患根治。把缺失依赖声明在源头 common_utils_crc（CMakeLists 加
  `PRIVATE logger crc16`，与既有 logger 声明方式一致；common_utils_crc 是 SHARED，
  PRIVATE 即让 libcommon_utils_crc.so NEEDED libcrc16.so 并内部解析 cal_crc16），
  并移除 src/manifest/CMakeLists.txt 里的 crc16 workaround 行。
  验证（双平台）：sim + T32 build/link 干净；readelf -d 证实 libcommon_utils_crc.so
  在 build_sim 与 build 均 NEEDED libcrc16.so；libmanifest.so NEEDED libcommon_utils_crc.so
  且不再直接 NEEDED libcrc16.so（改为经 common_utils_crc 传递）；Manifest.cpp 零 cal_crc16
  直接引用。此变更把 T9 登记的 crc16 risk 推荐动作落地（reviewer 当时建议 PUBLIC logger crc16，
  实际用 PRIVATE logger crc16 —— 对 SHARED 库 link 结果等价，且与既有 logger 的 PRIVATE 声明一致、
  语义更准：crc16 是 CRC.cpp 的实现细节，不在 CRC.h 接口）。htc_main_app 仍保留自己的 crc16
  链接行（可能冗余但无害，移除需先确认它不直接用 crc16，超出本任务范围）。本 closure 把 T10 置为 done。
deliverables:
  - src/common/utils/crc/CMakeLists.txt
  - src/manifest/CMakeLists.txt
verification:
  commands:
    - cmake --build build_sim -j$(nproc) --target htc_main_app
    - cmake --build build -j$(nproc) --target htc_main_app
    - readelf -d build/lib/libcommon_utils_crc.so | grep NEEDED
    - readelf -d build/lib/libmanifest.so | grep NEEDED
  evidence_ref: src/common/utils/crc/CMakeLists.txt
state_delta:
  set_task_status:
    T10: done
artifact_path: src/common/utils/crc/CMakeLists.txt
---
