# T4 Planner — 完整实现方案

> 独立测试程序，逐个调用 `src/hardware/mcu/MCU.h` 全部 public API，输出「API→返回→正常/异常」+ 汇总。
> 仅 T32 真机运行验证数据，但双平台必须能编译。

---

## 1. Goals / Non-goals

### Goals
- 新增独立可执行 `htc_mcu_api_test`，**穷举 MCU.h 全部 public API**（132 个，不含 ctor/dtor/getInstance）。
- 数据驱动：不写 120 个 if/else，用表/数组 + lambda 驱动。
- 输出逐行结果 + 末尾汇总（Total/Pass/Review/Fail/Skip）。
- **Phase 1** 穷举所有 read；**Phase 2** 遍历所有 write，每个 write **写完必读回比对**。
- 异常判定解决核心难题：read I2C 失败返 0 与真实 0 值都返 0，不可区分。
- 双平台编译通过。

### Non-goals
- **不修改** `src/hardware/mcu/MCU.{h,cpp}`（被测对象）。只调用。
- **不碰** `src/hal/**`（PIC-owned）。
- 不修复测试中发现的 MCU.cpp 缺陷（如 writeESOR_WID 字节溢出）——记录为 FAIL 备注即可。
- 不做 CI 自动化 / gtest 框架接入（与 test_mcu_service 的纯 main 风格一致）。
- 未经许可不 git commit。
- sim 下不验证数据正确性（read 全 bypass 返 0，仅验证编译/表结构/调用路径）。

---

## 2. Impacted files

| 文件 | 动作 | 说明 |
|---|---|---|
| `tools/mcu_api_test.cpp` | **新建** | 主测试程序，~600-800 行 |
| `tools/CMakeLists.txt` | **新建** | 定义 `htc_mcu_api_test` target |
| `tools/README.md` | **新建** | guide 文件（CLAUDE.md 禁止建空目录无 guide） |
| `CMakeLists.txt`（顶层） | **改 1 行** | `add_subdirectory(tools)`（紧跟 tests/ 之后） |

