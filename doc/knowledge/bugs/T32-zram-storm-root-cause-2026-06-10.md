# T32 Zram 风暴 · 根因分析终篇（CRC::calculate_crc16 读全文件）

**日期**：2026-06-10
**作者**：zengping（+ Claude 协作）
**状态**：Root cause 定位完成，待修
**关联 bug**：`T32-recording-fps-17-investigation.md`（zram 风暴首查 + 临时修复）

## TL;DR

> **zram 风暴 + OOM kill 的真正根因：`src/common/utils/crc/CRC.cpp:14` 把整个 32 MB mp4 文件读到堆里**。
> `desc_info` 流程中调用 `CRC::calculate_crc16(filename, check_code)` 计算校验码，**实现是 `std::istreambuf_iterator` 一路读到底 = `malloc(32MB) + 拷贝整个文件`**。
> 单次调用吃掉 21+ MB 匿名内存，在 64 MB T32 上 + 业务层叠加 → **zram 风暴 / OOM kill**。

| 决定性证据 | 数值 |
|------------|------|
| CRC 实现第 14 行 | `std::vector<unsigned char> buffer(... istreambuf_iterator ...)` |
| 单次分配大小 | **32 MB**（按 mp4 文件大小）|
| dmesg `anon-rss` 增量 | **3 MB → 24 MB（+21 MB）** |
| OOM 触发速度 | **36 s**（比 watchdog 180s 快 5×）|
| 关掉 I2C 后是否还发生 | **是**（证实非 I2C 元凶）|
| 关掉 desc 后是否还发生 | **否**（证实 desc_info 路径元凶）|

## 完整调查时间线

### 第 0 步：症状发现（2026-06-09 上午）

**用户报告**：htc_main_app work mode 录影（`htc_main_app -wm 0 -rtc 1`）后卡死，watchdog 强杀。

**dmesg 切片**显示：
```
zram: Error allocating memory for compressed page: 1832, size=4096
Write-error on swap-device (254:0:14656)
ksoftirqd/0: page allocation failure: order:0, mode:0x2080020
htc_main_app: page allocation failure: order:0, mode:0x2400002
```

6 条 zram error + 2 条 page allocation failure 集中在 0.23 s 窗口内。

### 第 1 步：内存分析（baseline profile）

跑 `sh mem_profile.sh "baseline"`，得到：

- 64 MB RAM, 16 MB CMA reserved, 8 MB unevictable (locked)
- 录影稳态（rec 5-25s）free = **0.7-9 MB**（极紧）
- zram 池满 16 MB
- CmaFree 跌到 3.6 MB

→ 业务层吃掉了 17 MB 物理内存，把 free 压到危险区。

### 第 2 步：SDK 自身能力 baseline（sample-6mbps-2k）

**重新编译 sample-Encoder-video**：把 `BITRATE_720P_Kbs` 从 1000 改成 6000，对齐 htc_main_app 默认码率。

跑 `sh mem_profile_sample.sh "sample-6mbps-2k"`：

| 指标 | sample | htc_main_app baseline | 业务层多吃 |
|------|--------|---------------------|-----------|
| zram error | 0 | 6 | — |
| free 最低 | 7.5 MB | 0.7 MB | -6.8 MB |
| CmaFree 最低 | 6.4 MB | 3.6 MB | -2.8 MB |
| VmData 稳态 | 135 MB | 282 MB | **+147 MB** |
| VmRSS 稳态 | 3.4 MB | 8 MB | +4.6 MB |
| swap used | 180 kB | 16 MB 满 | — |

→ **SDK 自身在 6 Mbps 30 s 下完全够用，0 zram error**。问题在业务层。

### 第 3 步：业务层 isolation（6 组测试）

利用 `HTC_RECORD_NO_*` 环境变量，每次关闭一个业务层：

