# 录影音频支持开发计划

## 一、目前状态

**目前录影代码没有音频支持**，只有纯视频录制：
- `VideoRecorder.cpp` 只处理 H.264/H.265 视频编码
- 使用 `minimp4` 库的 `mp4_h26x_write_*` 系列函数，只写入视频 NAL 单元
- RTSP Server 中音频是禁用的 (`audio_enable = 0`)

---

## 二、需要完成的工作

### 2.1 音频采集模块 (使用 libimp API)

需要新增 `AudioCapture` 类，调用以下 libimp API：

```cpp
// 初始化流程
IMP_AI_SetPubAttr(devId, &attr);     // 设置音频输入属性
IMP_AI_Enable(devId);                 // 启用音频设备
IMP_AI_EnableChn(devId, chnId);       // 启用音频通道

// 采集流程 (循环)
IMP_AI_PollingFrame(devId, chnId, timeout_ms);
IMP_AI_GetFrame(devId, chnId, &frm, BLOCK);
// ... 使用音频数据 ...
IMP_AI_ReleaseFrame(devId, chnId, &frm);

// 反初始化
IMP_AI_DisableChn(devId, chnId);
IMP_AI_Disable(devId);
```

**音频属性配置示例：**
```cpp
IMPAudioIOAttr attr;
attr.samplerate = AUDIO_SAMPLE_RATE_16000;  // 16KHz
attr.bitwidth = AUDIO_BIT_WIDTH_16;          // 16bit
attr.soundmode = AUDIO_SOUND_MODE_MONO;      // 单声道
attr.frmNum = 20;                            // 缓存帧数
attr.numPerFrm = 320;                        // 每帧采样点 (16000/50=320 for 20ms)
attr.chnCnt = 1;
```

---

### 2.2 音频编码模块 (可选)

如果需要压缩音频（AAC/G.711），需要使用 libimp 的音频编码 API：

```cpp
// 创建编码通道
IMPAudioEncChnAttr encAttr;
encAttr.type = PT_G711A;  // 或注册 AAC 编码器
encAttr.bufSize = 20;
IMP_AENC_CreateChn(aeChn, &encAttr);

// 编码流程
IMP_AENC_SendFrame(aeChn, &frm);           // 发送 PCM 帧
IMP_AENC_PollingStream(aeChn, timeout_ms);
IMP_AENC_GetStream(aeChn, &stream, BLOCK); // 获取编码后数据
// ... 使用编码数据 ...
IMP_AENC_ReleaseStream(aeChn, &stream);

// 销毁
IMP_AENC_DestroyChn(aeChn);
```

---

### 2.3 修改 minimp4 Muxer 支持音频

`minimp4` 库支持音频轨道，需要：

```cpp
// 添加音频轨道
MP4E_track_t audio_track;
audio_track.track_media_kind = e_audio;
audio_track.object_type_indication = MP4_OBJECT_TYPE_AUDIO_ISO_IEC_14496_3; // AAC
audio_track.time_scale = 16000;  // 采样率
audio_track.u.a.channelcount = 1;
int audio_track_id = MP4E_add_track(mux, &audio_track);

// 设置 AAC DSI (Decoder Specific Info)
MP4E_set_dsi(mux, audio_track_id, aac_dsi, dsi_len);

// 写入音频样本
MP4E_put_sample(mux, audio_track_id, audio_data, audio_len, duration, MP4E_SAMPLE_DEFAULT);
```

---

### 2.4 修改 VideoRecorder 类

需要修改的文件和内容：

| 文件 | 修改内容 |
|------|----------|
| `VideoRecorder.h` | 添加音频相关成员变量和方法 |
| `VideoRecorder.cpp` | 集成音频采集、编码、mux 逻辑 |
| `VideoRecorderParams` | 添加音频参数配置 |

