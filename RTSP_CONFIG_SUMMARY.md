# RTSP Server 配置总结

## ✅ 验证结果：所有配置正确

### 媒体文件规格

#### 📹 视频：full_frame_camera.h264
- **Codec**: H.264
- **分辨率**: 1920x1080
- **帧率**: 30 fps (30/1)
- **SPS/PPS**: 自动提取 (30 bytes / 4 bytes)

#### 🎵 音频：full_frame_camera.pcm
- **格式**: PCM S16LE (Little-Endian)
- **采样率**: 16000 Hz
- **声道数**: 1 (单声道)
- **比特率**: 256 kb/s

---

## 完整链路说明

### 音频数据流

```
输入文件
   │
   ├─ full_frame_camera.pcm
   ├─ 格式: PCM S16LE (Little-Endian)
   ├─ 采样率: 16KHz
   └─ 声道: Mono
   │
   ▼
AudioFileSource.cpp (读取层)
   │
   ├─ 位置: src/media/audio/AudioFileSource.cpp:146-149
   ├─ 行为: 直接返回原始 PCM 数据
   └─ 输出: PCM S16LE (Little-Endian)
   │
   ▼
rtsp.c (发送层 - 字节序转换)
   │
   ├─ 位置: src/media/rtsp/rtsp.c:761-771
   ├─ 检测: AUDIO_CODEC_L16
   ├─ 转换: htons() - Little-Endian → Big-Endian
   └─ 输出: PCM S16BE (Big-Endian)
   │
   ▼
RTSP/RTP 网络传输
   │
   ├─ Payload Type: 97 (Dynamic)
   ├─ 格式: L16 (Linear PCM 16-bit)
   └─ 标准: Big-Endian (网络字节序)
   │
   ▼
客户端 (ffplay/ffprobe)
   │
   ├─ 接收: PCM S16BE (Big-Endian)
   └─ 播放: ✓ 正确
```

### 视频数据流

```
输入文件
   │
   ├─ full_frame_camera.h264
   ├─ Codec: H.264
   ├─ 分辨率: 1920x1080
   └─ 帧率: 30 fps
   │
   ▼
RtspServer.cpp (SPS/PPS 提取)
   │
   ├─ 位置: src/media/rtsp/RtspServer.cpp:323-396
   ├─ 提取 SPS: 30 bytes
   └─ 提取 PPS: 4 bytes
   │
   ▼
SDP 生成
   │
   ├─ m=video 0 RTP/AVP 96
   ├─ a=rtpmap:96 H264/90000
   ├─ a=fmtp:96 packetization-mode=1;sprop-parameter-sets=<SPS>,<PPS>
   └─ a=framerate:30
   │
   ▼
RTSP/RTP 网络传输
   │
   └─ Payload Type: 96 (H.264)
   │
   ▼
客户端 (ffplay/ffprobe)
   │
   ├─ Codec: h264
   ├─ 分辨率: 1920x1080
   ├─ 帧率: 30 fps
   └─ 播放: ✓ 正确
```

---

## 配置文件

### sim_sdcard/rtsp_config.ini

```ini
# 视频: H.264, 1920x1080, 30fps
[video]
file      = sim_sdcard/video/full_frame_camera.h264
fps       = 30
codec     = 0      # 0=H264, 1=H265

# 音频: PCM S16LE, 16KHz, mono
# 注意：输入为 s16le，RTSP L16 会转换为大端（网络字节序）
[audio]
file               = sim_sdcard/video/full_frame_camera.pcm
sample_rate        = 16000
channels           = 1
samples_per_packet = 320
codec              = 2      # 2=L16 (Linear PCM 16-bit)

[playback]
loop = true
```

---

## SDP 输出

### 视频 SDP
```
m=video 0 RTP/AVP 96
a=rtpmap:96 H264/90000
a=fmtp:96 packetization-mode=1;sprop-parameter-sets=<SPS_BASE64>,<PPS_BASE64>
a=framerate:30
a=control:video
```

### 音频 SDP
```
m=audio 0 RTP/AVP 97
a=rtpmap:97 L16/16000/1
a=control:audio
```

---

## 客户端检测结果

### ffprobe 输出
```
Stream #0:0: Audio: pcm_s16be, 16000 Hz, 1 channels, s16, 256 kb/s
Stream #0:1: Video: h264 (High), yuv420p(tv, bt709, progressive), 
             1920x1080 [SAR 1:1 DAR 16:9], 30 fps
```

**验证结果**：
- ✅ 音频: pcm_s16be (Big-Endian) - 字节序转换正确
- ✅ 音频采样率: 16000 Hz
- ✅ 音频声道: 1
- ✅ 视频: h264
- ✅ 视频分辨率: 1920x1080
- ✅ 视频帧率: 30 fps

---

## 关键代码位置

| 功能 | 文件 | 行号 |
|------|------|------|
| 音频文件读取 | src/media/audio/AudioFileSource.cpp | 146-149 |
| 音频字节序转换 | src/media/rtsp/rtsp.c | 761-771 |
| SDP 生成 (音频) | src/media/rtsp/rtsp.c | 294-298 |
| SDP 生成 (视频) | src/media/rtsp/rtsp.c | 303-330 |
| 视频 SPS/PPS 提取 | src/media/rtsp/RtspServer.cpp | 323-396 |

---

## 测试脚本

### 验证脚本
```bash
# 最终验证 - 检查所有配置
./verify_config.sh

# 快速验证 - 检查流是否正常
./quick_verify.sh

# 完整测试 - 详细的音视频测试
./test_new_media.sh
```

### 手动测试
```bash
# 启动服务器
./build_sim/bin/htc_main_app -rs

# 播放测试
ffplay rtsp://localhost:8554/live

# 流分析
ffprobe -show_streams rtsp://localhost:8554/live
```

---

## 配置匹配表

| 参数 | 文件规格 | 配置文件 | 实际检测 | 状态 |
|------|---------|---------|---------|------|
| 视频 Codec | H.264 | 0 (H.264) | h264 | ✅ |
| 视频分辨率 | 1920x1080 | - | 1920x1080 | ✅ |
| 视频帧率 | 30 fps | 30 | 30/1 | ✅ |
| 音频 Codec | PCM S16LE | 2 (L16) | pcm_s16be | ✅* |
| 音频采样率 | 16000 Hz | 16000 | 16000 | ✅ |
| 音频声道 | 1 (Mono) | 1 | 1 | ✅ |

*注：L16 标准为 Big-Endian，输入为 Little-Endian，系统正确转换

---

## 注意事项

### 1. 字节序处理
- **RTSP 标准 L16 必须是 Big-Endian**
- 输入文件：PCM S16LE (Little-Endian)
- 系统转换：htons() (Little → Big)
- 网络传输：PCM S16BE (Big-Endian)
- 客户端兼容：标准格式

### 2. 视频参数匹配
- 配置文件中的 `fps` 必须与实际文件匹配
- 当前配置：fps=30，实际文件：30fps ✅

### 3. 音频参数匹配
- 配置文件中的 `sample_rate` 必须与实际文件匹配
- 当前配置：sample_rate=16000，实际文件：16000 Hz ✅

---

## 结论

✅ **RTSP Server 已完全正确配置**

1. ✅ 视频配置正确：H.264, 1920x1080, 30fps
2. ✅ 音频配置正确：PCM S16LE → L16 (Big-Endian), 16KHz, Mono
3. ✅ SDP 生成正确：符合 RTSP 标准
4. ✅ 字节序转换正确：Little-Endian → Big-Endian
5. ✅ 客户端兼容：ffplay/ffprobe 正常播放

**可以正常启动服务器进行测试！**