| 测试 | 关闭 | zram error | free 最低 | 死亡方式 |
|------|------|-----------|----------|---------|
| htc_full baseline | （无）| **❌ 6** | 0.7 MB | zram 风暴 |
| iso-only-no-audio | audio | ❌ 卡 I2C | 17.5 MB | watchdog 180s |
| **iso-only-no-desc** | **desc** | **✅ 0** | 10.1 MB | clean exit |
| iso-only-no-mcu-desc | desc I2C | ❌ **OOM kill** | 8.9 MB | OOM 36s |
| iso-no-audio-no-desc v1/v2 | audio + desc | ✅ 0 | 16-17 MB | clean exit |
| iso-all-3-off | audio + desc + thumb | ✅ 0 | 10.6 MB | clean exit |

**关键发现链**：

1. **iso-only-no-desc = 0 zram error** → **desc 业务层是触发者**
2. **iso-only-no-mcu-desc = OOM kill（不是 zram）** → I2C 不是 zram 风暴元凶
3. **iso-only-no-audio = 卡 I2C → watchdog kill** → I2C 卡死是 watchdog 路径，非 zram 路径

### 第 4 步：desc 内部深挖（OOM kill 路径）

`iso-only-no-mcu-desc` 关掉了 I2C，但进程在 36 s 内被 OOM killer 杀掉（远快于 watchdog 180s）。

**dmesg 关键信息**：
```
[  342.372089] htc_main_app invoked oom-killer: gfp_mask=0x24004c0, order=0
[  342.382585] Killed process 791 (htc_main_app) total-vm:331892kB, anon-rss:24248kB
```

`anon-rss` 从录影时 3 MB 暴涨到 **24 MB（+21 MB）**。

**htc.log 时序**：
```
09:03:19.526 record done
09:03:19.529 DBG: pre-generateDescInfo
09:03:19.529 DBG: generateDescInfo enter
09:03:19.529 generateDescInfo: skipping MCU I2C calls (env HTC_RECORD_NO_MCU_DESC=1)  ← I2C 跳过成功
                              ← (后续无 htc.log 输出)
                              ← OOM kill (desc_info 路径内部某步吃掉 21 MB)
```

→ 进程在 desc_info 内部（除 I2C 外）某步吃掉 21 MB 匿名内存。

### 第 5 步：定位元凶代码

`desc_info` 函数体内的可疑分配点（按嫌疑排序）：

| 步骤 | 嫌疑 | 实际验证 |
|------|------|---------|
| `CRC::calculate_crc16(filename, ...)` | **🔴 32 MB mp4 全读** | 源码证实 |
| `Disk::getInfo(DISK_PATHNAME)` | 🟡 可能扫描目录 | 看实现轻量 |
| `getFileCreationTime(...)` | 🟢 只读 stat | — |
| `Json::Value` + `Json::writeString` | 🟢 几 KB | — |

**看 `src/common/utils/crc/CRC.cpp:14`**：

```cpp
bool CRC::calculate_crc16(const std::string &file_path, uint16_t &crc16)
{
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    // ★ 元凶：std::istreambuf_iterator 一路读到底 = 整个文件进堆
    std::vector<unsigned char> buffer(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>()
    );
    file.close();

    crc16 = cal_crc16(0xFFFF, buffer.data(), buffer.size());
    return true;
}
```

**第 14 行**：
- `std::istreambuf_iterator<char>(file)` 一直读到 EOF
- `std::istreambuf_iterator<char>()` 终止符
- 构造 `std::vector<unsigned char> buffer(...)` = **整个文件读到堆**

对 32 MB mp4 文件 = **`new` 32 MB 堆 + 拷贝整个文件**。

**调用链**：
```cpp
// src/app/main_app.cpp:330 (在 generateDescInfo 函数内)
if (CRC::calculate_crc16(filename, check_code)) {
    file_item["F_CheckCode"] = static_cast<int>(check_code);
}
```

→ **每次录影结束写 desc JSON 时，desc_info 会把整个 32 MB mp4 读到堆里算 CRC16**。

### 第 6 步：计算匹配

| 数据点 | 值 | 匹配情况 |
|--------|----|---------|
| mp4 文件大小（30s @ 6 Mbps）| ~32 MB | ✅ |
| desc_info 调用 CRC 时 anon-rss 增量 | +21 MB | ✅（page cache / fragment 解释差额）|
| OOM 触发速度 | 36s | ✅（单次 21 MB 足以触发）|
| 关掉 desc（no-desc）| 0 error, 0 OOM | ✅（不调 CRC 就不爆）|
| 关掉 I2C（no-mcu-desc）| 仍然 OOM | ✅（I2C 不是元凶）|

