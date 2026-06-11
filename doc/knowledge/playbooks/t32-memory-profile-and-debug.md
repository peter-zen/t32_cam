# T32 内存 Profile 与 Debug 方法论

**日期**：2026-06-10
**作者**：zengping（+ Claude 协作）
**状态**：Active playbook
**适用场景**：T32 (4.4.94 kernel, 64 MB RAM) 上 zram error / OOM kill / 录影卡死 / 内存碎片化 等内存相关问题的诊断

## 何时用这个文档

碰到以下任何症状，先来这里查：
- dmesg 出现 `zram: Error allocating memory for compressed page`
- dmesg 出现 `Out of memory: Kill process ... (htc_main_app)`
- 进程被 watchdog 强杀（rc=137）但 htc.log 看起来"卡在某个步骤"
- 录影中 `wall_fps` 突然暴跌（业务逻辑 OK，但内存压力导致调度）
- 修改某个业务功能后录影变慢或失败

## 第一步：环境准备（10 分钟）

### 工具

**项目内已有**：
- `tools/mem_profile.sh` —— 跑 htc_main_app profile（接受 env 变量做 isolation）
- `tools/mem_profile_sample.sh` —— 跑 sample-Encoder-video profile（裸 SDK 对照）
- `tools/analyze_profile.sh` —— host 端分析 profile 目录
- `build/` —— NFS 共享到 T32 `/mnt/huntcam/`

### 把脚本放到 build/（一次性）

```bash
cd /home/zengping/project/huntcam/code/t32_cam
cp tools/mem_profile_sample.sh build/
chmod +x build/mem_profile*.sh
ls -la build/mem_profile*.sh
# 应该有 2 个：mem_profile.sh（已有）+ mem_profile_sample.sh（新增）
```

### 现有 env 开关（业务层 isolation）

| env 变量 | 关闭的功能 | 适用场景 |
|----------|-----------|---------|
| `HTC_RECORD_NO_AUDIO=1` | 音频线程 + encoder | 拆 audio 业务层 |
| `HTC_RECORD_NO_THUMB=1` | CH2 JPEG 流 | 拆 thumbnail 业务层 |
| `HTC_RECORD_NO_DESC=1` | desc JSON 序列化 + 写盘 | 拆 desc 业务层 |
| `HTC_RECORD_NO_MCU_DESC=1` | 5+ 次 MCU I2C 读 | 拆 I2C 调用 |
| `HTC_RECORD_NO_JSON_BUILD=1` | Json::Value 构造 | 拆 JSON 堆分配 |
| `HTC_NO_RELEASE=1` | releaseVideoResources + sync + sleep + trim | 验证 release 时序 |
| `HTC_RECORD_TMPFS=1` | 写 mp4 到 /tmp | 排除 SD 卡因素 |
| `HTC_RECORD_BITRATE_KBPS=<kbps>` | 覆盖 bitrate | 排除编码器压力 |
| `HTC_FORCE_RECORD_DAY_MODE=1` | 禁用 day/night auto-switch | 排除光照切换 |

**注意**：环境变量名必须完全匹配，busybox 工具链不报错只是默默忽略。

## 第二步：跑 baseline profile（5 分钟）

### 命令

```bash
# T32 上
cd /mnt/huntcam
sh mem_profile.sh "baseline" 2>&1 | tee /tmp/run.log
# 或指定 env
env HTC_RECORD_BITRATE_KBPS=6000 sh mem_profile.sh "baseline-6mbps"
```

### 输出位置

```
build/logs/mem-profile-<YYYYMMDD-HHMMSS>-<label>/
├── T0.log ... T8.log      # 内存快照（13 个时间点）
├── htc.log                # 进程完整输出
├── dmesg.log              # 本次 dmesg 切片
├── rc.log                 # 进程退出码
├── watchdog.log           # 是否被 watchdog 杀
└── h.pid                  # 进程 PID
```

### Host 端快速分析

```bash
# 1) 单 profile summary
tools/analyze_profile.sh build/logs/mem-profile-<dir>

# 2) 多 profile 对比（T4 时刻）
for p in build/logs/mem-profile-*; do
  echo "=== $(basename $p) ==="
  [ -f $p/rc.log ] && cat $p/rc.log
  [ -f $p/dmesg.log ] && echo "zram: $(grep -cE 'zram|Error allocating' $p/dmesg.log)"
  [ -f $p/T4.log ] && {
    grep '^MemFree:' $p/T4.log | awk '{print "T4 free:    " $2 " kB"}'
    grep '^CmaFree:' $p/T4.log | awk '{print "T4 CmaFree: " $2 " kB"}'
    grep '^VmData:' $p/T4.log | awk '{print "T4 VmData:  " $2 " kB"}'
    grep '^VmRSS:'  $p/T4.log | awk '{print "T4 VmRSS:   " $2 " kB"}'
  }
done
```

