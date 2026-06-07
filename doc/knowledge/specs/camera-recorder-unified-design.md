# CameraRecorder 统一录影设计规格

> 设计日期：2026-06-07
> 状态：方案已锁，进入实现阶段
> 关联：[`decisions/asymmetric-snap-vs-record-design.md`](../decisions/asymmetric-snap-vs-record-design.md)、[`camera-photo-recording-implementation-status.md` 八节](camera-photo-recording-implementation-status.md)

## 1. 目的

定义通用录影组件 `CameraRecorder` 的 API 契约、内部数据流、与现有代码的集成方式。

适用范围：work mode（一次性录影）和 HTTP API（异步可停止录影）共用同一份录影实现。

## 2. 核心抽象

### 2.1 类层次

```
Caller (work mode / HTTP)
    │
    │  RecordOptions { autoCover, onComplete }
    │  record(filePath, durationSec, options)
    ▼
┌─────────────────────────────────────┐
│  CameraRecorder (策略/编排层)        │
│  ─────────────────────────          │
│  + 读 CameraPropertyService 配置     │
│  + 构建 VideoParams + AudioParams   │
│  + 磁盘检查 + autoCover 循环         │
│  + 包裹 VideoRecorder               │
│  + 异步 + callback                  │
│  + stop() 支持                      │
│  + getCurrentDurationMs() 状态查询   │
└──────────────┬──────────────────────┘
               │
               ▼
┌─────────────────────────────────────┐
│  VideoRecorder (MP4 muxer / 编码)   │   ← 现有类，职责不变
│  ─────────────────────────          │
│  + H.264/H.265 编码                 │
│  + 音频采集 + 混流                   │
│  + minimp4 封装 fMP4                │
│  + 缩略图抓取 (CH2 in-stream)        │
│  + saveToDatabase 写 DB             │
└─────────────────────────────────────┘
               │
               ▼
       HAL (IVideo / IAudio)
               │
               ▼
       Ingenic IMP SDK
```

### 2.2 角色分工

| 组件 | 职责 | 不该做什么 |
|------|------|----------|
| `CameraRecorder` | 读 config、磁盘检查、autoCover 策略、回调分发、stop 控制、状态查询 | 不直接调用 SDK；不复用 muxer 代码 |
| `VideoRecorder` | MP4 编码/封装、音频混流、缩略图抓取、DB 写入 | 不读 config（保持单一职责） |
| `RecordingPostProcess` | 把 JSON 字符串落盘为 desc 文件（仅 file write） | 不参与录影流程、不生成 JSON 内容 |
| `CameraPropertyService` | 暴露 config getter | 不参与录影流程 |
| `Settings` | 持久化、autoCover 默认值来源 | — |

## 3. 公共 API

### 3.1 头文件位置

`src/service/camera/CameraRecorder.h`

> 位置说明：原本规划在 `src/media/video/`，但实现时发现 `media_recorder` 库没有 `setting` / `camera_service` 依赖（且 `camera_service` 反向依赖 `media_recorder` 会形成循环）。`CameraRecorder` 是服务层编排器，需要读 `CameraPropertyService` 和 `Settings`，因此放在 `src/service/camera/`，由 `camera_service` 静态库统一构建。

### 3.2 类声明

