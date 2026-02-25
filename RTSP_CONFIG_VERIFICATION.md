# RTSP Server 配置验证报告

## 测试日期
2026-01-18 14:53

## 新媒体文件规格

### 视频：full_frame_camera.h264
- ✅ Codec: H.264 (High Profile)
- ✅ 分辨率: 1920x1080
- ✅ 帧率: 30 fps (30/1)

### 音频：full_frame_camera.pcm
- ✅ 格式: PCM S16LE (Little-Endian)
- ✅ 采样率: 16000 Hz
- ✅ 声道数: 1 (单声道)

---

## 配置文件 (sim_sdcard/rtsp_config.ini)

```ini
[video]
file = sim_sdcard/video/full_frame_camera.h264
fps = 30                    # ✅ 匹配 30fps
codec = 0                  # ✅ H.264

[audio]
file = sim_sdcard/video/full_frame_camera.pcm
sample_rate = 16000        # ✅ 匹配 16KHz
channels = 1               # ✅ 匹配单声道
samples_per_packet = 320   # 20ms @ 16kHz
codec = 2                  # ✅ L16 (Linear PCM 16-bit)

[playback]
loop = true
```

---

## 完整链路验证

### 1. 文件读取层 (AudioFileSource.cpp)

**位置**: `src/media/audio/AudioFileSource.cpp:146-149`

```cpp
} else {
    // L16，直接返回PCM数据
    *data = packetBuffer.data();
    *size = bytesRead;
}
```

**行为**:
- 读取 `full_frame_camera.pcm` (s16le, Little-Endian)
- 直接返回原始数据，不进行转换
- ✅ 输出: PCM S16LE (Little-Endian)

### 2. SDP 生成 (rtsp.c)

**位置**: `src/media/rtsp/rtsp.c:294-298`

```c
// L16 (Linear PCM 16-bit)
audio_pt = AUDIO_DYN_PAYLOAD_TYPE;
SMOLRTSP_SDP_DESCRIBE(
    ret, sdp,
    (SMOLRTSP_SDP_MEDIA, "audio 0 RTP/AVP %d", audio_pt),
    (SMOLRTSP_SDP_ATTR, "rtpmap:%d L16/%d/%d", audio_pt, rtsp_param.audio_sample_rate, rtsp_param.audio_channels),
    (SMOLRTSP_SDP_ATTR, "control:audio"));
```

**SDP 输出**:
```
a=rtpmap:97 L16/16000/1
```

**说明**:
- ✅ `L16` 表示 Linear PCM 16-bit
- ✅ `16000` 表示 16KHz 采样率
- ✅ `1` 表示单声道
- ✅ 根据 RTSP/RTP 标准，L16 必须是 Big-Endian（网络字节序）

### 3. 数据发送层 (rtsp.c)

**位置**: `src/media/rtsp/rtsp.c:756-771`

```c
// 对于 L16 格式，需要转换为网络字节序（大端）
// 注意：需要复制数据，不能修改原始缓冲区
static uint8_t audio_send_buffer[4096];
uint8_t *send_data = audio_data;

if (ctx->audio_codec == AUDIO_CODEC_L16) {
    if (audio_size <= sizeof(audio_send_buffer)) {
        memcpy(audio_send_buffer, audio_data, audio_size);
        int16_t *samples = (int16_t *)audio_send_buffer;
        size_t num_samples = audio_size / 2;
        for (size_t j = 0; j < num_samples; j++) {
            samples[j] = htons(samples[j]);  // Little-Endian → Big-Endian
        }
        send_data = audio_send_buffer;
    }
}
```

**行为**:
- 检测到 `AUDIO_CODEC_L16`
- 复制数据到临时缓冲区
- 使用 `htons()` 将每个样本转换为网络字节序（Big-Endian）
- ✅ 输出: PCM S16BE (Big-Endian)

### 4. 客户端接收 (ffplay/ffprobe)

**实际接收到的流信息**:
```
Stream #0:0: Audio: pcm_s16be, 16000 Hz, 1 channels, s16, 256 kb/s
```

**验证结果**:
- ✅ Codec: `pcm_s16be` (Big-Endian) - 正确！
- ✅ 采样率: 16000 Hz
- ✅ 声道数: 1

---

## 链路总结

```
┌────────────────────────────────────────────────────────────────────────┐
│                        音频数据流                                    │
├────────────────────────────────────────────────────────────────────────┤
│                                                                      │
│  1. 输入文件: full_frame_camera.pcm                                  │
│     格式: PCM S16LE (Little-Endian)                                 │
│     采样率: 16000 Hz                                                 │
│     声道数: 1                                                       │
│                                                                      │
│  2. AudioFileSource.cpp                                            │
│     读取文件，直接返回原始数据                                        │
│     输出: PCM S16LE (Little-Endian)                                │
│                                                                      │
│  3. rtsp.c:761-771 (发送层)                                        │
│     检测到 L16 格式                                                 │
│     使用 htons() 转换字节序                                         │
│     输出: PCM S16BE (Big-Endian)                                  │
│                                                                      │
│  4. RTSP/RTP 网络传输                                              │
│     Payload Type: 97 (Dynamic)                                       │
│     格式: L16 (Linear PCM 16-bit)                                    │
│     标准: Big-Endian (网络字节序)                                    │
│                                                                      │
│  5. 客户端 (ffplay/ffprobe)                                         │
│     接收: PCM S16BE (Big-Endian)                                   │
│     解码: 正确播放                                                  │
│                                                                      │
└────────────────────────────────────────────────────────────────────────┘
```