## 第三步：判读 dmesg（决定性步骤）

### 关键错误码

| dmesg 错误 | 含义 | 死亡方式 | 处理方向 |
|------------|------|---------|---------|
| `zram: Error allocating memory for compressed page` | **zram 风暴** | 不一定死（看次数）| 找谁吃 free |
| `Out of memory: Kill process ...` | **OOM killer** | 立即 SIGKILL | 找谁吃 16+ MB anon |
| `Killed process ... total-vm:N, anon-rss:M` | OOM killer 报告 | 跟上面配对 | 看 anon-rss 数字 |
| `page allocation failure: order:0, mode:0x2400002` | 内核分配失败 | 跟 zram error 配对 | 通常是 zram 的前兆 |

### 关键 meminfo 字段

| 字段 | 含义 | 判读 |
|------|------|------|
| `MemFree` | 空闲物理页 | **< 1 MB = 危险** |
| `CmaFree` | CMA 空闲 | 16 MB 总，< 5 MB 录影时正常 |
| `Cached` | clean page cache | 应该 10-25 MB（writeback 没跟上就涨）|
| `Dirty` | 等待写盘的页 | < 5 MB 正常（6 Mbps 视频 ~3.5 MB）|
| `SwapFree` | zram 剩余 | **0 = zram 池满**（风暴前兆）|
| `VmData` | 进程虚拟数据段 | 270+ MB 业务层叠加 |
| `VmRSS` | 进程实际驻留 | 8 MB 正常；> 24 MB 警惕 |

### 进程 /proc/PID/status 关键字段

| 字段 | 含义 | 警戒线 |
|------|------|--------|
| `VmRSS` | 实际物理占用 | > 16 MB 异常 |
| `VmData` | 虚拟映射 | 200-300 MB 正常 |
| `Threads` | 线程数 | 4-5 SDK 必需，20+ 业务层 |
| `State` | D=disk sleep（I/O 阻塞）| 持续 D = 卡 I/O |

## 第四步：Isolation 测试矩阵

### 设计思路

**目标**：把"zram 风暴"或"OOM kill"的**根因模块**定位到单一业务层。

**方法**：每次只关一个 env，跑一遍，对比 zram error 数 + free 最低 + 死亡方式。

### 标准 isolation 矩阵

| 测试 | env | 预期判读 |
|------|-----|---------|
| baseline | （无）| 复现问题，记录基线 |
| **no-audio** | `NO_AUDIO=1` | audio 业务层贡献？|
| **no-desc** | `NO_DESC=1` | desc 业务层贡献？|
| **no-thumb** | `NO_THUMB=1` | thumbnail 业务层贡献？|
| **no-mcu-desc** | `NO_MCU_DESC=1` | I2C 读贡献？|
| **no-json-build** | `NO_JSON_BUILD=1` | JSON 堆分配贡献？|
| **no-release** | `NO_RELEASE=1` | 验证 release 时序影响 |
| all-off | `NO_AUDIO NO_DESC NO_THUMB` | 最薄业务层对照 |
| **sample**（裸 SDK）| `mem_profile_sample.sh` | 硬件能力上限 |

### 跑一组 isolation（5-10 分钟 / 组）

```bash
# 1) 跑
cd /mnt/huntcam
env HTC_RECORD_NO_AUDIO=1 sh mem_profile.sh "iso-no-audio"

# 2) 看 dmesg
grep -cE "zram|Error allocating|Out of memory|Killed process" \
  build/logs/mem-profile-*-iso-no-audio/dmesg.log

# 3) 看死亡方式
cat build/logs/mem-profile-*-iso-no-audio/rc.log
cat build/logs/mem-profile-*-iso-no-audio/watchdog.log

# 4) 看 htc.log 末尾
tail -20 build/logs/mem-profile-*-iso-no-audio/htc.log

# 5) 对比 free / VmData
tools/analyze_profile.sh build/logs/mem-profile-*-iso-no-audio
```

### 判读 isolation 结果

**`0 zram error` 出现 = 关掉的功能就是触发者之一**

| 模式 | 判读 |
|------|------|
| baseline ❌ + no-X ✅ | **X 是触发者** |
| baseline ❌ + no-X ❌ | X 不是触发者，继续查 |
| baseline ❌ + all-off ❌ | SDK 物理极限，需要减分辨率或码率 |
| baseline ✅ + no-X ✅ | 不需要关 X，验证冗余 |

**死亡方式判读**：

| 死亡方式 | rc | 含义 |
|---------|----|----|
| clean exit | 0 | 正常退出，问题已绕过 |
| watchdog kill | 137 (180s 后) | 进程卡死（I/O、I2C 等）|
| OOM kill | 137 (秒级) | anon 内存暴涨，OOm killer 触发 |
| signal 11 | 139 | 段错误，需要 core dump |

