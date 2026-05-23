# 录影与拍照并发功能：差距分析与实现方案

> 基于 `doc/ref/T32 录影与拍照并发方案.md` 与 `doc/ref/T32 双码流配置与 Sensor-ISP-Encoder 联动机制.md` 的代码差距分析。
> 日期：2026-05-19

---

## 背景与目标

T32 SDK 支持在**同一 Encoder Group 内注册双 Channel**（Ch0: H264/H265 录影，Ch1: JPEG 拍照），利用硬件并行能力实现"录影中随时拍照"而无需软件切换或额外拷贝。

本文分析当前代码与该目标的差距，并给出可落地的实现方案。核心约束：**并发行为由用户设置的 `cameraMode` 决定，不是固定开启**。

---

## 协议规范对照

`CPS-CS-SET1-camera-parameter-settings-spec.md` 已正式定义 6 种 `CAM_Mode`：

| CAM_Mode | 协议名称 | 协议定义 | 正确的设计意图 |
|----------|---------|---------|--------------|
| 0 | 拍照 | 0：拍照 | 仅拍照，触发时只执行拍照 |
| 1 | 拍照+录像 | 1：拍照+录像 | 触发时**先拍照，再启动录影**（串行组合） |
| 2 | 录像 | 2：录像 | 仅录影，触发时只执行录影 |
| 3 | 同步拍录 | 3：同步拍录 | **边录边拍**（真正的并发，利用 SDK 双 Channel） |
| 4 | 智感录像 | 4：智感录像 | 运动检测触发的智能录像（需工厂开关 `Smart_DCVR_EN`） |
| 5 | 全时录像 | 5：全时录像 | 持续不间断录像（需工厂开关 `Video_ATR_EN`） |

**关键结论**：
- **Mode 3 "同步拍录"** 才是本文档要实现的"录影中并发拍照"场景
- Mode 1 "拍照+录像" 是串行组合（先拍后录），不需要 SDK 双 Channel 并发能力
- Mode 4/5 需要工厂开关和自动化触发层，不在本文档范围内

本文档后续方案将**对齐协议规范**，以 Mode 3 为并发拍照的目标模式。

---

## 差距分析

### 1. 架构层面：未利用 Encoder Group 多 Channel 能力

**文档要求的数据流：**
```
FrameSource Ch0 → [OSD Group 0] → Encoder Group 0 ─┬─→ Channel 0 (H264/H265, 录影)
                                                    └─→ Channel 1 (JPEG, 拍照)
```
同一 Group 内双 Channel **共享同一路输入图像缓存**，硬件自动分发。

**当前代码的数据流：**
```
VideoRecorder:  FrameSource Ch0 → Encoder Group 0 → Channel 0 (H264/H265)
ImageSnap:      FrameSource Ch0 → Encoder Group 0 → Channel 12 (JPEG)
```

表面看 group_id 都是 0，但实质问题是：

- `VideoRecorder` 和 `ImageSnap` **各自独立创建** `IVideo` / `IVideoStream` 实例
- `ImageSnap` 甚至独立调用 `video_->init()`（虽 refcounted）和 `video_->exit()`
- 两者都会调用 `IMP_FrameSource_SetChnAttr(group_id=0)`，**后配置者会覆盖前者**，导致录影分辨率被拍照参数篡改
- 两者都会独立 `EnableChn/DisableChn`，`IngenicVideoStream` 的 `ref_count_` 是**实例级而非全局级**，无法协调同一 FrameSource 的生命周期

### 2. HAL 层：缺少 FrameSource/Channel 全局引用计数

当前 HAL 已有 Group 和 Bind 的全局 ref_count（`g_group_ref_count`、`g_bind_ref_count`），但缺少 **FrameSource Channel 的全局 ref_count**。

`IngenicVideoStream::start()` 逻辑：
```cpp
if (ref_count_ == 0) {   // ← 实例级变量！不是全局的
    IMP_FrameSource_EnableChn(group_id_);
    IMP_Encoder_StartRecvPic(channel_id_);
}
```

