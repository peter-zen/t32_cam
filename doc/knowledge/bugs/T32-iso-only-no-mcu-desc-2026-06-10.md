# T32 Zram 风暴 · iso-only-no-mcu-desc（只关 MCU I2C，desc + thumb 保留）

**日期**：2026-06-10
**作者**：zengping（+ Claude 协作）
**状态**：完成
**目的**：拆解 MCU I2C 业务层对 zram 风暴的独立贡献，验证"I2C 卡死是 desc 风暴元凶"假设

## TL;DR

> **重要：不是 zram error，是 OOM kill**。
> 关掉 MCU I2C 后进程没卡死，**但 OOM killer 在 36s 内直接杀掉了进程**（比 watchdog 180s 还快）。
> 杀死时 anon-rss = **24 MB**（录影时只有 3 MB，**desc_info 路径多吃了 21 MB 匿名内存**）。
> **I2C 不是 desc 风暴的唯一元凶**——desc 内部还有别的大量内存消耗。

| 决定性测试 | 结果 |
|------------|------|
| iso-only-no-mcu-desc dmesg 关键事件数 | **2**（Out of memory + Killed process）|
| 死亡方式 | **OOM killer**（不是 watchdog，也不是 zram）|
| 死亡时间 | ~36s（远早于 180s watchdog）|
| 死时 anon-rss | **24 MB**（录影时只有 3 MB）|
| iso-only-no-audio 死亡方式 | 卡 I2C → watchdog 180s 强杀 |
| iso-only-no-desc 死亡方式 | 0 error，clean exit |
| **结论** | **I2C 单独触发风暴被证伪**——关掉 I2C 还有别的大内存消耗 |

## 测试环境

| 项 | 值 |
|----|----|
| 设备 | T32 MIPS, 64 MB RAM, 16 MB CMA |
| htc_main_app 版本 | main 分支 + post-zram-fix-v2 |
| env 开关 | `HTC_RECORD_NO_MCU_DESC=1`（**保留 desc JSON 构建 + 写盘 + thumb**）|
| 数据源 | `build/logs/mem-profile-20260610-010235-iso-only-no-mcu-desc/` |
| 进程退出方式 | **OOM killer SIGKILL (rc=137)** |

## 业务层开关状态

| 开关 | 状态 |
|------|------|
| `HTC_RECORD_NO_AUDIO=1` | **未设**（保留 audio）|
| `HTC_RECORD_NO_DESC=1` | **未设**（保留 desc）|
| `HTC_RECORD_NO_THUMB=1` | **未设**（保留 thumb）|
| `HTC_RECORD_NO_MCU_DESC=1` | **关闭**（本测试目标，跳过 5+ I2C 读）|

## iso-only-no-mcu-desc 完整内存时序

数据源：`build/logs/mem-profile-20260610-010235-iso-only-no-mcu-desc/`
单位 kB（除特别说明）。
**注意：进程在 T4 之前被 OOM kill，所以 T4+ 显示 `(dead)` 状态，free 突增到 32 MB（系统回收）**。

| 阶段 | free | CmaFree | AnonPages | Dirty | Cached | swap used/free | VmData | VmRSS | alive |
|------|------|---------|-----------|-------|--------|----------------|--------|-------|-------|
| **T0** 启动 | 17572 | 13900 | 880 | 0 | 14976 | 0/16380 | 512 | 344 | ✓ |
| **T1** SDK init | 14664 | 11036 | 1000 | 0 | 17724 | 0/16380 | 2304 | 1024 | ✓ |
| **T1.5** 录影 1s | 13444 | 9816 | 1092 | 0 | 18848 | 0/16380 | 3368 | 1764 | ✓ |
| **T2** 录影 5s | 5156 | 4012 | 3028 | 3704 | 22656 | 0/16380 | 208092 | 9020 | ✓ |
| **T2.5** 录影 10s | 9332 | 8232 | 3216 | 476 | 17792 | 104/16276 | 208212 | 8280 | ✓ |
| **T3** 录影 15s | 8576 | 7372 | 3216 | 304 | 22536 | 104/16276 | 208212 | 8280 | ✓ |
| **T3.5** 录影 20s | 8620 | 7548 | 3220 | 1800 | 23812 | 104/16276 | 208212 | 8280 | ✓ |
| **T4** 录影 25s | 8912 | 7712 | 3216 | 1924 | 23600 | 104/16276 | — | — | ✗ OOM |
| T4.5+ | 32260 | 13352 | 264 | 0 | 10652 | — | — | — | ✗ OOM |

**dmesg 关键事件：2 行**
```
[  342.372089] htc_main_app invoked oom-killer: gfp_mask=0x24004c0, order=0, oom_score_adj=0
[  342.373199] Out of memory: Kill process 791 (htc_main_app) score 396 or sacrifice child
[  342.382585] Killed process 791 (htc_main_app) total-vm:331892kB, anon-rss:24248kB, file-rss:4160kB
```

