# T32 拍照/录影功能实现现状

> 记录日期：2026-05-04
> 关联文档：[camera-photo-recording-analysis.md](camera-photo-recording-analysis.md)（属性管道规格 + 架构分析）

## 文档关系

```
camera-photo-recording-analysis.md          ← 属性管道规格（期望的产品规格）
  ├── 架构分层
  ├── SDK原生能力
  ├── 自研功能（HAL/Media/Service/HTTP）
  ├── workingMode（启动模式，已实现）
  └── CAM_Mode（拍摄模式，6种组合定义）

camera-photo-recording-implementation-status.md  ← T32实现现状（本文档）
  ├── 已实现的拍照功能
  ├── 已实现的录影功能
  ├── 已实现的HTTP API
  └── 差距分析（规格 vs 现状）
```

## 目的

本文档记录T32 DSP端拍照/录影功能的**实际实现情况**，与属性管道规格（CAM_Mode、PIR触发、定时触发等）进行对比，明确差距。

---

## 一、已实现的拍照功能

### 1.1 Media层 - ImageSnap

文件：`src/media/snap/ImageSnap.h/.cpp`

| 功能 | 状态 | 说明 |
|------|------|------|
| 单张拍照 | ✅ | `snap(filename)` — 启动JPEG编码流 → 轮询帧 → 写文件 → 停止 |
| 批量拍照 | ✅ | `snap(filenames)` — 逐帧写入多个文件 |
| 异步拍照 | ✅ | `snap(filename, callback)` — 后台线程执行，回调通知完成 |
| 日夜模式切换 | ✅ | 拍照前自动检测CDS传感器，控制ISP/IR-CUT/IR-LED |
| 分辨率配置 | ✅ | `ImageSnapParams::setImageSize(w, h)`，默认1920x1080 |
| JPEG编码配置 | ✅ | FIXQP模式，quality=40，15fps |
| 保存到数据库 | ❌ | `snap_internal()` 中数据库写入代码被 `#if 0` 禁用 |

### 1.2 Service层 - CameraServiceT32 拍照部分

文件：`src/service/camera/impl/CameraServiceT32.cpp`

| 功能 | 状态 | 说明 |
|------|------|------|
| `takePhoto()` | ✅ | 生成 `/sdcard/DCIM/IMG_<时间戳>.jpg`，委托 `ImageSnap::snap()` |
| `capturePreviewFrame()` | ✅ | 捕获JPEG到临时文件，读入内存buffer，删除临时文件 |
| `startTimerPhoto()` | ✅ | 后台线程按interval循环调用 `takePhoto()`，支持totalCount限制 |
| `stopTimerPhoto()` | ✅ | 条件变量通知停止，join线程 |
| `getPhotoStatus()` | ✅ | 基于 `is_capturing_` 原子标志 |
| `getTimerPhotoStatus()` | ✅ | 返回running/interval/completedCount |
| `startBurstPhoto()` | ❌ | **返回-1，未实现** |

### 1.3 启动拍照 - quick_snap

文件：`src/app/media_app.cpp:107`

| 功能 | 状态 | 说明 |
|------|------|------|
| 启动时快速拍照 | ✅ | `SNAP_ONLY`/`SNAP_UPLOAD` 模式下调用 |
| 读取配置 | ✅ | `Settings::burstNumber`（连拍数）、`Settings::stillSize`（分辨率索引） |
| 文件保存 | ✅ | `/tmp/quick_snap/<时间戳>/<时间戳>_N.JPG` |
| 元数据 | ✅ | 生成 `info.json` 记录文件列表和目录 |

---

## 二、已实现的录影功能

### 2.1 Media层 - VideoRecorder

文件：`src/media/video/VideoRecorder.h/.cpp`

| 功能 | 状态 | 说明 |
|------|------|------|
| H.264/H.265编码录影 | ✅ | 可从属性管道 `Video_Encoded` 切换编码格式 |
| 码率控制模式可选 | ✅ | CBR/VBR/CVBR/SMART，通过 `Video_Bitrate_Type` 属性切换 |
| MP4封装 | ✅ | 使用 minimp4 库，fMP4格式 |
| 可选音频 | ✅ | AAC 或 PCM16，独立线程采集 |
| 音频同步 | ✅ | 线程安全队列 + 条件变量，音频线程推送、视频循环消费 |
| AAC ADTS解析 | ✅ | 自动解析采样率/声道数，构建AudioSpecificConfig |
| 定时录影 | ✅ | `record(filename, duration)` — 按帧数计时自动停止 |
| 异步录影 | ✅ | `record(filename, callback, duration)` — 后台线程，回调通知 |
| 手动停止 | ✅ | `stopRecorder()` — 设置 `stopRecording=true` 标志 |
| 保存到数据库 | ✅ | 录影完成后写入 MetadataDao（与ImageSnap不同，这里已启用） |
| 日夜模式切换 | ✅ | 录影前自动切换，录影结束后恢复DAY模式 |
| 音频预卷 | ✅ | 等待首个音频时间戳对齐音视频起点 |

