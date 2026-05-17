# T32 录影分辨率与 Sensor-ISP-Encoder 联动配置说明

> 本文档描述在 T32 平台上，当 Sensor 物理输出分辨率（固定 2560x1440@30fps）与录影属性设置的分辨率（如 1920x1080）不一致时，系统如何通过 FrameSource Crop + Scaler + Encoder 三级联动来达成目标录影规格。

---

## 1. 当前硬件基线（GC4653）

| 参数 | 值 | 来源 |
|------|-----|------|
| Sensor 型号 | GC4653 | `CMakeLists.txt: -DSENSOR_TYPE_GC4653` |
| Sensor 物理分辨率 | 2560 x 1440 | `sensor-config.h: FIRST_SENSOR_WIDTH/HEIGHT` |
| Sensor 输出帧率 | 30 fps | `sensor-config.h: FIRST_SENSOR_FRAME_RATE_NUM/DEN` |
| FrameSource Ch0 初始化分辨率 | 2560 x 1440 | `IngenicVideo.cpp: fillFsAttrForOutput()` |
| FrameSource Ch0 初始化帧率 | 30 fps | `IngenicVideo.cpp: fillFsAttrForOutput()` |

Sensor 只输出一条物理流，所有多路输出由 ISP（FrameSource）内部的硬件 Scaler 完成。

---

## 2. 录影属性配置流程

### 2.1 属性来源

录影参数从 `Settings` + `DeviceConfig` 中读取，经 `CameraPropertyService` 解析：

| 属性 | 存储位置 | 默认值 |
|------|----------|--------|
| 分辨率 + FPS | `Settings::videoSize`（索引值） | 1080P/30FPS |
| 编码格式 | `Settings::videoCodec`（1=H264, 2=H265） | H264 |
| 码率控制模式 | `Settings::videoRcMode`（1=CBR, 2=VBR, 3=CVBR, 4=SMART） | CBR |
| 码率 | `Settings::bitRate_1080p / bitRate_4k / bitRate_720p` | 16 Mbps（1080p） |
| 录影时长 | `Settings::videoLength_h/l` | 30 秒 |

`CameraPropertyService::getVideoRecordConfig()` 将 `videoSize` 索引解析为具体的 `width x height x fps`。

### 2.2 应用到录影器

在 `CameraServiceT32::startRecord()` 中：

```cpp
auto vidParam = std::make_shared<media::VideoParams>();
applyConfiguredVideoParams(vidParam);  // 从 CameraPropertyService 读取并填充
video_recorder_ = std::make_shared<media::VideoRecorder>(vidParam, audParam);
```

`applyConfiguredVideoParams()` 完成以下映射：
- 分辨率/帧率/码率 -> `VideoParams`
- 编码格式（1/2） -> `VideoCodecFormat::H264/H265`
- RC 模式（1~4） -> `VideoRcMode::CBR/VBR/CVBR/SMART`

---

## 3. HAL 层动态调整（核心）

### 3.1 入口：`IngenicVideoStream::configure()`

`VideoRecorder::initVideo()` 创建 `IngenicVideoStream` 后调用 `configure(cfg)`，其中 `cfg` 包含目标录影参数：

```cpp
cfg.width = 1920;      // 目标录影宽
cfg.height = 1080;     // 目标录影高
cfg.fps_num = 30;      // 目标帧率
cfg.bitrate = 16384;   // 目标码率（Kbps）
cfg.payload = H264;    // 编码格式
cfg.rc_mode = CBR;     // 码控模式
```

### 3.2 FrameSource 属性修改

```cpp
// 1. 获取当前 FrameSource 通道属性（此时 picWidth/Height = Sensor 原生分辨率）
IMP_FrameSource_GetChnAttr(group_id_, &fs_chn_attr);

// 2. 保存原始 Sensor 分辨率
int sensor_width = fs_chn_attr.picWidth;    // 2560
int sensor_height = fs_chn_attr.picHeight;  // 1440

// 3. 设置 FrameSource 输出分辨率 = 目标录影分辨率
fs_chn_attr.picWidth = cfg.width;           // 1920
fs_chn_attr.picHeight = cfg.height;         // 1080
fs_chn_attr.scaler.enable = 1;
fs_chn_attr.scaler.outwidth = cfg.width;    // 1920
fs_chn_attr.scaler.outheight = cfg.height;  // 1080

// 4. 开启 Crop 并覆盖 Sensor 全画面
//    确保 Scaler 的输入是全幅 Sensor 图像，而不是左上角小区域
fs_chn_attr.crop.enable = 1;
fs_chn_attr.crop.top = 0;
fs_chn_attr.crop.left = 0;
fs_chn_attr.crop.width = sensor_width;      // 2560
fs_chn_attr.crop.height = sensor_height;    // 1440

// 5. 设置输出帧率
fs_chn_attr.outFrmRateNum = cfg.fps_num;    // 30
fs_chn_attr.outFrmRateDen = cfg.fps_den;    // 1

// 6. 下发属性
IMP_FrameSource_SetChnAttr(group_id_, &fs_chn_attr);
```

