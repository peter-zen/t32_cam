---
contract: report
contract_version: "1"
task_id: T4
node: tester
flow: feature
status: success
summary: |
  T4 tester loopback1 复测（对 implementer loopback1 三处判读修复的独立复验 + 回归重跑）。
  3 个修复全部确认有效、无回归：
  (1) convertVoltage 期望现是 "12.0"（tools/mcu_api_test.cpp:359，原 "12.6"），核对 MCU.cpp:1011
      实现 snprintf("%d.%d", 126/10, (126%10)/10)=snprintf("%d.%d",12,0)="12.0" 完全贴合；
      sim 冒烟该行现判 OK（上一轮 REVIEW，修复有效）；同类 convertVersion(10002)="V10.002"
      期望未被误改，未波及。
  (2) readGps 空定位现判 REVIEW（tools/mcu_api_test.cpp:759-760 加 find_first_not_of(", \t\r\n")==npos）；
      sim 冒烟 readGps 返 ",,,,," 现 REVIEW（上一轮误判 OK，修复有效）；note 区分 empty 与
      all-separator/blank 两分支；ID 组复验空串走 empty 分支、正常可打印串（PID/UPID/UPWD/
      DEVICE_NAME/SignalType/FirmwareVersion）含非分隔符可打印字符不落 all-separator 分支，未误伤。
  (3) readSignalCF 范围 [0,6000]→[0,65535]（tools/mcu_api_test.cpp:222），贴合 inventory §3.3
      PARAM_MCU_SIG_CF 0x10C 2B 寄存器理论域；sim 冒烟 note 标 [0,65535]，0 仍 REVIEW（0 与 I2C fail
      不可区分规则不变）。
  回归：双平台编译（build_sim x86-64 + build MIPS/uclibc）均 exit 0；sim --no-write 冒烟 Phase1
  99行无崩、汇总 OK=16/REVIEW=83/FAIL=0（两修复在 OK/REVIEW 间等量对调故计数不变，逻辑自洽）、
  Phase2 全 SKIP=33、exit 0；API 覆盖度仍 132=99+33（横幅自检 int=81/str=7/uint=1/bool=6/tm=1/
  pure=2/void=1 | READ=99 | WRITE=33）；四级判定逻辑/写阶段安全门/--restore(不存在文件 exit 2)/
  json_stress_test 附带构建 均未回归；硬约束全满足（MCU.{h,cpp} 未改、只 include MCU.h、未 commit、
  未碰 src/hal、未 rm -rf build）。判 success，移交 reviewer。
deliverables:
  - artifacts/T4-tester-evidence.md
  - doc/knowledge/playbooks/mcu-api-test-runbook.md
  - artifacts/T4-tester-report.md
verification:
  commands:
    - cmake --build build_sim -j$(nproc) --target htc_mcu_api_test
    - cmake --build build -j$(nproc) --target htc_mcu_api_test
    - file build/bin/htc_mcu_api_test build_sim/bin/htc_mcu_api_test
    - cd build_sim && LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH ./bin/htc_mcu_api_test --no-write
    - cd build_sim && LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH ./bin/htc_mcu_api_test --no-write --group ID
    - cd build_sim && LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH ./bin/htc_mcu_api_test --restore /tmp/nonexistent_snap.json
    - cd build_sim && LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH ./bin/json_stress_test
    - grep -n 'convertVoltage\|find_first_not_of\|readSignalCF\|"12\.0"' tools/mcu_api_test.cpp
    - sed -n '1011,1016p' src/hardware/mcu/MCU.cpp
    - git status --short src/hardware/mcu/MCU.h src/hardware/mcu/MCU.cpp tools/mcu_api_test.cpp tools/CMakeLists.txt
    - grep -nE '^\s*#include' tools/mcu_api_test.cpp
  evidence_ref: artifacts/T4-tester-evidence.md
