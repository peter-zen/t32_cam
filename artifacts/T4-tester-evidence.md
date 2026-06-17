# T4 Tester — 独立复现与代码审计证据

> 任务 T4（feature flow, tester 节点）：验证 `htc_mcu_api_test`。
> 平台约束：真机数据只能在 T32 上验证（PC 跑不了 MIPS）。本节点做：双平台编译 + sim 冒烟 +
> 代码逻辑审计（覆盖度/范围/四级判定/安全门真实性）+ --restore 回路 + 产出真机判读手册。
> 证据采集日 2026-06-16，基线 commit `3d854ab` + 本任务改动（未 commit）。
> 所有命令由 tester 独立重跑，未只信 implementer report。

---

## 1. API 覆盖度审计（重点）—— 脚本化逐项核对

用 Python 脚本从 `MCU.h` 正则提取全部 public 方法（排除 getInstance/ctor/dtor/成员变量），
再逐个核对其是否在 `tools/mcu_api_test.cpp` 出现调用点。

**MCU.h public 方法总数（脚本自动数出）= 132**，与 inventory §1 一致：

```
total methods (excl getInstance): 132
by type: {'int': 81, 'bool': 39, 'std::string': 9, 'struct tm': 1, 'unsigned int': 1, 'void': 1}
READ-class: 99
WRITE-class: 33
MISSING read-class (not referenced in test): []
MISSING write-class (not referenced in test): []
```

**结论：132 = 99 read-class + 33 write-class，零遗漏。**

read-class 99 拆分（与程序启动自检横幅一致）：
- int reader 表 81
- string reader 表 7（readFirmwareVersion/readSignalType/readPID/readUPID/readUPWD/readDEVICE_NAME/readGps）
- uint reader 表 1（readESOR_Value）
- bool-stub 表 6（powerEnoughForFirmwareUpdate/waitFor/IsWifiStationReady/Is4gExist/IsRemoteWakeup/useGpsTime）
- tm 表 1（getDatetime）
- pure-fn 表 2（convertVersion/convertVoltage）
- void 表 1（readAllTestData）

write-class 33 拆分：ID(4: writePID/UPID/UPWD/DEVICE_NAME) + Wakeup(1: writeRemoteWakeup) +
ESOR(2: writeESOR_WS/WID) + PIR(4: writePIR_MODE/SENS/INT/EN) + Timer(13: writeTIMER/TIMER_INT/
1..5START/1..5END/REPEATS) + Policy(7: writeCAM_MAXS/HEARTRATE/UP_MODE/UP_NUFQ/TDS_CF/TP/BW) +
RTC(1: setDatetime) + GPS(1: writeGps) = **4+1+2+4+13+7+1+1 = 33**。

**易漏点核对（全部覆盖）**：
- readRMID/RMType/RMValue/RMCount —— int 表 #23-26，✅
- readEventType/EventID/EventNum —— int 表 #27-29，✅
- readESOR_GPSA/GPSL/GPSH —— int 表 #55-57（open-range），✅
- readESOR_Value —— uint 表 #82，✅
- readTIMER_2~5 START/END —— int 表 #67-74，✅（TIMER_1 在 #65-66）
- readTDS_CF/TP/BW —— int 表 #79-81，✅
- readUP_MODE/NUFQ —— int 表 #77-78，✅
- convertVersion/convertVoltage —— pure-fn 表 #97-98，✅
- readAllTestData —— void 表 #99，✅
- bool 桩 6 个 —— bool-stub 表 #90-95，✅

**启动自检计数真实性**：程序 main 在 :1200-1219 固化打印
`API self-check: int=81 str=7 uint=1 bool=6 tm=1 pure=2 void=1 | READ total=99 | WRITE=33`
与脚本核对一致，且与 inventory §1 的 132 一致。自检是真实计数（`buildXxxReaders().size()`），
不是硬编码（除 nWrite=33 是硬编码常量，但与实际 buildWriteSpecs 输出条目数核对一致）。

---

