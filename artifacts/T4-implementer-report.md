---
contract: report
contract_version: "1"
task_id: T4
node: implementer
flow: feature
status: success
summary: |
  Loopback 1 修复（回应 tester §7 三个判读准确性瑕疵），改动仅落 tools/mcu_api_test.cpp：
  (1) convertVoltage 纯函数期望 "12.6"→"12.0"（核对 MCU.cpp:1011 整数除法 (126%10)/10=0，
      对 126 实返 "12.0"，小数位恒 0 是 MCU 精度特性，非 API 异常，expect 须如实反映实际输出），
      并在 note 标注。同类核对 convertVersion(10002) 实返 "V10.002"（MCU.cpp:446），表期望正确未改。
  (2) readGps 返 ",,,,,"（非空全逗号）原误判 OK，现 string reader 判定加
      find_first_not_of(", \t\r\n")==npos → REVIEW；正常可打印串（PID/UPID 等）不落此分支不误伤（ID 组复验空串仍走 empty 分支）。
  (3) readSignalCF 范围 [0,6000]→[0,65535]（寄存器 2B 理论域，inventory §3.3 无更精确工程上限）。
  复验：双平台编译（build_sim x86-64 + build MIPS/uclibc）均 exit 0；sim 冒烟 convertVoltage 现 OK
  （前 REVIEW）、readGps 现 REVIEW（前误判 OK）、readSignalCF note 标 [0,65535] 0 仍 REVIEW、
  convertVersion 仍 OK；Phase 1 汇总 OK=16/REVIEW=83/FAIL=0（convertVoltage 与 readGps 在 OK/REVIEW
  间等量对调故计数不变）、Phase 2 全 SKIP、exit 0。硬约束全满足（MCU.{h,cpp} 未改、未碰 src/hal、
  未 rm -rf build、未 commit）。
deliverables:
  - tools/mcu_api_test.cpp
  - tools/CMakeLists.txt
  - tools/README.md
  - CMakeLists.txt
  - artifacts/T4-implementer-evidence.md
  - artifacts/T4-implementer-report.md
verification:
  commands:
    - cmake --build build_sim -j$(nproc) --target htc_mcu_api_test
    - cmake --build build -j$(nproc) --target htc_mcu_api_test
    - cd build_sim && LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH ./bin/htc_mcu_api_test --no-write
    - cd build_sim && LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH ./bin/htc_mcu_api_test --no-write --group ID
    - grep -n '"12.0"' tools/mcu_api_test.cpp
    - grep -n 'readSignalCF' tools/mcu_api_test.cpp
    - grep -n 'find_first_not_of' tools/mcu_api_test.cpp
    - git status --short src/hardware/mcu/MCU.h src/hardware/mcu/MCU.cpp
    - git status --short tools/mcu_api_test.cpp
  evidence_ref: artifacts/T4-implementer-evidence.md
state_delta:
  set_task_status: {}
  add_decision:
    - key: T4-api-enumeration
      value: "MCU.h public API 实测 132 方法（排除 getInstance/ctor/dtor）：read-class 99 调用点（int=81/string=7 含 readGps/uint=1/bool-stub=6/tm=1/pure-fn=2/void=1）+ write-class 33。启动自检横幅固化打印计数，与 inventory §1 的 132 一致。"
    - key: T4-four-level-classification
      value: "四级判定 OK/REVIEW/FAIL/SKIP。read 返回 0 一律 REVIEW（MCU.cpp read 在 iic->read()<=0 时 return 0，与真实 0 不可区分，见 MCU.cpp:107/133/160/476）；非 0 在 inventory §3 范围内 OK、越界 REVIEW；readCDS_DN/readCDS_Value/readVTSAlarm/readSOR_AL/readSOR_UVL 语义 0 合法走 allowZero 通道判 OK；readWorkingMode 返 -1 越界 REVIEW。write 返 false→FAIL、回读不等→FAIL（writeESOR_WID 备注疑似 MCU.cpp:1518 sizeof(int) 溢出 2 字节寄存器）。--no-write/sim 下 write→SKIP。"
    - key: T4-write-safety-gates
      value: "默认 --no-write；--write 开启时 ASCII 警告横幅 + stdin 'yes' 确认（--yes 跳过）；Phase 2 前对所有将写参数 read 快照（内存 map + 落盘 mcu_snapshot_<ts>.json）；--restore <f> 子模式写回原值；安全测试值优先取快照原值（write 原值≈空操作）；sim 下 --write 自动跳过 Phase 2。"
    - key: T4-cmake-integration
      value: "tools/ 目录此前从未接入 CMake（既有 json_stress_test.cpp 自 commit d04f46c 起游离）。本任务顶层 CMakeLists.txt 加 add_subdirectory(tools)（无平台门控，满足双平台编译），tools/CMakeLists.txt 定义 htc_mcu_api_test（link mcu/logger/easylogger/pthread，include src/src/common/utils/string/time-rtc/logger/easylogger）并顺势把既有 json_stress_test 一并纳入（link jsoncpp）。"
    - key: T4-uclibc-portability
      value: "T32 uclibc 工具链（gcc 5.4）<string> 无 std::to_string/std::stoi（与 MCU.cpp 用 to_string_custom/stoi_custom 同因），用本地 toStr/fromStr（snprintf/strtol）替换；未用 std::thread/chrono，去掉对应 include 避免 uclibc 额外 link。"
    - key: T4-loopback1-fixes
      value: "Loopback1 修 tester §7 三处判读瑕疵（仅改 tools/mcu_api_test.cpp）：(1) convertVoltage 期望 12.6→12.0，核对 MCU.cpp:1011 整数除法 (126%10)/10=0 对 126 实返 '12.0'（小数位恒 0 是 MCU 精度特性），expect 如实反映实际输出；同类核对 convertVersion(10002)=V10.002 期望正确未改。(2) readGps ',,,,,' 空定位原误判 OK，string reader 加 find_first_not_of(', \\t\\r\\n')==npos→REVIEW，正常可打印串不误伤。(3) readSignalCF [0,6000]→[0,65535]（2B 寄存器理论域）。双平台编译 exit 0，sim 冒烟 convertVoltage 现 OK/readGps 现 REVIEW/汇总 OK=16 REVIEW=83 FAIL=0 exit 0（两修复在 OK/REVIEW 间等量对调故计数不变）。"
  add_risk:
    - key: T4-hw-unverified
      severity: medium
      description: "双平台编译 + sim 冒烟 + 代码审计已过，但真机 Phase 1（read 实数据 range 分类）、Phase 2（write+回读，定位 writeESOR_WID 已知 bug）、--restore 回路均需在 T32 硬件验证，PC 不能跑 MIPS，留用户执行（见 doc/knowledge/playbooks/mcu-api-test-runbook.md）。"