```cpp
namespace media {

enum class RecordError {
    None = 0,
    InsufficientDiskSpace,    // 磁盘不足，autoCover=0 或覆盖后仍不足
    EncoderInitFailed,         // VideoRecorder init 失败
    RecordStartFailed,         // record() 启动失败
    InternalError,             // 内部异常（lock/cv 失败等）
    UserStop                   // 用户主动 stop()（非失败）
};

struct RecordOptions {
    bool autoCover = false;  // 不传时 CameraRecorder 读 Settings::autoCover
    std::function<void(const RecordResult&)> onComplete;  // 必传
};

struct RecordResult {
    RecordError error = RecordError::None;
    std::string filePath;
    std::string thumbnailPath;    // CH2 in-stream 抓取，失败时为空
    int64_t durationMs = 0;       // 实际录了多久（毫秒）
    int64_t fileSizeBytes = 0;    // 录影文件大小
    bool stoppedManually = false; // stop() 触发时为 true
    std::string errorMessage;     // error != None 时填写
};

class CameraRecorder {
public:
    CameraRecorder();
    ~CameraRecorder();

    // 异步录影：返回 true 表示已成功启动；录完后通过 onComplete 回调通知
    // durationSec <= 0 时降级到 CameraPropertyService::getVideoRecordLength()
    bool record(const std::string& filePath, 
                int durationSec,
                const RecordOptions& options);

    // 异步停止：返回 true 表示 stop 信号已发出；MP4 收尾在后台完成
    // 收尾完成后 onComplete 触发，stoppedManually=true
    // 录制未启动时为 no-op + 警告日志
    bool stop();

    // 当前录了多久（毫秒）—— HTTP GET /video/status 调
    // 未录制时返回 0
    int64_t getCurrentDurationMs() const;

private:
    std::shared_ptr<VideoRecorder> video_recorder_;
    std::atomic<bool> is_recording_{false};
    std::atomic<bool> stop_requested_{false};
    std::atomic<int64_t> current_duration_ms_{0};
    std::chrono::steady_clock::time_point start_time_;
};

} // namespace media
```

### 3.3 关键不变量

1. **`record()` 返回 false 时 `onComplete` 不会触发**——失败是同步的
2. **`record()` 启动后 `stop()` 必须能正常工作**——支持手动结束
3. **`onComplete` 在后台录影线程触发**——caller 不应在 callback 内做阻塞操作
4. **同时只能有一份 `record()` 处于活跃态**——`record()` 第二次调用直接返回 false 并打 WARN
5. **`RecordResult.filePath` 与 caller 传入的 `filePath` 一致**——便于 caller 在 callback 内识别

## 4. 内部数据流

### 4.1 record() 启动流程

```
CameraRecorder::record(filePath, durationSec, options)
  │
  ├─ [前置] 状态检查
  │   if (is_recording_) { WARN; return false; }
  │
  ├─ [读 config] CameraPropertyService + Settings
  │   - width, height, fps, bitrateKbps ← getVideoRecordConfig()
  │   - codec ← getVideoRecordCodec() (1=H264, 2=H265)
  │   - rcMode ← getVideoRecordRcMode() (1=CBR, 2=VBR, 3=CVBR, 4=SMART)
  │   - duration (caller > 0 ? caller : getVideoRecordLength())
  │   - audio (默认 true，从 caller 决定；当前 Caller 都传默认)
  │   - audio volume/gain ← Settings::audioRecordVolume/Gain
  │   - autoCover (options.autoCover 显式 ? options.autoCover : Settings::autoCover != 0)
  │
  ├─ [构建参数] VideoParams + AudioParams
  │   videoParam->setResolution(w, h)
  │   videoParam->setFrameRate(fps)
  │   videoParam->setBitrate(bitrateKbps)
  │   videoParam->setCodecFormat(codec == 2 ? H265 : H264)
  │   videoParam->setRcMode(rcMode)
  │   videoParam->setGop(60)  // 硬编码
  │   audioParam->setVolume(Settings::audioRecordVolume)
  │   audioParam->setGain(Settings::audioRecordGain)
  │   // 音频 codec/sample rate/channels/device 硬编码（AAC/16kHz/mono/AUDIO_IN）
  │
  ├─ [磁盘检查] 同 CameraServiceT32 现状
  │   estimatedMB = bitrateKbps * 1000 * duration / 8 / (1024*1024) * 11/10 + 10
  │   statvfs("/mnt/sdcard")
  │   if (freeMB < estimatedMB):
  │     if (autoCover): 循环删除最旧 video (MetadataDao::getOldestMediaPath(2))
  │                     每次删完 statvfs 一次，最多 50 次
  │     else: return false (RecordError::InsufficientDiskSpace)
  │
  ├─ [启动录影]
  │   video_recorder_ = std::make_shared<VideoRecorder>(videoParam, audioParam)
  │   RecordResult result;
  │   result.filePath = filePath;
  │   std::string thumbPath = computeThumbnailPath(filePath);
  │       // 规则: <video dir>/thumb/<basename>.jpg
  │       // 例: /sdcard/DCIM/VID_xxx.mp4 → /sdcard/DCIM/thumb/VID_xxx.jpg
  │
  │   is_recording_ = true;
  │   start_time_ = now();
  │   current_duration_ms_ = 0;
  │
  │   bool ok = video_recorder_->record(filePath, 
  │       [this, options, filePath, thumbPath](bool /*success*/) {
  │           // [录完回调] 在 VideoRecorder 后台线程触发
  │           RecordResult result;
  │           result.filePath = filePath;
  │           result.thumbnailPath = thumbPath;
  │           result.durationMs = (now() - start_time_).count();
  │           result.fileSizeBytes = file_size(filePath);
  │           result.stoppedManually = stop_requested_.load();
  │           result.error = result.stoppedManually ? RecordError::UserStop : RecordError::None;
  │           result.errorMessage = ...;
  │           is_recording_ = false;
  │           options.onComplete(result);
  │       }, duration);
  │
  │   if (!ok) {
  │       is_recording_ = false;
  │       return false;
  │   }
  │   return true;
```

