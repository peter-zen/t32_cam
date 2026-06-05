# 拍照缩略图生成方案设计

**日期**: 2026-06-04
**状态**: ✅ 已实现并验证
**相关 sample**: `sdk/samples/libimp-samples/sample-thumb-test.c`

---

## 1. 目标

拍照时同步生成缩略图，存入 `media_thumb.db`，供 APP 文件浏览时显示。

### 核心要求

- 缩略图与主图来自**同一 sensor 帧**
- **硬件完成**缩放和编码，CPU 开销最小
- 支持所有拍照分辨率（2M ~ 42M）
- 不影响录影（RTSP/录制）的正常运行

---

## 2. 统一方案（当前实现）

### 2.1 架构

**ImageSnap 统一处理所有分辨率**，内部根据目标分辨率自动选择路径：

```
ImageSnap::snap()
  │
  ├── ≤ 8M 路径 (CH0 IVDC 开启):
  │     CH0 → 硬件 Scaler → 硬件 JPEG Encoder (IVDC 直通) → 主图文件
  │
  ├── > 8M 路径 (CH0 IVDC 关闭):
  │     CH0 → GetFrame(0) → CPU SIMD 放大 → InputJpege 硬件编码 → 主图文件
  │
  └── (两条路径共用) 缩略图:
        CH2 (IVDC) → 硬件 Scaler 320×180 → 硬件 JPEG Encoder → thumbData_
        → MetadataDao::saveThumbnail() → media_thumb.db
```

### 2.2 Pipeline 图

```
GC4653 Sensor (2560×1440 @ 30fps)
    │
    ├── FrameSource Ch0 (硬件 Scaler)
    │   │
    │   │  ≤ 8M: IVDC 开，Scaler 输出目标分辨率
    │   │  > 8M: IVDC 关，Scaler 输出 sensor 原始分辨率
    │   │
    │   └── Encoder Group 0, Channel 12: JPEG
    │       │
    │       │  ≤ 8M: IVDC 直通，硬件 Scaler + 硬件编码
    │       │  > 8M: GetFrame + CPU SIMD 放大 + InputJpege 硬件编码
    │       │
    │       └── → 主图 .jpg 文件
    │
    └── FrameSource Ch2 (硬件 Scaler → 320×180)
        │
        └── Encoder Group 2, Channel 14: JPEG (IVDC 直通)
            │
            └── → 缩略图 JPEG → MetadataDao::saveThumbnail() → media_thumb.db
```

### 2.3 关键设计点

| 设计点 | 说明 |
|---|---|
| **IVDC 条件开启** | ≤ 8M 时 CH0 开 IVDC（硬件直通），> 8M 时关 IVDC（需要 GetFrame） |
| **CH2 始终 IVDC** | 缩略图通道不受 CH0 影响，始终走硬件直通 |
| **统一入口** | CameraServiceT32 只调用 ImageSnap，不需要 LargeImageSnap |
| **JPEG encoder channel** | CH0 → 12, CH2 → 14（修复了 CH0/CH2 冲突） |
| **缩略图分辨率** | 320 宽，高度按原始宽高比计算（硬件 Scaler 处理） |

### 2.4 Encoder Channel 编号

```cpp
// 修复后的编号规则: 12 + sensor_index * 3 + stream_index
// CH0: 12 + 0*3 + 0 = 12
// CH2: 12 + 0*3 + 2 = 14  (原来 12 + 2/3 = 12，与 CH0 冲突)
```

---

## 3. IVDC (Image/Video Direct Connect) 约束

- IVDC 是 FrameSource → Encoder 的硬件直通路径，帧数据走 DMA，不经过内存 Buffer
- **IVDC 模式下 `IMP_FrameSource_GetFrame()` 不可用**
- 两个通道可以同时开 IVDC（sample-thumb-test 验证通过）
- IVDC 是 per-channel 设置，CH0 关闭 IVDC 不影响 CH2

---

## 4. 已验证的 Sample