### 2.2 Service层 - CameraServiceT32 录影部分

文件：`src/service/camera/impl/CameraServiceT32.cpp`

| 功能 | 状态 | 说明 |
|------|------|------|
| `startRecord()` | ✅ | 完整参数流程：生成文件名、CAM_Mode guard、读取分辨率/帧率/码率/编码/码率模式、创建 AudioParams（音量/增益从 Settings 读取）、时长默认值从 Settings 读取、磁盘空间检查+循环覆盖 |
| `stopRecord()` | ✅ | 调用 `video_recorder_->stopRecorder()` |
| `getRecordStatus()` | ✅ | 基于 `is_recording_` 原子标志，返回state/duration/filePath |
| CAM_Mode guard | ✅ | mode 0 拒绝录影，mode 2 拒绝拍照 |
| 循环录影 | ✅ | autoCover=1 且空间不足时，自动删除最旧录影文件腾空间 |

### 2.3 录影参数来源

`CameraServiceT32::startRecord()` 通过 `applyConfiguredVideoParams()` 读取参数：

```
CameraPropertyService::getVideoRecordConfig(width, height, fps, bitrateKbps)
  → 默认值: 1920x1080, 30fps, 16384kbps (1080p 桶)
  → 实际值: 从 Settings::videoSize → buildSupportedVideoModes() → VideoMode 列表映射

CameraPropertyService::getVideoRecordCodec()
  → Settings::videoCodec (1=H.264, 2=H.265)
  → 映射为 VideoCodecFormat enum → VideoRecorder::initVideo() cfg.payload

CameraPropertyService::getVideoRecordRcMode()
  → Settings::videoRcMode (1=CBR, 2=VBR, 3=CVBR, 4=SMART)
  → 映射为 VideoRcMode enum → VideoRecorder::initVideo() cfg.rc_mode

CameraPropertyService::getVideoRecordLength()
  → Settings::videoLength_h/l → int (秒)
  → API 未指定 duration 时作为默认值
```

音频参数（当 `audio=true`）：
- 设备类型：AUDIO_IN（固定）
- 编码：AAC（固定，硬件约束）
- 采样率：16000Hz（固定，硬件约束）
- 声道：单声道（固定，硬件约束）
- 音量：`Settings::audioRecordVolume`（默认 80）
- 增益：`Settings::audioRecordGain`（默认 28）

---

## 三、已实现的HTTP API

文件：`src/service/http_server/http_api_v1.cpp`

| 端点 | 方法 | 状态 | 说明 |
|------|------|------|------|
| `/api/v1/camera/photo` | POST | ✅ | 同步/异步拍照，支持channel/format/quality/response_mode |
| `/api/v1/camera/photo/status` | GET | ✅ | 按job_id或latest查询，也返回实时PhotoStatus |
| `/api/v1/camera/photo/timer` | POST | ✅ | action=start/stop，支持interval/count/channel |
| `/api/v1/camera/photo/burst` | POST | ⚠️ | API存在，但底层 `startBurstPhoto()` 返回-1 |
| `/api/v1/camera/video/start` | POST | ✅ | channel/duration/audio参数 |
| `/api/v1/camera/video/stop` | POST | ✅ | |
| `/api/v1/camera/video/status` | GET | ✅ | |
| `/api/v1/camera/video/list` | GET | ✅ | 从MetadataDao查询视频列表 |
| `/api/v1/camera/video/playback` | GET | ✅ | 视频文件流式回放（支持Range） |
| `/api/v1/camera/preview` | GET | ✅ | JPEG单帧或MJPEG流 |
| `/api/v1/camera/thumbnail` | GET | ✅ | 按文件路径或实时捕获 |
| `/api/v1/camera/photos` | GET | ✅ | 照片列表 |
| `/api/v1/camera/files/download` | GET | ✅ | 文件下载 |
| `/api/v1/camera/files/delete` | POST | ✅ | 文件删除 |
| `/api/v1/camera/properties` | GET/POST | ✅ | 参数读写 |
| `/api/v1/camera/status` | GET | ✅ | 相机状态汇总 |
| `/api/v1/camera/presets` | GET/POST | ✅ | 预设切换 |

