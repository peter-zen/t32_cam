---
contract: report
contract_version: "1"
task_id: T4
node: reviewer
flow: feature
status: success
summary: |
  T4 reviewer 终审（feature flow 末端）。独立读 tools/mcu_api_test.cpp + tools/CMakeLists.txt +
  tools/README.md + 顶层 CMakeLists.txt 全文，逐项核 MCU.h 全量 public API（脚本计 132 = read 99 +
  write 33）、四级判定逻辑、写阶段安全门、loopback1 三处修复、硬约束、附带 json_stress_test 改动。
  结论：无 blocker、无 major，仅有 minor/nit，**判 success（passed）**。
  正确性：API 覆盖零遗漏（易漏点 readRMID/Type/Value/Count、readEventType/ID/Num、
  readESOR_GPSA/GPSL/GPSH/Value、TIMER_2~5、TDS_*、convertVersion/convertVoltage、readAllTestData
  全在表）；四级判定 classifyInt/classifyIntAllowZero/write 回读比对逻辑正确无 off-by-one（范围含等号、
  LO/HI_OPEN 旁路）；convertVoltage(126)→"12.0"、convertVersion(10002)→"V10.002" 核 MCU.cpp:1011/446
  贴合；readGps ",,,,," 空定位走 find_first_not_of→REVIEW 不误伤 ID 组正常串。
  安全：默认 --no-write（main:1169）；--write 有 ASCII 横幅+stdin "yes" 门控（--yes 跳过）；sim 下
  --write 自动跳过 Phase2（main:1262）；无静默写路径；安全值优先取快照原值（write 原值≈空操作）；
  快照 json 合法、key 与 --restore 对齐、--restore 回路实测可解析可写回。
  健壮性：getInstance 不抛异常（call_once+new，IIC::open 失败只返 false）、IIC::read/write 未 open
  返 -1（<=0）→ MCU read 返 0；工具逐个 call try/catch 包裹，单点异常不中断汇总；sim/缺设备不崩。
  回归：顶层 add_subdirectory(tools) 无平台门控（双平台均编）；json_stress_test 附带纳入 link jsoncpp
  两平台 exit 0，未引入对无关第三方的硬耦合风险。
  硬约束复核全满足：git status src/hardware/mcu/MCU.{h,cpp} 空；tools/mcu_api_test.cpp 唯一项目头
  #include "MCU.h"（余皆标准头）；本任务改动均 untracked 未 commit；未碰 src/hal/**。
  独立 sim 冒烟复跑：Phase1 Total=99 OK=16 REVIEW=83 FAIL=0、Phase2 Total=33 全 SKIP、exit 0，
  与 tester report 一致。
  发现（minor/nit，不阻塞 merge）：
  - minor-1（GPS write 回读恒 mismatch，工具已诚实容忍）：MCU.cpp writeGps 写 ESOR_GPSL/GPSA/GPSH
    (0x30D/0x309/0x311) 而 readGps 读 LOCTION_LON/LAT/ELE (0x14/0x18/0x1C)，读写地址不一致 + writeGps
    内部经度纬度都写 PARAM_MCU_ESOR_GPSL（纬度地址 GPSA 被漏写）。真机 writeGps+readGps 回读比对必然
    FAIL。工具 buildWriteSpecs GPS 段 note 已标 "read-back format differs; FAIL tolerated"，属诚实记录，
    非工具 bug；但属 MCU.cpp 设计缺陷，建议在 --write 真机 runbook 中单独提示「writeGps 回读 FAIL 是
    MCU 地址错配，非本工具问题」，避免用户误判。
  - minor-2（GPS 占位安全值在真机会 write 失败）：buildWriteSpecs:638-639 快照空时占位
    "0.0000001,E,0.0000001,N,1"，MCU::writeGps 用 stoi_custom("0.0000001")=0（sscanf %d 截到小数点前=0）
    → *1e7=0 → longitude==0 → return false。即真机无 GPS 快照时该占位必 write 失败（FAIL by write false），
    不会破坏配置（写被拒），但 note 现标 "no-op-ish" 与实际 "write rejected" 不符。建议占位改非零整数串
    （如 "1,E,1,N,1" → stoi=1 → *1e7 不为 0）或 note 改标 "may be rejected if no GPS snapshot"。
    影响：仅 note 准确性，不破坏配置（writeGps 拒 0 值是安全的）。
  - nit-1（nWrite=33 硬编码常量）：main:1220 nWrite 硬编码 33，虽与 buildWriteSpecs 实际输出核对一致，
    但若后续增删 write-spec 需手动同步。建议 buildWriteSpecs({}).size() 动态取（与 read 表同模式）。
  - nit-2（dumpSnapshot JSON 转义不完整）：dumpSnapshot:929-932 只转义双引号/反斜杠，未处理控制字符
    (0x00-0x1F)。PID/UPID/UPWD 来自 I2C 原始字节可能含控制字符，理论上可生成非法 JSON。readGps 串
    与 tm 串无此风险。影响低（--restore 的 jsonGet 解析容错），建议后续补 \uXXXX 转义或过滤控制字符。
