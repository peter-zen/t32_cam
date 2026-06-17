# tools/

离线/诊断工具集合。这些产物**不**随固件主程序部署，仅用于现场排障与回归验证。

## 目标

| 目标 | 源文件 | 作用 |
|------|--------|------|
| `htc_mcu_api_test` | `mcu_api_test.cpp` | 穷举调用 `src/hardware/mcu/MCU.h` 全部 public API，逐行输出「API → 返回 → OK/REVIEW/FAIL/SKIP/STUB」五级判定 + 末尾汇总 + 落盘日志。 |
| `json_stress_test` | `json_stress_test.cpp` | 模拟 `generateDescInfo` 的 jsoncpp 路径压力测试，验证 zram 行为。纯用户态，无 SDK/HAL。 |

## htc_mcu_api_test

数据驱动的 MCU 黑盒遍历器：
- **Phase 1（读）**：逐个调用全部 read 类 API（int/string/uint/tm/bool/纯函数），按 `doc/knowledge/refs/mcu-api-and-register-inventory.md` 的期望范围做四级判定。
- **Phase 2（写）**：默认关闭。`--write` 开启后，对全部 write 类 API 写**辨识值**（在合法范围内、非 0、非默认、异于当前值）+ 回读比对——回读能对上辨识值才算证明 write 真的生效（不会被「写回原值」那种空操作掩盖）。`setDatetime` 写当前系统时间。写前自动快照、`--restore` 可恢复。另提供 `--write-original`（写回原值≈空操作，只验证调用路径，不改配置）。

### 四级状态判定（核心）

| 状态 | 触发条件 |
|------|----------|
| **OK** | read 返回非 0 且落在 inventory 期望范围内；或 write 后**回读 == 写入的辨识值**（写确实生效） |
| **STUB** | MCU.cpp 里是 stub（直接返回常量、不读 I2C、未实现）——真机上也永远返回那个值，无法验证功能。汇总单列。 |
| **REVIEW** | read 返回 0（I2C 失败与真实 0 值不可区分）、越界、或 string 为空；或 write 后回读既非写入值也非原值（被固件改写/截断） |
| **FAIL** | `readWorkingMode` 返回 -1（MCU 的 I2C 失败信号）；或 write 返回 false（I2C 写失败），或 write 后**回读 == 写前原值**（write 没生效，被吞/拒落盘） |
| **SKIP** | `--no-write`/sim 下的所有 write（Phase 2 全跳过） |

> 为何 0 一律 REVIEW：`MCU.cpp` 每个 read 在 `iic->read()<=0` 时 `return 0`（如 `MCU.cpp:107/133/160/476`），真实 0 值也是 0，C++ 层无法区分。

### 用法

```
htc_mcu_api_test [--no-write] [--write] [--write-original] [--yes] [--group <Name>]
                 [--log <path>] [--restore <snapshot.json>] [--help]

  (default)         Phase 1 only (read sweep), writes skipped
  --write           Enable Phase 2 with DISTINCTIVE values (confirms writes took effect; prompts unless --yes)
  --write-original  Enable Phase 2 writing back snapshot orig values (no-op-ish; call-path only)
  --no-write        Force skip Phase 2 (default)
  --yes             Skip write confirmation prompt
  --group <Name>    Only run one group: Version|Battery|Signal|Sensor|ESOR|...
  --log <path>      Log file (default: mcu_api_test_<ts>.log in cwd)
  --restore <f>     Restore mode: write back values from snapshot, then exit
  --help            Print usage
```

### 双平台编译

两平台都构建（不门控 `BUILD_FOR_SIMULATION`），满足 CLAUDE.md 双平台编译硬约束：

```bash
cmake --build build_sim -j$(nproc) --target htc_mcu_api_test   # PC sim
cmake --build build -j$(nproc) --target htc_mcu_api_test       # T32 交叉
```

sim 下 I2C 被 bypass（`I2C_BYPASS`/`MCU_EXIST`），read 全返 0 → 全 REVIEW，仅验证编译/表枚举完整/调用路径不崩。

### 真机运行（T32，NFS mount `/mnt/huntcam`）

```bash
cd /mnt/huntcam
LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH ./bin/htc_mcu_api_test              # Phase 1 only
LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH ./bin/htc_mcu_api_test --write      # +Phase 2（会写）
LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH ./bin/htc_mcu_api_test --restore mcu_snapshot_<ts>.json
```

> `/dev/hc32l13x` 需 root，以 root 运行或确保 udev 赋权。

### 约束

- **不改** `src/hardware/mcu/MCU.{h,cpp}`（被测对象，只 include + 调用）。
- **不碰** `src/hal/**`（PIC-owned）。
- 发现的 MCU.cpp 行为（如 `writeESOR_WID` 值 >65535 高位截断）只记录为备注，不在本工具修复。
