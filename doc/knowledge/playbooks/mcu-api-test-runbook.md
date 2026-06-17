# htc_mcu_api_test 真机运行与判读手册

## 1. 目的

`htc_mcu_api_test` 是 MCU 黑盒 API 遍历器，数据驱动穷举 `src/hardware/mcu/MCU.h` 全部 132 个 public API
（99 read-class + 33 write-class），按四级状态（OK/REVIEW/FAIL/SKIP）判定，输出逐行表 + 末尾汇总 +
落盘日志。Phase 1 只读，Phase 2 默认关闭（`--write` 开启后会写真机配置）。

**本手册给用户在 T32 真机上自测的清晰路径。** PC sim 只能验证编译/表枚举/调用路径不崩
（I2C 被 bypass，read 全 0），真机数据必须在本手册描述的 T32 流程下验证。

与下列手册互补、不重叠：
- `mcu-http-api-test.md` —— HTTP 端点（/device/info、/device/sensors 等）的真机联调
- `../../artifacts/T4-tester-evidence.md` —— 本工具的双平台编译/sim 冒烟/代码审计证据
- `../refs/mcu-api-and-register-inventory.md` —— 132 方法清单 + 寄存器映射（范围来源）

## 2. 适用范围

**适用于**：
- 在 T32 真机上遍历全部 MCU read API，看哪些寄存器真有数据、哪些读失败（返 0）。
- 在 T32 真机上验证 write API 的写+回读一致性（Phase 2，需明确风险，见 §5）。
- 事后用 `--restore` 把配置写回测试前的快照。

**不适用于**：
- 在 PC 上验证 MCU 真实值（sim 下 I2C bypass，read 全 0 → 全 REVIEW，数据无意义）。
- 替代单元测试（本工具是黑盒遍历，不做 mock 注入）。

## 3. 关键前提：sim vs 真机行为差异

| 维度 | T32 真机（`build/`） | PC sim（`build_sim/`） |
|------|----------------------|------------------------|
| I2C | 真实 `/dev/hc32l13x` | bypass，read 恒 0 |
| Phase 1 read 值 | 真实 MCU 寄存器值 | 全 0（OK=16/REVIEW=83）|
| Phase 2 write | 真实写配置 + 回读 | 自动跳过（无意义）|
| 适用 | 验证数据 + write 回路 | 只验编译/表/不崩 |

sim 下 `readGps` 返 `",,,,,"`、`readWorkingMode` 返 -1（MCU I2C fail 的真实行为），
这些在真机上会是真实值。

## 4. 环境准备

### 4.1 build host 编译 T32 目标

```bash
cd /home/zengping/project/huntcam/code/t32_cam
cmake --build build -j$(nproc) --target htc_mcu_api_test
# 产物：build/bin/htc_mcu_api_test（MIPS32/uclibc）+ build/lib/*.so
```

### 4.2 T32 设备运行（NFS mount /mnt/huntcam）

```bash
# T32 设备上（build host 的 build/ 已 NFS 共享到 /mnt/huntcam）
cd /mnt/huntcam
# /dev/hc32l13x 需 root 权限
LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH ./bin/htc_mcu_api_test --no-write
```

> 若提示 I2C open 失败：确认以 root 运行，或 `ls -l /dev/hc32l13x` 检查 udev 赋权。
> NFS 缓存导致旧二进制：确认 mount 带 `noac`（见项目 CLAUDE.md）。

### 4.3 二进制类型核对（排查 NFS 错挂）

```bash
file bin/htc_mcu_api_test   # 应为 ELF 32-bit MIPS, interpreter /lib/ld-uClibc.so.0
md5sum bin/htc_mcu_api_test # 与 build host 的 build/bin/htc_mcu_api_test 比对一致
```

## 5. Phase 1：READ 遍历（只读，安全）

### 5.1 运行

