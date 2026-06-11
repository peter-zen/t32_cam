# T32 CRC Fix 验证报告（2026-06-10）

**日期**：2026-06-10
**作者**：zengping（+ Claude 协作）
**状态**：✅ Fix 验证通过，可以 commit
**关联**：`T32-zram-storm-root-cause-2026-06-10.md`

## TL;DR

> **CRC::calculate_crc16 改为流式实现（64 KB chunk）后**：
> - baseline：6 zram errors → **0 errors**，rc 137 → **0**（clean exit）
> - iso-only-no-mcu-desc：OOM kill 36s → **clean exit**，VmRSS 不再暴涨到 10.7 MB
>
> **单点修复彻底消除 zram 风暴和 OOM kill 两个症状**。

## Fix 内容

**文件**：`src/common/utils/crc/CRC.cpp`
**变更**：1 处函数体（10 行 → 21 行，含注释）

```diff
-    std::vector<unsigned char> buffer(
-        (std::istreambuf_iterator<char>(file)),
-        std::istreambuf_iterator<char>()
-    );
-    file.close();
-    crc16 = cal_crc16(0xFFFF, buffer.data(), buffer.size());
+    constexpr size_t kChunkSize = 64 * 1024;
+    std::vector<unsigned char> buffer(kChunkSize);
+    uint16_t crc = 0xFFFF;
+    while (file) {
+        file.read(reinterpret_cast<char*>(buffer.data()), kChunkSize);
+        std::streamsize n = file.gcount();
+        if (n <= 0) break;
+        crc = cal_crc16(crc, buffer.data(), static_cast<size_t>(n));
+    }
+    crc16 = crc;
```

**内存占用**：32 MB → **64 KB**（500× 减少）
**正确性**：完全等价（CRC16 按块累加，数学保证）
**性能**：几乎无差异（CRC 算法 O(N) 跟分块无关）

## Fix 前 vs Fix 后对照

### 测试 1：baseline（无 env, 全部功能开启）

| 资源 | 修复前 | **修复后** | 改善 |
|------|--------|-----------|------|
| **rc 退出码** | 137 (watchdog 强杀) | **0 (clean exit)** | ✅ |
| **zram error 数** | **6** | **0** | ✅ |
| **OOM kill** | 0 | 0 | — |
| T2 free (rec 5s) | 5156 kB | **18448 kB** | +13 MB |
| T3.5 free (rec 20s) | n/a（已卡死）| **972 kB** | 系统撑住 |
| T4 free (rec 25s) | 8880 kB | **920 kB** | ⚠️ 仍然紧（业务层本身就需要 ~17 MB） |
| T4 VmRSS | 8020 kB | 8396 kB | ≈ 持平 |
| **T4.5 VmRSS (录完 desc_info)** | n/a | **8364 kB** | **不再暴涨** |
| T5 free (录完) | n/a | 7284 kB | 系统恢复 |

**关键观察**：
- 修复前：录影中就触发了 zram 风暴（6 errors），进程卡 I2C
- 修复后：录影中 free 极紧（0.9 MB）但没崩，desc_info 完成后 VmRSS 平稳，clean exit

### 测试 2：iso-only-no-mcu-desc（关闭 MCU I2C 调用）

| 资源 | 修复前 | **修复后** | 改善 |
|------|--------|-----------|------|
| **rc 退出码** | 137 (OOM killer) | **0 (clean exit)** | ✅ |
| **zram error 数** | 0（OOM 路径，不是 zram）| 0 | — |
| **OOM kill** | **2 行** | **0** | ✅ |
| 死亡时间 | 36s（远快于 watchdog 180s）| n/a（没死）| ✅ |
| **T4.5 VmRSS（desc_info 时）** | **7684 → 10760 kB（+3 MB 暴涨）**| **7680 → 7384 kB（正常）** | ✅ anon-rss 不再暴涨 |
| T2 free | 5156 kB | 22096 kB | +17 MB |
| T4 free | 8912 kB | 19060 kB | +10 MB |

**关键观察**：
- 修复前：CRC 读 32 MB mp4 → anon-rss 暴涨 21 MB → OOM killer 触发
- 修复后：CRC 流式 64 KB → anon-rss 不再暴涨 → 没有 OOM 触发

## T4.5 VmRSS 对比（最关键指标）

这是 **CRC fix 是否生效的最直接判据**：

| 测试 | 修复前 T4.5 VmRSS | 修复后 T4.5 VmRSS | 差值 |
|------|------------------|------------------|------|
| baseline | (n/a, 进程卡死) | 8364 kB | ✅ 平稳 |
| iso-only-no-mcu-desc | **10760 kB（暴涨 3 MB）** | **7384 kB（-296 kB, 正常）** | ✅ **不再暴涨** |

修复前 `T4 → T4.5` 阶段 VmRSS 跳变 **+3 MB**（CRC 读 32 MB 堆分配 + page cache 副本），修复后 **-296 kB**（正常波动）。

## 验证脚本

