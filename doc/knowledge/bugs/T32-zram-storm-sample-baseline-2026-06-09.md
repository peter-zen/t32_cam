# T32 Zram 风暴 · sample-Encoder-video 对照基线

**日期**：2026-06-09
**作者**：zengping（+ Claude 协作）
**状态**：进行中（基线已建，isolation 测试待跑）
**目的**：用裸 SDK sample-Encoder-video 跑出"SDK 自身在 2.5K@6Mbps@30s 下的内存基线"，跟 htc_main_app 对照，把 6 条 zram error 归因到"业务层开销"还是"SDK 物理极限"。

## TL;DR

> **SDK 自身在 2.5K @ 6 Mbps @ 30 s 下完全够用，0 zram error。**
> 同样的 SDK 配置，htc_main_app 撞 6 条 zram error。
> 差异不在 SDK，在 app 业务层（多吃了 ~150 MB VmData，~5 MB RSS，~7 MB free 余量）。

| 决定性测试 | 结果 |
|------------|------|
| sample dmesg 关键事件数（zram / OOM / Killed） | **0** |
| htc_main_app baseline 关键事件数 | 6 条 `zram: Error allocating memory for compressed page` |
| 业务层多吃的 VmData | +147 MB |
| 业务层多吃的 VmRSS 稳态 | +5 MB |
| 业务层挤掉的 free 余量 | -6.8 MB |

## 硬件 / 软件

| 项 | 值 |
|----|----|
| 设备 | T32 MIPS, 64 MB RAM, 16 MB CMA |
| Kernel | 4.4.94-Archon (Ingenic) |
| Sensor | GC4653 (主), 2560×1440 @ 30 fps |
| 工具链 | mips-linux-uclibc-gnu-gcc 5.4.0 |
| Sample 版本 | 改后（`BITRATE_720P_Kbs 1000 → 6000`），其他默认 |
| NFS 共享 | `build/` → T32 `/mnt/huntcam/` |

## Sample 配置（跟 htc_main_app baseline 对齐）

| 参数 | sample | htc_main_app baseline | 是否一致 |
|------|--------|----------------------|---------|
| 分辨率 | 2560×1440 (2.5K) | 2560×1440 (2.5K) | ✅ |
| 帧率 | 30 fps | 30 fps | ✅ |
| 码率 | 6 Mbps CBR | 6 Mbps CBR | ✅ |
| 时长 | 30 s (900 帧) | ~33 s | ≈ |
| 写盘 | `/mnt/sdcard/DCIM/stream-N-WxH.h264` | `/mnt/sdcard/media/<ts>.mp4` | 不同容器 |
| 业务层 | **无** thumbnail / JSON / DB / MCU I2C / HTTP | 全有 | ❌ |

## Sample 完整内存时序

数据源：`build/logs/mem-profile-20260609-120712-sample-6mbps-2k/`
全部单位 kB（除特别说明）。

| 阶段 | free | CmaFree | AnonPages | Dirty | Cached | swap used/free | VmData | VmRSS | alive |
|------|------|---------|-----------|-------|--------|----------------|--------|-------|-------|
| **T0** 启动 | 17536 | 13772 | 784 | 0 | 15432 | 0/16380 | 33680 | 2288 | ✓ |
| **T1** SDK init 完成 | **1924** | **664** | 18816 | 0 | 10800 | 180/16200 | 153416 | 20760 | ✓ |
| **T1.5** 录影 1s | 18916 | 13796 | 1332 | 428 | 11212 | 180/16200 | 135860 | 3400 | ✓ |
| **T2** 录影 5s | 16420 | 13796 | 1508 | 2520 | 13304 | 180/16200 | 135860 | 3580 | ✓ |
| **T2.5** 录影 10s | 12956 | 11488 | 1832 | 1572 | 16128 | 180/16200 | 135860 | 3904 | ✓ |
| **T3** 录影 15s | 8456 | 7436 | 1832 | 2028 | 20180 | 180/16200 | 135860 | 3904 | ✓ |
| **T3.5** 录影 20s | 7644 | 6508 | 1836 | 2268 | 23356 | 180/16200 | 135860 | 3404 | ✓ |
| **T4** 录影 25s | **7528** | **6392** | 1832 | 2836 | 25512 | 180/16200 | 135860 | 3404 | ✓ |
| **T4.5** 录影 28s | 11392 | 10348 | 1836 | 3528 | 21920 | 180/16200 | 135860 | 3408 | ✓ |
| **T5** 录完 ~0.2s | 13540 | 9796 | 584 | 612 | 22840 | 176/16204 | — | — | ✗ |
| **T6** 录完 release | 13532 | 9800 | 584 | 612 | 22848 | 176/16204 | — | — | ✗ |
| **T7** 录完 settle | 13520 | 9800 | 584 | 612 | 22860 | 176/16204 | — | — | ✗ |
| **T8** 录完 +30s | 13512 | 9800 | 584 | 612 | 22868 | 176/16204 | — | — | ✗ |

**dmesg 关键事件：0 行**（无 zram / OOM / Killed）
**process 退出码：rc=0**（干净退出）

## 关键观察

### 1. SDK init 瞬时峰值（T1）

T0→T1 一瞬间：
- `free`: 17536 → **1924 kB**（-89%）
- `CmaFree`: 13772 → **664 kB**（-95%）
- `VmData`: 33680 → 153416 kB（**+120 MB**）
- `VmRSS`: 2288 → 20760 kB（+18 MB）

但 **T1→T1.5 只用 1.5 秒** 系统自己就稳态了：free 1924 → 18916，CmaFree 664 → 13796。