## 2. 双平台编译（独立重跑，均 exit 0）

### 2.1 PC sim（build_sim/，gcc x86-64）

```
$ cmake --build build_sim -j$(nproc) --target htc_mcu_api_test
[ 50%] Built target easylogger
[100%] Built target logger
[100%] Built target mcu
[100%] Built target htc_mcu_api_test
SIM_BUILD_RC=0
```

### 2.2 T32 交叉编译（build/，mips-linux-uclibc-gnu-gcc 5.4）

```
$ cmake --build build -j$(nproc) --target htc_mcu_api_test
[ 33%] Built target easylogger
[ 66%] Built target logger
[100%] Built target mcu
[100%] Built target htc_mcu_api_test
T32_BUILD_RC=0
```

**两平台均 exit 0，无 warning/error。**

---

## 3. 二进制类型核对

```
$ file build/bin/htc_mcu_api_test build_sim/bin/htc_mcu_api_test
build/bin/htc_mcu_api_test:     ELF 32-bit LSB executable, MIPS, MIPS32 rel2 version 1 (SYSV),
                                dynamically linked, interpreter /lib/ld-uClibc.so.0, stripped
build_sim/bin/htc_mcu_api_test: ELF 64-bit LSB pie executable, x86-64, version 1 (SYSV),
                                dynamically linked, interpreter /lib/ld64-linux-x86-64.so.2
```

- `build/`（T32）：MIPS32 / uclibc（`ld-uClibc.so.0`），✅ 真机可运行
- `build_sim/`（PC）：x86-64，✅ 本地冒烟

---

## 4. sim 冒烟（--no-write）

```
$ cd build_sim && LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH ./bin/htc_mcu_api_test --no-write
================================================================
 htc_mcu_api_test -- MCU API sweep
 platform: SIM (I2C bypassed, reads NOT meaningful, all 0)
 write phase: disabled (--no-write)
================================================================
>>> SIM MODE -- I2C bypassed; read data is NOT meaningful (all 0 -> REVIEW).
>>> This run only verifies: compilation / table enumeration / call path no-crash.

API self-check: int=81 str=7 uint=1 bool=6 tm=1 pure=2 void=1 | READ total=99 | WRITE=33
MCU.h public API (excl getInstance/ctor/dtor): read-class + write-class = 132 methods enumerated.
=== Phase 1: READ API sweep ===
 ...（99 行 read 全部调用，无崩溃）
 33 | System  | readWorkingMode      | -    |            -1 | REVIEW  | ...; out of range   ← MCU I2C fail 返 -1，越界 REVIEW，正确
 35 | Base    | readCDS_DN           | -    |             0 | OK      | ...; in range (0 allowed)   ← allowZero 通道
 43 | SOR     | readSOR_AL           | -    |             0 | OK      | ...; in range (0 allowed)
 44 | SOR     | readSOR_UVL          | -    |             0 | OK      | ...; in range (0 allowed)
 89 | GPS     | readGps              | -    | ",,,,,"       | OK      | lon,dir,lat,dir,ele composite  ← ⚠ 见 §7 问题 1
 98 | Version | convertVoltage       | in   | "12.0"        | REVIEW  | expect="12.6"          ← ⚠ 见 §7 问题 2
 99 | Test    | readAllTestData      | -    |        (void) | OK      |
=== Phase 2: WRITE API sweep SKIPPED (--no-write or SIM) ===
 ...（33 行 write 全 SKIP）
=== SUMMARY ===
Phase 1 (read):  Total=99  OK=16  REVIEW=83  FAIL=0  SKIP=0
Phase 2 (write): Total=33  OK=0  REVIEW=0  FAIL=0  SKIP=33  (skipped)
Overall exit code: 0
SMOKE_RC=0
```

**结论：Phase 1 跑完 99 read 无崩溃、打印逐行表+汇总、Phase 2 全 SKIP、exit 0。符合预期。**

### 4.1 其它 flag 行为