```bash
# 1) 跑两组对照测试
cd /mnt/huntcam
env HTC_RECORD_NO_MCU_DESC=1 sh mem_profile.sh "post-crc-fix-no-mcu"
sh mem_profile.sh "post-crc-fix-baseline"

# 2) 关键判读
for p in build/logs/mem-profile-*-post-crc-fix-*; do
  echo "=== $(basename $p) ==="
  [ -f $p/rc.log ] && cat $p/rc.log
  [ -f $p/dmesg.log ] && {
    zram=$(grep -cE "zram|Error allocating" $p/dmesg.log)
    oom=$(grep -cE "Out of memory|Killed process" $p/dmesg.log)
    echo "  zram errors: $zram"
    echo "  OOM kills:   $oom"
  }
done
```

**预期输出**：
```
=== mem-profile-20260610-014450-post-crc-fix-no-mcu
htc_main_app rc=0
  zram errors: 0
  OOM kills:   0

=== mem-profile-20260610-030020-post-crc-fix-baseline
htc_main_app rc=0
  zram errors: 0
  OOM kills:   0
```

**实际输出**：
- post-crc-fix-no-mcu：`htc_main_app rc=0`, zram 0, OOM 0 ✅
- post-crc-fix-baseline：`htc_main_app rc=0`, zram 0, OOM 0 ✅

## 修复原理（终篇）

### 问题

```cpp
// 旧实现: 第 14 行把整个文件读到堆里
std::vector<unsigned char> buffer(
    (std::istreambuf_iterator<char>(file)),
    std::istreambuf_iterator<char>()
);
```

`std::istreambuf_iterator` 一路读到底 = **`new` 文件大小 + 拷贝整个文件**。

对 32 MB mp4 文件 = **32 MB 堆分配**。

### 调用链（desc_info 流程）

```
processCmdVideoRecord
  → record done (32 MB mp4 落盘)
  → generateDescInfo(filename, ...)        ← 算 desc JSON
    → CRC::calculate_crc16(filename, ...)  ← 32 MB 堆分配
    → Json::Value 构造（几 KB）
    → Json::writeString 序列化（几 KB）
  → writeWorkModeDescJson 写盘
```

### 修复后

```cpp
// 新实现: 流式 CRC, 64 KB chunk
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
```

内存占用 = 64 KB（一个 chunk），跟文件大小无关。

## 跟之前 band-aid 的关系

### commit 45a7c09 的 zram 风暴 fix

```cpp
::sync();
recorder.releaseVideoResources();
std::this_thread::sleep_for(std::chrono::milliseconds(200));
::malloc_trim(0);
```

**保留**。即使 CRC 修了，这套 sync+sleep+trim 仍然有用：
- sync() 提前刷 dirty
- sleep 200ms 让 kswapd 跑一轮
- malloc_trim(0) 归还 glibc arena
- 在 release SDK 30+ MB 之前把可回收页准备好

这是有意义的修复（不是 zram 风暴的根因，但是 release 路径的最佳实践）。

### 之前 session 加的额外 band-aid（已 rollback）

- `data-snapshot 模式`（desc_info 提前到 release 之前）：**不需要了**
- `sleep 200ms → 300ms + 50ms tail`：**不需要了**
- `desc_info.reserve(2048)`：**不需要了**
- `HTC_NO_RELEASE` env 开关：作为 debug 工具保留，作为代码逻辑不需要
- `HTC_RECORD_NO_MCU_DESC` / `NO_JSON_BUILD` env 开关：作为 debug 工具保留

**当前 main_app.cpp 已 rollback 到 45a7c09 状态**（仅保留 45a7c09 原始的 sync+sleep 200ms+trim + 业务层 env 隔离开关）。

## 业务层 env 开关未来用途

虽然 bug 修了，但 `HTC_RECORD_NO_AUDIO` / `NO_DESC` / `NO_THUMB` / `NO_MCU_DESC` / `NO_JSON_BUILD` 这些 env 开关**保留在代码里**：

- 作为未来类似 zram / OOM 问题的 **isolation 工具**
- 跟 `tools/mem_profile.sh` 配套使用（已支持 `HTC_EXTRA_ENV` 参数）
- 详见 `doc/knowledge/playbooks/t32-memory-profile-and-debug.md`

## 最终 commit 计划

```bash
# 1) 确认 working tree 干净（除 CRC.cpp）
git status

# 2) 看最终 diff（应该只有 CRC.cpp 一处改动）
git diff src/common/utils/crc/CRC.cpp

# 3) commit
git add src/common/utils/crc/CRC.cpp
git commit -m "fix(crc): stream CRC::calculate_crc16 to fix zram storm

详见 doc/knowledge/bugs/T32-zram-storm-root-cause-2026-06-10.md
"
```

## 进度

- [x] 根因定位（CRC::calculate_crc16 读 32 MB mp4）
- [x] 修复实现（流式 CRC, 64 KB chunk）
- [x] 编译验证（PC sim + T32 cross）
- [x] 部署验证（build/bin/htc_main_app）
- [x] 跑两组对照测试（baseline + no-mcu-desc）
- [x] 写最终 fix 验证报告
- [ ] **commit fix 到 git**

## 相关文档

- `T32-zram-storm-root-cause-2026-06-10.md` —— 根因分析终篇
- `t32-memory-profile-and-debug.md` —— debug 方法论 playbook
- `T32-recording-fps-17-investigation.md` —— 历史调查（含 45a7c09 时序修复）
- `T32-iso-*-2026-06-*.md` —— 6+ 组 isolation 数据
- `tools/mem_profile.sh` / `tools/mem_profile_sample.sh` / `tools/analyze_profile.sh`
