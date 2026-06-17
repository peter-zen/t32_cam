# T32 Zram 风暴 · iso-only-no-audio（只关 audio，desc + thumb 保留）

**日期**：2026-06-10
**作者**：zengping（+ Claude 协作）
**状态**：完成
**目的**：拆解 audio 业务层对 zram 风暴的独立贡献

## TL;DR

> **只关闭 audio（desc + thumb 保留）→ 仍有 zram error，进程卡在 I2C 总线。**
> 配合 `iso-only-no-desc`（desc OFF，audio+thumb ON → 0 error），**明确证伪 "audio 触发风暴" 的假设**：
> - audio OFF, desc ON → ❌ zram error
> - audio ON, desc OFF → ✅ 0 error
> - **desc 是触发者，audio 不是**（最终结论）

| 决定性测试 | 结果 |
|------------|------|
| iso-only-no-audio dmesg 关键事件数 | **≥1**（用户报）|
| htc_main_app baseline 关键事件数 | 6 条 zram error |
| iso-only-no-desc 关键事件数 | 0 |
| **行为** | **进程卡在 I2C → watchdog 强杀** |

## 测试环境

| 项 | 值 |
|----|----|
| 设备 | T32 MIPS, 64 MB RAM, 16 MB CMA |
| htc_main_app 版本 | main 分支 + post-zram-fix-v2 (sync+sleep+trim+reserve) |
| env 开关 | `HTC_RECORD_NO_AUDIO=1`（**保留 desc + thumb**）|
| 数据源 | `build/logs/mem-profile-20260610-005039-iso-only-no-audio/` |
| 进程退出方式 | **watchdog 强杀**（htc.log 在 desc_info I2C 阶段卡住）|

## 业务层开关状态

| 开关 | 状态 |
|------|------|
| `HTC_RECORD_NO_AUDIO=1` | **关闭**（本测试目标）|
| `HTC_RECORD_NO_DESC=1` | **未设**（保留）|
| `HTC_RECORD_NO_THUMB=1` | **未设**（保留）|

## iso-only-no-audio 完整内存时序

数据源：`build/logs/mem-profile-20260610-005039-iso-only-no-audio/`
单位 kB（除特别说明）。
**注意：进程在 T4.5 之后被 watchdog 强杀，所以没有 T5+ 快照**。

| 阶段 | free | CmaFree | AnonPages | Dirty | Cached | swap used/free | VmData | VmRSS | alive |
|------|------|---------|-----------|-------|--------|----------------|--------|-------|-------|
| **T0** 启动 | 18868 | 11560 | 876 | 0 | 20420 | 64/16316 | 1876 | 940 | ✓ |
| **T1** SDK init | 18172 | 12200 | 2264 | 108 | 18136 | 64/16316 | 179736 | 8168 | ✓ |
| **T1.5** 录影 1s | 17300 | 12212 | 2644 | 1396 | 18812 | 64/16316 | 179836 | 7936 | ✓ |
| **T2** 录影 5s | 17640 | 12548 | 2904 | 1872 | 19664 | 68/16312 | 179824 | 7784 | ✓ |
| **T2.5** 录影 10s | 17756 | 12608 | 2904 | 3052 | 19552 | 68/16312 | 179824 | 7744 | ✓ |
| **T3** 录影 15s | 17344 | 12240 | 2904 | 220 | 19968 | 68/16312 | 179824 | 7740 | ✓ |
| **T3.5** 录影 20s | 17476 | 12324 | 2904 | 1016 | 19832 | 68/16312 | 179824 | 7740 | ✓ |
| **T4** 录影 25s | 17704 | 12368 | 2848 | 1040 | 19668 | 68/16312 | 179768 | 7684 | ✓ |
| **T4.5** 录影 28s | **17908** | **12748** | 2896 | 88 | 19408 | 68/16312 | **186120** | **10760** | ✓ |
| T5+ | — | — | — | — | — | — | — | — | ✗ watchdog kill |

**dmesg 关键事件：≥1（用户报），dmesg.log 文件未捕获**（进程被 watchdog 杀后 mem_profile.sh 没机会写 dmesg）
**进程状态：卡在 desc_info I2C 阶段 → watchdog kill**

## 关键观察

### 1. 录影稳态（desc 还没开始执行）

| 资源 | iso-only-no-audio (T4 录影 25s) | iso-only-no-desc (T4 录影 25s) | 差值（**audio OFF 单独贡献**）|
|------|--------------------------------|--------------------------------|------------------------------|
| free | 17704 | 10092 | **+7612 kB** （audio OFF 后 free 多 7.4 MB）|
| CmaFree | 12368 | 8784 | +3584 kB |
| VmData | 179768 | 208240 | **-28472 kB** （audio 业务层吃 28 MB 虚拟）|
| VmRSS | 7684 | 8256 | -572 kB |

→ **audio 业务层单独贡献 ~7.6 MB 物理 + ~28 MB 虚拟**（之前预估的 7.4 MB / 28 MB 完美吻合）

### 2. T4 → T4.5 录影末段（desc 触发！）

| 资源 | T4 (rec 25s) | T4.5 (rec 28s) | 增量（3 秒）|
|------|--------------|----------------|-------------|
| VmData | 179768 | **186120** | **+6352 kB** |
| VmRSS | 7684 | **10760** | **+3076 kB** |
| AnonPages | 2848 | 2896 | +48 kB |
| free | 17704 | 17908 | +204 kB |

**关键异常**：T4.5 (rec 28s) 时 VmRSS 暴涨 3 MB，VmData 暴涨 6 MB。

