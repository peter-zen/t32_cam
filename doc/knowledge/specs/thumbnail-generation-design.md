# 拍照缩略图生成方案设计

**日期**: 2026-06-04
**状态**: 已验证（sample-thumb-test 硬件验证通过）
**相关 sample**: `sdk/samples/libimp-samples/sample-thumb-test.c`

---

## 1. 目标

拍照时同步生成缩略图，存入 `media_thumb.db`，供 APP 文件浏览时显示。

### 核心要求

- 缩略图与主图来自**同一帧**（同一 sensor 帧产生的 JPEG）
- **硬件完成**缩放和编码，零 CPU 缩放开销
- 支持所有拍照分辨率（2M ~ 42M）
- 不影响录影（RTSP/录制）的正常运行

---

## 2. Pipeline 架构

### 2.1 拍照时（非录影并发）

```
GC4653 Sensor (2560×1440 @ 30fps)
    │
    ├── FrameSource Ch0 (硬件 Scaler)
    │   │
    │   │  ≤ 8M: Scaler 输出目标分辨率 (如 3840×2160)
    │   │  > 8M: Scaler 输出 2560×1440 (sensor 原始分辨率)
    │   │
    │   └── Encoder Group 0, Channel 12: JPEG (IVDC 直通)
    │       │
    │       │  ≤ 8M: 硬件 Scaler + 硬件 JPEG 编码
    │       │  > 8M: CPU SIMD 放大 + 硬件 JPEG 编码
    │       │
    │       └── → 主图 .jpg 文件
    │
    └── FrameSource Ch2 (硬件 Scaler → 320×宽，保持宽高比)
        │
        └── Encoder Group 2, Channel 14: JPEG (IVDC 直通)
            │
            └── → 缩略图 .jpg → MetadataDao::saveThumbnail() → media_thumb.db
```

### 2.2 缩略图分辨率

| 主图宽高比 | 缩略图尺寸 | 说明 |
|---|---|---|
| 16:9 (2560×1440, 3840×2160, etc.) | 320×180 | 最常见 |
| 4:3 (2560×1944, 2688×2048) | 320×240 | 5M/6M |
| 其他 | 320×(按比例计算) | 保持原始宽高比 |

硬件 Scaler 自动处理缩放，无需 CPU 参与。

### 2.3 Encoder Channel 编号规则

根据 IMP SDK 的 channel 编号规则：

- Video encoder: `channel_id = group_id` (0, 1, 2, ...)
- JPEG encoder: `channel_id = 12 + group_id / 3`

| 通道 | Group ID | Encoder Channel | 用途 |
|---|---|---|---|
| CH0 | 0 | 12 | 主图 JPEG |
| CH1 | 1 | 1 (H265) / 12 (JPEG) | RTSP 子码流 |
| CH2 | 2 | 13 | 缩略图 JPEG |

> 注意：CH2 的 JPEG encoder channel = `12 + 2/3 = 12 + 0 = 12`，与 CH0 冲突。
> 需要手动指定为 channel 13 或其他未占用的编号。

---

## 3. 关键技术约束

### 3.1 IVDC (Image/Video Direct Connect)

- IVDC 是 FrameSource → Encoder 的硬件直通路径，帧数据走 DMA，不经过内存 Buffer
- 驱动层全局开启 (`insmod tx-isp.ko direct_mode=1`)，应用层 per-channel 设置 `bEnableIvdc = true`
- **IVDC 模式下 `IMP_FrameSource_GetFrame()` 不可用**（帧不在内存 Buffer 中）
- 已验证：**两个通道可以同时开 IVDC**（sample-thumb-test CH0 + CH1 验证通过）

### 3.2 FrameSource Scaler 硬件限制

| Scaler 操作 | 范围 | 说明 |
|---|---|---|
| 放大 | 最大 8M (3840×2160) | CH0 硬件 Scaler 上限 |
| 缩小 | 无明确下限 | 320×180 验证通过 |
| 方向 | 放大/缩小均可 | 硬件自动处理 |

### 3.3 与 RTSP 的关系

