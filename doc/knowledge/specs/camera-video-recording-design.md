# 录影属性规格与T32架构流程

> 记录日期：2026-05-04
> 关联文档：[camera-photo-recording-analysis.md](camera-photo-recording-analysis.md)（架构分析）、[camera-photo-recording-implementation-status.md](camera-photo-recording-implementation-status.md)（实现现状）

## 一、录影属性管道规格

### 参数总览

属性管道在 `CameraParameterRegistry` 中定义了以下录影相关参数：

| 参数 | 属性名 | Group | 类型 | 存储 | 选项/范围 | 默认值 |
|------|--------|-------|------|------|-----------|--------|
| 视频大小 | `Video_Size` | Camera_Setting | STRING | `Settings::videoSize` | 720P/30, 720P/60, 1080P/30, 1080P/60, 2K/30, 4K/30 | 2K/30FPS |
| 视频编码 | `Video_Encoded` | Camera_Setting | NUMBER | `Settings::videoCodec` | 1(H.264), 2(H.265) | 1 |
| 码率类型 | `Video_Bitrate_Type` | Camera_Setting | NUMBER | `Settings::videoRcMode` | 1(CBR), 2(VBR), 3(CVBR), 4(SMART) | 1 |
| 码率值 | `Video_Bitrate_Value` | Camera_Setting | NUMBER | 桶存储(computed) | 100~16000 Kbps, step 100 | 4000 |
| 视频时长 | `Video_Length` | Camera_Setting | NUMBER | split-byte(computed) | 1~600 s, step 1 | 10 |
| 循环录影 | `Cycle` | System_Setting | NUMBER | `Settings::autoCover` | 0/1 | 1 |
| 喇叭音量 | `Audio_SPK_Volume` | Audio_Setting | NUMBER | DeviceConfig | 0~100, step 10 | 50 |
| 录音音量 | `Audio_Record_Volume` | Audio_Setting | NUMBER | `Settings::audioRecordVolume` | 0~100, step 5 | 80 |
| 录音增益 | `Audio_Record_Gain` | Audio_Setting | NUMBER | `Settings::audioRecordGain` | 0~31, step 1 | 28 |
| 影音模式 | `CAM_Mode` | Camera_Setting | NUMBER | `Settings::cameraMode` | 0~5 (6种模式) | 0 |
| 时间戳叠加 | `Stamp` | System_Setting | NUMBER | `Settings::stampEn` | 0/1 | 1 |

### 分辨率+帧率选项

| 选项 string | 分辨率 | 帧率 | 编译宏条件 |
|-------------|--------|------|-----------|
| 720P/30FPS | 1280×720 | 30 | 始终 |
| 720P/60FPS | 1280×720 | 60 | 始终 |
| 1080P/30FPS | 1920×1080 | 30 | 始终 |
| 1080P/60FPS | 1920×1080 | 60 | 始终 |
| 2K/30FPS | 2560×1440 | 30 | MVIDEO≥2 |
| 4K/30FPS | 3840×2160 | 30 | MVIDEO≥4 |

选项列表由 `buildSupportedVideoModes()` 根据 `DeviceConfig::INI_KEY_MVIDEO` 动态构建，另有编译宏 `VIDEO_SIZE_HD_120FPS`、`VIDEO_SIZE_HD_240FPS`、`VIDEO_SIZE_FHD_120FPS` 控制高帧率选项。

### 码率桶存储

码率按分辨率桶分别存储，切换分辨率时自动切换对应桶值：

| 桶 | Settings 字段 | 默认(Mbps) | 默认(Kbps) |
|----|--------------|-----------|------------|
| 720p (≤1280×720) | `bitRate_720p` | 8 | 8192 |
| 1080p/2K (≤2560×1440) | `bitRate_1080p` | 16 | 16384 |
| 4K (≥3840×2160) | `bitRate_4k` | 32 | 32768 |

属性管道暴露的 `Video_Bitrate_Value` 是 computed 属性：读取时返回当前分辨率对应桶的 Kbps 值，写入时根据当前分辨率自动写入对应桶。

### 影音模式 (CAM_Mode)