### 异步拍照流程

```
POST /api/v1/camera/photo { response_mode: "async" }
  → PhotoJobManager::submit() → 返回 job_id
  → 客户端轮询 GET /api/v1/camera/photo/status?job_id=X
  → 或订阅 TCP事件（端口5000）：camera.photo.completed / camera.photo.failed
```

---

## 四、已实现的RTSP实时流

文件：`src/media/rtsp/RtspServer.h/.cpp`

- 端口554
- 使用 `MediaSession` 管理生产者线程和FIFO
- `VideoSource` 封装 IMP_Encoder 逻辑
- `AudioSource` 音频流

---

## 五、属性管道规格 vs T32实现 差距分析

### 5.1 有差距的功能

| 功能 | 属性管道规格 | T32现状 | 差距描述 |
|------|------------|---------|---------|
| 连拍(burst) | burstNumber=1~10 | `startBurstPhoto()` 返回-1 | API未实现 |
| PIR触发(mode 4) | pirEn + ckPirSensitivity | PIR MCU函数全部stubbed | DSP端PIR事件循环无法工作 |
| 常时录影(mode 5) | Video_ATR_EN工厂开关 | 仅定义，默认0 | 无代码读取此开关 |
| 定时触发(按cameraMode) | timerEn + timerLapse + 时间段 | 无按cameraMode的定时触发 | 有startTimerPhoto但不读cameraMode |
| 拍照存DB | 媒体数据库管理 | `#if 0` 禁用 | ImageSnap不写MetadataDao |
| 工作模式切换 | `POST /api/v1/system/workmode` | stub | `SystemServiceT32::setWorkMode()` 仅日志 |
| 时间戳叠加 | Stamp_EN / videoStamp | 仅属性写读，无渲染逻辑 | 需确认IMP SDK是否有OSD接口 |

### 5.2 无差距的功能

| 功能 | 状态 |
|------|------|
| 单张拍照 | ✅ 完整实现 |
| 定时拍照（HTTP触发） | ✅ 完整实现 |
| H.264/H.265录影 + MP4封装 | ✅ 完整实现（编码格式可通过属性切换） |
| 码率控制模式(CBR/VBR/CVBR/SMART) | ✅ 完整实现（可通过 Video_Bitrate_Type 切换） |
| 音频采集（AAC/PCM） | ✅ 完整实现 |
| 录影存DB | ✅ 完整实现 |
| 实时预览（JPEG/MJPEG） | ✅ 完整实现 |
| RTSP流 | ✅ 完整实现 |
| 视频回放 | ✅ 完整实现 |
| 参数管理（读写/持久化） | ✅ 完整实现 |
| 异步拍照（HTTP触发） | ✅ 完整实现 |
| 定时拍照（Service层） | ✅ 完整实现 |
| 日夜模式切换 | ✅ 完整实现 |
| CAM_Mode 分发(mode 0-3) | ✅ 拍照/录影操作级 guard 已实现 |
| 录影参数完整对齐 | ✅ Video_Encoded/Video_Bitrate_Type/Video_Bitrate_Value/Video_Length/Audio_Record_Volume/Gain 全部连通 |
| 循环录影(autoCover) | ✅ 录影前磁盘检查 + 自动删除最旧文件腾空间 |

### 5.3 差距的本质

**单个拍照/录影操作已完整实现，录影参数已与属性管道全面对齐**。

仍缺少的：
- mode 4/5 的自动化触发（PIR MCU 函数 stubbed，全时录影无代码）
- "触发→读取模式→组合执行"编排层（PIR/定时器触发后按 cameraMode 自动执行拍照/录影组合）