这意味着：
- 录影已启动（EnableChn(0) 已调用）
- 再启动 `ImageSnap`，会再次调用 `EnableChn(0)` —— SDK 是否幂等未知，风险高
- 录影结束调用 `DisableChn(0)` 时，会把 `ImageSnap` 也搞断

### 3. Service 层：没有并发模式定义和交叉协调

- `cameraMode` 定义在 `Settings.h` 中为裸 `uint8_t`，取值 0-5
- 代码中只有 **mode 2 = video only** 有语义（scheduler 跳过定时拍照）
- `takePhoto()` 不检查 `is_recording_`，`startRecord()` 不检查 `is_capturing_`
- 两者没有"录影中复用编码器拍照"的代码路径

### 4. Media 层：`ImageSnap` 设计为独占式

`ImageSnap::initialize()` 完整流程：
```cpp
createVideo() → init() → createStream() → configure(JPEG) → start() → snap() → stop() → exit()
```

这个流程假设它是 FrameSource 的唯一使用者。在录影期间运行此流程，必然与 `VideoRecorder` 冲突。

### 5. `LargeImageSnap` 直接操作 FrameSource

`LargeImageSnap` 直接调用 `IMP_FrameSource_GetFrame(0)` 获取原始 NV12，完全绕过 Encoder。即使前面问题都解决，大图拍照路径（>2560x1440）仍会与 Encoder 竞争原始帧。

---

## 推荐实现方案

### 核心思路

**让 `VideoRecorder` 在并发模式下同时管理 H264 和 JPEG 两个 Channel**，而不是让 `ImageSnap` 独立创建。拍照请求在录影期间直接由 `VideoRecorder` 的 JPEG Channel 输出一帧。

未录影时，拍照保持现有逻辑（`ImageSnap` 或 `LargeImageSnap`）。

### Phase 1：定义 cameraMode 语义（对齐协议规范）

`CPS-CS-SET1` 已定义 6 种模式，代码实现应与之对齐：

| Mode | 协议名称 | 行为定义 | 实现优先级 |
|------|---------|---------|-----------|
| 0 | 拍照 | 触发时仅执行拍照；手动调用 `startRecord()` 时拒绝 | P1 |
| 1 | 拍照+录像 | 触发时**先拍照，再启动录影**（串行组合，非并发）；手动调用均放行 | P2 |
| 2 | 录像 | 触发时仅执行录影；手动调用 `takePhoto()` 时拒绝；scheduler 跳过定时拍照 | P1 |
| 3 | 同步拍录 | **边录边拍**（真正的并发）；录影期间 `takePhoto()` 走 `VideoRecorder` 的 JPEG Channel | P1（本文档核心目标） |
| 4 | 智感录像 | 运动检测触发录像（需工厂开关 `Smart_DCVR_EN`） | P3（依赖 PIR/运动检测） |
| 5 | 全时录像 | 持续不间断录像（需工厂开关 `Video_ATR_EN`） | P3（需开机自动录影逻辑） |

**关于 Mode 1 vs Mode 3 的区别**：
- **Mode 1** 是"先拍照，再录影"的**串行组合**（例如 PIR 触发后：拍 1 张照片 → 启动 10 秒录影）。不需要 SDK 双 Channel 能力，只需在触发逻辑中按顺序调用 `takePhoto()` → `startRecord()`。
- **Mode 3** 是"边录边拍"的**真正的并发**（录影进行中，用户或触发器随时调用 `takePhoto()`）。必须利用 SDK 同一 Encoder Group 内双 Channel 能力，否则 `ImageSnap` 会与 `VideoRecorder` 冲突。

修改点：
- `CameraParameterRegistry.cpp`：为 `CAM_Mode` 的 0-5 各值添加中文描述文本
- `CameraServiceT32.cpp`：`takePhoto()` / `startRecord()` / scheduler 中增加 mode 路由守卫
- 新增 C++ enum 或常量定义（如 `enum class CamMode { PHOTO=0, PHOTO_THEN_VIDEO=1, VIDEO=2, CONCURRENT=3, SMART=4, ALWAYS_ON=5 }`）