## 第五步：Isolation 内部深挖

如果 isolation 指向某个 env（比如 desc），**继续拆**该 env 内部的子模块。

### 典型 desc 内部子模块

| 子模块 | 函数 | 嫌疑大小 | 验证 env |
|--------|------|---------|---------|
| CRC 计算 | `CRC::calculate_crc16` | 32 MB（**如果实现是 mmap 全文件**）| （需改代码加 env）|
| Disk info | `Disk::getInfo` | 1-5 MB（如果扫描目录）| （需改代码加 env）|
| getFileCreationTime | `stat()` | ~0 MB | （需改代码加 env）|
| JSON 构建 | `Json::Value` + `Json::writeString` | 0.1-0.5 MB | `NO_JSON_BUILD=1` |
| JSON 写盘 | `writeWorkModeDescJson` | 0.1 MB | `NO_DESC=1` |
| MCU I2C | `mcu->readXxx` | 0.3 MB | `NO_MCU_DESC=1` |

### 怎么发现 CRC 是元凶

看 htc.log 录完时间点：
```
record done
pre-generateDescInfo
generateDescInfo enter
Failed to open the iic bus       ← 卡 I2C
```

**关键观察**：dmesg OOM 时 `anon-rss` 比录影时大 16-32 MB → **某个调用一次分配 16+ MB**。

**快速判定**：搜可疑函数实现里的 `mmap`、`malloc`、`new`、`vector` 一次性大块分配。

## 第六步：写根因分析文档

参考 `doc/knowledge/bugs/T32-zram-storm-root-cause-2026-06-10.md` 的结构：

1. **TL;DR** —— 一句话结论
2. **症状 + 决定性测试** —— dmesg 错误、死亡方式
3. **时间线 / 调查过程** —— 每个 isolation 跑完写一段
4. **根因** —— 哪个函数哪一行
5. **修复方案** —— 最小代码改动
6. **验证** —— fix 前 vs fix 后对比
7. **未来类似问题的预防** —— code review checklist

## 常见陷阱

| 陷阱 | 说明 |
|------|------|
| **绝对内存数 ≠ 决定性指标** | 不同 profile 跑之间系统状态（page cache 残留）不同，绝对 free 不可比。**zram error 数是硬指标** |
| **dmesg 文件可能缺失** | 如果进程被 watchdog 杀在 180s 前，mem_profile.sh 来不及写 dmesg.log。**先看 htc.log 末尾，再看 watchdog 状态** |
| **VmData 跟 VmRSS 含义不同** | VmData 是虚拟地址空间（mmap 不一定消耗物理页），VmRSS 是实际驻留。**怀疑物理压力时看 VmRSS** |
| **I2C 卡死 ≠ zram 风暴** | I2C 卡死走 watchdog 路径（180s 后杀），zram 风暴走 reclaim 路径（秒级）。**不要混为一谈** |
| **OOM 触发是累积的** | 进程先做 desc_info 分配 21 MB anon 内存（没死），但**累积**到一定量后 OOM killer 触发。**关注 desc_info 路径上的 mmap/malloc 调用** |
| **Sample 跟 app 的差异** | 同样 SDK 跑 1 Mbps vs 6 Mbps，**业务层叠加才触发**。**先 sample baseline 划出硬件能力，再 app baseline 看业务层开销** |

## 工具脚本速查

```bash
# T32 上

# 跑 htc_main_app profile
sh mem_profile.sh "<label>"

# 跑 sample-Encoder-video profile（裸 SDK）
sh mem_profile_sample.sh "<label>"

# 跑 isolation（任意 env 组合）
env HTC_RECORD_NO_AUDIO=1 HTC_RECORD_NO_DESC=1 sh mem_profile.sh "iso-label"

# Host 上

# 单 profile summary
tools/analyze_profile.sh build/logs/<dir>

# 多 profile 对照
for p in build/logs/mem-profile-*; do
  echo "=== $(basename $p) ==="
  [ -f $p/rc.log ] && cat $p/rc.log
  [ -f $p/dmesg.log ] && echo "zram: $(grep -cE 'zram|Error allocating' $p/dmesg.log)"
done
```

## 相关文档

- `doc/knowledge/bugs/T32-recording-fps-17-investigation.md` —— 历史上 zram 风暴首查
- `doc/knowledge/bugs/T32-zram-storm-root-cause-2026-06-10.md` —— 完整根因分析（CRC 元凶）
- `doc/knowledge/bugs/T32-zram-storm-sample-baseline-2026-06-09.md` —— 裸 SDK baseline
- `doc/knowledge/bugs/T32-iso-*-2026-06-*.md` —— 各 isolation 测试记录
- `tools/mem_profile.sh` / `tools/mem_profile_sample.sh` / `tools/analyze_profile.sh`
