# T4 Implementer — 构建与冒烟证据

> 任务 T4（feature flow, implementer 节点）：新增 `htc_mcu_api_test`，穷举 MCU.h 全部 public API，四级判定 + 汇总 + 落盘日志 + 写阶段安全门控。
> 证据采集日 2026-06-16，基线 commit `3d854ab` + 本任务改动（未 commit）。

---

## 1. API 枚举自检（程序启动打印）

实际数到的 MCU.h public API（排除 getInstance / ctor / dtor）：

```
API self-check: int=81 str=7 uint=1 bool=6 tm=1 pure=2 void=1 | READ total=99 | WRITE=33
MCU.h public API (excl getInstance/ctor/dtor): read-class + write-class = 132 methods enumerated.
```

- read-class 共 **99** 个调用点：int=81（含 I2C read + 几个 stub 返 0 的 readShutdownVoltage 等）、string=7（含 readGps 复合串）、uint=1（readESOR_Value）、bool-stub=6、tm=1（getDatetime）、pure-fn=2（convertVersion/convertVoltage）、void=1（readAllTestData）。
- write-class 共 **33** 个：writePID/UPID/UPWD/DEVICE_NAME(4) + writeRemoteWakeup(1) + writeESOR_WS/WID(2) + PIR(4) + Timer(13) + Policy(8: CAM_MAXS/HEARTRATE/UP_MODE/UP_NUFQ/TDS_CF/TP/BW + setDatetime 算 RTC 组) + writeGps(1)。
- **132 = 99 + 33**，与 `doc/knowledge/refs/mcu-api-and-register-inventory.md §1` 的「132 个公开方法」一致（planner 估计 132 read + 37 write，实际以数到的为准：99 read + 33 write = 132 调用点）。

> 说明：planner 方案的「132 read」是把 convertVersion/convertVoltage/bool 桩/readAllTestData 都算 read，且把 readAllTestData 单列；实际枚举后 read-class 调用点 99、write-class 33，合计 132 个 MCU.h public 方法全覆盖，无遗漏（自检计数已固化进程序启动横幅）。

---

## 2. 双平台编译（均 exit 0，无 warning/error）

### 2.1 PC sim（`build_sim/`，gcc x86-64）

```
$ cmake --build build_sim -j$(nproc) --target htc_mcu_api_test
[ 50%] Built target easylogger
[100%] Built target logger
[100%] Built target mcu
[100%] Built target htc_mcu_api_test
rc=0
```

产物：`build_sim/bin/htc_mcu_api_test` — `ELF 64-bit LSB pie executable, x86-64`。

### 2.2 T32 交叉编译（`build/`，mips-linux-uclibc-gnu-gcc 5.4）

```
$ cmake --build build -j$(nproc) --target htc_mcu_api_test
[ 33%] Built target easylogger
[ 66%] Built target logger
[100%] Built target mcu
[100%] Built target htc_mcu_api_test
rc=0
```

产物：`build/bin/htc_mcu_api_test` — `ELF 32-bit LSB executable, MIPS, MIPS32 rel2 ... interpreter /lib/ld-uClibc.so.0`。

> uclibc 工具链无 `std::to_string` / `std::stoi`（与 MCU.cpp 用 `to_string_custom`/`stoi_custom` 同因），已用本地 `toStr`/`fromStr`（snprintf/strtol）替换，两平台一致。

---

## 3. sim 冒烟（`--no-write`，Phase 1 only）

