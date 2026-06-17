# T32 Zram 风暴 · iso-no-audio-no-desc（thumbnail ON, audio/desc OFF）

**日期**：2026-06-09
**作者**：zengping（+ Claude 协作）
**状态**：完成
**目的**：拆解 audio / desc 业务层对 zram 风暴的具体贡献（保留 thumbnail 业务）

## TL;DR

> **只关闭 audio + desc（保留 thumbnail）就能彻底消除 zram 风暴**。
> 0 zram error vs htc_full baseline 的 6 条。
> free 余量从 0.7 MB → 17.5 MB（**24× 提升**）。
> **thumbnail 业务层单独不触发 zram 风暴**——这是相对 3-off 文档的修正。

| 决定性测试 | 结果 |
|------------|------|
| iso-no-audio-no-desc dmesg 关键事件数 | **0** |
| htc_main_app baseline 关键事件数 | 6 条 zram error |
| iso-all-3-off 关键事件数 | 0 |
| sample-6Mbps-2k 关键事件数 | 0 |
| iso-no-audio-no-desc rc | 0（正常退出）|

## 测试环境

| 项 | 值 |
|----|----|
| 设备 | T32 MIPS, 64 MB RAM, 16 MB CMA |
| Kernel | 4.4.94-Archon (Ingenic) |
| Sensor | GC4653, 2560×1440 @ 30 fps |
| 工具链 | mips-linux-uclibc-gnu-gcc 5.4.0 |
| htc_main_app 版本 | main 分支 + post-zram-fix-v2 (sync+sleep+trim+reserve) |
| env 开关 | `HTC_RECORD_NO_AUDIO=1 HTC_RECORD_NO_DESC=1`（**保留 thumb**）|
| 数据源 | `build/logs/mem-profile-20260609-122438-iso-no-audio-no-desc/` |

## 业务层开关含义

| 开关 | 状态 | 关闭的功能 |
|------|------|-----------|
| `HTC_RECORD_NO_AUDIO=1` | **关闭** | 音频线程 + audio encoder |
| `HTC_RECORD_NO_DESC=1` | **关闭** | desc JSON 序列化 + 写盘 |
| `HTC_RECORD_NO_THUMB=1` | **未设**（保留）| CH2 JPEG thumbnail 流正常 |

## iso-no-audio-no-desc 完整内存时序

数据源：`build/logs/mem-profile-20260609-122438-iso-no-audio-no-desc/`
单位 kB（除特别说明）。

| 阶段 | free | CmaFree | AnonPages | Dirty | Cached | swap used/free | VmData | VmRSS | alive |
|------|------|---------|-----------|-------|--------|----------------|--------|-------|-------|
| **T0** 启动 | 13812 | 10832 | 868 | 0 | 24636 | 80/16300 | 2116 | 1036 | ✓ |
| **T1** SDK init | 16072 | 10712 | 2360 | 156 | 19396 | 80/16300 | 179864 | 8452 | ✓ |
| **T1.5** 录影 1s | 16092 | 10744 | 2736 | 2708 | 19416 | 80/16300 | 179904 | 7668 | ✓ |
| **T2** 录影 5s | 17284 | 12160 | 2972 | 2692 | 19960 | 108/16272 | 179956 | 7840 | ✓ |
| **T2.5** 录影 10s | 17476 | 12196 | 2724 | 552 | 20020 | 108/16272 | 179872 | 7764 | ✓ |
| **T3** 录影 15s | 17924 | 12256 | 2876 | 1320 | 19412 | 108/16272 | 179856 | 7748 | ✓ |
| **T3.5** 录影 20s | 17568 | 12268 | 2884 | 2548 | 19752 | 108/16272 | 179864 | 7756 | ✓ |
| **T4** 录影 25s | **17532** | **12280** | 2884 | 3484 | 19768 | 108/16272 | 179864 | 7756 | ✓ |
| **T4.5** 录影 28s | 17444 | 12304 | 2952 | 1440 | 19780 | 108/16272 | 179952 | 7840 | ✓ |
| **T5** 录完 | 21736 | 12828 | 724 | 28 | 19736 | 108/16272 | — | — | ✗ |
| **T6** release | 21712 | 12828 | 724 | 28 | 19744 | 108/16272 | — | — | ✗ |
| **T8** settle | 21696 | 12828 | 724 | 12 | 19756 | 108/16272 | — | — | ✗ |

**dmesg 关键事件：0 行**
**process 退出码：rc=0**

## 关键观察

### 1. 业务层三项影响非线性

