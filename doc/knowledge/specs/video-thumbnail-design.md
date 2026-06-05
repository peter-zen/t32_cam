# 录影缩略图生成方案设计

**日期**: 2026-06-05
**状态**: ✅ 已实现
**前置依赖**: 拍照缩略图方案（`thumbnail-generation-design.md`）

---

## 1. 目标

录影时同步生成缩略图，存入 `media_thumb.db`，供 APP 历史记录页面显示。

### 核心要求

- 缩略图来自录影过程中的实际画面
- 不影响录影帧率和质量
- 复用已验证的 CH2 硬件缩放 + 硬件 JPEG 编码方案

---

## 2. 方案

### 2.1 架构

```
VideoRecorder::record()
  │
  ├── 录影开始前:
  │     captureThumbnail()
  │       ├── jpegStream_->start()  (CH2, 3840×2160, IVDC)
  │       ├── 抓一帧 JPEG → thumbData_ (内存)
  │       └── jpegStream_->stop()
  │
  ├── 录影循环:
  │     CH0 (IVDC) → H265 Encoder → MP4 文件
  │
  └── onRecordDone 回调:
        ├── MetadataDao::addMedia()  ← 录影元数据
        └── MetadataDao::saveThumbnail()  ← 缩略图
```

### 2.2 Pipeline 图

```
GC4653 Sensor (2560×1440 @ 30fps)
    │
    ├── FrameSource Ch0 (硬件 Scaler)
    │   └── Encoder Group 0, Channel 0: H265 (IVDC 直通)
    │       └── → 录影 MP4 文件
    │
    └── FrameSource Ch2 (硬件 Scaler → 3840×2160)
        └── Encoder Group 2, Channel 14: JPEG (IVDC 直通)
            └── → 录影开始时抓一帧 → thumbData_ → DB
```

### 2.3 关键设计点

| 设计点 | 说明 |
|---|---|
| **CH2 独立 Group** | 与录影 CH0 完全独立，不影响录影帧率 |
| **抓取时机** | 录影循环开始前抓一帧，确保有画面 |
| **内存存储** | 缩略图存在内存中（thumbData_），录影结束后写 DB |
| **IVDC** | CH2 始终开 IVDC，硬件直通 |
| **复用 captureJpeg 机制** | 使用 VideoRecorder 已有的 initJpegStream() + CH2 JPEG stream |

### 2.4 与拍照缩略图的关系

| 场景 | CH0 | CH2 | 缩略图来源 |
|---|---|---|---|
| 拍照（≤ 8M） | JPEG (IVDC) | JPEG 320×180 (IVDC) | CH2 硬件缩放 |
| 拍照（> 8M） | GetFrame + SIMD | JPEG 320×180 (IVDC) | CH2 硬件缩放 |
| 录影 | H265 (IVDC) | JPEG 3840×2160 (IVDC) | CH2 硬件 JPEG |

录影和拍照的 CH2 使用互斥（不会同时发生），硬件资源无冲突。

---

## 3. 修改文件清单

| 文件 | 改动 |
|---|---|
| `src/media/video/VideoRecorder.h` | 添加 `captureThumbnail()`、`thumbData_`、访问接口 |
| `src/media/video/VideoRecorder.cpp` | 实现 `captureThumbnail()`，录影开始时调用 |
| `src/service/camera/impl/CameraServiceT32.cpp` | 构造 VideoRecorder 时传 `concurrentSnap=true`，onRecordDone 中保存缩略图 |

---

## 4. 测试验证

| 测试项 | 状态 |
|---|---|
| 录影 + 缩略图生成 | 待验证 |
| 录影帧率不受影响 | 待验证 |
| media_thumb.db 有录影缩略图 | 待验证 |
| HTTP API 获取录影缩略图 | 待验证 |
| 连续多段录影缩略图 | 待验证 |

---

## 5. 已知限制

- **缩略图分辨率**: CH2 配置为 3840×2160（与并发拍照共用），缩略图文件较大。后续可优化为 320×180 专用配置。
- **抓取时机**: 在录影循环开始前抓取，如果录影开始瞬间画面不佳（如过渡帧），缩略图质量可能不理想。
- **并发拍照**: 如果录影中触发拍照（`captureJpeg`），缩略图通道会被拍照占用，但此时缩略图已抓取完毕，无影响。