| 命令 | 行为 | RC |
|------|------|----|
| `--help` | 打印完整用法（7 flag） | 0 |
| `--no-write --group Signal` | 只跑 Signal 组 9 行，Total=9 | 0 |
| `--write --yes`（sim） | 打印 "SIM MODE: --write requested but Phase 2 skipped"，Phase 2 全 SKIP，不触发门控/快照 | 0 |
| `--no-write --group ZZZNope`（不存在组） | 不崩，Total=0/0/0/0/0，exit 0 | 0 |
| `--restore /tmp/nonexistent_snap.json` | "ERROR: cannot open snapshot"，exit **2** | 2 |

### 4.2 --restore json 回路（构造快照验证解析）

构造 `/tmp/test_snap.json`（含 12 个 key），sim 下跑 `--restore`：
- 找到 key 的（writePID/writeESOR_WID/writeHEARTRATE/setDatetime...）→ sim I2C bypass write 返 false → FAIL
- 没找到的 key（writePIR_SENS/写TIMER_*...）→ "no snapshot value; skip" → SKIP
- RTC 解析 `2026-06-16 12:00:00` 正确（setDatetime 调用路径走通）
- SUMMARY: Total=32 OK=1 FAIL=10 SKIP=21，exit 1（FAIL>0）

**证明 --restore 的 json 读取、key 匹配、int/string 分类、RTC 串解析回路都真实工作。**
（sim 下 write 必然 FAIL 是因 I2C bypass，真机 write 返 true 时会判 OK。）

---

## 5. 范围正确性抽查（19 个 reader 对照 inventory §3）

脚本核对 reader 表 lo/hi 是否覆盖 inventory 期望域：

| API | 表[lo,hi] | allow0 | inventory[lo,hi] | 结论 |
|---|---|---|---|---|
| readSignalRSSI/RSRP/RSRQ | [-200,40] | F | [-200,40] | OK（负值范围正确）|
| readSignalSNR | [-50,50] | F | [-50,50] | OK |
| readSignalTP | [0,50] | F | [0,50] | OK |
| readSignalRL | [0,32767] | F | [0,32767] | OK |
| **readSignalCF** | **[0,6000]** | F | [0,65535] | **偏窄（见下）** |
| readTemperature | [-40,125] | F | -40~85 | OK（表放宽到 125 含 MCU -125 offset）|
| readSOR_UVL | [0,15] | T | [0,15] | OK |
| readSOR_AL | [0,65535] | T | [0,65535] | OK |
| readSOR_O2 | [0,100] | F | [0,100] | OK |
| readESOR_GPSA/GPSL/GPSH | INT_MIN~INT_MAX | F | open | OK（÷1e7 open-range，0→REVIEW 保留）|
| readBattery1Voltage | [0,255] | F | 1B 0~255 | OK |
| readHEARTRATE | [0,16777215] | F | 3B | OK |
| readTIMER_2START | [0,65535] | F | 2B | OK |
| readRMCount | [0,255] | F | 1B | OK |
| readEventNum | [0,65535] | F | 2B | OK |

**19 个里 18 个匹配，1 个偏差（readSignalCF）**：表填 `[0,6000]`，inventory §3.3 该寄存器是 2 字节
（`PARAM_MCU_SIG_CF` 0x10C, 2B），理论域 `[0,65535]`。6000 是"常见中心频率上限"的工程估计，
不是瞎编，但**偏保守**：真机上 CF > 6000 的合法值会被误判 REVIEW。属于可接受偏差，建议后续放宽到 65535。

**信号负值范围、温度 -40、SOR_UVL 0-15、GPSA/L/H ÷1e7 open-range** 这些高风险点全部正确，无瞎编。

---

## 6. 四级判定逻辑审计

### 6.1 read 判定（classifyInt / classifyIntAllowZero）