**不改**：MCU.{h,cpp}、src/hal/**、现有任何生产代码。

---

## 3. API 枚举（分类表骨架）

来自 `MCU.h` + inventory §4。按返回/参数类型分 5 组驱动。

### 3.1 int-readers（绝大部分，~80 个）

每个 `{name, lambda返回int, [min,max]期望范围, 备注}`：

| 组 | API | 期望范围（来自 inventory §3） |
|---|---|---|
| 版本/电源 | readVersion, readShutdownVoltage, readLowPowerVoltage, readBatteryLevel, readBatteryType | 版本>0(如10002)；电压 0-255；电量 0-100；类型枚举 |
| 电池 | readBattery1/2Voltage, readBatteryVoltage, readRMID/Type/Value/Count/SunPowerValue, readRMSunPowerValue | 电压 ÷10，0-255 合理 |
| 信号 | readSignalCF/TP/RSSI/RSRP/RSRQ/SNR/TD/TP, readSignalRL | CF 0-6000；TP 0-50；RSSI/RSRP/RSRQ -200~40；SNR -50~50；RL 0-32767；TD 0-65535 |
| 环境 | readTemperature, readHumidity, readAtmosPressure, readExternalVoltage, readCds | 温度 -40~85；湿度 0-100(255=无)；气压 300-1100hPa；电压 0-255 |
| 事件/RM | readEventType, readEventID, readEventNum | 小整数 |
| 系统0x00 | readFworkMark(0-1), readEventStatus, readPType(0-10), readWorkingMode(0-4) | 枚举小整数 |
| 基础0x10-0xFF | readUWS(0-2), readCDS_DN(0-1), readCDS_Value, readVTSAlarm(0-1), readVTSSens(0-3), readNUFQ(0-65535), readTimeout(0-120,200), readCamStatus(0-4), readAIAlarm | 多为枚举/小整数 |
| SOR 0x200 | readSOR_AL(0-65535,65535=无), readSOR_UVL(0-15,255=无), readSOR_NOISE, readSOR_CO/CO2/O2 | AL 0-65535；UVL 0-15；O2 0-100 |
| ESOR 0x300 | readESOR_WS(0-2), readESOR_WID/ADD/ID, readESOR_TYPE(0-200), readESOR_BAT, readESOR_GPSA/GPSL/GPSH | GPS ÷1e7 |
| 设置0x400 读(25) | readCAM_MAXS, readPIR_MODE/SENS/INT/EN, readTIMER, readTIMER_INT, readTIMER_1START..5END, readTIMER_REPEATS, readHEARTRATE, readUP_MODE/NUFQ, readTDS_CF/TP/BW | 各自枚举/范围 |

> 范围数据全部从 inventory §3 转录进表的 `min`/`max` 字段。无明确范围的填 `{INT_MIN, INT_MAX}` 并标注「范围未知→REVIEW 阈值放宽」。

### 3.2 uint-readers（1 个）
- `readESOR_Value()` → unsigned int，范围 0-0xFFFFFFFF，单位随类型。

### 3.3 string-readers（5 个）
`{name, lambda返回string, 长度上限, 备注}`：
- `readFirmwareVersion()`（桩返 "1.0.0"，无 I2C）
- `readSignalType()`（12 字节）
- `readPID()`（32 字节）
- `readUPID()`（32 字节）
- `readUPWD()`（64 字节）

> readGps() 返回 "lon,lat,ele" 拼串——归 string 组，备注「复合读」。

### 3.4 tm-readers（1 个）
- `getDatetime()` → struct tm。校验 `tm_year>=100`(2000年+)、mon 0-11、mday 1-31、hour 0-23、min/sec 0-59。

### 3.5 工具类（纯计算/桩，无 I2C，2 个 + 若干 bool 桩）
- `convertVersion(int)` → "V10.002" 格式，可独立验证（传 10002 应得 "V10.002"）。
- `convertVoltage(int)` → "12.6V" 格式（传 126 得 "12.6V"）。
- bool 桩：`powerEnoughForFirmwareUpdate`(返 power_enough 初值), `waitFor`(恒 false), `IsWifiStationReady`(恒 true), `Is4gExist`, `IsRemoteWakeup`, `useGpsTime` → 单独「bool/桩」组，标注「无 I2C，仅调用不报错即 OK」。

### 3.6 write-specs（Phase 2，~37 个）
`{name, write-lambda(安全值), read-back-lambda, 比对方式, 安全值来源}`：

| 类别 | write API | 对应 read-back | 安全测试值 |
|---|---|---|---|
| 标识 | writePID/UPID/UPWD(string) | readPID/UPID/UPWD | 启动快照的当前值（写回原值=空操作）或 "TEST_PID_001" |
| 时钟 | setDatetime(tm*) | getDatetime | 当前系统时间（写回≈不变） |
| GPS | writeGps(string) | readGps | 当前快照值 |
| 唤醒 | writeRemoteWakeup(int), writeESOR_WS/WID(int) | IsRemoteWakeup, readESOR_WS/WID | 0（最保守） |
| PIR | writePIR_MODE/SENS/INT/EN | 对应 readPIR_* | 当前快照值 |
| Timer | writeTIMER/TIMER_INT/TIMER_1START..5END/TIMER_REPEATS | 对应 readTIMER_* | 当前快照值 |
| 策略 | writeCAM_MAXS/DEVICE_NAME/HEARTRATE/UP_MODE/UP_NUFQ/TDS_CF/TP/BW | 对应 read* | 当前快照值 |

> **核心策略**：安全测试值优先用「启动时快照的当前读回值」，即 write(原值) → read-back(原值)，相当于空操作，最不易破坏配置。仅当快照值为 0/异常时改用保守默认。

---

## 4. 异常判定策略（核心）

### 状态机（四级）

| 状态 | 触发条件 | 适用 |
|---|---|---|
| **OK** | 返回值在 inventory §3 期望范围内（含 min/max） | read |
| **REVIEW** | read 返回 0（与 I2C 失败不可区分），或越界，或 string 为空 | read（需人工复核） |
| **FAIL** | write 返回 false，或 write 后 read-back 与写入值不等 | write |
| **SKIP** | `--no-write` 模式下的所有 write API；或 sim 下写阶段 | write |

### 为什么这样设计
`MCU.cpp` 每个 read 在 `iic->read()<=0` 时 `return 0`（如 :107/133/160/476）。真实 0 值也是 0。**C++ 层无法区分**。故：
- 0 值一律标 REVIEW（除非该寄存器语义上 0 合法且范围不含 0，如 readCDS_DN 的 0=夜晚→OK）。
- 非 0 且在范围内 → OK。
- 非 0 但越界 → REVIEW（可能是 I2C 读到了脏数据）。

### write 阶段判定
- `write()` 返回 false → 直接 FAIL（I2C write 失败）。
- `write()` 返回 true 后调 `read()`，回读值 != 写入值 → FAIL（回读不匹配）。
- 注：string 比对用 `==`；tm 逐字段比对；ESOR_WID 预期可能 FAIL（见风险 §8）。

---

## 5. 输出格式

### 5.1 逐行样例（Phase 1 读）
```
=== Phase 1: READ API sweep (132 APIs) ===
 #  | Group   | API                  | Args | Return        | Status  | Note
----|---------|----------------------|------|---------------|---------|----------------------------
 1  | Version | readFirmwareVersion  | -    | "1.0.0"       | OK      | stub, no I2C
 2  | Version | readVersion          | -    | 10002         | OK      | in [1,99999]
 3  | Battery | readBattery1Voltage  | -    | 126           | OK      | 12.6V, in [0,255]
 4  | Signal  | readSignalRSSI       | -    | 0             | REVIEW  | 0 indistinguishable from I2C fail
 5  | SOR     | readSOR_UVL          | -    | 300           | REVIEW  | out of range [0,15]
 ...
```

### 5.2 逐行样例（Phase 2 写，仅 --write）
```
=== Phase 2: WRITE API sweep (37 APIs, write+readback) ===
 #  | Group | API             | WriteValue        | write() | Readback        | Status | Note
----|-------|-----------------|-------------------|---------|-----------------|--------|------------------
 1  | ID    | writePID        | "T32-TEST-0001"   | true    | "T32-TEST-0001" | OK     |
 2  | RTC   | setDatetime     | 2026-06-16 12:00  | true    | 2026-06-16 12:00| OK     |
 3  | ESOR  | writeESOR_WID   | 12345             | true    | 12345           | FAIL   | readback mismatch (suspected MCU.cpp:1518 sizeof(int) overflow bug)
```

### 5.3 末尾汇总
```
=== SUMMARY ===
Phase 1 (read):  Total=132  OK=98  REVIEW=31  FAIL=0  SKIP=3
Phase 2 (write): Total=37   OK=35  REVIEW=0   FAIL=2  SKIP=0  (run: --write)
Overall exit code: 0  (non-zero only if FAIL>0 in executed phase)
```

### 5.4 落盘
默认同时写日志文件 `mcu_api_test_<timestamp>.log` 到 cwd（或 `--log <path>` 指定），含完整逐行 + 汇总。stdout 同步打印精简版。

---

## 6. 写阶段安全门控（关键设计）

### 6.1 默认关闭
- **默认 `--no-write`**：只跑 Phase 1。Phase 2 全部 SKIP。
- 必须 `htc_mcu_api_test --write` 显式开启。

### 6.2 启动确认门控
开启 `--write` 时，程序启动先打印 ASCII 警告横幅：
```
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
!!  WARNING: WRITE PHASE ENABLED
!!  This will MODIFY real hardware config: PID/UPID/UPWD/
!!  DeviceName/RTC/Timer/PIR/TDS/HeartRate on the MCU.
!!  A pre-write snapshot is saved for --restore.
!!  Type 'yes' to continue (or pass --yes to skip prompt):
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
```
stdin 读 "yes" 才继续；`--yes` 跳过（用于脚本）。

### 6.3 启动快照 + restore
- Phase 2 开始前，对所有将写的参数先 read 一遍存内存 `snapshot` map + 落盘 `mcu_snapshot_<ts>.json`。
- 提供 `--restore <snapshot.json>` 子模式：读快照，逐个 write 回原始值（用于事后恢复出厂前状态）。
- 安全测试值优先取 snapshot 当前值（write 原值≈空操作）。

### 6.4 安全测试值表（当 snapshot 异常时的回退默认）
见 §3.6。保守原则：能写 0 的写 0，枚举类写最小合法值，string 写短可打印测试串。

---

## 7. CMake 接入

### 7.1 顶层 `CMakeLists.txt`（+1 行）
在现有 `add_subdirectory(tests)` 附近加：
```cmake
add_subdirectory(tools)
```

### 7.2 `tools/CMakeLists.txt`（新建）
```cmake
add_executable(htc_mcu_api_test mcu_api_test.cpp)

target_link_libraries(htc_mcu_api_test
    PRIVATE
    mcu          # 已封装 MCU.cpp + IIC.cpp，PUBLIC link logger，双平台可编译
    easylogger
    logger
    pthread
    rt
)

target_include_directories(htc_mcu_api_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/hardware/mcu
    ${CMAKE_SOURCE_DIR}/third_party/easylogger/inc
)

set_target_properties(htc_mcu_api_test PROPERTIES
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
```
**两平台都构建**（不加 `if(NOT BUILD_FOR_SIMULATION)`），满足 CLAUDE.md 双平台编译硬约束。sim 下 `mcu` 库的 IIC 已 bypass，能链接运行（数据无意义但不崩）。

### 7.3 验证编译命令
```bash
cmake --build build_sim -j$(nproc) --target htc_mcu_api_test   # PC sim 编译
cmake --build build -j$(nproc) --target htc_mcu_api_test       # T32 交叉编译
```

---

## 8. 调用方式 / 运行步骤

### 8.1 命令行参数
```
htc_mcu_api_test [--no-write] [--write] [--yes] [--group <Name>]
                 [--log <path>] [--restore <snapshot.json>] [--help]

  (default)        Phase 1 only (read sweep), writes skipped
  --write          Enable Phase 2 (destructive, prompts unless --yes)
  --no-write       Force skip Phase 2 (default)
  --yes            Skip confirmation prompt
  --group <Name>   Only run one group: Battery|Signal|Sensor|ESOR|Timer|PIR|ID|RTC|...
  --log <path>     Log file (default: mcu_api_test_<ts>.log)
  --restore <f>    Restore mode: write back values from snapshot file, then exit
  --help           Print usage
```

### 8.2 真机运行步骤（T32）
```bash
# 1. build host 编译 T32
cmake --build build -j$(nproc) --target htc_mcu_api_test

# 2. T32 设备（NFS mount /mnt/huntcam）
cd /mnt/huntcam
LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH ./bin/htc_mcu_api_test            # Phase 1 only
LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH ./bin/htc_mcu_api_test --write    # +Phase 2

# 3. 恢复
LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH ./bin/htc_mcu_api_test --restore mcu_snapshot_<ts>.json
```
> 权限：`/dev/hc32l13x` 需 root，以 root 运行或确保 udev 赋权。

### 8.3 sim 运行（仅验证编译/表结构）
```bash
./build_sim/bin/htc_mcu_api_test --no-write
# 输出横幅：SIM MODE — I2C bypassed, read data is NOT meaningful (all 0).
# 所有 read 标 REVIEW，验证调用路径不崩、表枚举完整。
```

---

## 9. 数据结构骨架（实现提示，非最终代码）

```cpp
// int-readers 表
struct IntReader {
    const char* name;
    const char* group;
    std::function<int()> call;
    int lo, hi;          // 期望范围，来自 inventory §3
    const char* note;
};
static const std::vector<IntReader> g_int_readers = {
  {"readVersion", "Version", []{return MCU::getInstance()->readVersion();}, 1, 999999, ""},
  {"readBattery1Voltage","Battery",[]{return MCU::getInstance()->readBattery1Voltage();},0,255,"÷10=V"},
  {"readSignalRSSI","Signal",[]{return MCU::getInstance()->readSignalRSSI();},-200,40,"dBm"},
  /* ... ~80 项 ... */
};