### 4.2 持续时长追踪

后台线程在 VideoRecorder 录影主循环中，每写入一帧（每 ~33ms @ 30fps）调一次 `updateCurrentDuration()`：

```cpp
void CameraRecorder::updateCurrentDuration() {
    auto now = std::chrono::steady_clock::now();
    current_duration_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_).count();
}
```

`getCurrentDurationMs()` 读 atomic 返回——O(1) 无锁。

### 4.3 stop() 流程

```
CameraRecorder::stop()
  │
  ├─ [前置] 状态检查
  │   if (!is_recording_) { WARN "stop() called when not recording"; return false; }
  │
  ├─ [发停止信号]
  │   stop_requested_ = true;
  │   if (video_recorder_) {
  │       video_recorder_->stopRecorder();  // 设 stopRecording_=true，主循环看到后做收尾
  │   }
  │   return true;  // 不等收尾完成
```

主循环收尾（写 moov atom、flush muxer）完成后，触发 record() 时传入的 onComplete，stoppedManually=true，error=UserStop。

## 5. 缩略图机制

### 5.1 抓取时点

`VideoRecorder::record()` 启动时自动调 `captureThumbnail()`（**已有逻辑，不变**），结果存到 `VideoRecorder::thumbData_`（`std::vector<uint8_t>`）。

### 5.2 尺寸计算

修改 `VideoRecorder::captureThumbnail()`：当前硬编码 320x180，**改为按 video 宽高比计算高度**：

```cpp
int videoW, videoH;
videoParams_->getResolution(videoW, videoH);
int thumbW = 320;
int thumbH = (thumbW * videoH) / videoW;  // 比例缩放
if (thumbH % 2 != 0) thumbH += 1;          // 偶数对齐硬件 scaler
cfg.width = thumbW;
cfg.height = thumbH;
```

不同 video 分辨率下的缩略图尺寸：

| Video 分辨率 | 缩略图尺寸 |
|------------|----------|
| 1280×720 (16:9) | 320×180 |
| 1920×1080 (16:9) | 320×180 |
| 2560×1440 (16:9) | 320×180 |
| 3840×2160 (16:9) | 320×180 |
| 1024×768 (4:3) | 320×240 |
| 720×720 (1:1) | 320×320 |

### 5.3 路径规则

`CameraRecorder` 自动从 video 路径生成 thumbnail 路径：

```cpp
// 规则: <video dir>/thumb/<basename>.jpg
// 例: /sdcard/DCIM/VID_20260607_101010.mp4 
//   → /sdcard/DCIM/thumb/VID_20260607_101010.jpg
```

**注意：缩略图文件名去后缀，存为 .jpg**——CH2 in-stream 抓的是 JPEG，不存在扩展名转换问题。

## 6. 磁盘检查与 autoCover