```
已实现（操作级 guard）：
  HTTP API → startRecord()
    → 读取 Settings::cameraMode
    → mode==0 → 拒绝录影（仅拍照模式）
    → 其他 mode → 继续执行

  HTTP API → takePhoto()
    → 读取 Settings::cameraMode
    → mode==2 → 拒绝拍照（仅录影模式）
    → 其他 mode → 继续执行

未实现（自动化触发层）：
  PIR传感器触发 / 定时器触发
    → 读取 Settings::cameraMode
    → switch(cameraMode):
        case 0: takePhoto()                    // 仅拍照
        case 1: takePhoto() → startRecord()    // 拍照+录像
        case 2: startRecord()                  // 仅录像
        case 3: takePhoto() + startRecord()    // 同步拍录
        case 4: startRecord() (运动检测)        // 智感录像（PIR不可用）
        case 5: startRecord() (持续)            // 全时录像（未实现）
```

---

## 六、录影功能属性管道对齐现状

> 记录日期：2026-05-04
> 关联文档：[camera-video-recording-design.md](camera-video-recording-design.md)（录影属性规格与架构流程）

### 6.1 录影属性对齐状态

| 属性 | 存储 | 对齐状态 | 说明 |
|------|------|---------|------|
| Video_Size | Settings::videoSize | ✅ 完整对齐 | 6档分辨率+帧率，通过 getVideoRecordConfig() 读取 |
| Video_Encoded | Settings::videoCodec | ✅ 完整对齐 | 1=H.264, 2=H.265 → VideoRecorder::initVideo() cfg.payload |
| Video_Bitrate_Type | Settings::videoRcMode | ✅ 完整对齐 | 1=CBR, 2=VBR, 3=CVBR, 4=SMART → cfg.rc_mode |
| Video_Bitrate_Value | bitRate_4k/1080p/720p (桶) | ✅ 完整对齐 | 按分辨率桶读写，computed 属性 |
| Video_Length | videoLength_h/l (split-byte) | ✅ 完整对齐 | API duration=0 时作为默认值 |
| Cycle (循环录影) | Settings::autoCover | ✅ 完整对齐 | 录影前磁盘检查+删除最旧文件腾空间 |
| Audio_Record_Volume | Settings::audioRecordVolume | ✅ 完整对齐 | 默认值映射 |
| Audio_Record_Gain | Settings::audioRecordGain | ✅ 完整对齐 | 默认值映射 |
| Audio_SPK_Volume | DeviceConfig | ✅ 原有对齐 | 喇叭音量（非录影专属） |
| CAM_Mode | Settings::cameraMode | ⚠️ 部分对齐 | 操作级 guard 已实现（见下表），自动化未实现 |
| Stamp (时间戳) | Settings::stampEn | ❌ 未实现 | 属性可读写，无渲染逻辑 |

### 6.2 CAM_Mode 对齐详情

| Mode | 名称 | 操作级 guard | 自动化触发 | 说明 |
|------|------|-------------|-----------|------|
| 0 | 仅拍照 | ✅ takePhoto 放行，startRecord 拒绝 | ❌ | 手动 API 调用已对齐 |
| 1 | 拍照+录影 | ✅ 两者均放行 | ❌ | 组合执行未实现 |
| 2 | 仅录影 | ✅ startRecord 放行，takePhoto 拒绝 | ❌ | 手动 API 调用已对齐 |
| 3 | 同时拍照+录影 | ✅ 两者均放行 | ❌ | 同步执行未实现 |
| 4 | PIR 智能触发 | — | ❌ | PIR MCU 函数全部 stubbed (`return 0`)，硬件层不通 |
| 5 | 常时录影 | — | ❌ | Video_ATR_EN 是死配置，无代码读取 |

### 6.3 已对齐：手动 API 调用流程

```
POST /api/v1/camera/video/start {channel, duration, audio}
  → CameraServiceT32::startRecord()
    ├─ CAM_Mode guard (mode 0 → 拒绝)
    ├─ 参数读取
    │   ├─ Video_Size → width/height/fps ← ✅
    │   ├─ Video_Encoded → codec (H.264/H.265) ← ✅
    │   ├─ Video_Bitrate_Type → rcMode (CBR/VBR/CVBR/SMART) ← ✅
    │   ├─ Video_Bitrate_Value → bitrate (桶存储) ← ✅
    │   ├─ Video_Length → duration (API未指定时) ← ✅
    │   ├─ Audio_Record_Volume/Gain → audio params ← ✅
    │   └─ Cycle/autoCover → 磁盘检查+循环覆盖 ← ✅
    ├─ 磁盘空间检查
    │   ├─ 估算所需空间 (bitrate × duration / 8)
    │   ├─ statvfs("/sdcard") 获取可用空间
    │   └─ autoCover=1 → 自动删除最旧录影文件腾空间
    └─ VideoRecorder::record()
        ├─ initVideo(): payload/rcMode 从 VideoParams 读取
        ├─ initAudio(): volume/gain 从 AudioParams 读取
        └─ minimp4 muxer → fMP4 文件
```