代码 :119-134：
- `classifyInt`：v==0 → REVIEW（0 与 I2C fail 不可区分）；lo/hi 是 OPEN → 非 0 即 OK；范围内 → OK；越界 → REVIEW。✅
- `classifyIntAllowZero`：范围内（含 0）→ OK；越界 → REVIEW。✅
- allowZero 通道 5 个：readCDS_DN、readCDS_Value、readVTSAlarm、readSOR_AL、readSOR_UVL、readCAM_MAXS
  （表里 `true` 标记，sim 冒烟确认这 6 个 0 值判 OK）。✅

**readWorkingMode 特例**：表填 [0,4]，sim 下 MCU I2C fail 返 -1，-1<0 越界 → REVIEW。sim 冒烟 #33 确认。✅

### 6.2 write 判定（runPhase2 :970-984）

- write 返 false → FAIL（"; write() returned false"）。✅
- write 返 true 后 readBack != expect → FAIL（"; readback mismatch"）。✅
- writeESOR_WID 失败时追加备注（:979-981）。✅
- 否则 OK。✅

### 6.3 string/uint/tm/bool/pure 判定

- string：空 → REVIEW，非空 → OK（:752）。⚠ readGps 的 `,,,,,` 误判 OK，见 §7 问题 1。
- uint：0 → REVIEW，非 0 → OK（:736）。✅
- tm：6 字段全合法 → OK，否则 REVIEW（:794-804）。✅
- bool-stub：调用不崩即 OK（:772）。✅
- pure-fn：输出 == expect → OK，否则 REVIEW（:821）。⚠ convertVoltage expect 错，见 §7 问题 2。

---

## 7. 发现的问题（需 implementer 关注，不阻塞双平台编译/sim 冒烟）

### 问题 1（低，判读准确性）：readGps 空串判定不严

string reader 只判 `v.empty()`。sim 下 readGps 返 `",,,,,"`（非空但全逗号）被判 OK。
真机上 GPS 无定位时 MCU 也可能返 `",,,,,"`，会被误判"有数据"。
**建议**：string 判定加 `v.find_first_not_of(',') != npos` 或对 readGps 单独处理。
**影响**：仅判读准确性，不影响真机 write 安全。

### 问题 2（中，纯函数期望值标错 → 真机恒误报 REVIEW）：convertVoltage

表 :356 期望 `convertVoltage(126) == "12.6"`，但 MCU.cpp:1011 实现是
`snprintf("%d.%d", 126/10, (126%10)/10)` = `snprintf("%d.%d", 12, 0)` = **`"12.0"`**
（`(126%10)/10 = 6/10 = 0`，整数除法吃掉第二位小数）。

sim 冒烟确认实际输出 `"12.0"`，被判 REVIEW（≠ 期望 "12.6"）。
**这是工具的 expect 标错，不是 MCU 行为异常**（MCU 的 convertVoltage 本身确实把第二位小数截掉了，
那是 MCU.cpp 的精度缺陷，但工具的 expect 必须如实反映实际输出）。
**影响**：真机上 convertVoltage 永远判 REVIEW（恒误报），污染汇总。
**建议**：expect 改为 `"12.0"`，或在 note 标注"MCU convertVoltage 第二位小数恒 0（MCU.cpp 精度缺陷）"。

### 问题 3（信息，范围偏保守）：readSignalCF

表填 [0,6000]，inventory 2 字节理论域 [0,65535]。见 §5。建议放宽。

---

## 8. 写阶段安全门真实性审计（全部真实，非空操作）

| 安全门 | 代码位置 | 真实性 |
|--------|----------|--------|
| 默认 --no-write | main :1157 `enableWrite=false` | ✅ 默认关闭 |
| --write 警告横幅 | printWriteBanner :1121-1131 | ✅ 真实打印 7 行 ASCII 警告 |
| stdin 'yes' 确认 | main :1234-1248（fgets+trim+=="yes"）| ✅ 非 yes 跳过 Phase 2 |
| --yes 跳过提示 | main :1234 `if (!assumeYes)` | ✅ |
| sim 下 --write 自动跳过 | main :1250-1254 + :1232 `!kSim` 门 | ✅ sim 冒烟确认 |
| 启动快照采集 | collectSnapshot :848-905（对所有将写参数 read）| ✅ |
| 快照落盘 json | dumpSnapshot :907-925（合法 json，含转义）| ✅ |
| --restore 读 json 写回 | doRestore :1003-1116 | ✅ §4.2 回路验证 |
| **安全值优先快照原值** | intSpec/strSpec 的 snapInt/snapStr :396-446 | ✅ **关键**：write(原值)≈空操作 |