**VideoRecorderParams 扩展：**
```cpp
class VideoRecorderParams {
    // 现有视频参数...
    
    // 新增音频参数
    bool audioEnable = false;
    int audioSampleRate = 16000;
    int audioChannels = 1;
    int audioBitWidth = 16;
};
```

**录制流程修改：**
```
┌─────────────────────────────────────────────────────────┐
│                    VideoRecorder::record()               │
├─────────────────────────────────────────────────────────┤
│  1. 初始化视频编码器 (现有)                               │
│  2. 初始化音频采集 + 编码器 (新增)                        │
│  3. 创建 MP4 muxer                                       │
│  4. 添加视频轨道 (现有)                                   │
│  5. 添加音频轨道 (新增)                                   │
│  6. 启动两个线程:                                        │
│     - 视频采集线程 → 写入视频样本                         │
│     - 音频采集线程 → 写入音频样本 (新增)                  │
│  7. 同步等待录制完成                                      │
│  8. 关闭 muxer，释放资源                                  │
└─────────────────────────────────────────────────────────┘
```

---

### 2.5 音视频同步

这是最关键的部分：

```cpp
// 视频时间戳 (90kHz 时基)
video_timestamp = frame_count * 90000 / fps;

// 音频时间戳 (采样率时基，如 16000)
audio_timestamp = sample_count;

// 写入 MP4 时需要统一时基或正确设置各轨道 timescale
```

**同步策略：**
- 使用系统时间戳对齐音视频
- 或使用 PTS (Presentation Time Stamp) 从 libimp 获取

---

## 三、新增文件建议

```
src/media/audio/
├── AudioCapture.h        # 音频采集类
├── AudioCapture.cpp
├── AudioEncoder.h        # 音频编码类 (可选，如需 AAC)
├── AudioEncoder.cpp
└── CMakeLists.txt
```

---

## 四、工作清单总结

| 序号 | 工作项 | 优先级 | 说明 |
|------|--------|--------|------|
| 1 | **确认硬件音频输入** | 高 | 确认 T32 板子上 MIC 的连接方式 (数字MIC/模拟MIC)，对应 devId=0 或 1 |
| 2 | **新增 AudioCapture 类** | 高 | 封装 `IMP_AI_*` API，实现音频采集 |
| 3 | **测试音频采集** | 高 | 单独测试能否正确获取 PCM 数据 |
| 4 | **选择音频编码格式** | 中 | PCM (无压缩) / G.711A / AAC，AAC 兼容性最好但需额外编码器 |
| 5 | **新增 AudioEncoder 类** | 中 | 如选 G.711/AAC，封装 `IMP_AENC_*` API |
| 6 | **修改 VideoRecorder** | 高 | 集成音频采集、编码、mux |
| 7 | **修改 minimp4 mux 逻辑** | 高 | 添加音频轨道，写入音频样本 |
| 8 | **音视频同步** | 高 | 确保音视频时间戳对齐 |
| 9 | **参数配置** | 低 | 在 config.ini 或 setting.json 中添加音频开关和参数 |
| 10 | **测试验证** | 高 | 录制 MP4 后用播放器验证音视频同步 |

---

## 五、关键代码示例

### 5.1 AudioCapture 类框架

```cpp
// AudioCapture.h
#ifndef AUDIO_CAPTURE_H
#define AUDIO_CAPTURE_H

#include <imp/imp_audio.h>
#include <functional>
#include <thread>
#include <atomic>

class AudioCapture {
public:
    struct Params {
        int devId = 1;                              // 0=数字MIC, 1=模拟MIC
        int chnId = 0;
        IMPAudioSampleRate sampleRate = AUDIO_SAMPLE_RATE_16000;
        IMPAudioBitWidth bitWidth = AUDIO_BIT_WIDTH_16;
        IMPAudioSoundMode soundMode = AUDIO_SOUND_MODE_MONO;
        int frmNum = 20;
        int numPerFrm = 320;  // 20ms @ 16kHz
    };

    AudioCapture(const Params& params = Params());
    ~AudioCapture();

    bool start(std::function<void(IMPAudioFrame*)> onFrame);
    void stop();
    bool isRunning() const { return running; }

private:
    bool initialize();
    void deinitialize();
    void captureLoop();

    Params params;
    bool initialized = false;
    std::atomic<bool> running{false};
    std::thread captureThread;
    std::function<void(IMPAudioFrame*)> frameCallback;
};

#endif
```