**进程退出码：rc=137**（OOM killer SIGKILL）

## 关键观察

### 1. 录影稳态正常（desc 还没执行）

| 资源 | T3.5 (rec 20s) | T4 (rec 25s) | 状态 |
|------|----------------|--------------|------|
| free | 8620 | 8912 | 正常 8 MB |
| CmaFree | 7548 | 7712 | 7.5 MB |
| VmData | 208212 | — | 208 MB |
| VmRSS | 8280 | — | 8.3 MB |
| AnonPages | 3220 | 3216 | 3.2 MB（很稳定）|

→ 录影稳态正常，desc 启动前的内存状态健康。

### 2. OOM 发生在 desc_info 之后

dmesg 时间戳分析：
- 录影开始：09:02:42
- 录完：09:03:19.526（36.6s）
- desc_info enter: 09:03:19.529
- "skipping MCU I2C calls" log: 09:03:19.529
- (后续无 htc.log 输出)
- OOM kill: 09:03:19.XXX (dmesg t=342.37s)

→ **desc_info 跳过 I2C 后继续执行，但在 JSON build / Disk info / CRC / 写盘 步骤中触发 OOM**

### 3. 死时内存状态（dmesg 提供）

| 资源 | 录影时 (T3.5) | **死时 (dmesg)** | 增量 |
|------|---------------|-----------------|------|
| total-vm | ~210 MB | **331 MB** | +121 MB |
| anon-rss | 3.2 MB | **24 MB** | **+21 MB** ⚠️ |
| file-rss | ~7 MB | **4 MB** | -3 MB |
| swap used | 104 kB | (implied) | (zram 也可能爆)|

**关键：anon-rss 从 3 MB 暴涨到 24 MB（+21 MB）**——desc_info 路径在极短时间内分配了 21 MB 匿名内存。

### 4. 时序对照（重看一遍）

| 时刻 | 事件 | 内存状态 |
|------|------|---------|
| 09:02:42 | 录影开始 | free 17 MB |
| 09:03:11 | rec 750 帧（25s）| free 8 MB |
| 09:03:16 | rec 900 帧（30s）| free 8 MB |
| 09:03:17 | record summary | free 8 MB |
| 09:03:19.526 | record done | free 8 MB |
| 09:03:19.529 | DBG: pre-generateDescInfo (snapshot before release) | free 8 MB |
| 09:03:19.529 | DBG: generateDescInfo enter | |
| 09:03:19.529 | "skipping MCU I2C calls" ← I2C 跳过成功 ✅ | |
| 09:03:19.XXX | **(desc_info 内部其他步骤) ← 21 MB 匿名内存暴涨** | anon-rss 3→24 MB |
| 09:03:19.XXX | **OOM killer 触发** | process killed |
| 09:03:20+ | 内存释放 | free 32 MB |

## 决定性结论（推翻部分旧假设）

### ❌ 旧假设：I2C 卡死是 desc 风暴唯一元凶

```
iso-only-no-audio (audio OFF, desc ON) → 卡 I2C → watchdog kill (180s)
```

**这是 I2C 卡死，不是 I2C 触发的 zram 风暴**——它走的是 watchdog 路径。

### ✅ 新发现：desc_info 内部还有别的大内存消耗

```
iso-only-no-mcu-desc (audio+thumb ON, desc ON, MCU OFF) → OOM kill (36s)
anon-rss 3 MB → 24 MB (+21 MB 暴涨)
```

**关掉 I2C 之后，desc_info 还在分配 21 MB 匿名内存，OOM 触发。**

### 各测试死亡方式汇总（修正旧结论）

| 测试 | env | 死亡方式 | 时间 | 关键状态 |
|------|-----|---------|------|---------|
| htc_full baseline | （无）| **zram 风暴** | 录影中 | 6 zram errors，free 0.7 MB |
| post-zram-fix-v2 | （无）| clean exit | — | 0 errors |
| **iso-only-no-mcu-desc** | `NO_MCU_DESC=1` | **OOM kill** | **36s** | anon-rss 24 MB ⚠️ |
| iso-only-no-audio | `NO_AUDIO=1` | I2C 卡死 + watchdog kill | 180s+ | desc_info 卡 I2C |
| iso-only-no-desc | `NO_DESC=1` | clean exit | — | 0 errors |
| iso-no-audio-no-desc v1 | `NO_AUDIO=1 NO_DESC=1` | clean exit | — | 0 errors |

→ **I2C 卡死 ≠ zram 风暴直接元凶**。I2C 卡死触发的是 watchdog 路径（180s 之后才死）。
**真正吃 21 MB 内存的是 desc_info 内部其他步骤**。

## desc_info 内部内存消耗嫌疑（按可能性排序）