**安全值策略核对**（这是避免破坏配置的关键）：
- `intSpec`/`strSpec` 都先 `snapInt`/`snapStr` 取快照原值，失败/空才回退 defVal（保守默认 0 / "TEST_*"）。
- 真机上写回的值 = 启动时读到的原值，相当于空操作，最不易破坏配置。✅
- RTC setDatetime 安全值取 getDatetime 快照（写回≈不变）。✅
- GPS writeGps 安全值取 readGps 快照，空则用占位 `"0.0000001,E,0.0000001,N,1"`。✅

### writeESOR_WID 已知 bug 定位真实性

MCU.cpp:1518 `writeESOR_WID` 确认有 `memcpy(buf, &wid, sizeof(wid))`（拷 4 字节 int）
但寄存器 `PARAM_MCU_ESOR_WID` 只有 2 字节（`PARAM_PACK(0x301, 2)`），`iic->write(reg, buf, nbytes=2)`
只写前 2 字节。wid > 65535 时高 16 位被截断。工具备注 :979-981 在 FAIL 时追加提示，
定位方向正确（会显现为写 >65535 后回读被截断 → readback mismatch FAIL）。
注：planner report 说的"溢出到相邻寄存器"不太准确——实际是"只写 2 字节、高位丢弃"，
不会污染相邻 ESOR_ADD（0x303），但 >65535 截断 bug 真实存在。

---

## 9. 附带改动审查（json_stress_test）

implementer 把既有游离的 `tools/json_stress_test.cpp` 一并纳入 CMake（link jsoncpp）。

- 两平台编译：`build/bin/json_stress_test`（MIPS）+ `build_sim/bin/json_stress_test`（x86-64）均生成，exit 0。
- jsoncpp 链接：`build/lib/libjsoncpp.so` + `build_sim/lib/libjsoncpp.so` 均存在，
  `ldd` 确认 json_stress_test 动态链接 `libjsoncpp.so`。✅
- sim 冒烟：`json_stress_test: built 1 JSON(s)... wrote 482 bytes`，exit 0。✅

**结论：附带改动 OK，未引入新问题。** json_stress_test 是纯用户态 jsoncpp 压测（与 MCU 无关），
纳入构建合理（避免目录接入但源文件游离）。建议保留。

---

## 10. 硬约束复核

| 约束 | 状态 | 证据 |
|------|------|------|
| 不改 `src/hardware/mcu/MCU.{h,cpp}` | ✅ | `git status --short src/hardware/mcu/MCU.h src/hardware/mcu/MCU.cpp` 空 |
| 不碰 `src/hal/**`（本任务）| ✅ | `src/hal/ingenic/*` 的 M 是 T2 遗留，非本任务 |
| `tools/mcu_api_test.cpp` 只 include MCU.h | ✅ | grep 确认唯一项目头是 `#include "MCU.h"`，余皆标准头 |
| 双平台编译 exit 0 | ✅ | §2 |
| 绝不 `rm -rf build/` | ✅ | 全程增量编译，未删 build/ |
| 未 git commit | ✅ | 本任务改动均 unstaged |

---

## 11. 覆盖缺口 / 残余风险

**本节点已覆盖**：
- 双平台编译（sim + T32 交叉）✅
- sim 冒烟（--no-write/--write/--group/--help/--restore）✅
- API 覆盖度逐项核对（132 = 99+33，零遗漏）✅
- 范围正确性抽查（18/19 匹配，1 偏窄）✅
- 四级判定逻辑审计 ✅
- 写阶段安全门真实性审计 ✅
- --restore json 回路 ✅
- json_stress_test 附带改动 ✅