### 6.4 仍未对齐

| 缺口 | 原因 | 影响 |
|------|------|------|
| 自动化编排层 | 无 "触发→读 cameraMode→执行组合" 的 switch/case | 无法通过 PIR/定时器自动触发拍照+录影组合 |
| PIR 触发 (mode 4) | MCU::readRMType/readEventType/readEventNum 全部 stubbed | PIR 传感器数据不可读取 |
| 常时录影 (mode 5) | Video_ATR_EN 工厂开关无代码消费 | 无法实现开机即录 |
| 时间戳叠加 | stampEn 可读写但无视频帧渲染逻辑 | 录影文件无时间戳水印 |
| Video_Seamless | 属性存在但无代码读取 | 无缝录影功能为空 |
| Video_Quality | 属性存在但无代码读取 | JPEG质量参数不用于录影 |

---

## 七、关键文件索引

| 文件 | 实现的功能 |
|------|-----------|
| `src/media/snap/ImageSnap.h/.cpp` | 单拍、批量拍、异步拍、日夜切换 |
| `src/media/video/VideoRecorder.h/.cpp` | H.264录影、MP4封装、音频同步、定时录影 |
| `src/service/camera/impl/CameraServiceT32.cpp` | takePhoto、startRecord、stopRecord、timerPhoto、previewFrame |
| `src/service/http_server/http_api_v1.cpp` | 所有HTTP API端点 |
| `src/service/http_server/PhotoJobManager.h` | 异步拍照任务队列 |
| `src/service/camera/CameraPropertyService.h` | 参数管理、getVideoRecordConfig() |
| `src/app/media_app.cpp` | quick_snap() 启动拍照 |
| `src/app/main_app.cpp` | workingMode→命令映射（已实现） |
| `src/media/rtsp/RtspServer.h/.cpp` | RTSP实时流 |
| `src/storage/MetadataDao.h` | 媒体数据库（VideoRecorder写入，ImageSnap未写入） |

---

## 八、C2 重构计划：CameraRecorder 统一录影

> 设计日期：2026-06-07
> 状态：方案已锁，进入实现阶段
> 关联 ADR：[`decisions/asymmetric-snap-vs-record-design.md`](../decisions/asymmetric-snap-vs-record-design.md)
> 关联 Spec：[`specs/camera-recorder-unified-design.md`](camera-recorder-unified-design.md)

### 8.1 现状与动机

当前录影"消费者"分散在两处独立实现，参数管道不一致：

| 调用方 | 位置 | 视频参数来源 | 缩略图 | 后置动作 |
|--------|------|-------------|--------|---------|
| `processCmdVideoRecord` (work mode) | `src/app/main_app.cpp:617` | **硬编码** 2560×1440/30fps/4000Kbps/CBR | 无 | 写 desc JSON |
| `CameraServiceT32::startRecord` (HTTP) | `src/service/camera/impl/CameraServiceT32.cpp:407` | `applyConfiguredVideoParams()` 读 config | CH2 in-stream | 写 `MetadataDao` |

work mode 那条路径关键参数（分辨率/帧率/码率/RC 模式）全部硬编码，与 HTTP 路径参数管道不一致——用户改 `settings.json` 里的 `Video_Size` / `Video_Bitrate_Type` 不影响 work mode 录影。

### 8.2 目标

引入通用 `CameraRecorder` 类，把"录影"做成一份统一实现，由 work mode 和 HTTP 共用。

| 目标 | 度量 |
|------|------|
| work mode 录影参数从 config 读 | 移除 main_app.cpp:624-629 的 4 行硬编码 |
| work mode 缩略图可生成 | `RecordOptions` 控制是否生成 |
| HTTP / work mode 共用同一录影管道 | `CameraRecorder::record()` 一份代码 |
| 录完写 desc/DB 通过回调 | 写后置文件由 `RecordingPostProcess` 工具类负责 |

### 8.3 设计要点

**类与契约**（详见 [`specs/camera-recorder-unified-design.md`](camera-recorder-unified-design.md)）：