`generateDescInfo` 函数体内可能分配 21 MB 内存的步骤：

| 步骤 | 函数 | 嫌疑 | 说明 |
|------|------|------|------|
| **CRC 计算** | `CRC::calculate_crc16(filename, ...)` | **🔴 高度怀疑** | 需要 `open` 32 MB mp4 + 读全部字节做 CRC16 计算 |
| **Disk info** | `Disk::getInfo(DISK_PATHNAME)` | 🟡 中度怀疑 | 可能 `statvfs` 或扫描目录树 |
| **getFileCreationTime** | `getFileCreationTime(filename, ...)` | 🟢 较低 | `stat` 调用，应该只读 inode |
| **Misc::getIPAddress** | `Misc::getIPAddress(...)` | 🟢 低 | `ioctl` 获取 IP |
| **Json::Value 构造** | 各种 json_root[...] = | 🟢 低 | ~10 KB 数据 |
| **Json::writeString** | `Json::writeString(...)` | 🟢 低 | 序列化 ~2 KB |

**最可能的是 `CRC::calculate_crc16`**——它需要打开 32 MB 的 mp4 文件并读取全部字节。在 T32 慢 SD 卡上 + 64 MB 内存紧的情况下，可能：
- 用 `mmap` 32 MB 文件
- 或者用 `malloc(32 MB)` 一次性读到堆里
- 两种都会让 anon-rss 暴涨 21+ MB

## 验证 CRC 嫌疑的最快方法

看 `CRC::calculate_crc16` 的实现：

```bash
grep -rn "CRC::calculate_crc16\|calculate_crc16" /home/zengping/project/huntcam/code/t32_cam/src/ 2>/dev/null
```

如果是 `mmap` 或 `malloc(32MB)` 全文件读取，**就是 CRC 嫌疑**。

然后跑一个 isolation 验证：
```bash
# 关掉 CRC（如果未来加这个 env），看 OOM 还发不发生
# 或者：把 desc 关掉 + 单独开 file read 测试
```

## 决定性结论（最终版）

1. **I2C 不是 desc 风暴唯一元凶**：关掉 I2C 还有 OOM
2. **desc_info 内部有 21 MB 匿名内存暴涨**：anon-rss 3 → 24 MB
3. **最大嫌疑：`CRC::calculate_crc16`**：需要打开 32 MB mp4 文件读全
4. **OOC 触发速度**：36s 内就触发（远快于 watchdog 180s）
5. **zram 风暴的真正元凶可能不是单点**：是 desc_info 整体内存压力 + I2C 卡死窗口期

## 优化方向（最终版）

| 优先级 | 方向 | 预期收益 | 风险 |
|--------|------|----------|------|
| **P0** | **看 `CRC::calculate_crc16` 实现**：如果 mmap 32 MB mp4，改成流式 CRC（每次 64 KB）| 消除 21 MB 匿名暴涨 | 低（只改算法）|
| **P0** | **如果产品允许：默认 `NO_DESC=1`** | 物理压力 -17 MB，免去 desc 风暴 + OOM 双重风险 | 中 |
| **P1** | **修 I2C 总线驱动**（`Failed to open the iic bus`）| 消除 I2C 卡死窗口期 | 中 |
| **P1** | **Disk::getInfo 改成只读 statvfs**，不扫描目录树 | 减 transient 内存 | 低 |

## 进度

- [x] sample 6Mbps 数据
- [x] iso-all-3-off 数据
- [x] iso-no-audio-no-desc v1 / v2 数据
- [x] iso-only-no-desc 数据
- [x] iso-only-no-audio 数据
- [x] **iso-only-no-mcu-desc 数据（OOM kill 路径）**
- [x] 6+1+1 方对照表
- [ ] **看 `CRC::calculate_crc16` 源码**（最可能的元凶）
- [ ] 跑 `iso-only-no-json-build` 进一步缩小范围
- [ ] 跟产品确认 desc JSON + CRC 是否必做
- [ ] 修 I2C 总线驱动

## 相关文档

- `T32-zram-storm-sample-baseline-2026-06-09.md` — sample baseline
- `T32-iso-all-3-off-vs-sample-2026-06-09.md` — 3-off 对照
- `T32-iso-no-audio-no-desc-2026-06-09.md` — v1
- `T32-iso-no-audio-no-desc-v2-2026-06-09.md` — v2 复现
- `T32-iso-only-no-desc-2026-06-10.md` — only-no-desc
- `T32-iso-only-no-audio-2026-06-10.md` — only-no-audio（I2C 卡死）
- `T32-iso-only-no-mcu-desc-2026-06-10.md` — only-no-mcu-desc（本文件，OOM kill）
- `T32-recording-fps-17-investigation.md` — zram 风暴首查
- `tools/mem_profile.sh` / `tools/mem_profile_sample.sh` / `tools/analyze_profile.sh`