**未覆盖（留真机，PC 跑不了 MIPS）**：
- T32 真机 Phase 1：read 实数据 + range 分类是否合理（sim 下全 0 无法验）
- T32 真机 Phase 2：--write 写+回读，定位 writeESOR_WID 截断 bug 的实际显现
- T32 真机 --restore 回路：写后 restore 再 Phase 1 读回应≈快照
- /dev/hc32l13x 权限/不存在时的健壮性（sim 下 I2C bypass 不触发真 open）

**判读准确性遗留**（建议 implementer 修，见 §7）：
- 问题 2（convertVoltage expect 标错）会导致真机纯函数恒 REVIEW，**建议修**
- 问题 1（readGps `,,,,,` 误判 OK）、问题 3（readSignalCF 偏窄）影响判读精度，**建议修**

---

## 12. Loopback 1 复测（2026-06-16，tester 独立重跑）

> 本节为对 implementer loopback1（`artifacts/T4-implementer-report.md`）的复测。
> 3 个修复逐项独立验证 + 回归项重跑，未只信 report。

### 12.1 修复点 1：convertVoltage 期望值（中级，必修）—— 已修

- `tools/mcu_api_test.cpp:359`：期望现是 `"12.0"`（修复前 `"12.6"`），note 标
  `pure fn, 126->12.0 (MCU integer-div precision: decimal digit always 0)`。
- 核对 `MCU.cpp:1011` 实现 `snprintf("%d.%d", 126/10, (126%10)/10)` =
  `snprintf("%d.%d", 12, 0)` = `"12.0"`，expect 与实际行为完全贴合。
- sim 冒烟（`/tmp/t4_loopback1_smoke.log:113`）：
  `98 | Version | convertVoltage | in | "12.0" | OK | expect="12.0"; ...`
  → **判 OK**（上一轮同一行是 REVIEW，修复有效）。
- 回归核对：convertVersion(10002) 期望 `"V10.002"` 未被误改（`tools/mcu_api_test.cpp:354`），
  对 `MCU.cpp:446` 实现 `snprintf("V%02d.%03d", 10, 2)` 贴合，未被波及。

### 12.2 修复点 2：readGps 空定位判定（低）—— 已修

- `tools/mcu_api_test.cpp:759-760`：string reader 判定现是
  `blank = v.empty() || (v.find_first_not_of(", \t\r\n") == std::string::npos)`
  → REVIEW；note 区分 `empty`（`v.empty()` 分支）与 `all-separator/blank`（全分隔符分支）。
- sim 冒烟（`/tmp/t4_loopback1_smoke.log:104`）：
  `89 | GPS | readGps | - | ",,,,," | REVIEW | ...; all-separator/blank (no real data)`
  → **判 REVIEW**（上一轮同一行误判 OK，修复有效）。
- 未误伤正常可打印串（ID 组复验）：sim 下 readPID/UPID/UPWD/DEVICE_NAME 返空串，
  走 `empty` 分支（note 标 `empty` 而非 `all-separator/blank`），分支逻辑正确区分；
  正常可打印串（PID/UPID/UPWD/DEVICE_NAME/SignalType/FirmwareVersion）含非分隔符可打印字符，
  不落 all-separator 分支，OK 判定不受影响。

### 12.3 修复点 3：readSignalCF 范围（信息）—— 已修

- `tools/mcu_api_test.cpp:222`：`{"readSignalCF", ..., 0, 65535, false, "center freq/band, 2-byte reg [0,65535]"}`
  （修复前 `0, 6000`）。
- sim 冒烟（`/tmp/t4_loopback1_smoke.log:25`）：
  `10 | Signal | readSignalCF | - | 0 | REVIEW | center freq/band, 2-byte reg [0,65535]; 0 indistinguishable from I2C fail`
  → note 标 `[0,65535]`，0 仍 REVIEW（0 与 I2C fail 不可区分规则不变，符合四级判定语义）。