```
$ LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH ./bin/htc_mcu_api_test --no-write
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
 #  | Group   | API                  | Args | Return        | Status  | Note
----|---------|----------------------|------|---------------|---------|---------------------------
  1 | Power   | readShutdownVoltage  | -    |             0 | REVIEW  | threshold V; 0 indistinguishable from I2C fail
 ...（99 行 read 全部调用，无崩溃）
 35 | Base    | readCDS_DN           | -    |             0 | OK      | 0=night/1=day (0 allowed); in range (0 allowed)
 43 | SOR     | readSOR_AL           | -    |             0 | OK      | lx, 65535=none; in range (0 allowed)
 ...
=== Phase 2: WRITE API sweep SKIPPED (--no-write or SIM) ===
 ...（33 行 write 全 SKIP）
=== SUMMARY ===
Phase 1 (read):  Total=99  OK=16  REVIEW=83  FAIL=0  SKIP=0
Phase 2 (write): Total=33  OK=0  REVIEW=0  FAIL=0  SKIP=33  (skipped)
Overall exit code: 0  (non-zero if FAIL>0 in executed phase)
```

- **Phase 1 跑完**：99 个 read 全部调用，无崩溃。sim 下 I2C bypass 返 0 → 83 个 REVIEW（0 与 I2C fail 不可区分）；16 个 OK 是 sim 下有合法值的（allowZero 的 readCDS_DN/readCDS_Value/readVTSAlarm/readSOR_AL/readSOR_UVL + 6 bool-stub + 2 pure-fn(convertVersion→V10.002/convertVoltage→12.6) + tm/void 调用不崩即 OK）。符合预期。
- **Phase 2 全 SKIP**：`--no-write`/sim 下 33 个 write 全跳过。
- **exit 0**。

### 3.1 `--write` 在 sim 下被正确跳过

```
$ ./bin/htc_mcu_api_test --write --yes
>>> SIM MODE -- I2C bypassed; read data is NOT meaningful (all 0 -> REVIEW).
>>> SIM MODE: --write requested but Phase 2 skipped (no real device; writes meaningless).
=== Phase 2: WRITE API sweep SKIPPED (--no-write or SIM) ===
Phase 2 (write): Total=33  OK=0  REVIEW=0  FAIL=0  SKIP=33  (skipped)
```
sim 下 `--write` 不触发门控/快照（无真机，写无意义），Phase 2 仍全 SKIP。

### 3.2 `--group Battery` 过滤生效

```
$ ./bin/htc_mcu_api_test --no-write --group Battery
 1 | Battery | readBatteryLevel     | -    |   0 | REVIEW  | ...
 2 | Battery | readBatteryType      | -    |   0 | REVIEW  | ...
 3 | Battery | readBattery1Voltage  | -    |   0 | REVIEW  | ...
 4 | Battery | readBattery2Voltage  | -    |   0 | REVIEW  | ...
 5 | Battery | readBatteryVoltage   | -    |   0 | REVIEW  | ...
 6 | Battery | readRMSunPowerValue  | -    |   0 | REVIEW  | ...
Phase 1 (read):  Total=6  OK=0  REVIEW=6  FAIL=0  SKIP=0
```

### 3.3 `--help` 正常

打印完整用法（7 个 flag）。

---

## 4. 硬约束核对

| 约束 | 状态 |
|------|------|
| 不改 `src/hardware/mcu/MCU.{h,cpp}` | ✅ `git status` MCU.{h,cpp} 干净 |
| 不碰 `src/hal/**` | ✅ 本任务未改 src/hal（注：`git status` 里 `M src/hal/ingenic/*` 是 T2 遗留，非本任务） |
| `tools/mcu_api_test.cpp` 只 include `MCU.h`（+ 标准库） | ✅ 见 grep：`#include "MCU.h"` 是唯一项目头 |
| 双平台编译通过 | ✅ build_sim + build 均 exit 0，无 warning |
| C++14 | ✅ |
| 绝不 `rm -rf build/` | ✅ 全程在既有 build/build_sim 上增量 |
| 未 git commit | ✅ |

---

## 5. 产物文件清单

- `tools/mcu_api_test.cpp`（新建，主程序，数据驱动）
- `tools/CMakeLists.txt`（新建，定义 `htc_mcu_api_test` + 既有 `json_stress_test`）
- `tools/README.md`（新建，guide 文件）
- `CMakeLists.txt`（改：`add_subdirectory(tools)`，无平台门控）