### 5.2 VideoRecorder 修改示例

```cpp
// VideoRecorder.h 新增
class VideoRecorderParams {
    // ... 现有成员 ...
    
    // 音频参数
    bool enableAudio = false;
    int audioDevId = 1;
    int audioSampleRate = 16000;
    int audioChannels = 1;
};

// VideoRecorder.cpp record() 修改
bool VideoRecorder::record(const std::string &filename, int duration) {
    // ... 现有视频初始化 ...
    
    // 音频初始化 (新增)
    std::shared_ptr<AudioCapture> audioCapture;
    int audioTrackId = -1;
    
    if (params.enableAudio) {
        AudioCapture::Params audioParams;
        audioParams.sampleRate = (IMPAudioSampleRate)params.audioSampleRate;
        audioCapture = std::make_shared<AudioCapture>(audioParams);
        
        // 添加音频轨道到 MP4
        MP4E_track_t audioTrack = {};
        audioTrack.track_media_kind = e_audio;
        audioTrack.object_type_indication = MP4_OBJECT_TYPE_AUDIO_ISO_IEC_14496_3;
        audioTrack.time_scale = params.audioSampleRate;
        audioTrack.u.a.channelcount = params.audioChannels;
        audioTrackId = MP4E_add_track(muxer, &audioTrack);
        
        // 设置 AAC DSI (如果用 AAC)
        // MP4E_set_dsi(muxer, audioTrackId, aac_dsi, dsi_len);
    }
    
    // 启动音频采集 (新增)
    if (audioCapture) {
        audioCapture->start([&](IMPAudioFrame* frm) {
            // 编码 (可选) + 写入 MP4
            int duration = frm->len / (params.audioChannels * 2); // 16bit = 2 bytes
            MP4E_put_sample(muxer, audioTrackId, frm->virAddr, frm->len, 
                           duration, MP4E_SAMPLE_DEFAULT);
        });
    }
    
    // ... 现有视频录制循环 ...
    
    // 停止音频采集 (新增)
    if (audioCapture) {
        audioCapture->stop();
    }
    
    // ... 现有清理代码 ...
}
```

---

## 六、注意事项

1. **音频设备 ID**
   - `devId=0`: 数字 MIC (DMIC)
   - `devId=1`: 模拟 MIC (AMIC)
   - 需要确认 T32 硬件使用哪种

2. **音频格式选择**
   - **PCM**: 无压缩，文件大，兼容性好
   - **G.711A/U**: 压缩比低，CPU 占用小，电话级音质
   - **AAC**: 压缩比高，音质好，但需要额外编码器库

3. **线程安全**
   - 音视频分别在不同线程采集
   - 写入 MP4 时需要加锁保护 muxer

4. **时间戳同步**
   - libimp 的 `IMPAudioFrame.timeStamp` 和 `IMPEncoderStream` 的时间戳需要对齐
   - 建议使用相对时间戳，从 0 开始递增

---

## 七、预估工作量

| 阶段 | 工作内容 | 预估时间 |
|------|----------|----------|
| 1 | 音频采集模块开发 + 测试 | 2-3 天 |
| 2 | 音频编码集成 (如需 AAC) | 1-2 天 |
| 3 | VideoRecorder 集成修改 | 2-3 天 |
| 4 | 音视频同步调试 | 1-2 天 |
| 5 | 整体测试验证 | 1-2 天 |
| **总计** | | **7-12 天** |

如果只用 PCM 或 G.711 (libimp 内置)，可以省去 AAC 编码器的工作，时间会更短。