### Phase 2：HAL 层增强 — FrameSource 全局引用计数

在 `IngenicVideo.cpp` 中新增 `g_fs_ref_count`（类似 `g_group_ref_count`）：

```cpp
static std::mutex g_fs_mutex;
static std::map<int, int> g_fs_ref_count;

static bool acquireFrameSource(int group_id) {
    // ref_count++，首次调用时 IMP_FrameSource_EnableChn
}
static void releaseFrameSource(int group_id) {
    // ref_count--，归零时 IMP_FrameSource_DisableChn
}
```

修改 `IngenicVideoStream::start()` 和 `stop()`，将 `EnableChn/DisableChn` 替换为 `acquireFrameSource/releaseFrameSource`。

> 保留 `IMP_Encoder_StartRecvPic/StopRecvPic` 在各自的 `start/stop` 中按 channel 独立调用，因为不同 Channel 的启停应该独立。

修改点：
- `src/hal/ingenic/IngenicVideo.cpp`

### Phase 3：Media 层 — `VideoRecorder` 支持并发 JPEG Channel

在 `VideoRecorder` 中新增 JPEG stream 管理：

```cpp
class VideoRecorder {
    // 现有成员
    std::shared_ptr<hal::IVideo> video_;
    std::shared_ptr<hal::IVideoStream> stream_;   // H264/H265 录影流

    // 新增成员（仅并发模式使用）
    std::shared_ptr<hal::IVideoStream> jpegStream_;  // JPEG 拍照流
    bool concurrentSnapEnabled_ = false;

public:
    // 新增接口
    bool captureJpeg(const std::string& filename, int quality);
    bool isConcurrentSnapEnabled() const { return concurrentSnapEnabled_; }
};
```

`initVideo()` 增强逻辑：
1. 保持现有 H264/H265 stream 创建逻辑不变
2. 如果检测到并发模式（通过外部传入标志，或查询 `Settings::cameraMode`），额外创建 JPEG stream：
   - `sensor_index = VIDEO_SENSOR_ID`, `stream_index = VIDEO_STREAM_ID`（与录影流同一个 FrameSource）
   - `payload = JPEG`
   - `width/height` 使用当前拍照配置分辨率
   - `quality` 使用配置质量
3. `acquireGroup` 和 `acquireBind` 的 refcount 会确保 Group 0 和 Bind 只创建一次

`captureJpeg()` 逻辑：
1. 检查 `jpegStream_` 已配置且录影中
2. `jpegStream_->start()` → `polling()` → `getFrame()` → 写入文件 → `releaseFrame()` → `jpegStream_->stop()`
3. 由于 FrameSource Enable/Disable 已改为全局 ref_count，`jpegStream_->start()` 不会重复 Enable FrameSource，`stop()` 也不会 Disable 它（因为录影流还在持有）

修改点：
- `src/media/video/VideoRecorder.h`
- `src/media/video/VideoRecorder.cpp`

### Phase 4：Service 层 — 模式路由与并发拍照

修改 `CameraServiceT32`：

**`startRecord()` 增强（增加 mode guard + 并发检测）：**
```cpp
int CameraServiceT32::startRecord(...) {
    std::lock_guard<std::mutex> lock(op_mutex_);

    uint8_t camMode = Settings::getInstance()->cameraMode;

    // Mode 0 (仅拍照) → 拒绝录影
    if (camMode == 0) {
        elog_w(TAG, "startRecord rejected: cameraMode=%d (photo only)", camMode);
        return -1;
    }

    // ... 现有录影逻辑 ...

    // Mode 3 (同步拍录) → VideoRecorder 需要同时创建 JPEG Channel
    bool concurrentMode = (camMode == 3);
    video_recorder_ = std::make_shared<media::VideoRecorder>(vidParam, audParam, concurrentMode);
    // VideoRecorder 内部根据 concurrentMode 决定是否创建 jpegStream_
}
```

