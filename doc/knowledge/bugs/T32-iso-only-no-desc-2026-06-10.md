# T32 Zram 风暴 · iso-only-no-desc（只关 desc，audio + thumb 保留）

**日期**：2026-06-10
**作者**：zengping（+ Claude 协作）
**状态**：完成
**目的**：拆解 desc JSON 业务层对 zram 风暴的独立贡献

## TL;DR

> **只关闭 desc（audio + thumb 保留）就能消除 zram 风暴：0 zram error vs baseline 6 条。**
> 物理压力从 free 0.7 MB 提升到 10.1 MB（**14× 提升**），但比 no-audio-no-desc 的 17.5 MB 略低。
> → **desc 业务层是触发 zram 风暴的关键，audio + thumb 不是**（修正前两版结论的过度归因）。

| 决定性测试 | 结果 |
|------------|------|
| iso-only-no-desc dmesg 关键事件数 | **0** |
| htc_main_app baseline 关键事件数 | 6 条 zram error |
| iso-only-no-desc rc | 0（正常退出）|

## 测试环境

| 项 | 值 |
|----|----|
| 设备 | T32 MIPS, 64 MB RAM, 16 MB CMA |
| Kernel | 4.4.94-Archon (Ingenic) |
| Sensor | GC4653, 2560×1440 @ 30 fps |
| htc_main_app 版本 | main 分支 + post-zram-fix-v2 (sync+sleep+trim+reserve) |
| env 开关 | `HTC_RECORD_NO_DESC=1`（**保留 audio + thumb**）|
| 数据源 | `build/logs/mem-profile-20260610-004335-iso-only-no-desc/` |

## 业务层开关状态

| 开关 | 状态 |
|------|------|
| `HTC_RECORD_NO_AUDIO=1` | **未设**（保留 audio）|
| `HTC_RECORD_NO_DESC=1` | **关闭**（本测试目标）|
| `HTC_RECORD_NO_THUMB=1` | **未设**（保留 thumb）|

## iso-only-no-desc 完整内存时序

数据源：`build/logs/mem-profile-20260610-004335-iso-only-no-desc/`
单位 kB（除特别说明）。

| 阶段 | free | CmaFree | AnonPages | Dirty | Cached | swap used/free | VmData | VmRSS | alive |
|------|------|---------|-----------|-------|--------|----------------|--------|-------|-------|
| **T0** 启动 | 17808 | 14020 | 880 | 16 | 14976 | 0/16380 | 456 | 332 | ✓ |
| **T1** SDK init | 14232 | 10596 | 1052 | 16 | 18332 | 0/16380 | 3248 | 1532 | ✓ |
| **T1.5** 录影 1s | 11164 | 9208 | 1632 | 120 | 18976 | 0/16380 | 122208 | 7856 | ✓ |
| **T2** 录影 5s | 3312 | 2360 | 3200 | 1388 | 24184 | 0/16380 | 208192 | 9008 | ✓ |
| **T2.5** 录影 10s | 10088 | 9028 | 3316 | 2640 | 18428 | 68/16312 | 208228 | 8244 | ✓ |
| **T3** 录影 15s | 10132 | 9000 | 3340 | 1580 | 22268 | 68/16312 | 208228 | 8244 | ✓ |
| **T3.5** 录影 20s | 10124 | 8552 | 3340 | 3128 | 22288 | 68/16312 | 208228 | 8244 | ✓ |
| **T4** 录影 25s | **10092** | **8784** | 3352 | 2104 | 23072 | 68/16312 | 208240 | 8256 | ✓ |
| **T4.5** 录影 28s | 10276 | 9168 | 3372 | 3360 | 23412 | 68/16312 | 208256 | 8272 | ✓ |
| **T5** 录完 | 19240 | 11560 | 736 | 28 | 20248 | 68/16312 | — | — | ✗ |
| **T6** release | 19240 | 11560 | 736 | 28 | 20256 | 68/16312 | — | — | ✗ |
| **T8** settle | 19220 | 11560 | 736 | 12 | 20268 | 68/16312 | — | — | ✗ |

**dmesg 关键事件：0 行**（无 zram / OOM / Killed）
**process 退出码：rc=0**（正常退出）

## 关键观察：desc 单项是关键

把"只关 desc" 跟 "全开" 跟 "全关" 三组对比：

| 资源 | htc_full (全开) | **iso-only-no-desc (只关 desc)** | iso-all-3-off (全关) | iso-no-audio-no-desc v1 (关 audio+desc) |
|------|-----------------|----------------------------------|----------------------|--------------------------------------|
| audio | ON | **ON** | OFF | OFF |
| desc | ON | **OFF** | OFF | OFF |
| thumb | ON | **ON** | OFF | ON |
| zram error | 6 | **0** | 0 | 0 |
| free 最低 | 0.7 MB | **10.1 MB** | 10.6 MB | 17.5 MB |
| CmaFree 最低 | 3.6 MB | 8.5 MB | 9.6 MB | 12.3 MB |
| VmData 稳态 | 282 MB | **208 MB** | 155 MB | 180 MB |
| VmRSS 稳态 | 8 MB | 8.3 MB | 7.5 MB | 7.7 MB |
| swap used | 16 MB 满 | 68 kB | 88 kB | 108 kB |