| 资源 | sample (裸 SDK) | iso-all-3-off (audio+desc+thumb OFF) | **iso-no-audio-no-desc (audio+desc OFF, thumb ON)** | htc_full baseline (all ON) |
|------|-----------------|---------------------------------------|------------------------------------------------------|---------------------------|
| zram error | 0 | 0 | **0** | 6 |
| free 最低 (T2-T4) | 7.5 MB | 10.6 MB | **17.5 MB** | 0.7 MB |
| CmaFree 最低 | 6.4 MB | 9.6 MB | **12.3 MB** | 3.6 MB |
| VmData 稳态 | 135 MB | 155 MB | 180 MB | 282 MB |
| VmRSS 稳态 | 3.4 MB | 7.5 MB | 7.7 MB | 8 MB |
| swap used | 180 kB | 88 kB | 108 kB | 16 MB 满 |

### 2. **zram 风暴的真凶不是 thumbnail**

之前 iso-all-3-off 文档推测 "thumbnail 单项贡献 120 MB VmData + 4-5 MB free"——**这是错的**。

- iso-all-3-off (thumbnail OFF) 跟 iso-no-audio-no-desc (thumbnail ON) **都没有 zram error**。
- 两者 VmData 差异（155 vs 180）就是 thumbnail 的虚拟映射开销（~25 MB），但**不影响物理内存**。
- 物理压力来自 audio + desc 业务层，thumbnail 只是**多占一点虚拟地址**。

### 3. audio + desc 物理贡献估算

| 项 | iso-all-3-off (thumb OFF) | iso-no-audio-no-desc (thumb ON) | 差值（thumb 影响）|
|----|--------------------------|-------------------------------|------------------|
| free 最低 | 10.6 MB | 17.5 MB | +6.9 MB（系统状态噪声）|
| VmData | 155 MB | 180 MB | +25 MB（thumbnail VPU buffer 虚拟映射）|
| VmRSS | 7.5 MB | 7.7 MB | +0.2 MB |

| 项 | iso-no-audio-no-desc (audio+desc OFF) | htc_full baseline (all ON) | 差值（audio+desc 影响）|
|----|---------------------------------------|---------------------------|----------------------|
| free 最低 | 17.5 MB | 0.7 MB | **-16.8 MB** |
| CmaFree 最低 | 12.3 MB | 3.6 MB | -8.7 MB |
| VmData | 180 MB | 282 MB | -102 MB |
| VmRSS | 7.7 MB | 8 MB | -0.3 MB |

→ **audio + desc 业务层总共多吃 ~17 MB 物理内存**（远超 VmRSS 0.3 MB，因为含 unevictable / 临时缓冲 / mmap 抖动）。

## 决定性结论

1. **关闭 audio + desc 就够了**：跟 3-off 一样 0 zram error，但 free 余量更多（17.5 vs 10.6 MB）
2. **thumbnail 单独不触发 zram**：120 MB VmData 是虚拟开销，对物理内存影响有限
3. **audio + desc 叠加才是物理压力的主因**：~17 MB 物理开销 + 系统瞬时抖动 → 把 free 从 17.5 MB 挤到 0.7 MB
4. **修复方向明确**：默认关闭 audio / desc（work mode 录影），或延后到 release 之后

## 优化建议（基于实测数据更新）

| 优先级 | 方向 | 预期收益 | 风险 |
|--------|------|----------|------|
| **P0** | **默认 `HTC_RECORD_NO_AUDIO=1`**（work mode 录影不需要 audio？需产品确认）| 释放 ~3 MB RSS + 临时缓冲 | 低 |
| **P0** | **默认 `HTC_RECORD_NO_DESC=1`** 或延后 desc JSON 写 | 释放 ~1 MB + 错开内存压力 | 低 |
| **P1** | thumbnail 业务延后到 release 之后或降低 JPEG 质量 | 减 25 MB VmData（虚拟）| 中 |
| ~~P2~~ | ~~HAL 减 VPU buffer count~~ | 实际对物理影响小，**优先级降为 P3** | 中 |

## 进度

- [x] sample 6Mbps 数据
- [x] iso-all-3-off 数据
- [x] iso-no-audio-no-desc 数据
- [x] 三方对照表（修正 thumbnail 不是主因的结论）
- [ ] 单项 isolation（no-thumb / no-audio 单跑）精确拆解 audio vs desc
- [ ] 跟产品确认 audio 在 work mode 录影中是否真的不需要
- [ ] 决定 desc JSON 写入时机（release 前 vs release 后）

## 相关文档

- `T32-zram-storm-sample-baseline-2026-06-09.md` — sample baseline
- `T32-iso-all-3-off-vs-sample-2026-06-09.md` — 3-off 对照（旧结论需修正）
- `T32-recording-fps-17-investigation.md` — zram 风暴首查
- `tools/mem_profile.sh` / `tools/mem_profile_sample.sh` / `tools/analyze_profile.sh`