deliverables:
  - tools/mcu_api_test.cpp
  - tools/CMakeLists.txt
  - tools/README.md
  - CMakeLists.txt
  - artifacts/T4-reviewer-report.md
verification:
  commands:
    - grep -cE '^\s+(int|string|bool|void|struct tm|unsigned int|static)' src/hardware/mcu/MCU.h
    - sed -n '1005,1020p;444,460p;1510,1535p' src/hardware/mcu/MCU.cpp
    - sed -n '823,1010p' src/hardware/mcu/MCU.cpp
    - grep -n 'PARAM_MCU_ESOR_WID\|PARAM_MCU_ESOR_WS' src/hardware/mcu/MCUParams.h
    - sed -n '16,60p' src/hardware/mcu/MCU.cpp
    - sed -n '33,46p' src/hardware/mcu/IIC.cpp
    - git status --short src/hardware/mcu/MCU.h src/hardware/mcu/MCU.cpp tools/mcu_api_test.cpp tools/CMakeLists.txt tools/README.md CMakeLists.txt
    - cd build_sim && LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH ./bin/htc_mcu_api_test --no-write
  evidence_ref: artifacts/T4-reviewer-report.md
state_delta:
  set_task_status: {}
  add_decision:
    - key: T4-review-verdict
      value: "reviewer 终审判 success（passed），无 blocker/major，仅 minor/nit。API 覆盖 132=99+33 零遗漏；
        四级判定逻辑正确无 off-by-one；写阶段安全门（默认 --no-write / --write 横幅+stdin 确认 / sim 自动跳过 /
        无静默写 / 安全值优先快照原值 / 快照 json + --restore 回路）全部真实非空操作；convertVoltage/readGps/
        readSignalCF 三个 loopback1 修复经独立核 MCU.cpp 贴合；硬约束全满足（MCU.{h,cpp} 未改、只 include MCU.h、
        未 commit、未碰 src/hal）。遗留真机验证项：Phase1 read 实数据 range 分类、Phase2 write+回读（定位
        writeESOR_WID 2B 截断）、--restore 写后回读≈快照、writeGps 地址错配（MCU.cpp 缺陷，工具诚实容忍）。
        附带 json_stress_test 纳入构建评估：合理（避免源文件游离），两平台 link jsoncpp exit 0，未引入对无关
        第三方硬耦合风险。发现的 MCU.cpp writeESOR_WID 缺陷（memcpy sizeof(int)=4 写 2B 寄存器 PARAM_PACK(0x301,2)
        高位截断）在工具 buildWriteSpecs/Phase2 FAIL 备注 :991-993 如实记录、未在本任务误修（non-goals 遵守）。"
    - key: T4-review-findings
      value: "4 项 minor/nit（不阻塞）：minor-1 GPS write 回读恒 mismatch（MCU.cpp writeGps 写 ESOR_GPSL/GPSA/GPSH
        而 readGps 读 LOCTION_LON/LAT/ELE 地址不一致 + writeGps 经纬度都写 ESOR_GPSL 漏写 GPSA，工具 note 已
        标 FAIL tolerated，建议 runbook 单独提示）；minor-2 GPS 占位安全值 0.0000001 在真机 stoi_custom 截为 0 →
        writeGps 拒 0 值失败（note 标 no-op-ish 与 write rejected 不符，建议占位改 1,E,1,N,1 或修 note）；
        nit-1 nWrite=33 硬编码（建议动态取）；nit-2 dumpSnapshot JSON 转义未处理控制字符（PID/UPID/UPWD 含原始
        字节理论可生非法 JSON，readGps/tm 无此风险，影响低）。"
  add_risk:
    - key: T4-hw-unverified
      severity: medium
      description: "reviewer 复跑仅限 sim 冒烟 + 代码审计（PC 不能跑 MIPS）。真机 Phase1（read 实数据 range 分类）、
        Phase2（write+回读，定位 writeESOR_WID 2B 截断）、--restore 回路（写后回读≈快照）需 T32 硬件验证，留用户执行
        （见 doc/knowledge/playbooks/mcu-api-test-runbook.md）。另 writeGps 地址错配在真机会恒显 readback mismatch FAIL，
        非 writeESOR_WID 同类 bug，runbook 应区分说明。"