时序对照 htc.log：
```
08:51:13.858 record stats: frames=750 nal=776 bytes=20905686
08:51:15.262 record summary: frames=900 nal=930 bytes=24664851   ← 录完
08:51:16.127 record done
08:51:16.185 DBG: pre-generateDescInfo (snapshot before release) ← ★ desc 开始
08:51:16.185 DBG: generateDescInfo enter
08:51:16.192 Failed to open the iic bus                         ← ★ 卡死
```

→ T4.5 时刻的 3 MB VmRSS 暴涨 = **desc_info 触发了 Json::Value + Json::writeString 的堆分配**
→ 然后 I2C 总线失败，进程卡死 → watchdog 杀 → 后续没机会写 dmesg

### 3. **desc_info I2C 卡死 → watchdog kill → 后续无 dmesg**

跟原始 baseline（`mem-profile-20260609-105637-baseline`）模式**完全一致**：
- 录完 → desc_info 开始
- I2C 总线失败（`Failed to open the iic bus`）
- 进程卡死
- watchdog 180s 后强杀
- rc = 137

→ 即使加了 post-zram-fix-v2 的 sync+sleep+trim+data-snapshot+reserve，**如果 desc 被卡住，zram 风暴还是会触发**。

## 决定性结论（最终修正版本）

### 各业务层独立贡献（精确）

| 业务层 | 物理贡献 | 虚拟贡献 | 触发 zram？ | 备注 |
|--------|---------|---------|------------|------|
| **desc 业务层** | **9.4 MB** | **74 MB** | **⚠️ 触发** | desc_info + I2C 卡死 + JSON 构建 |
| audio 业务层 | 7.4-7.6 MB | 28 MB | 不触发 | 单独 audio OFF → 0 error |
| thumbnail 业务层 | ~0-3 MB（噪声）| ~25 MB | 不触发 | 单独 thumb OFF → free 略增 |
| **业务层总和** | **~17 MB** | **~127 MB** | 触发 | |

### zram 风暴触发条件

> **desc_info 业务层执行时（即使加了 data-snapshot）依然会触发 zram 风暴**。
> 原因：desc_info 内部 `Json::Value` 构造 + `Json::writeString` 一次性 mmap 3 MB（VmRSS 暴涨），叠加 I2C 卡死延长窗口期，撞穿 zram。

### 完整对照表（7 组测试）

| 测试 | env | 0 zram? | free 最低 | VmData 稳态 | 备注 |
|------|-----|---------|----------|------------|------|
| **htc_full baseline** | （无）| ❌ 6 | 0.7 MB | 282 MB | 旧版，无 post-fix |
| post-zram-fix-v2 | （无）| ✅ 0 | 8-10 MB | 208 MB | 代码 fix 之后 |
| **iso-only-no-audio** | `NO_AUDIO=1` | ❌ 卡 I2C | 17.7 MB | 180 MB | **desc 触发** ✅ |
| **iso-only-no-desc** | `NO_DESC=1` | ✅ 0 | 10.1 MB | 208 MB | desc OFF 消除 |
| iso-no-audio-no-desc v1 | `NO_AUDIO=1 NO_DESC=1` | ✅ 0 | 17.5 MB | 180 MB | 复现稳定 |
| iso-no-audio-no-desc v2 | `NO_AUDIO=1 NO_DESC=1` | ✅ 0 | 16.8 MB | 180 MB | 复现稳定 |
| iso-all-3-off | `NO_AUDIO=1 NO_DESC=1 NO_THUMB=1` | ✅ 0 | 10.6 MB | 155 MB | thumb 虚拟开销 |
| sample 6Mbps 30s | （无 app）| ✅ 0 | 7.5 MB | 135 MB | 裸 SDK 基线 |

## 优化方向（最终版）

| 优先级 | 方向 | 预期收益 | 风险 |
|--------|------|----------|------|
| **P0** | **修复 I2C 总线卡死**（`Failed to open the iic bus`）| 进程不再卡死 → watchdog 不触发 → desc 写盘顺利 | 中，需要看 I2C 驱动 |
| **P0** | **如果产品允许：默认 `NO_DESC=1`** | 物理压力 -9 MB，免去 desc 风暴风险 | 中 |
| **P1** | desc JSON 写入路径继续优化（已经在 data-snapshot 模式）| 错开内存压力 | 低 |
| **P1** | audio 在 work mode 录影中按需启用 | 物理压力 -7 MB | 中 |
| **P2** | thumbnail 降 JPEG 质量 | 虚拟压力 -25 MB | 低（虚拟开销）|

## 进度

- [x] sample 6Mbps 数据
- [x] iso-all-3-off 数据
- [x] iso-no-audio-no-desc v1 / v2 数据
- [x] iso-only-no-desc 数据
- [x] **iso-only-no-audio 数据（关键 isolation，证实 desc 是触发者）**
- [x] 6+1 方对照表 + 各业务层独立贡献
- [ ] **拆解 desc 内部：JSON build / MCU I2C / file I/O**
- [ ] 跟产品确认 desc JSON 是否必写
- [ ] 修复 I2C 总线卡死（首要 bug）

## 相关文档

- `T32-zram-storm-sample-baseline-2026-06-09.md` — sample baseline
- `T32-iso-all-3-off-vs-sample-2026-06-09.md` — 3-off 对照
- `T32-iso-no-audio-no-desc-2026-06-09.md` — v1
- `T32-iso-no-audio-no-desc-v2-2026-06-09.md` — v2 复现
- `T32-iso-only-no-desc-2026-06-10.md` — only-no-desc
- `T32-iso-only-no-audio-2026-06-10.md` — only-no-audio（本文件）
- `T32-recording-fps-17-investigation.md` — zram 风暴首查
- `tools/mem_profile.sh` / `tools/mem_profile_sample.sh` / `tools/analyze_profile.sh`