artifact_path: artifacts/T4-implementer-report.md
next: tester
---

# T4 Implementer Report (Loopback 1)

## 本次改动一句话
Loopback 1 修 tester §7 三个判读准确性瑕疵，改动仅落 `tools/mcu_api_test.cpp`：convertVoltage 期望 `12.6`→`12.0`（贴合 MCU.cpp 整数除法实际输出）、readGps 空定位 `,,,,,` 由误判 OK 改为 REVIEW（加 find_first_not_of 判定，不误伤正常可打印串）、readSignalCF 范围 `[0,6000]`→`[0,65535]`（2B 寄存器理论域）。

## 三处修复明细

### 1. convertVoltage 期望值（中级，必修）
- 核对 MCU.cpp:1011-1016：`snprintf("%d.%d", value/10, (value%10)/10)`，对 126 = `snprintf("%d.%d", 12, 0)` = **`"12.0"`**（`(126%10)/10 = 6/10 = 0` 整数除法）。这是 MCU 精度特性，非 API 异常——expect 须如实反映实际输出。
- 改 `tools/mcu_api_test.cpp:359` 期望 `"12.6"`→`"12.0"`，note 标「MCU integer-div precision: decimal digit always 0」。
- 同类核对 convertVersion（MCU.cpp:446-456）：convertVersion(10002) = major=10/minor=2 → `"V10.002"`，表期望正确**未改**。

### 2. readGps 空定位判定（低）
- 原 string reader 只判 `v.empty()`，readGps 返 `",,,,,"`（非空全逗号）误判 OK。
- 改 `tools/mcu_api_test.cpp:755-760`：`blank = v.empty() || (v.find_first_not_of(", \t\r\n") == npos)` → REVIEW。
- 正常可打印串（PID/UPID/UPWD/DEVICE_NAME/SignalType/FirmwareVersion）不落此分支，不误伤（ID 组复验：空串仍走 empty 分支，note 标 `empty` 而非 `all-separator/blank`）。

### 3. readSignalCF 范围（信息）
- `tools/mcu_api_test.cpp:222`：`0,6000`→`0,65535`，note 标 `2-byte reg [0,65535]`（inventory §3.3 `PARAM_MCU_SIG_CF` 0x10C 2B，无更精确工程上限）。

## 复验结果

| 验证项 | 结果 |
|--------|------|
| build_sim 编译（x86-64）| exit 0 |
| build 编译（MIPS/uclibc）| exit 0 |
| convertVoltage（sim）| 返 `"12.0"` = expect `"12.0"` → **OK**（修复前 REVIEW）|
| readGps（sim）| 返 `",,,,,"` → **REVIEW**（修复前误判 OK），note 标 `all-separator/blank` |
| readSignalCF（sim）| note 标 `[0,65535]`，0 仍 REVIEW（0 规则不变）|
| convertVersion（sim）| 返 `"V10.002"` → OK（期望正确未误伤）|
| Phase 1 汇总 | OK=16/REVIEW=83/FAIL=0（两修复在 OK/REVIEW 间等量对调，计数不变，逻辑自洽）|
| Phase 2（sim）| 全 SKIP=33 |
| exit code | 0 |
| ID 组 string reader 未误伤 | 空串仍判 REVIEW（empty 分支），正常可打印串不落 all-separator 分支 |
| 硬约束 | MCU.{h,cpp} 未改、未碰 src/hal、未 rm -rf build、未 commit |

## 遗留（不变）
- 真机 Phase 1/Phase 2/restore 回路未验证（PC 不能跑 MIPS），见 `doc/knowledge/playbooks/mcu-api-test-runbook.md`。
- writeESOR_WID 已知 MCU.cpp 缺陷只在真机 Phase 2 显现为 FAIL 备注，不在本工具修复（non-goals）。
- 未 git commit（未授权）。

详见 `artifacts/T4-implementer-evidence.md`（§7 为本次 loopback1 修复 + 复验证据）。