artifact_path: artifacts/T4-reviewer-report.md
next: audit
---

# T4 Reviewer Report (终审)

## 结论一句话

无 blocker、无 major，仅 4 项 minor/nit（GPS write 回读恒 mismatch 工具已诚实容忍 / GPS 占位安全值真机会被拒 /
nWrite 硬编码 / snapshot JSON 控制字符转义不完整），**判 success（passed）**，可进末端 audit。

## 审查范围（实际读代码）

- `tools/mcu_api_test.cpp`（1329 行，全文）
- `tools/CMakeLists.txt`、`tools/README.md`、顶层 `CMakeLists.txt`
- 参考：`artifacts/T4-planner-full.md`、`T4-implementer-report.md`、`T4-tester-report.md`、
  `T4-tester-evidence.md`、`doc/knowledge/refs/mcu-api-and-register-inventory.md`
- 被测真相源：`src/hardware/mcu/MCU.h`（public 方法脚本计 132）、`MCU.cpp`（convertVoltage:1009 /
  convertVersion:446 / writeESOR_WID:1511 / readGps:823 / writeGps:907 / setDatetime:682 / getDatetime:769）、
  `MCUParams.h`（PARAM_MCU_ESOR_WID=PARAM_PACK(0x301,2)）、`IIC.cpp`（open/read/write 返回码）、
  `StringConvert.h`（stoi_custom/to_string_custom）

## 1. Correctness

### 1.1 API 覆盖（零遗漏）
- `grep -cE` MCU.h public 方法声明 = 132，去重方法名 = 与工具启动自检横幅
  `int=81 str=7 uint=1 bool=6 tm=1 pure=2 void=1 | READ=99 | WRITE=33` 完全吻合。
- 易漏点逐一核到表：readRMID/Type/Value/Count（int 表 237-240）、readEventType/ID/Num（241-243）、
  readESOR_GPSA/GPSL/GPSH（273-275 open-range）、readESOR_Value（uint 表 322）、TIMER_2~5 START/END（286-293）、
  readTDS_CF/TP/BW（298-300）、convertVersion/convertVoltage（pure-fn 表 354/358）、readAllTestData（void 表 368）。

### 1.2 四级判定逻辑（正确，无 off-by-one）
- `classifyInt`（119-126）：v==0→REVIEW；LO_OPEN&&HI_OPEN→非0即OK；`v>=lo && v<=hi`（含等号）→OK；越界→REVIEW。
- `classifyIntAllowZero`（129-134）：范围含等号，0 合法→OK。allowZero 通道 6 个（readCDS_DN/CDS_Value/VTSAlarm/
  SOR_AL/SOR_UVL/CAM_MAXS）正确标记。