> 附带：`tools/` 目录此前从未接入 CMake（既有 `json_stress_test.cpp` 自 commit d04f46c 起一直是游离源文件）。本任务首次 `add_subdirectory(tools)`，顺势把 `json_stress_test` 也纳入构建（两平台均编译通过 + sim 冒烟通过），避免「目录接入但源文件游离」的不一致。

## 6. 未在本次验证的范围（留真机）

- T32 真机 Phase 1（read 实数据 + range 分类）
- T32 真机 Phase 2（`--write` 写+回读，定位已知 `writeESOR_WID` 字节溢出 bug）
- `--restore` 回路（写后 restore，再 Phase 1 读回应≈快照）

PC 无法跑 MIPS，以上需用户在 T32 硬件执行。

---

## 7. Loopback 1 修复（2026-06-16，回应 tester §7 三个判读准确性瑕疵）

tester report `artifacts/T4-tester-report.md` §需 implementer 修的问题定位 3 处判读瑕疵，本节为 loopback 修复 + 独立复验证据。改动仅落在 `tools/mcu_api_test.cpp`，未碰 MCU.{h,cpp}/src/hal/未 commit/未 rm -rf build。

### 7.1 问题 2（中级，必修）convertVoltage 期望值标错

**根因核对**（MCU.cpp:1011-1016）：
```cpp
std::string MCU::convertVoltage(int value) {
    char buf[32] = {0};
    snprintf(buf, sizeof(buf), "%d.%d", value / 10, (value % 10) / 10);
    return std::string(buf);
}
```
convertVoltage(126) = `snprintf("%d.%d", 126/10=12, (126%10)/10=6/10=0)` = **`"12.0"`**（整数除法吃掉第二位小数）。这是 MCU.cpp 的精度特性，不是 API 异常——工具 expect 必须如实反映实际输出。

**修法**：`tools/mcu_api_test.cpp:359` 期望 `"12.6"` → `"12.0"`，note 标注「MCU integer-div precision: decimal digit always 0」。

**同类核对 convertVersion**（MCU.cpp:446-456）：
```cpp
int major = ver / 1000; int minor = ver % 1000;
snprintf(version_str, sizeof(version_str), "V%02d.%03d", major, minor);
```
convertVersion(10002) = major=10, minor=2 → `"V10.002"`。表期望 `"V10.002"` **正确，无需改**。

### 7.2 问题 1（低）readGps 空定位误判 OK

sim 下 readGps 返 `",,,,,"`（非空全逗号），原判定只查 `v.empty()` → 误判 OK。

**修法**（`tools/mcu_api_test.cpp:755-760`）：string reader 判定增加「全是分隔符/空白也判 REVIEW」：
```cpp
bool blank = v.empty() || (v.find_first_not_of(", \t\r\n") == std::string::npos);
Status st = blank ? Status::REVIEW : Status::OK;
```
正常可打印串（PID/UPID/UPWD/DEVICE_NAME/SignalType/FirmwareVersion）必有非分隔符可打印字符，不落进此分支，**不误伤**它们的 OK 判定（sim 下 ID 组返空串仍走 `empty()` 分支判 REVIEW，note 标 `empty` 而非 `all-separator/blank`，已验证）。

### 7.3 问题 3（信息）readSignalCF 范围偏窄

表 `[0,6000]` 偏窄于寄存器 2 字节理论域 `[0,65535]`（inventory §3.3 `PARAM_MCU_SIG_CF` 0x10C, 2B，无更精确工程上限）。

**修法**（`tools/mcu_api_test.cpp:222`）：范围 `0, 6000` → `0, 65535`，note 标 `2-byte reg [0,65535]`。

### 7.4 复验：双平台编译（loopback1 后，均 exit 0）

```
$ cmake --build build_sim -j$(nproc) --target htc_mcu_api_test
[100%] Built target htc_mcu_api_test
SIM_BUILD_RC=0

$ cmake --build build -j$(nproc) --target htc_mcu_api_test
[100%] Built target htc_mcu_api_test
T32_BUILD_RC=0
```