```bash
LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH ./bin/htc_mcu_api_test --no-write
# 或只跑某组：
LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH ./bin/htc_mcu_api_test --no-write --group Battery
LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH ./bin/htc_mcu_api_test --no-write --group Signal
```

### 5.2 如何判读 OK / REVIEW / STUB / FAIL

每个 read 输出一行：`# | Group | API | Return | Status | Note`。

| Status | 含义 | 真机上的处置 |
|--------|------|-------------|
| **OK** | 返回非 0 且落在 inventory §3 期望范围内 | ✅ 该 API 正常工作 |
| **STUB** | MCU.cpp 里是 stub（直接 `return` 常量、不读 I2C、**未实现**） | ⛔ 不是测试失败，是 API 本身没实现——真机上也永远那个值。汇总单列 STUB 计数。需实现才能验证。 |
| **REVIEW** | 返回 0（与 I2C 失败不可区分）、或越界、或 string 空 | ⚠ 需人工复核：可能是真 0 值、可能 I2C 读失败、可能传感器未接 |
| **FAIL** | `readWorkingMode` 返回 -1（MCU 的 I2C 失败信号，唯一能确定判失败的 read） | ❌ 该 read 明确失败（MCU 没响应 mode 寄存器）；其余 read 失败仍混在 REVIEW 里 |
| **SKIP** | （Phase 1 不出现）| — |

**已知 STUB 清单（18 个，真机上也返回常量、无法验证功能）**：
readShutdownVoltage / readLowPowerVoltage / readBatteryLevel / readBatteryType（Power/Battery，返 0）、
readRMID / readRMType / readRMValue / readRMCount / readEventType / readEventID / readEventNum（RM/Event，返 0）、
readFirmwareVersion（返 "1.0.0"）、
waitFor / IsWifiStationReady / Is4gExist / IsRemoteWakeup / useGpsTime / powerEnoughForFirmwareUpdate（bool 桩）。
→ 这些在结果里直接标 STUB，**不再混进 REVIEW**。

**REVIEW 的核心难题**：`MCU.cpp` 几乎所有真 read 在 `iic->read()<=0` 时 `return 0`，真实 0 值也是 0，
C++ 层无法区分。所以真 read 返 0 一律标 REVIEW（除非该寄存器语义上 0 合法且范围含 0：readCDS_DN/
readCDS_Value/readVTSAlarm/readSOR_AL/readSOR_UVL/readCAM_MAXS 这 6 个走 allowZero 通道，0 判 OK）。
例外：`readWorkingMode` 失败时 `return -1`（见 MCU.cpp:357），-1 是明确失败信号 → FAIL。

**真机 REVIEW 排查顺序**（现在 REVIEW 已不含 stub，量小、有意义）：
1. 看是不是"语义 0 合法"的寄存器（如 readNUFQ=0 可能真的没有未上传）。
2. 看 Note 列的范围，确认值是否越界（如 readSOR_UVL 返 300 = 越界脏数据）。
3. 多次运行看是否稳定返 0（稳定 0 多半是该功能未启用/传感器未接/I2C 地址不对）。
4. 对照 `mcu-api-and-register-inventory.md §3` 的寄存器语义判断。

### 5.3 末尾汇总

```
=== SUMMARY ===
Phase 1 (read):  Total=99  OK=??  REVIEW=??  FAIL=??  SKIP=0  STUB=18
```

- **STUB=18** 是固定的（未实现的 API），与硬件无关。
- **OK** 数量取决于实际接了哪些传感器/模块 + MCU 是否响应。
- **REVIEW** 是"真 read 返 0/越界"——真机上才是要逐个查的项（传感器接没接、线序、I2C 地址）。
- **FAIL** 只会是 readWorkingMode（MCU 没响应 mode 寄存器）；其余 read 失败混在 REVIEW 里。

## 6. Phase 2：WRITE 遍历（会改配置，高风险）

### 6.1 风险