- write 回读比对（runPhase2 982-996）：write false→FAIL、readBack!=expect→FAIL、writeESOR_WID 失败追加备注，else OK。

### 1.3 loopback1 三处修复（独立核 MCU.cpp 贴合）
- convertVoltage(126)：MCU.cpp:1011 `snprintf("%d.%d", 126/10, (126%10)/10)` = `snprintf("%d.%d",12,0)` = "12.0"，
  工具 expect "12.0"（359 行）贴合，sim 冒烟 #98 判 OK。
- convertVersion(10002)：MCU.cpp:446 `snprintf("V%02d.%03d",10,2)` = "V10.002"，expect 未被波及。
- readGps `,,,,,`：string reader 加 `find_first_not_of(", \t\r\n")==npos`（759-760）→REVIEW，
  ID 组空串走 empty 分支、正常可打印串不误伤，sim 冒烟 #89 判 REVIEW。
- readSignalCF：范围 [0,65535]（222 行）贴合 PARAM_MCU_SIG_CF 2B 寄存器域，0 仍 REVIEW。

## 2. 安全（写阶段，重点）

| 安全门 | 位置 | 真实性 |
|--------|------|--------|
| 默认 --no-write | main:1169 enableWrite=false | 真实 |
| --write 警告横幅 | printWriteBanner 1133-1143 | 真实（7 行 ASCII）|
| stdin "yes" 确认 | main:1246-1260 fgets+trim+=="yes" | 真实（非 yes 跳过）|
| --yes 跳过提示 | main:1246 if(!assumeYes) | 真实 |
| sim 下 --write 自动跳过 | main:1262 + 1244 `!kSim` 门 | 真实（sim 冒烟确认）|
| 启动快照采集 | collectSnapshot 860-917 | 真实（全部将写参数 read）|
| 快照落盘 json | dumpSnapshot 919-937 | 真实（合法 json + 转义）|
| --restore 回路 | doRestore 1015-1128 | 真实（json 解析/int-string 分类/RTC 串解析）|
| 安全值优先快照原值 | intSpec/strSpec snapInt/snapStr 421-450 | 真实（write 原值≈空操作）|

无「静默改硬件」路径：所有 write 必经 enableWrite && !kSim 门 + 确认门控。

## 3. 边界/健壮性

- getInstance 不抛异常（call_once + new，IIC::open 失败只返 false，IIC.cpp:43）。
- IIC::read/write 未 open 时返 -1（<=0）→ MCU 各 read 返 0。sim 下或 /dev/hc32l13x 不存在时程序不崩，
  read 全 0 判 REVIEW。
- 工具每个 call 用 try/catch 包裹（718-719/738/755/782/800/832/846/977/980），单点异常不中断汇总。
- 独立 sim 冒烟复跑：Phase1 Total=99 OK=16 REVIEW=83 FAIL=0、Phase2 Total=33 全 SKIP、exit 0。

## 4. 回归风险

- 顶层 `add_subdirectory(tools)`（CMakeLists.txt:106）无平台门控，双平台均编，满足 CLAUDE.md 双平台硬约束，
  不影响既有 build（tools target 独立，不进主程序 link 链）。
- json_stress_test 附带纳入（tools/CMakeLists.txt:41-56 link jsoncpp）：合理（避免源文件游离），
  两平台 link libjsoncpp.so exit 0，未引入对无关第三方的硬耦合（jsoncpp 本就是 third_party 既有依赖）。

## 5. 规范契合