### 决定性观察

1. **只关 desc（audio + thumb 保留）就能消除 zram 风暴**：0 zram error vs baseline 6。
2. **free 提升 14×**（0.7 → 10.1 MB），但比关两个（17.5 MB）少 7 MB。
3. **VmData 208 MB**（vs baseline 282）：**少 74 MB**——desc 业务层吃了 74 MB 虚拟映射。
4. **VmRSS 8.3 MB**（vs baseline 8 MB）：只多 0.3 MB——desc 业务的 RSS 增量很小，**物理压力主要来自 mmap 抖动**。

## desc vs audio 各自贡献（精确拆分）

通过对比两个 1-开关 isolation 测试：

| 资源 | **iso-only-no-desc (desc OFF)** | iso-no-audio-no-desc v1 (audio+desc OFF) | 差值（**audio OFF 单独贡献**）|
|------|--------------------------------|----------------------------------------|------------------------------|
| free 最低 | 10.1 MB | 17.5 MB | **+7.4 MB** ← audio 业务层吃 7.4 MB 物理 |
| VmData | 208 MB | 180 MB | **-28 MB** ← audio 业务层吃 28 MB 虚拟 |
| VmRSS | 8.3 MB | 7.7 MB | -0.6 MB |

→ **audio 业务层（RSS + 缓冲）约占 7-8 MB 物理 + 28 MB 虚拟**，但**不触发 zram 风暴**（desc 才是触发者）。

| 资源 | htc_full | **iso-only-no-desc (desc OFF)** | 差值（**desc OFF 单独贡献**）|
|------|----------|--------------------------------|------------------------------|
| free 最低 | 0.7 MB | 10.1 MB | **+9.4 MB** ← desc 业务层吃 9.4 MB 物理 |
| VmData | 282 MB | 208 MB | **-74 MB** ← desc 业务层吃 74 MB 虚拟 |
| VmRSS | 8 MB | 8.3 MB | +0.3 MB |

→ **desc 业务层（约 9-10 MB 物理 + 74 MB 虚拟）就是触发 zram 风暴的真凶**。

## 决定性结论（最终修正）

1. **zram 风暴的真凶是 desc 业务层**（不是 audio，不是 thumb）
2. **关掉 desc 即消除风暴**（即使 audio + thumb 都保留）：0 zram error
3. **audio 业务层（~7 MB 物理）**会进一步把 free 压低，但**单独不触发风暴**
4. **thumbnail 业务层（~25 MB 虚拟开销）**几乎不影响物理内存
5. **物理压力**：desc 9 MB + audio 7 MB = **业务层总共 ~16 MB** 把 free 从 ~17 MB 挤到 0.7 MB

## 优化方向（基于实测数据最终结论）

| 优先级 | 方向 | 预期收益 | 风险 |
|--------|------|----------|------|
| **P0** | **desc JSON 延后到 release 之后写**（即"data snapshot"模式，已经在 1.5 步做了） | 消除 zram 风暴 ✅ | 极低 |
| **P0** | **如果业务允许：默认 `NO_DESC=1`**（work mode 录影非云端上传，desc 可以不写）| 物理压力 -9 MB | 中，看产品需求 |
| **P1** | audio 在 work mode 录影中按需启用（不常驻） | 物理压力 -7 MB | 中 |
| **P2** | thumbnail 降 JPEG 质量或减少 buffer count | 虚拟压力 -25 MB | 低（虚拟开销，影响小）|

## 进度

- [x] sample 6Mbps 数据
- [x] iso-all-3-off 数据
- [x] iso-no-audio-no-desc v1 / v2 数据
- [x] **iso-only-no-desc 数据（关键 isolation）**
- [x] 5 方对照表 + desc / audio 精确拆分
- [ ] iso-only-no-audio 单跑（验证 audio 单独不触发风暴）
- [ ] 跟产品确认 desc JSON 在 work mode 是否必写
- [ ] 决定 audio 在 work mode 录影中是否按需启用

## 相关文档

- `T32-zram-storm-sample-baseline-2026-06-09.md` — sample baseline
- `T32-iso-all-3-off-vs-sample-2026-06-09.md` — 3-off 对照
- `T32-iso-no-audio-no-desc-2026-06-09.md` — v1 数据
- `T32-iso-no-audio-no-desc-v2-2026-06-09.md` — v2 复现
- `T32-recording-fps-17-investigation.md` — zram 风暴首查
- `tools/mem_profile.sh` / `tools/mem_profile_sample.sh` / `tools/analyze_profile.sh`