**文件**: `sdk/samples/libimp-samples/sample-thumb-test.c`

验证结果：
- ✅ IVDC 双通道同时工作 (CH0 + CH1)
- ✅ 硬件 Scaler 缩小到 320×180 正常
- ✅ 硬件 JPEG 编码小图正常
- ✅ 主图和缩略图同时抓取无崩溃
- ✅ T32 真机测试通过

---

## 5. 测试验证

| 测试项 | 状态 |
|---|---|
| ≤ 8M 拍照 + 缩略图 | ✅ 已验证 |
| > 8M 拍照 + 缩略图 | 待验证 |
| 连拍 + 缩略图 | 待验证 |
| RTSP 不受影响 | ✅ 已验证 |
| daynight 模式不被改变 | ✅ 已验证 |
| HTTP API 获取缩略图 | ✅ 已验证 |
| media_file.db 照片记录 | ✅ 已验证 |

---

## 6. 修改文件清单

| 文件 | 改动 |
|---|---|
| `src/hal/ingenic/sensor-config.h` | 启用 `CHN2_EN = 1`，CH2 Scaler 320×180 |
| `src/hal/ingenic/IngenicVideo.cpp` | JPEG encoder channel 编号修复 |
| `src/media/snap/ImageSnap.h` | 添加 CH2 thumbnail stream、isLargeImage_ 标记 |
| `src/media/snap/ImageSnap.cpp` | 统一 ≤8M/>8M 路径，CH2 缩略图，daynight no-op |
| `src/service/camera/impl/CameraServiceT32.cpp` | 统一走 ImageSnap，缩略图存 DB |
| `sdk/samples/libimp-samples/sample-thumb-test.c` | 新增：双 JPEG IVDC 验证 sample |
| `sdk/samples/libimp-samples/Makefile` | 新增 sample-thumb-test 构建规则 |

---

## 7. 废弃方案记录

### 废弃方案 A：从 EXIF 提取缩略图

**思路**: JPEG 文件中嵌入 EXIF 缩略图，拍照后从文件中提取。

**废弃原因**:
- T32 硬件 JPEG 编码器输出的是裸 JPEG，不包含 EXIF 数据
- SDK 没有 EXIF 写入 API
- `Jpeg::extractThumbnail()` 依赖文件中有内嵌的第二个 JPEG SOI/EOI，T32 编码器不产生这种数据

### 废弃方案 B：GetFrame 获取 NV12 + 软件缩放 + 硬件编码

**思路**: 拍照时从 FrameSource GetFrame 获取 NV12，软件缩放到 320×180，用 InputJpege 硬件编码为缩略图。

**废弃原因**:
- IVDC 开启时 GetFrame 崩溃（帧走 DMA 直通，不在内存 Buffer 中）
- 从 CH1 GetFrame 也崩溃（CH1 被 RTSP encoder 占用时冲突）
- 在 stream_->stop() 之后 GetFrame 也崩溃（FrameSource 通道状态异常）

**参考**: 2026-06-04 多次调试记录，确认 GetFrame 在 encoder 活跃时不可用。

### 废弃方案 C：LargeImageSnap 独立路径

**思路**: ≤ 8M 走 ImageSnap，> 8M 走 LargeImageSnap 独立代码路径。

**废弃原因**:
- LargeImageSnap 路径没有缩略图支持
- 两条独立路径增加维护成本
- 缩略图逻辑无法统一

**替代**: 合并到 ImageSnap 中，> 8M 时关闭 CH0 IVDC，用 GetFrame + CPU SIMD + InputJpege。

---

## 8. 后续优化

1. **> 8M 路径验证**: 需要在 T32 真机上验证 16M/42M 拍照 + 缩略图
2. **连拍优化**: 多张连拍时 CH2 缩略图的性能
3. **LargeImageSnap 清理**: 确认不再需要后可以移除
4. **CameraServiceT32 清理**: 移除 large_snap_ 成员和相关 include