| 值 | 模式 | 拍照 | 录影 | 实现状态 |
|----|------|------|------|---------|
| 0 | 仅拍照 | ✅ | ❌ | 已实现 guard |
| 1 | 拍照+录影 | ✅ | ✅ | 已实现 guard |
| 2 | 仅录影 | ❌ | ✅ | 已实现 guard |
| 3 | 同时拍照+录影 | ✅ | ✅ | 已实现 guard |
| 4 | PIR 智能触发 | — | — | 未实现 (PIR MCU 函数 stubbed) |
| 5 | 常时录影 | — | — | 未实现 |

---

## 二、T32 录影架构流程

### 分层调用链

```
HTTP POST /api/v1/camera/video/start {channel, duration, audio}
  │
  ▼
Layer 4: http_api_v1.cpp
  │  api_v1_camera_video_start()
  │  调用 CameraServiceT32::startRecord(channel, duration, audio, recordId)
  ▼
Layer 3: CameraServiceT32
  │  ├─ CAM_Mode guard: 检查 cameraMode 是否允许录影
  │  ├─ 生成文件名: /sdcard/DCIM/VID_YYYYMMDD_HHMMSS.mp4
  │  ├─ applyConfiguredVideoParams()
  │  │   └─ CameraPropertyService::getVideoRecordConfig()
  │  │       → width/height/fps: 从 Video_Size → VideoMode 列表
  │  │       → bitrateKbps: 从 bitRate_4k/1080p/720p 桶
  │  │   └─ CameraPropertyService::getVideoRecordCodec()
  │  │       → Settings::videoCodec (1=H.264, 2=H.265)
  │  │   └─ CameraPropertyService::getVideoRecordRcMode()
  │  │       → Settings::videoRcMode (1=CBR, 2=VBR, 3=CVBR, 4=SMART)
  │  ├─ AudioParams (如果 audio=true)
  │  │   → volume/gain: 从 Settings::audioRecordVolume/Gain
  │  │   → codec: AAC (固定)
  │  │   → sampleRate: 16000 (固定)
  │  │   → channels: 1 mono (固定)
  │  ├─ 时长: API duration > Settings videoLength > 默认 30s
  │  ├─ 磁盘检查 + 循环覆盖 (autoCover=1 时删除最旧录影)
  │  └─ VideoRecorder::record(filename, callback, duration)
  ▼
Layer 2: VideoRecorder
  │  ├─ initVideo()
  │  │   └─ IVideoStream::configure(cfg)
  │  │       → payload: VideoParams::codecFormat → H264/H265
  │  │       → rc_mode: VideoParams::rcMode → CBR/VBR/CVBR/SMART
  │  │       → width/height/fps/gop: 来自 VideoParams
  │  ├─ initAudio() (如果 audParam != null)
  │  │   └─ IAudioStream 配置 + audioCaptureLoop 线程
  │  ├─ 主循环 (后台线程)
  │  │   ├─ 轮询 HAL video stream → H.264/H.265 NAL units
  │  │   ├─ 轮询 audio queue → AAC frames
  │  │   └─ minimp4 muxer → fMP4 文件写入
  │  └─ 完成回调 → MetadataDao::addMedia()
  ▼
Layer 1: HAL (IngenicVideo / IngenicAudio)
  │  IMP SDK 调用 (imp_encoder.h, imp_audio.h)
  ▼
Layer 0: IMP SDK (libimp.so)
     H.264/H.265 硬件编码器 + AAC 软件编码
```

### 关键数据流

```
属性管道                     Settings                    startRecord()              VideoRecorder
─────────                    ────────                    ─────────────              ─────────────
Video_Size ──────────────► videoSize (index) ──────► videoModeToSpecString() ──► VideoParams::resolution
                                                       buildSupportedVideoModes()

Video_Encoded ───────────► videoCodec (1/2) ───────► applyConfigured ──────────► VideoParams::codecFormat
                                                       VideoParams()               initVideo()::cfg.payload

Video_Bitrate_Type ──────► videoRcMode (1-4) ──────► applyConfigured ──────────► VideoParams::rcMode
                                                       VideoParams()               initVideo()::cfg.rc_mode

Video_Bitrate_Value ─────► bitRate_4k/1080p/720p ──► currentBitrateKbps ───────► VideoParams::bitrate
                          (computed, 桶存储)           ForMode()

Video_Length ────────────► videoLength_h/l ────────► getVideoRecordLength() ───► record(filename, duration)
                          (computed, split-byte)

Audio_Record_Volume ─────► audioRecordVolume ──────► AudioParams::setVolume()
Audio_Record_Gain ───────► audioRecordGain ────────► AudioParams::setGain()

Cycle / loop_recording ──► autoCover ──────────────► 录影前空间不足时
                                                      自动删除最旧录影文件
                                                        (MetadataDao::getOldestMediaPath)
```