- 对照 inventory §3.3：`PARAM_MCU_SIG_CF` 0x10C 2B，理论域 `[0,65535]`，范围现贴合寄存器位宽。

### 12.4 回归项（全部未回归）

| 回归项 | 上一轮结果 | 本轮结果 | 结论 |
|--------|----------|---------|------|
| build_sim 编译 exit 0 | exit 0 | exit 0（`SIM_BUILD_RC=0`）| 未回归 |
| build(T32) 编译 exit 0 | exit 0 | exit 0（`T32_BUILD_RC=0`）| 未回归 |
| 二进制类型 | build/=MIPS32/uclibc, sim/=x86-64 | 同 | 未回归 |
| sim --no-write 冒烟 | Phase1 99行无崩, Phase2 全SKIP, exit 0 | 同（`SMOKE_RC=0`）| 未回归 |
| API 覆盖度 | 132=99+33 | 132=99+33（横幅自检 `int=81 str=7 uint=1 bool=6 tm=1 pure=2 void=1 | READ=99 | WRITE=33`）| 未回归 |
| 四级判定逻辑 | 正确 | convertVoltage OK（修复后）/readGps REVIEW（修复后）/readSignalCF 0 REVIEW 均符合 | 未回归 |
| 写阶段安全门 | 全部真实非空 | 未改（仅改 3 处 reader 表/判定），sim --write 仍自动跳过 | 未回归 |
| --restore 回路 | 不存在文件 exit 2 | 不存在文件 exit 2（`RESTORE_EXIT=2`）| 未回归 |
| json_stress_test 附带 | 两平台 link jsoncpp, 冒烟 exit 0 | build/+build_sim/ 均 link libjsoncpp.so, sim 冒烟 exit 0 | 未回归 |
| 硬约束 | MCU.{h,cpp} 未改/只 include MCU.h/未 commit | 全满足（见 §12.5）| 未回归 |

Phase 1 汇总：OK=16 / REVIEW=83 / FAIL=0（与上一轮计数一致——convertVoltage REVIEW→OK 与
readGps OK→REVIEW 在 OK/REVIEW 间等量对调，逻辑自洽，计数不变本身是修复正确的旁证）。

### 12.5 硬约束复核（全满足）

```
$ git status --short src/hardware/mcu/MCU.h src/hardware/mcu/MCU.cpp
（空）
$ git status --short tools/mcu_api_test.cpp tools/CMakeLists.txt
?? tools/CMakeLists.txt
?? tools/mcu_api_test.cpp
$ grep -nE '^\s*#include' tools/mcu_api_test.cpp
...标准头...
#include "MCU.h"   ← 唯一项目头
```

- `src/hardware/mcu/MCU.{h,cpp}` 未改（git status 空）。
- `tools/mcu_api_test.cpp` 只 include `MCU.h` 一个项目头（余皆 cstdio/cstring/string/vector 等标准头）。
- 本任务改动（tools/ 两文件 + 顶层 CMakeLists.txt add_subdirectory）均 untracked，未 commit。
- 未碰 `src/hal/**`（src/hal/ingenic/* 的 M 是 T2 遗留，非本任务）。
- 未 `rm -rf build/`（全程增量编译）。

### 12.6 二进制 md5（可追溯）

```
build_sim/bin/htc_mcu_api_test:  9c0bd21cb42a671bb887b20dc647372b  (x86-64)
build/bin/htc_mcu_api_test:      3fcdc58e10e77e117e28499da7cdbbba  (MIPS32/uclibc)
```

### 12.7 复测结论

3 个修复全部确认有效、无回归：convertVoltage expect 现贴合 MCU.cpp 实际输出（OK）、
readGps 空定位现判 REVIEW（find_first_not_of，不误伤正常可打印串）、readSignalCF 范围现贴合
2B 寄存器域 `[0,65535]`。双平台编译 exit 0，sim 冒烟 FAIL=0 exit 0，回归项全部未回归，
硬约束全满足。判 **success**，移交 reviewer。