state_delta:
  set_task_status: {}
  add_decision:
    - key: T4-loopback1-retest
      value: "tester 独立复测 implementer loopback1 三处判读修复，全部确认有效无回归：(1) convertVoltage 期望 12.6→12.0 贴合 MCU.cpp:1011 整数除法 (126%10)/10=0 对 126 实返 '12.0'，sim 冒烟该行 REVIEW→OK；convertVersion(10002)='V10.002' 期望未波及。(2) readGps ',,,,,' 加 find_first_not_of(', \\t\\r\\n')==npos 现 REVIEW（原误判 OK），note 区分 empty/all-separator 两分支，ID 组空串走 empty 分支、正常可打印串不误伤。(3) readSignalCF [0,6000]→[0,65535] 贴合 2B 寄存器域，0 仍 REVIEW。双平台编译 exit 0、sim 冒烟 OK=16/REVIEW=83/FAIL=0 exit 0、API 覆盖度 132=99+33、四级判定/安全门/--restore exit 2/json_stress_test 均未回归，硬约束全满足。判 success。"
    - key: T4-loopback1-fixes-confirmed
      value: "上一轮 tester 标 failed 的 3 个判读准确性瑕疵经 implementer loopback1 修复后 tester 独立复测确认全部解决：convertVoltage 真机不再恒 REVIEW 假阳性、readGps 无定位不再误判 OK、readSignalCF 范围不再误窄。回归项（编译/sim 冒烟/覆盖度/四级判定/安全门/--restore/json_stress_test/硬约束）全部未回归。"
  add_risk:
    - key: T4-hw-unverified
      severity: medium
      description: "双平台编译 + sim 冒烟 + 代码审计 + loopback1 复测均过，但真机 Phase 1（read 实数据 range 分类，含修复后的 convertVoltage/readGps/readSignalCF 判读）、Phase 2（write+回读，定位 writeESOR_WID 截断）、--restore 回路（写后 restore 再 Phase 1 读回应≈快照）仍需 T32 硬件验证，PC 跑不了 MIPS，留用户执行（见 doc/knowledge/playbooks/mcu-api-test-runbook.md）。"
artifact_path: artifacts/T4-tester-report.md
next: reviewer
---

# T4 Tester Report (Loopback 1 复测)

## 结论一句话

implementer loopback1 三处判读修复（convertVoltage expect/readGps 空定位/readSignalCF 范围）
经 tester 独立重跑确认全部有效、无回归；双平台编译 exit 0、sim 冒烟 FAIL=0 exit 0、
覆盖度仍 132、四级判定/安全门/--restore/json_stress_test 均未回归，硬约束全满足，判 success。

## 三个修复确认

| 修复点 | 修复前（上一轮）| 修复后（本轮独立验证）| 结论 |
|--------|----------------|---------------------|------|
| convertVoltage expect（中级必修）| expect "12.6" vs 实际 "12.0" → REVIEW | expect "12.0"（tools/mcu_api_test.cpp:359），核对 MCU.cpp:1011 整数除法 (126%10)/10=0 实返 "12.0" 贴合 → **OK** | 已修 |
| readGps 空定位（低）| ",,,,," 非空 → 误判 OK | find_first_not_of(", \t\r\n")==npos → REVIEW（tools/mcu_api_test.cpp:759-760），note all-separator/blank | 已修 |
| readSignalCF 范围（信息）| [0,6000] 偏窄 | [0,65535]（tools/mcu_api_test.cpp:222），贴合 2B 寄存器域，0 仍 REVIEW | 已修 |

## 未误伤确认

- ID 组 string reader（readPID/UPID/UPWD/DEVICE_NAME）sim 下返空串走 `empty` 分支（note 标 empty），
  与 readGps 的 `all-separator/blank` 分支区分正确；正常可打印串含非分隔符可打印字符不落新分支。
- convertVersion(10002)="V10.002" 期望未被波及，sim 冒烟仍 OK。

## 回归确认

| 验证项 | 结果 |
|--------|------|
| build_sim 编译 | exit 0（SIM_BUILD_RC=0）|
| build（T32）编译 | exit 0（T32_BUILD_RC=0）|
| 二进制类型 | build/=MIPS32/uclibc, build_sim/=x86-64 |
| sim --no-write 冒烟 | Phase1 99行无崩, Phase2 全SKIP, exit 0（SMOKE_RC=0）|
| 汇总 | OK=16/REVIEW=83/FAIL=0（两修复等量对调，计数不变，逻辑自洽）|
| API 覆盖度 | 132=99+33（横幅自检真实计数）|
| 四级判定逻辑 | convertVoltage OK/readGps REVIEW/readSignalCF 0 REVIEW 均符合 |
| --restore 不存在文件 | exit 2（RESTORE_EXIT=2）|
| json_stress_test 附带 | 两平台 link libjsoncpp.so, sim 冒烟 exit 0 |
| 硬约束 | MCU.{h,cpp} 未改/只 include MCU.h/未 commit/未碰 src/hal/未 rm -rf build |

## 遗留（不变，留真机）

- T32 真机 Phase 1（read 实数据 + range 分类，含修复后的三个判读）
- T32 真机 Phase 2（--write 写+回读，定位 writeESOR_WID 截断 bug）
- T32 真机 --restore 回路（写后 restore 再 Phase 1 读回应≈快照）

PC 跑不了 MIPS，见 `doc/knowledge/playbooks/mcu-api-test-runbook.md`。

详见 `artifacts/T4-tester-evidence.md` §12（loopback1 复测完整证据）。