```cpp
// src/service/camera/CameraRecorder.h
class CameraRecorder {
public:
    CameraRecorder();  // 内部读 CameraPropertyService + Settings
    bool record(const std::string& filePath, int durationSec, 
                const RecordOptions& options);  // 异步，立即返回
    bool stop();  // 异步收尾，触发 onComplete(stoppedManually=true)
    int64_t getCurrentDurationMs() const;
};

struct RecordOptions {
    bool autoCover = false;  // 不传时读 Settings::autoCover
    std::function<void(const RecordResult&)> onComplete;
};

struct RecordResult {
    RecordError error = RecordError::None;
    std::string filePath;
    std::string thumbnailPath;  // 规则: <video dir>/thumb/<basename>.jpg
    int64_t durationMs = 0;
    int64_t fileSizeBytes = 0;
    bool stoppedManually = false;
    std::string errorMessage;
};

enum class RecordError {
    None, InsufficientDiskSpace, EncoderInitFailed, 
    RecordStartFailed, InternalError, UserStop
};
```

**`VideoRecorder` 角色不变** —— 仍是 MP4 muxer / 音频混流 / 缩略图抓取（CH2 in-stream）。`CameraRecorder` 包裹 `VideoRecorder`，加策略层（读 config / 磁盘检查 / autoCover / 回调 / 缩略图路径生成）。

**`RecordingPostProcess` 工具类** —— `src/service/camera/RecordingPostProcess.h/.cpp`，暴露两个静态方法：
- `writeWorkModeDescJson(const RecordResult&)` —— work mode 写 desc JSON
- `writeMetadataDaoEntry(const RecordResult&)` —— HTTP 写 `MetadataDao`

### 8.4 改动清单

| 文件 | 改动类型 | 内容 |
|------|---------|------|
| `src/service/camera/CameraRecorder.h` | 新增 | 类/结构体/枚举声明（**位置调整**：从 `media/video/` 移到 `service/camera/`，避免循环依赖） |
| `src/service/camera/CameraRecorder.cpp` | 新增 | 实现：读 config / 磁盘检查 / 录影编排 / 回调 / stop |
| `src/service/camera/RecordingPostProcess.h` | 新增 | 工具类声明 |
| `src/service/camera/RecordingPostProcess.cpp` | 新增 | 工具类实现 |
| `src/media/video/VideoRecorder.cpp` | 改 | `captureThumbnail()` 高度按 video 宽高比算（320 × videoH/videoW） |
| `src/service/camera/impl/CameraServiceT32.cpp` | 改 | `startRecord` / `stopRecord` / `getRecordStatus` 委托给 `CameraRecorder`；callback 调 `RecordingPostProcess::writeMetadataDaoEntry` |
| `src/app/main_app.cpp` | 改 | `processCmdVideoRecord` 用 `CameraRecorder` + `std::promise/future` + `RecordingPostProcess::writeWorkModeDescJson` |
| `src/service/camera/CMakeLists.txt` | 不改 | GLOB 自动收录新文件 |

### 8.5 范围限定（不重构）

| 不动 | 原因 |
|------|------|
| `processCmdConcurrentSnapRecord` (cameraMode=3 边录边拍) | 与 work mode 一次性语义解耦度高，是另一故事 |
| `-vr` / `--video-record` 测试入口 (`CMD_VIDEO_RECORD` 分支) | 硬编码参数是测试隔离需要，不该被统一实现吞掉 |
| RTSP 流 | 不落盘，不属于"录影文件"概念 |
| `camera/thumbnail` 独立 HTTP 端点 | 走 `ImageSnap`，独立功能 |
| desc JSON schema | 维持现状（不增删字段），是上传协议契约 |

### 8.6 验证方案

- 编译验证：`cmake -DBUILD_FOR_SIMULATION=ON -B build_sim -S . && cmake --build build_sim -j$(nproc)`
- work mode 启动时，验证录影分辨率/帧率/码率来自 `settings.json`（不是硬编码 2560×1440/30/4000）
- work mode 录完，desc JSON 格式与重构前一致（仅文件列表，不增字段）
- HTTP `POST /api/v1/camera/video/start`，DB 中有 `MediaItem` 记录
- HTTP `POST /api/v1/camera/video/stop`，MP4 文件 moov 完整，DB `duration` 字段是实际时长
- 缩略图生成：HTTP 路径下 `dao.getThumbnail(filePath)` 能拿到 320×宽高比高度 的 JPEG