**关键逻辑**：
- `picWidth/Height` 是 FrameSource 通道的**输出分辨率**，必须等于目标录影分辨率
- `scaler.outwidth/height` 是硬件 Scaler 的目标输出，也必须等于目标分辨率
- `crop` 定义了从 Sensor 输入中取哪一块区域。开启 crop 并设为全画面（2560x1440），确保 Scaler 处理的是完整图像，而非左上角裁剪

### 3.3 Encoder 配置

`configureEncoderAttr()` 根据 `cfg` 填充 `IMPEncoderCHNAttr`：

```cpp
chn_attr->encAttr.enType = PT_H264;         // 或 PT_H265
chn_attr->encAttr.picWidth = cfg.width;     // 1920
chn_attr->encAttr.picHeight = cfg.height;   // 1080
chn_attr->rcAttr.outFrmRate.frmRateNum = cfg.fps_num;   // 30
chn_attr->rcAttr.outFrmRate.frmRateDen = cfg.fps_den;   // 1
chn_attr->rcAttr.maxGop = cfg.gop;          // 60（2秒 GOP）
// CBR/VBR/CVBR/SMART 的码率、QP 等参数... 
```

Encoder 分辨率必须与 FrameSource 输出分辨率一致（均为目标录影分辨率）。

### 3.4 绑定与启动

```cpp
IMP_Encoder_CreateChn(channel_id_, &chn_attr);
IMP_Encoder_RegisterChn(group_id_, channel_id_);  // 绑定到 FrameSource Group
IMP_System_Bind(fs_cell, enc_cell);               // FS -> ENC

// 启动时
IMP_FrameSource_EnableChn(group_id_);
IMP_Encoder_StartRecvPic(channel_id_);
```

---

## 4. 数据流完整链路

```
Sensor (GC4653)
  |
  | 物理输出: 2560x1440 @ 30fps
  v
FrameSource Ch0 (ISP)
  |
  |-- Crop: 0,0 -> 2560x1440 (全画面)
  |-- Scaler: 2560x1440 -> 1920x1080
  |-- 输出: 1920x1080 @ 30fps
  v
Encoder Group 0 / Channel 0
  |
  |-- H264/H265 编码
  |-- 输出: 1920x1080 @ 30fps 编码码流
  v
VideoRecorder (MP4 封装)
```

---

## 5. 关于帧率的补充说明

### 5.1 请求帧率 vs 实际帧率

当前 GC4653 硬编码为 30fps，但日志中实际录影帧率约为 **17fps**（夜间模式）。

可能原因：
- **夜间模式曝光限制**：`daynight_switch: detected state=1` 表示当前处于夜间模式，Sensor 自动延长曝光时间以降低噪点，帧率随之下降
- 若强制白天模式（设置 `HTC_FORCE_RECORD_DAY_MODE=1`），帧率可恢复接近 30fps
- Sensor 的 30fps 是物理上限，实际帧率受光照条件、AE 策略、曝光时间综合影响

### 5.2 帧率设置的原则

- 目标录影帧率（`cfg.fps_num`）不应超过 Sensor 输出帧率（30fps）
- 若目标帧率 < Sensor 帧率，FrameSource / Encoder 会自动丢帧实现
- 若目标帧率 > Sensor 帧率，不可能达成，应 fallback 到 Sensor 帧率

---

## 6. 多 Sensor / 多通道扩展

`fillFsAttrForOutput()` 中已统一修正所有 Sensor 的 `outputIndex==1`（子码流通道）：

- `crop.enable = 0` -> `1`
- `crop.width/height` = 对应 Sensor 全画面分辨率
- `picWidth/Height` = 目标子码流分辨率（初始化时）
- `scaler.outwidth/height` = 目标子码流分辨率

当前各 Sensor 子码流初始化分辨率：

| Sensor | 主码流 | 子码流 (Ch1) |
|--------|--------|-------------|
| GC4653 (Sensor 0) | 2560x1440 | 1280x720 |
| GC2063s1 (Sensor 1) | 1920x1080 | 720x576 |
| GC2063s2 (Sensor 2) | 1920x1080 | 720x576 |
| GC2063s3 (Sensor 3) | 1920x1080 | 720x576 |

---

## 7. 关键文件速查

| 文件 | 职责 |
|------|------|
| `src/service/camera/impl/CameraServiceT32.cpp` | 读取录影属性，组装 VideoParams，启动录影 |
| `src/service/camera/CameraPropertyService.cpp` | 解析 Settings / DeviceConfig，提供 `getVideoRecordConfig()` |
| `src/media/video/VideoRecorder.cpp` | 创建视频流，配置 Encoder，MP4 封装 |
| `src/hal/ingenic/IngenicVideo.cpp` | FrameSource / Encoder 的 IMP API 调用，动态分辨率调整 |
| `src/hal/ingenic/sensor-config.h` | Sensor 硬编码参数（分辨率、帧率、I2C、GPIO 等） |

---

## 8. 后续待确认项

- [ ] 到 BSP 中确认 GC4653 的 `template_win_sizes`，了解 Sensor 支持的所有物理 mode（分辨率/帧率组合）
- [ ] 确认当前硬编码的 2560x1440@30fps 是否为 GC4653 的最大能力
- [ ] 评估是否需要在 `buildSupportedVideoModes()` 中根据 Sensor 实际能力过滤不支持的分辨率/帧率组合