Phase 2 会写 PID/UPID/UPWD/DeviceName/RTC/Timer/PIR/TDS/HeartRate/GPS 等**真实硬件配置**。
**默认关闭**（`--no-write`）。必须显式 `--write` 开启。

### 6.2 写入值策略（重要）+ 安全机制

**两种写模式：**
- `--write`（默认「辨识值」）：对每个 write 写一个**在合法范围内、非 0、非默认、且异于当前快照值**的辨识值。
  回读能对上这个辨识值，才算证明 write **真的生效**——不会被「写回原值」那种空操作掩盖。
  这是「确认写成功」用的模式。
- `--write-original`（保守）：写回快照原值（≈空操作），只验证 write 调用路径不崩、不改任何配置。
  给只想走调用路径、不想动配置的场景。

**安全机制（两种模式都有）：**
1. **默认 --no-write**（Phase 2 全跳过）。
2. `--write` / `--write-original` 开启时打印 ASCII 警告横幅，stdin 等 `yes` 才继续（`--yes` 跳过）。
3. Phase 2 开始前，对所有将写参数 read 一遍存快照（内存 + 落盘 `mcu_snapshot_<ts>.json`）。
4. 提供 `--restore <snapshot.json>` 子模式把快照原值写回。

> ⚠ `--write`（辨识值）会**真实改变** PID/UPID/UPWD/DeviceName/RTC/Timer/PIR/TDS/HeartRate/GPS 等配置。
> 跑完务必 `--restore` 恢复（见 §6.6），否则设备会保留测试值（如上传密码变成 `MCUTEST-UPWD-001`）。

**辨识值清单**（均在合法范围内、非 0、非默认）：

| 写 API | 辨识值 |
|---|---|
| `setDatetime` | 当前系统时间（每次不同，永不 stale） |
| `writePID` / `writeUPID` / `writeUPWD` / `writeDEVICE_NAME` | `MCUTEST-PID-001` / `MCUTEST-UPID-001` / `MCUTEST-UPWD-001` / `MCUTEST-DEV-001` |
| `writeGps` | `12123.4567,E,3123.4567,N,123.4`（读回格式可能不同 → REVIEW） |
| `writeRemoteWakeup` / `writeESOR_WS` / `writePIR_MODE` / `writePIR_EN` / `writeTIMER` / `writeUP_MODE` | `1` |
| `writeESOR_WID` | `12345` |
| `writePIR_SENS` | `2` |
| `writePIR_INT` | `30` |
| `writeTIMER_INT` | `15` |
| `writeTIMER_1..5 START/END` | `480/1020`、`540/1080`、`600/1140`、`420/960`、`660/1200`（分钟） |
| `writeTIMER_REPEATS` | `127`（全周位图） |
| `writeCAM_MAXS` | `50` |
| `writeHEARTRATE` | `60` |
| `writeUP_NUFQ` | `12` |
| `writeTDS_CF` | `2350` |
| `writeTDS_TP` | `20` |
| `writeTDS_BW` | `10` |

### 6.3 运行（真机，谨慎）

```bash
cd /mnt/huntcam
# 辨识值模式（确认写是否生效；会改配置，跑完务必 --restore）
LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH ./bin/htc_mcu_api_test --write
# 按提示输入 yes

# 脚本化（跳过确认）：
LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH ./bin/htc_mcu_api_test --write --yes

# 保守模式（写回原值≈空操作，只验证调用路径）：
LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH ./bin/htc_mcu_api_test --write-original --yes
```

### 6.4 判读 OK / FAIL / REVIEW（3 态）

每行 note 会显示 `写入值 / 读回值 / 原值(orig=…)`，一眼三者对比。

| Status | 含义 |
|--------|------|
| **OK** | write 返 true，且**回读 == 写入的辨识值** → 写确实生效 |
| **FAIL** | write 返 false（I2C 写失败）；或**回读 == 写前原值**（write 没生效，被吞掉/拒落盘） |
| **REVIEW** | 回读既不是写入值、也不是原值 → 被固件改写/截断/部分写入，需人工查 |

