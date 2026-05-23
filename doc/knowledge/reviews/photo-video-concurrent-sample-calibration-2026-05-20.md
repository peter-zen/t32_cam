# 录影并发拍照 Sample 验证校准记录

**日期**: 2026-05-20  
**主题**: photo-video-concurrent  
**验证人**: Claude Code session  

---

## 本轮验证目标

在 T32 真机（GC4653 @ 2560×1440）上验证 `sample-Encoder-video-jpeg` 的多种并发架构，确认：
1. 并发录影 + 拍照的可行路径
2. 分辨率上限（硬件 scaler vs 软件 scaler）
3. GC4653 30fps 稳定性问题

---

## 验证结果汇总

| 方案 | CH0 | CH2 | JPEG 目标 | 录影 FPS | 拍照结果 | 结论 |
|------|-----|-----|----------|---------|---------|------|
| 基线 | 2560×1440 H265 | — | 2560×1440 | 30fps | ✅ | 无缩放基准 |
| **CH2 硬件 8M** | 2560×1440 H265 | 3840×2160 | 3840×2160 | **30fps** | **✅ 8M** | **当前推荐稳定方案** |
| CH2 硬8M + 软16M | 2560×1440 H265 | 3840×2160 | 4608×3456 | 17fps（VTS问题） | ❌ `work_done=1` | CH2 Encoder 不支持软件缩放 |
| CH0 纯软件 16M | 2560×1440 H265 | — | 4608×3456 | ~0.5fps | ✅ 16M | 能工作但录影几乎瘫痪 |
| CH0 硬8M + 软16M | 3840×2160（无H265） | 2560×1440 H265 | 4608×3456 | ~1fps | ✅ 16M | 主通道按需编码阻塞，拖累CH2 |
| CH0 硬8M 无缩放 | 3840×2160（无H265） | 2560×1440 H265 | 3840×2160 | ~1fps | ✅ 8M | 8M硬件放大本身不影响录影，JPEG按需编码阻塞主通道是元凶 |

---

## 关键发现

### 1. 并发拍照最佳路径：CH2 硬件 8M

```
CH0 (2560×1440) ──→ H265 Encoder → 录影 30fps
CH2 (3840×2160) ──→ JPEG Encoder → 8M 拍照
```

- CH2 硬件放大到 8M 不影响 CH0 录影
- JPEG 在 CH2 上按需编码，不阻塞主通道

### 2. Encoder 软件缩放限制

- **CH2（Group 2）Encoder 不支持软件缩放**：`picWidth/picHeight > 3840×2160` 时 `StartRecvPic` 返回 0 但 `work_done=1`，编码器实际未启动
- **CH0（Group 0）Encoder 支持软件缩放到 16M**：但 CPU 负载极高，同 Group H265 录影从 30fps 掉到 0.5fps
- **并发拍照分辨率上限 = 8M**

### 3. GC4653 VTS 问题

- `IMP_ISP_Tuning_SetSensorFPS` 会触发 `gc4653_set_fps()` 错误计算 VTS=3000，导致 FPS 降到 17fps
- `libimp.so` 动态库在 `EnableSensor/EnableTuning` 期间内部也会改写 VTS 到 3000
- **Sample 级 workaround**：在 `sample_system_init()` 末尾通过 `IMP_ISP_SetSensorRegister` 强制写入 VTS=1680（`0x0340=0x06, 0x0341=0x90`）
- 该 workaround 已 commit & push（`4e79f75`）

### 4. "孤儿" FrameSource 通道

任何启用的 FrameSource Channel 必须绑定到至少一个有效 Encoder Channel。CH2 启用但无有效 Encoder 时，会导致 SDK 内部状态异常，拖垮其他 Group。

---

## 文档更新

- `doc/knowledge/specs/photo-video-concurrent-implementation-plan.md` — 更新验证方案和风险章节
- `sdk/samples/libimp-samples/sample-common.c` — 添加 VTS=1680 workaround，禁用 `SetSensorFPS`

---

## 遗留问题

1. **16M+ 并发拍照**：当前无可行路径。如需 >8M，只能暂停录影后走 `LargeImageSnap`
2. **LargeImageSnap 并发性能**：已在项目中落地，但并发录影时的 CPU 负载影响未实测