**`takePhoto()` 增强（增加 mode guard + 并发路径）：**
```cpp
int CameraServiceT32::takePhoto(...) {
    std::lock_guard<std::mutex> op_lock(op_mutex_);

    uint8_t camMode = Settings::getInstance()->cameraMode;

    // Mode 2 (仅录影) → 拒绝拍照（但允许 preview/burst/timer 等内部调用根据需求调整）
    if (camMode == 2) {
        elog_w(TAG, "takePhoto rejected: cameraMode=%d (video only)", camMode);
        return -1;
    }

    // Mode 3 (同步拍录) + 正在录影 → 走 VideoRecorder 的 JPEG Channel（并发拍照）
    if (camMode == 3 && isRecording()) {
        if (video_recorder_ && video_recorder_->isConcurrentSnapEnabled()) {
            return video_recorder_->captureJpeg(filename, jpegQuality) ? 0 : -1;
        }
    }

    // 其他 mode 或未录影时 → 保持现有 ImageSnap / LargeImageSnap 逻辑
    // ... 现有逻辑 ...
}
```

**`startBurstPhoto()` / `startTimerPhoto()` 增强：**
- Mode 3 下如果正在录影，每个 `takePhoto()` 调用会自动走 `VideoRecorder::captureJpeg()` 并发路径
- 未录影时，保持现有独立 `ImageSnap` 逻辑
- Mode 2 下应拒绝启动 burst/timer 拍照

**Scheduler 增强：**
```cpp
uint8_t camMode = Settings::getInstance()->cameraMode;

// Mode 2 (仅录影) → 跳过定时拍照（现有行为）
if (camMode == 2) {
    elog_w(TAG, "Scheduler: skipped photo, cameraMode=%d (video only)", camMode);
    continue;
}

// Mode 0 (仅拍照) / Mode 1 (拍照+录像) / Mode 3 (同步拍录) → 正常执行定时拍照
// Mode 3 下如果正在录影，takePhoto() 内部会自动走并发路径
PhotoResult result;
if (takePhoto(0, true, "jpg", 85, result) == 0) {
    elog_i(TAG, "Scheduler: photo taken -> %s", result.filePath.c_str());
}
```

修改点：
- `src/service/camera/impl/CameraServiceT32.h`
- `src/service/camera/impl/CameraServiceT32.cpp`

### Phase 5：OSD/水印一致性（可选但建议）

当前 `group_id_ == 0` 时，`IngenicVideoStream` 会触发 `IspOsdManager::prepare/start/stop`。并发模式下，JPEG Channel 也在 group 0，需要确保 OSD 不会因为 JPEG stream 的 start/stop 而受影响。

由于 Phase 2 已将 FrameSource Enable/Disable 改为全局 ref_count，OSD 的启停逻辑（当前绑定在 `IngenicVideoStream::start/stop` 中）可能需要移到全局 ref_count 的回调中，或保持现状（因为 OSD start/stop 当前也随 group_id==0 的 stream start/stop 触发，可能重复执行）。

**建议**：将 `IspOsdManager::start/stop` 也改为全局 ref_count 驱动，或至少在 `IngenicVideoStream::start()` 中检查 `g_fs_ref_count[group_id_]` 是否为 1（首次启用）时才触发 OSD start。

---

## 关键文件清单

| 文件 | 修改内容 |
|------|----------|
| `src/hal/ingenic/IngenicVideo.cpp` | 新增 `g_fs_ref_count`，修改 `start()/stop()` 中的 FrameSource Enable/Disable 为全局 ref_count |
| `src/media/video/VideoRecorder.h` | 新增 `jpegStream_`、`concurrentSnapEnabled_`、`captureJpeg()` 声明 |
| `src/media/video/VideoRecorder.cpp` | `initVideo()` 中创建 JPEG stream；实现 `captureJpeg()` |
| `src/service/camera/impl/CameraServiceT32.h` | 新增 `isConcurrentMode()` 辅助方法 |
| `src/service/camera/impl/CameraServiceT32.cpp` | `takePhoto()` / `startRecord()` / burst / timer / scheduler 中增加 mode 路由 |
| `src/service/camera/CameraParameterRegistry.cpp` | 为 `CAM_Mode` 的 0-5 各值添加描述文本 |