---

## 视频链路验证

```
┌────────────────────────────────────────────────────────────────────────┐
│                        视频数据流                                    │
├────────────────────────────────────────────────────────────────────────┤
│                                                                      │
│  1. 输入文件: full_frame_camera.h264                                 │
│     Codec: H.264                                                     │
│     分辨率: 1920x1080                                                │
│     帧率: 30 fps                                                     │
│                                                                      │
│  2. RtspServer.cpp:323-396 (启动)                                   │
│     提取 SPS: 30 bytes                                               │
│     提取 PPS: 4 bytes                                               │
│     编码到 SDP 的 fmtp 参数                                          │
│                                                                      │
│  3. SDP 生成                                                        │
│     a=rtpmap:96 H264/90000                                           │
│     a=fmtp:96 packetization-mode=1;sprop-parameter-sets=<SPS>,<PPS>  │
│     a=framerate:30                                                   │
│                                                                      │
│  4. 客户端接收 (ffplay/ffprobe)                                     │
│     Codec: h264                                                      │
│     分辨率: 1920x1080                                               │
│     帧率: 30/1 (30 fps)                                             │
│                                                                      │
└────────────────────────────────────────────────────────────────────────┘
```

---

## 测试结果

### ✅ 所有测试通过

#### 视频流验证
- ✅ Codec: H.264
- ✅ 分辨率: 1920x1080
- ✅ 帧率: 30 fps
- ✅ SPS/PPS: 正确提取并包含在 SDP 中

#### 音频流验证
- ✅ Codec: PCM S16BE (Big-Endian)
- ✅ 采样率: 16000 Hz
- ✅ 声道数: 1 (单声道)
- ✅ 字节序: Little-Endian → Big-Endian 转换正确

#### 功能测试
- ✅ ffprobe 正确识别音视频流
- ✅ ffplay 正常播放（headless 模式）
- ✅ 无错误检测
- ✅ 文件循环播放正常

---

## 服务器日志验证

```
[14:53:55.034] Video: sim_sdcard/video/full_frame_camera.h264 (fps=30)
[14:53:55.034] Audio: sim_sdcard/video/full_frame_camera.pcm (rate=16000, enabled)
[14:53:55.925] Found SPS: 30 bytes
[14:53:55.925] Found PPS: 4 bytes
[14:53:55.925] SPS size: 30, PPS size: 4
[14:53:55.934] RTSP server started, waiting for client connection
```

---

## 配置匹配确认

| 参数 | 文件规格 | 配置文件 | 实际检测 | 状态 |
|------|---------|---------|---------|------|
| 视频 Codec | H.264 | 0 (H.264) | h264 | ✅ |
| 视频分辨率 | 1920x1080 | - | 1920x1080 | ✅ |
| 视频帧率 | 30 fps | 30 | 30/1 | ✅ |
| 音频 Codec | PCM S16LE | 2 (L16) | pcm_s16be | ✅* |
| 音频采样率 | 16000 Hz | 16000 | 16000 | ✅ |
| 音频声道 | 1 | 1 | 1 | ✅ |

*注：L16 标准为 Big-Endian，输入为 Little-Endian，系统正确转换

---

## 关键实现点

### 1. 字节序转换
- **位置**: `src/media/rtsp/rtsp.c:761-771`
- **转换**: 使用 `htons()` 将 Little-Endian 转为 Big-Endian
- **标准**: RTSP/RTP 的 L16 格式要求 Big-Endian

### 2. SDP 正确性
- **位置**: `src/media/rtsp/rtsp.c:294-298`
- **格式**: `a=rtpmap:97 L16/16000/1`
- **含义**: Dynamic PT 97 = L16 (Big-Endian), 16KHz, Mono

### 3. 视频参数
- **位置**: `src/media/rtsp/RtspServer.cpp:323-396`
- **SPS/PPS**: 正确从 H.264 文件提取
- **SDP**: 正确编码到 fmtp 参数

---

## 结论

✅ **RTSP Server 完全正确配置并工作正常**

1. **视频配置正确**: H.264, 1920x1080, 30fps
2. **音频配置正确**: PCM S16LE → L16 (Big-Endian), 16KHz, Mono
3. **SDP 生成正确**: 符合 RTSP 标准
4. **字节序转换正确**: Little-Endian → Big-Endian
5. **客户端兼容**: ffplay/ffprobe 正常播放

---

## 测试文件

- `test_new_media.sh` - 完整测试脚本
- `quick_verify.sh` - 快速验证脚本
- `test_av_full.sh` - 音视频完整测试

---

## 命令参考

```bash
# 启动服务器
./build_sim/bin/htc_main_app -rs

# 验证流
./quick_verify.sh

# 完整测试
./test_new_media.sh

# 手动测试
ffplay rtsp://localhost:8554/live
ffprobe -show_streams rtsp://localhost:8554/live
```