### T32 分辨率枚举

`src/common/Common.h:154-162` 定义了 6 档 VIDEO_SIZE 枚举，`Settings::videoSize` 存储其索引：

| 枚举 | 值 | 说明 |
|------|-----|------|
| VIDEO_SIZE_HD_30FPS | 0 | 1280×720@30 |
| VIDEO_SIZE_HD_60FPS | 1 | 1280×720@60 |
| VIDEO_SIZE_FHD_30FPS | 2 | 1920×1080@30 (默认) |
| VIDEO_SIZE_FHD_60FPS | 3 | 1920×1080@60 |
| VIDEO_SIZE_2K_30FPS | 4 | 2560×1440@30 |
| VIDEO_SIZE_4K2K_30FPS | 5 | 3840×2160@30 |

### 码率控制模式映射

| Video_Bitrate_Type | T32 VideoRcMode | HAL VideoRcMode | IMP SDK |
|--------------------|-----------------|-----------------|---------|
| 1 (CBR) | CBR | CBR | IMP_ENC_RC_MODE_CBR |
| 2 (VBR) | VBR | VBR | IMP_ENC_RC_MODE_VBR |
| 3 (CVBR) | CVBR | CVBR | IMP_ENC_RC_MODE_CVBR |
| 4 (SMART) | SMART | SMART | IMP_ENC_RC_MODE_SMART |

### 编码格式映射

| Video_Encoded | T32 VideoCodecFormat | HAL VideoPayloadType |
|---------------|---------------------|----------------------|
| 1 (H.264) | H264 | H264 |
| 2 (H.265) | H265 | H265 |

---

## 三、HTTP API

### 录影控制端点

| 方法 | 端点 | 说明 |
|------|------|------|
| POST | `/api/v1/camera/video/start` | 开始录影，body: `{channel, duration, audio}` |
| POST | `/api/v1/camera/video/stop` | 停止录影 |
| GET | `/api/v1/camera/video/status` | 获取录影状态 `{status, duration, filepath}` |

### 录影参数端点（属性管道）

| 方法 | 端点 | 说明 |
|------|------|------|
| GET | `/api/v1/camera/properties` | 读取所有录影属性 |
| POST | `/api/v1/camera/properties` | 设置录影属性 `[{id: "...", value: ...}]` |

---

## 四、循环录影机制

录影前执行磁盘空间检查：

```
1. 根据 bitrate × duration / 8 估算所需空间 (+10% overhead + 10MB safety)
2. statvfs("/sdcard") 获取可用空间
3. 如果 space < need:
   a. autoCover == 1: 循环删除最旧录影文件 (MetadataDao::getOldestMediaPath)
      → 每次删除后重新检查空间，最多尝试 50 次
      → 仍不足则拒绝录影
   b. autoCover == 0: 直接拒绝录影，返回错误
```

---

## 五、参数默认值汇总

| 参数 | 默认值 | 来源 |
|------|--------|------|
| 分辨率+帧率 | 1920×1080@30 (FHD_30FPS) | `Settings::videoSize` = VIDEO_SIZE_FHD_30FPS |
| 编码格式 | H.264 | `Settings::videoCodec` = 1 |
| 码率控制 | CBR | `Settings::videoRcMode` = 1 |
| 1080p 码率 | 16384 Kbps (16 Mbps) | `Settings::bitRate_1080p` = 16 |
| 录影时长 | 30 s | `Settings::videoLength_h/l` → 30 |
| 录音音量 | 80 | `Settings::audioRecordVolume` = 80 |
| 录音增益 | 28 | `Settings::audioRecordGain` = 28 |
| Audio codec | AAC | 硬编码 (硬件固定) |
| Audio sample rate | 16000 Hz | 硬编码 (硬件固定) |
| Audio channels | 1 (mono) | 硬编码 (硬件固定) |
| 循环录影 | 开启 | `Settings::autoCover` = 1 |