完全复用 `CameraServiceT32::startRecord` 现状的检查逻辑（line 504-551），迁入 `CameraRecorder::record()` 内部。`autoCover` 标志来源：

- `RecordOptions.autoCover` 显式传值：true/false
- `RecordOptions.autoCover` 不显式传：读 `Settings::autoCover != 0`（默认 false）

详见 [`camera-photo-recording-analysis.md` § 6 循环录影机制](camera-photo-recording-analysis.md)。

## 7. 错误处理矩阵

| 失败场景 | RecordError | 同步/异步 | is_recording_ |
|---------|------------|----------|--------------|
| 重复 record() | — | 同步：WARN + return false | 保持当前值 |
| 磁盘空间不足（autoCover=0）| InsufficientDiskSpace | 同步：return false | 保持 false |
| 磁盘空间不足（autoCover=1 仍不足）| InsufficientDiskSpace | 同步：return false | 保持 false |
| VideoRecorder init 失败 | EncoderInitFailed | 同步：return false | 保持 false |
| VideoRecorder record() 启动失败 | RecordStartFailed | 同步：return false | 设 false |
| 录影中编码器异常 | InternalError | 异步：onComplete 触发，error=InternalError | 设 false |
| 录影中 stop() | UserStop | 异步：onComplete 触发，stoppedManually=true | 设 false |
| 录完自然结束 | None | 异步：onComplete 触发，stoppedManually=false | 设 false |

## 8. RecordingPostProcess 工具类

`src/service/camera/RecordingPostProcess.h/.cpp`

```cpp
namespace service::camera {

class RecordingPostProcess {
public:
    // work mode 写 desc JSON 落盘
    // - jsonContent: 由 caller 用 main_app 的 generateDescInfo() 生成（与现状格式一致）
    // - filePath: desc 文件完整路径
    // 负责：父目录创建、文件写入、错误日志
    static void writeWorkModeDescJson(const std::string& jsonContent,
                                       const std::string& filePath);

private:
    // 不允许实例化
    RecordingPostProcess() = delete;
};

} // namespace service::camera
```

**设计决策**：

- **`generateDescInfo` 留在 `main_app.cpp` 不迁移**——它依赖 MCU/Disk/CRC/Timezone/DeviceConfig 等模块，迁到 camera_service 库会引入过多交叉依赖。`writeWorkModeDescJson` 只负责"落盘"，JSON 内容由 caller 提供。
- **`writeMetadataDaoEntry` 已删除**——`VideoRecorder` 内部仍调 `MetadataDao::addMedia` 写 MediaItem 主条目（line 936-958），属于"编码/封装"职责的一部分，不应被 `CameraRecorder` 取代。`CameraServiceT32` 的 thumbnail 保存逻辑（`dao.saveThumbnail`）也保留在该文件内，不抽到工具类。
- **desc JSON 维持现有格式（不增字段）**——HTTP 上传协议契约，caller 用 `r.filePath` 拼 desc 文件名。

## 9. 集成：work mode

`processCmdVideoRecord` (main_app.cpp:617) 重构后：

```cpp
static bool processCmdVideoRecord(bool is_rtc_work_well) {
    (void)is_rtc_work_well;
    
    // 同步等待异步录影完成
    std::promise<media::RecordResult> done;
    auto future = done.get_future();
    
    media::CameraRecorder recorder;
    std::string filePath = std::string(MEDIA_TARGET_PATH) + getCurrentTimeFormatted() + ".mp4";
    
    media::RecordOptions opts;
    opts.autoCover = false;  // work mode 不循环覆盖
    opts.onComplete = [&done](const media::RecordResult& r) {
        done.set_value(r);
    };
    
    if (!recorder.record(filePath, 0 /* 用 config 默认时长 */, opts)) {
        Logger::log(LogLevel::ERROR, "Work Mode record start failed");
        return false;
    }
    
    media::RecordResult r = future.get();  // 阻塞等到录完
    
    if (r.error != media::RecordError::None && r.error != media::RecordError::UserStop) {
        Logger::log(LogLevel::ERROR, "Work Mode record failed: %s", r.errorMessage.c_str());
        return false;
    }

    // 写 desc（与现状格式一致：generateDescInfo 仍由 main_app 提供）
    std::string descJson;
    std::vector<std::string> files = { r.filePath };
    if (generateDescInfo(files, descJson) == 0) {
        std::string descPath = std::string(MEDIA_UPLOAD_PATH) + getCurrentTimeFormatted() + ".json";
        service::camera::RecordingPostProcess::writeWorkModeDescJson(descJson, descPath);
    }
    return true;
}
```