- 与 tests/ 风格一致（纯 main、不引 gtest 框架）。
- tools/README.md guide 文件在（满足「不建无 guide 空目录」）。
- 双平台编译（sim + T32 交叉均 exit 0）。
- 不碰 src/hal/**（src/hal/ingenic/* 的 M 是 T2 遗留，非本任务）。
- 不改 MCU.{h,cpp}（git status 空）。

## 6. 硬约束复核

- `git status --short src/hardware/mcu/MCU.h src/hardware/mcu/MCU.cpp` → 空。
- `tools/mcu_api_test.cpp` include：唯一项目头 `#include "MCU.h"`，余皆 cstdio/cstring/string/vector/functional/
  fstream/sstream/iomanip/map 等标准头。
- 本任务改动（tools/ 三文件 + 顶层 CMakeLists.txt add_subdirectory）均 untracked，未 commit。

## 7. 发现（minor/nit，不阻塞 merge）

### minor-1（GPS write 回读恒 mismatch，工具已诚实容忍）
- MCU.cpp:907 writeGps 写 PARAM_MCU_ESOR_GPSL/GPSA/GPSH（0x30D/0x309/0x311），readGps(:823) 读
  PARAM_MCU_LOCTION_LON/LAT/ELE（0x14/0x18/0x1C），读写地址不一致。且 writeGps 内部经度(:963)与纬度(:972)
  都写 PARAM_MCU_ESOR_GPSL（纬度地址 GPSA 被漏写）。
- 后果：真机 writeGps+readGps 回读比对必然 mismatch → FAIL。工具 buildWriteSpecs:643 note 已标
  "read-back format differs; FAIL tolerated"，诚实记录，非工具 bug。
- 建议：--write 真机 runbook 单独提示「writeGps 回读 FAIL 是 MCU.cpp 地址错配，非本工具问题」，避免用户误判。
- 不阻塞 merge（属 MCU.cpp 缺陷，工具如实暴露，符合 non-goals「发现的 MCU 缺陷只记录不修」）。

### minor-2（GPS 占位安全值真机会 write 失败）
- buildWriteSpecs:638-639 快照空时占位 `"0.0000001,E,0.0000001,N,1"`。MCU::writeGps(:931) 用
  stoi_custom("0.0000001")，StringConvert.h:63 sscanf("%d",...) 截到小数点前 = 0 → *1e7 = 0 →
  writeGps:948 `if(longitude==0...) return false`。
- 后果：真机无 GPS 快照时该占位必 write 返 false（FAIL by write false），不破坏配置（写被拒，安全），
  但 note(:643) 现 "no-op-ish" 与实际 "write rejected" 不符。
- 建议：占位改非零整数串（如 "1,E,1,N,1" → stoi=1 → *1e7≠0）或 note 改 "may be rejected if no GPS snapshot"。
- 影响：仅 note 准确性，不破坏配置。

### nit-1（nWrite=33 硬编码）
- main:1220 `long nWrite = 33;` 硬编码常量，与 buildWriteSpecs 实际输出核对一致，但后续增删 write-spec 需手动同步。
- 建议：`buildWriteSpecs({}).size()` 动态取（与 read 表 buildIntReaders().size() 同模式）。

### nit-2（dumpSnapshot JSON 转义不完整）
- dumpSnapshot:929-932 只转义双引号/反斜杠，未处理控制字符（0x00-0x1F）。PID/UPID/UPWD 来自 I2C 原始字节
  理论含控制字符，可生成非法 JSON。readGps 串与 tm 串无此风险。
- 影响：低（--restore 的 jsonGet 解析容错）。建议后续补 \uXXXX 转义或过滤控制字符。

## 8. Must-fix before merge / Nice-to-have

- Must-fix before merge：**无**（无 blocker/major）。
- Nice-to-have：minor-1（runbook 补 writeGps 提示）、minor-2（GPS 占位或 note 修一）、nit-1/nit-2。

## 9. 移交

判 success（passed），可进末端 audit（`orchestrator audit --flow feature --task T4`）。
遗留真机验证项（Phase1 read 实数据 / Phase2 write+回读定位 writeESOR_WID 截断 / --restore 回路 / writeGps 地址错配）
留用户 T32 执行，见 `doc/knowledge/playbooks/mcu-api-test-runbook.md`。