- RTSP 使用 CH1 (H265 encoder)，不占用 CH2
- 缩略图使用 CH2，与 RTSP 完全独立，无冲突

---

## 4. 产品代码集成方案

### 4.1 需要修改的文件

| 文件 | 改动 |
|---|---|
| `src/hal/ingenic/sensor-config.h` | 启用 `CHN2_EN = 1`，配置 CH2 scaler 为 320×180 |
| `src/media/snap/ImageSnap.h` | 添加 CH2 JPEG stream 成员 |
| `src/media/snap/ImageSnap.cpp` | 拍照时同时抓 CH0 + CH2 JPEG |
| `src/media/snap/LargeImageSnap.h` | 缩略图相关接口 |
| `src/media/snap/LargeImageSnap.cpp` | > 8M 拍照时同步生成缩略图 |
| `src/service/camera/impl/CameraServiceT32.cpp` | HW_ENCODER_MAX 调整到 8M，缩略图存 DB |

### 4.2 ImageSnap 改动

```
ImageSnap 初始化时：
  ├── 创建 CH0 JPEG stream (encoder channel 12, IVDC)
  └── 创建 CH2 JPEG stream (encoder channel 13, IVDC, 320×180)

ImageSnap::snap() 时：
  ├── 同时 StartRecvPic(CH0) + StartRecvPic(CH2)
  ├── PollingStream + GetStream 两个通道
  ├── CH0 → 写主图文件
  ├── CH2 → 写缩略图数据 (内存中)
  └── MetadataDao::saveThumbnail(filePath, thumbData)
```

### 4.3 LargeImageSnap 改动

```
LargeImageSnap (> 8M) 时：
  ├── CH0: GetFrame → CPU SIMD 放大 → InputJpege → 主图文件
  ├── CH2: GetStream → 缩略图 JPEG (硬件 Scaler 已缩小)
  └── MetadataDao::saveThumbnail(filePath, thumbData)
```

> 注意：LargeImageSnap 路径中 CH0 没有 IVDC（因为 > 8M 需要 CPU 处理），
> 但 CH2 仍然可以开 IVDC 用于缩略图。

### 4.4 HW_ENCODER_MAX 调整

```cpp
// 从 2560×1440 调整到 3840×2160 (8M)
// 8M 及以下走 ImageSnap (硬件 Scaler + 硬件 JPEG)
// 超过 8M 走 LargeImageSnap (CPU SIMD + 硬件 JPEG)
static constexpr int HW_ENCODER_MAX_W = 3840;
static constexpr int HW_ENCODER_MAX_H = 2160;
```

---

## 5. 已验证的 Sample

**文件**: `sdk/samples/libimp-samples/sample-thumb-test.c`

验证结果：
- ✅ IVDC 双通道同时工作 (CH0 + CH1)
- ✅ 硬件 Scaler 缩小到 320×180 正常
- ✅ 硬件 JPEG 编码小图正常
- ✅ 主图和缩略图同时抓取无崩溃
- ✅ T32 真机测试通过

---

## 6. 风险与备选

| 风险 | 概率 | 影响 | 备选 |
|---|---|---|---|
| CH2 IVDC 与 CH0 IVDC 冲突 | 低 | 缩略图失败 | 用 CH1 代替 CH2（需协调 RTSP） |
| CH2 Scaler 320×180 输出异常 | 极低 | 缩略图质量差 | 软件缩放 fallback |
| Encoder channel 13 编号冲突 | 低 | 初始化失败 | 调整 channel 编号 |
| > 8M 时 CH2 IVDC 状态不确定 | 低 | 缩略图失败 | LargeImageSnap 内部单独处理 CH2 |

---

## 7. 测试计划

1. **单元测试**: sample-thumb-test 已通过
2. **集成测试**:
   - 2M/4M/8M 拍照 → 验证主图 + 缩略图
   - 16M/42M 拍照 → 验证主图 + 缩略图
   - 连拍 → 验证缩略图全部生成
   - 录影中拍照 → 验证缩略图不影响录影
3. **DB 验证**: 检查 `media_thumb.db` 中缩略图数据正确
4. **APP 验证**: HTTP API 获取缩略图显示正常