---

## 验证方案

### 已完成：Sample 级硬件验证（2026-05-20）

使用 `sdk/samples/libimp-samples/sample-Encoder-video-jpeg` 在 T32 真机（GC4653 @ 2560×1440）上完成多轮对照实验：

| 方案 | CH0 | CH2 | JPEG 目标 | 录影 FPS | 拍照结果 | 结论 |
|------|-----|-----|----------|---------|---------|------|
| **基线** | 2560×1440 H265 | — | 2560×1440 | 30fps | ✅ | 无缩放基准 |
| **CH2 硬件 8M** | 2560×1440 H265 | 3840×2160 | 3840×2160 | **30fps** | **✅ 8M** | **当前推荐稳定方案** |
| CH2 硬件 8M + 软件 16M | 2560×1440 H265 | 3840×2160 | 4608×3456 | 17fps（VTS 问题） | ❌ `work_done=1` | CH2 Encoder **不支持软件缩放** |
| CH0 纯软件 16M | 2560×1440 H265 | — | 4608×3456 | ~0.5fps | ✅ 16M | 能工作但录影几乎瘫痪 |
| CH0 硬件 8M + 软件 16M | 3840×2160（无 H265） | 2560×1440 H265 | 4608×3456 | ~1fps | ✅ 16M | 主通道按需编码导致阻塞，拖累 CH2 |
| CH0 硬件 8M 无缩放 | 3840×2160（无 H265） | 2560×1440 H265 | 3840×2160 | ~1fps | ✅ 8M | 8M 硬件放大本身不影响录影，**JPEG 按需编码阻塞主通道**是元凶 |

**关键发现**：
1. **CH2 硬件放大到 8M 是并发拍照的最佳路径** — 录影 30fps 不受影响，拍照 8M 稳定
2. **CH2 Encoder 软件缩放不支持 >4K** — 任何 >3840×2160 的目标分辨率都会导致 `work_done=1`（编码器未启动）
3. **CH0 软件缩放 16M 可行但录影掉帧严重** — 软件缩放 CPU 负载高，同 Group 的 H265 录影从 30fps 掉到 0.5fps
4. **GC4653 需要 VTS=1680 workaround** — `libimp.so` 内部会自动改写 VTS 到 3000，导致 FPS 降到 17fps；sample 中已添加寄存器级 workaround（`0x0340=0x06, 0x0341=0x90`）
5. **不要调用 `IMP_ISP_Tuning_SetSensorFPS`** — 该调用会触发 `gc4653_set_fps()` 错误计算 VTS=3000

### 待验证：项目代码集成

1. **编译验证**
   ```bash
   cmake -DBUILD_FOR_SIMULATION=ON -B build_sim -S . && cmake --build build_sim -j$(nproc)
   ```
   确保仿真编译通过（HAL 层改动需注意 `#ifndef SIMULATION_MODE` 包裹）。

2. **单元/集成测试（仿真环境）**
   - 设置 `cameraMode = 3`（同步拍录）
   - 调用 `startRecord()`，验证 `VideoRecorder` 初始化成功
   - 在录影中调用 `takePhoto()`，验证走 `VideoRecorder` 的 JPEG Channel 并发路径
   - 验证照片文件生成，且录影未中断
   - 停止录影后，验证独立 `takePhoto()` 仍走 `ImageSnap` / `LargeImageSnap` 路径

3. **硬件验证（T32 设备）**
   - 并发模式下录影 + 拍照，检查 MP4 文件完整性
   - 检查 JPEG 图片质量与分辨率是否符合配置（当前验证上限 8M）
   - 验证 mode 0/2 的原有行为不受影响
   - 验证 VTS=1680 workaround 在产品代码中生效（参考 `doc/knowledge/bugs/T32-recording-fps-17-investigation.md`）