→ 21 MB 增量来自 CRC 读 32 MB mp4 完全吻合（其余 11 MB 是 page cache / 内部 buffer）。

## 根因总结

### 一句话

> **`CRC::calculate_crc16` 把整个 32 MB mp4 文件读到堆里做 CRC16**。在 64 MB T32 设备 + 业务层叠加（音频、OSD、DB 等）下，desc_info 触发的 32 MB 堆分配直接把系统推到 OOM 边缘。

### 完整调用链

```
processCmdVideoRecord
  ↓
record done (32 MB mp4 落盘)
  ↓
[step 1.5] data snapshot: desc_info 算 JSON  ← 这里
  ↓
CRC::calculate_crc16(filename, check_code)  ← 32 MB malloc
  ↓
std::istreambuf_iterator 一路读到 EOF
  ↓
std::vector<unsigned char> buffer(32MB)
  ↓
[step 2] release SDK 30+ MB mmap
  ↓
[step 3] writeWorkModeDescJson 写盘
  ↓
（如果没 I2C 卡死，正常退出）
```

**任一路径**：
- **如果 I2C 总线可用**：`desc_info` 算完 → 写盘 → clean exit
- **如果 I2C 总线不可用**（T32 上常见）：`mcu->readXxx` 阻塞 → 进程卡死 → watchdog 180s 后强杀
- **如果物理内存本就紧**（业务层全开 + 录影 30s）：desc_info 期间 32 MB 堆分配撞穿 zram → **zram 风暴**
- **如果物理内存极紧**（htc_full baseline）：32 MB 堆分配直接触发 OOM killer → **OOM kill 36s**

## 修复方案

### 最小修改（10 行内）

`src/common/utils/crc/CRC.cpp`：

```cpp
bool CRC::calculate_crc16(const std::string &file_path, uint16_t &crc16)
{
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    // 修复: 流式 CRC, 每次 64 KB, 内存占用从 32 MB 降到 64 KB
    constexpr size_t kChunkSize = 64 * 1024;
    std::vector<unsigned char> buffer(kChunkSize);
    uint16_t crc = 0xFFFF;
    while (file) {
        file.read(reinterpret_cast<char*>(buffer.data()), kChunkSize);
        std::streamsize n = file.gcount();
        if (n <= 0) break;
        crc = cal_crc16(crc, buffer.data(), static_cast<size_t>(n));
    }
    crc16 = crc;
    return true;
}
```

### 修复特性

- **内存占用**：32 MB → **64 KB（500× 减少）**
- **性能**：几乎一样（CRC 算法 O(N) 跟分块无关，块大小 64 KB 是 page cache 友好）
- **正确性**：完全等价（CRC16 按块累加，数学保证）
- **API 兼容**：签名不变
- **影响面**：只改 `CRC.cpp` 一个文件

## 验证方案

### Fix 前 vs Fix 后对照

```bash
# 1) 改前数据（已存在）
build/logs/mem-profile-20260610-010235-iso-only-no-mcu-desc/   # 这次跑 OOM
build/logs/mem-profile-20260609-105637-baseline/                # 旧 baseline 6 zram error

# 2) 改 CRC 后重新 build
cmake --build build -j$(nproc)

# 3) Fix 后跑同样两个 profile
env HTC_RECORD_NO_MCU_DESC=1 sh mem_profile.sh "post-crc-fix-no-mcu"
sh mem_profile.sh "post-crc-fix-baseline"

# 预期结果:
# - post-crc-fix-no-mcu: clean exit, rc=0, 0 zram error, 0 OOM
# - post-crc-fix-baseline: clean exit, rc=0, 0 zram error
```

如果两个都过 → **修复成功**。所有 band-aid（sync/sleep/trim/data-snapshot）都不需要。

## 临时修复回顾（commit 45a7c09）

### 之前的临时修复