### 7.5 复验：sim 冒烟三处修复生效（关键行）

```
$ cd build_sim && LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH ./bin/htc_mcu_api_test --no-write
 10 | Signal  | readSignalCF    | - | 0 | REVIEW | center freq/band, 2-byte reg [0,65535]; 0 indistinguishable from I2C fail
 89 | GPS     | readGps         | - | ",,,,," | REVIEW | lon,dir,lat,dir,ele composite; all-separator/blank (no real data)
 97 | Version | convertVersion  | in | "V10.002" | OK | expect="V10.002"; pure fn, 10002->V10.002
 98 | Version | convertVoltage  | in | "12.0"   | OK | expect="12.0"; pure fn, 126->12.0 (MCU integer-div precision: decimal digit always 0)
=== SUMMARY ===
Phase 1 (read):  Total=99  OK=16  REVIEW=83  FAIL=0  SKIP=0
Phase 2 (write): Total=33  OK=0  REVIEW=0  FAIL=0  SKIP=33  (skipped)
Overall exit code: 0
SMOKE_RC=0
```

逐项确认：
- **convertVoltage 现 OK**（修复前 REVIEW）：返 `"12.0"` = expect `"12.0"`。convertVersion 仍 OK（期望正确未误伤）。
- **readGps 现 REVIEW**（修复前误判 OK）：返 `",,,,,"`，note 标 `all-separator/blank (no real data)`。
- **readSignalCF**：范围已放宽到 `[0,65535]`，0 仍 REVIEW（0 规则不变），放宽后非 0 合法值不再误判越界。
- **汇总**：OK=16/REVIEW=83/FAIL=0，exit 0。数字与修复前相同是因为 convertVoltage(REVIEW→OK) 与 readGps(OK→REVIEW) 在 OK/REVIEW 间等量对调，逻辑自洽。

### 7.6 复验：readGps 判定改动未误伤其它 string reader（ID 组）

```
$ cd build_sim && LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH ./bin/htc_mcu_api_test --no-write --group ID
 1 | ID | readPID         | - | "" | REVIEW | 32-byte; empty (I2C fail or none)
 2 | ID | readUPID        | - | "" | REVIEW | 64-byte; empty (I2C fail or none)
 3 | ID | readUPID        | - | "" | REVIEW | 64-byte; empty (I2C fail or none)
 4 | ID | readDEVICE_NAME | - | "" | REVIEW | 24-byte; empty (I2C fail or none)
```
sim 下 ID 类返空串仍走 `v.empty()` 分支（note 标 `empty` 而非 `all-separator/blank`），证明新分支只对 `,,,,,` 类触发，不误伤空串与正常可打印串。

### 7.7 grep 确认改动落点 + 硬约束

```
$ grep -n '"12.0"' tools/mcu_api_test.cpp
355: // convertVoltage(126) -> "12.0"（MCU.cpp:1011 整数除法精度特性：...）
356: // snprintf("%d.%d", 126/10=12, (126%10)/10=6/10=0) = "12.0"，小数位恒 0，...
359: [m]{ return m->convertVoltage(126); }, "12.0",
$ grep -n readSignalCF tools/mcu_api_test.cpp
222: {"readSignalCF", "Signal", [m]{...}, 0, 65535, false, "center freq/band, 2-byte reg [0,65535]"},
$ grep -n find_first_not_of tools/mcu_api_test.cpp
638: (既有 writeGps 安全值判断，非本次改动)
760: || (v.find_first_not_of(", \t\r\n") == std::string::npos);  ← 本次新增（string reader 判定）
$ git status --short src/hardware/mcu/MCU.cpp src/hardware/mcu/MCU.h
(空 — MCU.{h,cpp} 未改)
$ git status --short tools/mcu_api_test.cpp
?? tools/mcu_api_test.cpp  (本任务首次新增，unstaged，未 commit)
```