### 6.5 已知点：writeESOR_WID（值 >65535 会截断）

`MCU.cpp:1518 writeESOR_WID` 先 `memcpy(buf,&wid,4)`，再 `iic->write(reg,buf,2)`（寄存器 2 字节），
即**只写低 16 位**。默认辨识值 **12345** 在 2 字节范围内 → 回读 == 写入 → **OK**（正常写确认通过）。
只有写入值 **>65535** 时高位被丢弃、回读 ≠ 写入 → FAIL（默认辨识值不会触发）。
这是 MCU.cpp 的行为，**本工具只记录不修复**（non-goals）。

### 6.6 事后恢复

```bash
# 用 Phase 2 开始时落盘的快照恢复
LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH ./bin/htc_mcu_api_test --restore mcu_snapshot_<ts>.json
# SUMMARY (restore): Total=33 OK=?? FAIL=?? SKIP=??
# exit 1 表示有写失败（看具体哪行 FAIL）
```

恢复后建议再跑一次 Phase 1 确认配置回到快照值：
```bash
LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH ./bin/htc_mcu_api_test --no-write
```

## 7. 通过标准

### 7.1 Phase 1（只读，必跑）
- 程序跑完 99 行 read 无崩溃，输出逐行表 + 汇总，exit 0。
- REVIEW 的项经人工复核后归类（真 0 / I2C fail / 未接传感器），记录到测试报告。
- 无 FAIL（Phase 1 不应出现 FAIL）。

### 7.2 Phase 2（write，选跑，高风险）
- 程序跑完 33 行 write，输出逐行表 + 汇总。
- 大部分 write 应 OK（因为写的是快照原值，空操作）。
- writeESOR_WID 预期 FAIL（已知 bug，可接受）。
- 其它 FAIL 需逐一排查（可能是 MCU 拒绝非法值、或 read-back 格式差异如 GPS）。
- exit 0 表示无 FAIL，exit 1 表示有 FAIL（非零不一定是工具 bug）。

### 7.3 --restore 回路
- restore 后 SUMMARY 多为 OK（写回快照原值）。
- 再跑 Phase 1 确认读回值 ≈ 快照。

## 8. 已知判读注意（来自代码审计）

- **convertVoltage(126)**：MCU.cpp 实现返 `"12.0"`（非 `"12.6"`，第二位小数被整数除法吃掉）。
  工具 expect 标的是 `"12.6"`，所以这一行会判 REVIEW（工具 expect 与实际不符，非 MCU 异常）。
  人工判读时忽略这一行的 REVIEW。
- **readGps**：GPS 无定位时 MCU 返 `",,,,,"`，工具按"非空"判 OK（可能误判）。人工确认 GPS 实际定位状态。
- **readSignalCF**：范围表填 [0,6000]，CF > 6000 的合法值会被判 REVIEW（偏保守）。

详见 `artifacts/T4-tester-evidence.md §7`。

## 9. 落盘日志

每次运行自动落盘 `mcu_api_test_<timestamp>.log`（cwd），含完整逐行 + 汇总。
可用 `--log <path>` 自定义路径。日志供事后归档对比。

## 10. 相关文档

- `../../artifacts/T4-tester-evidence.md` —— 双平台编译/sim 冒烟/代码审计证据
- `../../artifacts/T4-planner-full.md` —— 工具设计方案
- `../refs/mcu-api-and-register-inventory.md` —— 132 方法清单 + 寄存器映射（范围来源）
- `mcu-http-api-test.md` —— HTTP 端点真机联调（互补，覆盖 HTTP 触达的 23 个 MCU 方法）
- `../../../src/hardware/mcu/MCU.h` —— API 真相源（被测对象）