---

## 风险与注意事项

1. **`channel_id` 冲突**：当前 JPEG channel_id 计算为 `12 + group_id_/3`。对于 group 0,1,2（sensor 0 的三个 stream），JPEG channel 都是 12。如果未来需要同时支持 stream 0 和 stream 1 的并发拍照，channel 分配需要重新设计。但当前产品只使用 sensor 0 + stream 0，暂不冲突。

2. **分辨率一致性**：JPEG Channel 的分辨率可以与 H264 Channel 不同（SDK 内部自动 software scaler）。但 `IngenicVideoStream::configure()` 会调用 `IMP_FrameSource_SetChnAttr` 修改 FrameSource 的 scaler 输出。如果 JPEG stream 和 H264 stream 配置不同的分辨率，后配置者会覆盖前者。解决方案：并发模式下，JPEG stream 的 configure **不修改 FrameSource 属性**（或只修改 Encoder 属性），让 JPEG 分辨率完全由 Encoder 内部处理。

2. **分辨率限制 — 并发拍照最大 8M**：
   - T32 FrameSource 硬件 scaler 上限为 **4K（3840×2160，约 8M）**
   - CH2（Group 2）的 Encoder **不支持软件缩放**，任何 >3840×2160 的 `picWidth/picHeight` 都会导致 `work_done=1`（编码器未启动）
   - CH0（Group 0）的 Encoder **支持软件缩放到 16M（4608×3456）**，但会严重拖垮同 Group 的 H265 录影（30fps → 0.5fps）
   - **结论**：并发拍照（录影不中断）的最大分辨率只能是 **8M（3840×2160）**。如果需要 16M/32M，必须：
     - 方案 A：暂停录影后走 `LargeImageSnap` 软件路径
     - 方案 B：接受录影严重掉帧（不推荐）

3. **分辨率一致性**：JPEG Channel 的分辨率可以与 H264 Channel 不同（SDK 内部自动 software scaler）。但 `IngenicVideoStream::configure()` 会调用 `IMP_FrameSource_SetChnAttr` 修改 FrameSource 的 scaler 输出。如果 JPEG stream 和 H264 stream 配置不同的分辨率，后配置者会覆盖前者。解决方案：并发模式下，JPEG stream 的 configure **不修改 FrameSource 属性**（或只修改 Encoder 属性），让 JPEG 分辨率完全由 Encoder 内部处理。

4. **"孤儿" FrameSource 通道**：任何启用的 FrameSource Channel 必须绑定到至少一个有效的 Encoder Channel（Video 或 JPEG）。如果 CH2 启用了 FrameSource 但没有绑定有效 Encoder（例如把 JPEG 移到 CH0 后忘记处理 CH2），会导致 SDK 内部状态异常，进而拖垮其他 Group 的编码器。

5. **GC4653 VTS 问题**：
   - **不要调用 `IMP_ISP_Tuning_SetSensorFPS`** — 会触发 `gc4653_set_fps()` 错误计算 VTS=3000，导致 FPS 降到 17fps
   - `libimp.so` 动态库在 `IMP_ISP_EnableSensor`/`EnableTuning` 期间内部也会改写 VTS 到 3000
   - **Workaround**：在 sensor init 后通过 `IMP_ISP_SetSensorRegister` 强制写入 VTS=1680（`0x0340=0x06, 0x0341=0x90`）
   - 参考 `doc/knowledge/bugs/T32-recording-fps-17-investigation.md`

6. **LargeImageSnap**：超过硬件编码器最大分辨率（2560×1440）的拍照需求，仍然需要 `LargeImageSnap` 的软件路径。并发模式下如果用户请求超大分辨率拍照，需要：
   - 方案 A：拒绝并发模式下的超大分辨率拍照（回退到停止录影后拍照）
   - 方案 B：让 `LargeImageSnap` 也能与录影共存（直接 GetFrame 原始 NV12，不经过 Encoder）
   - 推荐方案 A，因为并发拍照通常用于"抓拍"场景，不需要超大分辨率。