→ **SDK 自身有合理的延迟分配 / 释放节奏**，不需要应用层 sync+sleep+trim 干预。

### 2. 录影稳态（T1.5 ~ T4.5，30 秒）

| 指标 | sample (6 Mbps 30s) | htc_main_app baseline (6 Mbps 33s) | 业务层多吃的 |
|------|---------------------|--------------------------------------|--------------|
| free 最低点 | **7.5 MB** (T4) | **0.7 MB** (T2) | **-6.8 MB** |
| CmaFree 最低 | **6.4 MB** (T4) | **3.6 MB** (T2.5) | -2.8 MB |
| VmData 稳态 | **135 MB** | **282 MB** | **+147 MB** |
| VmRSS 稳态 | **3.4 MB** | **8 MB** | +4.6 MB |
| Dirty 峰值 | 3.5 MB (T4.5) | 3.5 MB (T3.5) | 0（都是 6 Mbps 写卡）|
| Cached 峰值 | 25.5 MB (T4) | 24 MB (T4) | ≈ 0 |
| swap used | **180 kB** | 0 KB（zram 满 16 MB） | 业务层把 zram 撑爆 |
| 进程线程数 | 4-5 | 22 | +17 |
| zram error 数 | **0** | **6** | **决定性** |

### 3. 录完退出（T5+）

- T4.5 → T5: free 11 → 13.5 MB（系统自然恢复，进程已死）
- CmaFree: 10 → 9.8 MB（CMA 残留，因为 SDK 进程退出时没显式释放，但**没影响系统**）
- **整个 teardown 阶段没有任何内存压力**

## 业务层开销分解（按可能性排序）

| 资源来源 | 预期开销 | 验证方法 |
|----------|----------|----------|
| **VPU/ISP mlock 业务缓冲**（unevictable 8 MB） | 4-8 MB | `HTC_RECORD_NO_THUMB=1` 缩一个 stream |
| **Audio 子系统**（线程 + buffer + encoder）| 2-3 MB RSS | `HTC_RECORD_NO_AUDIO=1` |
| **OSD 区域**（字体 + 缓冲）| 1-2 MB shared | 跟 HAL 确认 |
| **DB / MetadataDao**（sqlite + thumb DB）| 0.5-1 MB RSS | 关 desc JSON 后单测 |
| **JSON 业务**（MCU I2C 读 5 个电压 + GPS）| 0.5-1 MB | `HTC_RECORD_NO_MCU_DESC=1` |
| **HTTP server**（如果 work mode 启动了）| 1-2 MB | 关 HTTP |
| **HAL mmap / ioremap**（多 stream，preview，JPEG）| +147 MB VmData | 跟 HAL 确认 buffer count |

## 决定性结论

1. **SDK 自身够用**：同样的 2.5K@6Mbps@30s，sample 完全不撞 zram，free 稳态 7.5 MB。
2. **业务层是主因**：htc_main_app 多吃了 +147 MB VmData / +5 MB RSS，挤掉 -6.8 MB free 余量。
3. **不能甩锅给物理极限**：64 MB 设备 + 30 s 录影本身是 OK 的，问题在 app 业务层。
4. **优化方向**：减 VPU buffer count / 关多余 stream / 优化 audio / 减 OSD 区域。

## Isolation 测试矩阵（设计）

每个开关跑一次 `sh mem_profile.sh "<env>:<label>"`，对照同一时刻的内存数据：

| 测试 | env | 关闭的功能 | 预期收益 |
|------|-----|-----------|----------|
| baseline | （无） | 全部启用 | 6 zram errors |
| no-audio | `HTC_RECORD_NO_AUDIO=1` | 音频线程 + 编码器 | 减 2-3 MB RSS + 1-2 MB unevictable |
| no-thumb | `HTC_RECORD_NO_THUMB=1` | CH2 JPEG 流 | 减 4-5 MB VPU buffer |
| no-desc | `HTC_RECORD_NO_DESC=1` | desc JSON + I2C | 减 0.5 MB RSS |
| no-mcu-desc | `HTC_RECORD_NO_MCU_DESC=1` | 5+ 次 MCU I2C | 减 0.3 MB RSS + 0.5 MB 临时 |
| no-json-build | `HTC_RECORD_NO_JSON_BUILD=1` | Json::Value 构造 | 减 0.2 MB RSS |
| no-release | `HTC_NO_RELEASE=1` | releaseVideoResources + sync + sleep + trim | SDK 缓冲还在，**预期更糟**（zram error 应激增）|

**目的**：通过叠加环境变量，把业务层开销一项项拆出来。最终算总和，对照 sample 的 135 MB VmData 看 htcmain app 的 282 MB VmData 怎么分布的。

## 进度

- [x] sample 6Mbps 跑完（2026-06-09 12:07:12）
- [x] sample 数据完整记录到本文档
- [x] sample dmesg 干净：0 zram error
- [ ] isolation 测试矩阵跑全（6-7 次 profile，每次 5-10 分钟）
- [ ] 汇总 isolation 数据到本文档末尾
- [ ] 跟 HAL 团队过 VPU buffer count 是否能减半
- [ ] 决定是否要把 audio 移出 work mode 录影路径

## 相关文档

- `T32-recording-fps-17-investigation.md` — zram 风暴首查 + 原始 fix 三个根因
- `tools/mem_profile.sh` — htc_main_app profile 脚本
- `tools/mem_profile_sample.sh` — sample-Encoder-video profile 脚本（新建）
- `tools/analyze_profile.sh` — host 端分析脚本