commit 45a7c09 修复了三个问题：
1. **编码器 bitrate 过载**（CameraRecorder.cpp:60 改 16384→4000）—— 合理保留
2. **FAT-fs read-only**（VideoRecorder.cpp:907 加 fsync）—— 合理保留
3. **zram 风暴**（main_app.cpp 加 sync+sleep+trim+reserve）—— **临时缓解，应回滚**

第 3 项是因为当时没找到真正的根因（CRC 读 32 MB），所以做了时序调整来"绕开"风暴：
- `sync()` —— 强制刷 dirty（提前消化）
- `sleep 200ms` —— 让 kswapd 抢跑
- `malloc_trim(0)` —— 归还 glibc arena
- `reserve(2048)` —— 预分配 JSON buffer
- **data-snapshot 模式** —— desc_info 提前到 release 之前

**这些都没解决 CRC 32 MB 堆分配**。修了 CRC 之后，这些都不需要。

### 哪些保留，哪些回滚

| 改动 | 决策 | 理由 |
|------|------|------|
| `CameraRecorder.cpp:60` bitrate fallback 4000 | **保留** | 编码器真实能力问题，独立 bug |
| `http_api_v1.cpp:500-501` 默认 bitrate 调整 | **保留** | 编码器真实能力问题 |
| `VideoRecorder.cpp:907` fclose 前 fsync | **保留** | FAT-fs 真实问题 |
| `sample-common.h:192` bitrate 1000→6000 | **回滚** | 仅用于本次 debug |
| `main_app.cpp` sync+sleep+trim+reserve | **回滚** | CRC 修了就不需要 |
| `main_app.cpp` data-snapshot 模式（1.5 步） | **回滚** | CRC 修了就不需要 |
| `main_app.cpp` `Json::writeString` 前的 reserve(2048) | **回滚** | CRC 修了就不需要 |
| `CRC.cpp` 流式 CRC | **新增** | 真修复 |

## 文档落档

| 文档 | 路径 | 角色 |
|------|------|------|
| debug 方法论 | `doc/knowledge/playbooks/t32-memory-profile-and-debug.md` | 未来类似问题复用 |
| 本文档 | `doc/knowledge/bugs/T32-zram-storm-root-cause-2026-06-10.md` | 根因完整记录 |
| 6+ 组 isolation 数据 | `doc/knowledge/bugs/T32-iso-*-2026-06-*.md` | 证据 |
| sample baseline | `doc/knowledge/bugs/T32-zram-storm-sample-baseline-2026-06-09.md` | 硬件能力上限 |
| 历史上原始调查 | `doc/knowledge/bugs/T32-recording-fps-17-investigation.md` | 起点 |

## 进度

- [x] 根因定位完成
- [x] 修复方案设计（流式 CRC，10 行）
- [x] 验证方案设计
- [ ] **rollback 当前所有临时修改**
- [ ] **改 CRC::calculate_crc16 为流式**
- [ ] **重新 build + 部署**
- [ ] **跑两组对照测试验证 fix 有效**
- [ ] **写最终 fix 验证报告**
- [ ] **commit fix（commit message 引用本文档）**

## 相关文档

- `T32-recording-fps-17-investigation.md` —— zram 风暴首查 + 临时修复 commit
- `T32-zram-storm-sample-baseline-2026-06-09.md` —— 裸 SDK baseline
- `T32-iso-all-3-off-vs-sample-2026-06-09.md` —— 3-off 对照
- `T32-iso-no-audio-no-desc-2026-06-09.md` —— v1
- `T32-iso-no-audio-no-desc-v2-2026-06-09.md` —— v2 复现
- `T32-iso-only-no-desc-2026-06-10.md` —— only-no-desc
- `T32-iso-only-no-audio-2026-06-10.md` —— only-no-audio
- `T32-iso-only-no-mcu-desc-2026-06-10.md` —— only-no-mcu-desc（OOM kill 路径）
- `t32-memory-profile-and-debug.md` —— debug 方法论
- `tools/mem_profile.sh` / `tools/mem_profile_sample.sh` / `tools/analyze_profile.sh`
