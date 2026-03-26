# VLC 先出声音、约 3 秒后才出画分析

## 背景与目标

现象：

- VLC 连接 RTSP 后，声音几乎马上出来
- 画面约 3 秒后才出现

目标：

- 判断是否是服务端视频发送启动慢
- 判断是否与关键帧/GOP/传输方式有关

## 现状描述

本次排查基于：

- 日志文件 [build_sim/sdcard/logs/app.log](/home/zengping/project/huntcam/code/t32_yb/build_sim/sdcard/logs/app.log)
- 当前 simu 默认视频源 [build_sim/bin/res/full_frame_camera.h264](/home/zengping/project/huntcam/code/t32_yb/build_sim/bin/res/full_frame_camera.h264)

## 问题与发现

### 1. 服务端不是 3 秒后才开始发视频

从日志看，`PLAY` 后视频发送几乎立刻开始：

- `08:24:05.800` `PLAY`
- `08:24:05.833` `onSessionPlay: video session started`
- `08:24:05.837` `[VIDEO] First frame: capture_ts=33333 us, rtp_ts=2999`
- `08:24:05.883` `[AUDIO] First frame: capture_ts=40000 us, rtp_ts=0`

结论：

- 服务端首个视频帧发送时间不晚于音频
- “3 秒后才出画”不是 RTSP 服务端启动视频慢导致的

### 2. VLC 当前走的是 UDP 传输

日志中可以看到：

- `SETUP: Transport request=RTP/AVP;unicast;client_port=...`
- `SETUP: transport lower=UDP`

说明 VLC 当前不是 TCP interleaved，而是 RTP over UDP。

### 3. 当前视频文件首帧是 IDR，但非常大

对 `full_frame_camera.h264` 解析结果：

- 第 0 帧是关键帧（IDR）
- 第 0 帧大小约 `69561` bytes

该首帧前导 NAL 顺序为：

- `SEI`
- `SPS`
- `PPS`
- `IDR`

这说明首帧本身是可解码起点，但它会被切成很多 RTP 分片。

### 4. GOP 间隔正好是约 3 秒

对前 200 帧解析结果：

- 关键帧位置：`0, 90, 180`
- 帧率：`30 fps`

因此 GOP 间隔为：

- `90 / 30 = 3 秒`

这与“VLC 约 3 秒后才出画”的现象高度吻合。

### 5. 实验验证：问题与 GOP/关键帧周期强相关

后续验证结果：

- 将默认视频源切换为 `full_frame_camera_720p_gop30_single.h264` 后，VLC 很快出画
- 该测试源的关键帧位置为 `0, 30, 60, 90...`
- 当前帧率为 `30 fps`
- 因此该测试源的关键帧周期约为 `1 秒`

这说明：

- 出画时延会随着关键帧周期显著变化
- “约 3 秒后出画”并不是一个固定的网络或客户端建链时间
- 现象更接近“新会话没有稳定利用第一个起播关键点，而是在等待后续关键帧”

同时，用户还验证了：

- 将 VLC 改为 TCP 传输模式后，仍然是约 `3 秒` 才出画

因此原先“UDP 首帧丢包/乱序”只能作为次要可能，不再是主假设。

### 6. 更具体的怀疑：`PLAY` 后首个关键帧发送得过早

当前代码路径中：

- [src/media/rtsp/rtsp.c](/home/zengping/project/huntcam/code/t32_yb/src/media/rtsp/rtsp.c) 的 `Client_play()` 会先触发 `FUNC_ID_ON_SESSION_PLAY`
- 然后创建音视频播放上下文
- 最后才发送 `PLAY` 的 `200 OK` 响应

对应代码位置：

- `Client_play()` 中先调用 `self->peer->funcs[FUNC_ID_ON_SESSION_PLAY]`
- 最后才调用 `smolrtsp_respond_ok(ctx)`

同时，日志显示：

- `PLAY` 在 `08:24:05.800`
- 第一个视频帧在 `08:24:05.837` 就已发送

也就是 `PLAY` 后大约 `37 ms` 就开始发第一个视频关键帧。

这带来一个很合理的解释：

- 客户端还没完全处理完 `PLAY` 成功响应和内部起播准备
- 服务端已经把第一个关键帧送出去了
- 客户端稳定错过第一个起播关键点后，只能等下一个 IDR

这条解释同时满足：

- TCP / UDP 都会复现
- GOP90 时约 3 秒出画
- GOP30 时明显更快出画

音频之所以马上出来，是因为：

- 当前音频包很小（`320 bytes`）
- 音频 RTP 包不需要等待视频关键帧
- 音频路径的起播依赖条件明显比视频低

## 结论与建议

结论：

- 这次现象不像服务端“视频 3 秒后才开始发送”
- 更像是 **新会话没有稳定命中首个可用视频起播点，客户端实际在等待后续关键帧**
- 由于切换到 `GOP30` 视频源后可以很快出画，这个问题已经被证明与 **关键帧周期/GOP** 强相关

建议：

1. 继续检查首会话起播策略，重点看是否应该只从 `IDR` 开始向新客户端送视频。
2. 检查首个 `IDR` 前后的 `SPS/PPS/SEI/IDR` 发送顺序与起播时机，尤其是 `sps_pps_bypass` 对首会话的影响。
3. 新会话开始时增加关键帧日志，明确打印首帧是否 `key`、大小、关键帧序号、是否为首个会话起播帧。
4. 如果短期先追求稳定体验，可优先使用更短 GOP 的测试源，或在 simu 环境中默认改为 `GOP30`。
5. 优先尝试调整 `PLAY` 时序：
   - 先完成 `PLAY 200 OK` 响应
   - 再启动视频发送
   - 或者给首个视频关键帧增加一个小的启动延迟（例如 `150-300 ms`）