// string-readers 表
struct StrReader { const char* name; const char* group; std::function<std::string()> call; size_t maxlen; const char* note; };

// write-specs 表
struct WriteSpec {
    const char* name; const char* group;
    std::function<bool()> doWrite;          // 用安全值写
    std::function<std::string()> readBack;  // 回读（序列化为 string 便于比对/落盘）
    std::function<std::string()> expect;    // 期望回读值（通常=安全值）
    const char* note;
};

// 判定
Status classifyInt(int v, int lo, int hi) {
    if (v == 0) return REVIEW;              // 0 不可区分
    if (v >= lo && v <= hi) return OK;
    return REVIEW;                          // 越界待人工
}
```
> `convertVersion`/`convertVoltage` 独立表（纯函数，传固定入参验证格式）。tm 组单独一个函数处理。

---

## 10. 超越 readAllTestData 的点
`MCU.cpp:2318 readAllTestData()` 的局限（方案全部超越）：
1. **死代码且不全**：只覆盖 ~40 个 read，漏了全部 write、全部 ESOR_GPSA/L/H、全部 TIMER_2..5、TDS、UP_*、SOR 全部（只 readAllTestData 没调 readSOR_*，实际只在注释段提了部分）。
2. **只打印不判定**：无状态分类，人工肉眼判断。
3. **无范围校验**：纯打日志，不对照 inventory 范围。
4. **无写回验证**：完全不覆盖 write。
5. **无汇总**：无 Total/OK/Review/Fail 统计。
6. **无落盘/无 restore**。

方案覆盖全部 132 + 37 write，四级判定，range check，汇总，快照 restore。

---

## 11. 风险与缓解

| 风险 | 严重度 | 缓解 |
|---|---|---|
| write 破坏真机配置（PID/RTC/Timer/DeviceName…） | **high** | 默认 --no-write；启动快照 + --restore；安全值优先用快照原值（空操作）；启动确认门控 |
| writeESOR_WID 字节溢出 bug（MCU.cpp:1518 sizeof(int)=4 写入 2 字节寄存器） | medium | 测试将定位此 bug（写 WID 后 readback 或相邻 ESOR_ADD 异常）；FAIL 备注 'suspected MCU.cpp overflow'；本任务不修 |
| sim 下 read 全 0，数据无意义 | medium | sim 运行打印横幅；read 全 REVIEW；目的仅为验证编译/表/调用路径 |
| /dev/hc32l13x 权限 | low | 运行步骤注明 root 运行 |
| write 回读不等未必是 bug（MCU 可能拒绝非法值） | medium | 安全值取快照原值避免此情况；FAIL 仅作 REVIEW 级提示，不自动判定为代码缺陷 |
| 表枚举遗漏某个 API | low | 编译期/启动时对照 MCU.h 方法数自检（统计 g_*_readers 总数 vs 预期 132，不等则告警） |

---

## 12. 测试策略（本任务自身的验收测试）

1. **编译测试**（必过）：`build_sim` + `build` 两平台 `--target htc_mcu_api_test` 均 exit 0。
2. **表完整性自检**：程序启动打印 API 计数，人工对照 MCU.h（132 read 类 + 37 write）。
3. **sim 冒烟**：`build_sim/bin/htc_mcu_api_test --no-write` 不崩，Phase 1 跑完，输出汇总。
4. **真机 Phase 1**（用户执行）：read 全部有返回，range check 分类合理。
5. **真机 Phase 2**（用户执行 --write）：write+readback，OK/FAIL 分布合理；ESOR_WID 预期 FAIL 定位已知 bug。
6. **--restore 回路**：写后 restore，再 Phase 1 读回应≈原始快照。

---

## 13. 验收标准（Acceptance criteria）

- [ ] `tools/mcu_api_test.cpp` + `tools/CMakeLists.txt` + `tools/README.md` 新建，顶层 CMakeLists.txt 加 `add_subdirectory(tools)`。
- [ ] 双平台 `cmake --build build_sim/build --target htc_mcu_api_test` 均 exit 0。
- [ ] 程序覆盖 MCU.h 全部 public API（read 132 范畴 + write 37），启动自检计数。
- [ ] Phase 1 / Phase 2 顺序执行，Phase 2 默认关闭需 `--write`。
- [ ] 四级状态判定（OK/REVIEW/FAIL/SKIP）+ 末尾汇总。
- [ ] write 阶段有启动快照 + `--restore` + 确认门控 + 警告横幅。
- [ ] 输出含逐行表 + 落盘日志。
- [ ] 不修改 MCU.{h,cpp}、不碰 src/hal（grep 确认 tools/ 只 include MCU.h）。
- [ ] 未经许可不 git commit。