## 10. 集成：HTTP API

`CameraServiceT32::startRecord` 重构后：

```cpp
int CameraServiceT32::startRecord(int channel, int duration, bool audio, std::string& recordId) {
    (void)channel;
    std::lock_guard<std::mutex> lock(op_mutex_);
    if (is_recording_) { return -1; }
    
    recordId = generateRecordId();
    std::string filePath = std::string(MEDIA_STORE_FOLDER_PATH) + "/VID_" + timestamp + ".mp4";
    
    auto recorder = std::make_shared<media::CameraRecorder>();
    RecordOptions opts;
    opts.autoCover = (Settings::getInstance()->autoCover != 0);  // 默认值
    opts.onComplete = [this, recordId](const media::RecordResult& r) {
        std::lock_guard<std::mutex> l(op_mutex_);
        // MediaItem 主条目由 VideoRecorder::saveToDatabase 写
        // 这里只做 thumbnail 保存 + 状态更新
        if (r.error == media::RecordError::None || r.error == media::RecordError::UserStop) {
            if (video_recorder_ && video_recorder_->hasThumbnail()) {
                MetadataDao dao;
                dao.saveThumbnail(r.filePath, video_recorder_->getThumbnailData());
            }
        }
        is_recording_ = false;
    };
    
    if (!recorder->record(filePath, duration, opts)) {
        return -1;
    }
    
    video_recorder_ = recorder;
    current_record_file_ = filePath;
    is_recording_ = true;
    return 0;
}
```

`stopRecord` / `getRecordStatus` 委托给 `video_recorder_->stop()` / `video_recorder_->getCurrentDurationMs()`。

## 11. 不在重构范围

| 不动 | 原因 |
|------|------|
| `processCmdConcurrentSnapRecord` (cameraMode=3) | 边录边拍是另一故事，CH2/JPEG Channel 与本设计解耦 |
| `-vr` / `--video-record` 测试入口 | 硬编码参数是测试隔离需要 |
| RTSP 流 | 不落盘，不属于"录影文件" |
| 独立 thumbnail HTTP 端点 | 走 ImageSnap，独立功能 |
| desc JSON schema | 维持现状（HTTP 上传协议契约）|

## 12. 风险与注意事项

1. **callback 线程**：`onComplete` 在 `VideoRecorder` 后台线程触发，caller 不应做阻塞操作。work mode 现有 `processCmdVideoRecord` 是同步阻塞的，需要 `std::promise/future` 桥接。
2. **磁盘检查的 TOCTOU**：检查通过到 record() 启动之间有微小时间窗，期间其他进程可能写满 SD。现有代码也有这个风险，保持现状。
3. **`CameraPropertyService::getInstance()` 单例依赖**：`CameraRecorder` 隐式依赖此单例（与 `CameraServiceT32` 现状一致）。如果未来要做多实例，需重构为依赖注入。
4. **缩略图尺寸偶数对齐**：硬件 scaler 拒绝奇数尺寸，所以 `thumbH = (320 * videoH) / videoW; if (thumbH % 2 != 0) thumbH++` 是必要的。`LargeImageSnap` 已有类似处理。
5. **MediaItem 字段映射**：`VideoRecorder::saveToDatabase` 内部写全字段。`CameraServiceT32` 的 thumbnail 保存 (`dao.saveThumbnail`) 是 caller 责任。两者职责清晰，不抽到 `RecordingPostProcess`。

## 13. 验证方案

详见 [`camera-photo-recording-implementation-status.md` § 8.6](camera-photo-recording-implementation-status.md)。
