# Android RTSP 抖动现象日志检查

## 背景与目标

现象：

- Android 手机上的 RTSP client 可以连接并播放
- 但用户观感是“整个视频都在抖”

目标：

- 基于服务端当前日志判断是否存在明显异常
- 区分“致命错误”与“可疑但未直接坐实”的迹象

## 现状描述

本次检查基于日志：

- [build_sim/sdcard/logs/app.log](/home/zengping/project/huntcam/code/t32_yb/build_sim/sdcard/logs/app.log)

本次 Android 客户端会话时间范围：

- 建链开始：`2026-03-26 05:04:04`
- 连接结束：`2026-03-26 05:04:10`

会话特征：

- URL：`rtsp://192.168.0.210:554/live`
- 传输方式：`RTP/AVP/TCP;unicast;interleaved`

## 问题与发现

### 1. RTSP 握手与会话建立正常

日志显示 `OPTIONS / DESCRIBE / SETUP / PLAY` 全部成功：

- `DESCRIBE: video=1 sps_len=30 pps_len=4 audio=1 asr=8000`
- 视频、音频两个 track 都完成 `SETUP`
- `PLAY` 后 video/audio session 都成功启动

这说明：

- 当前没有出现协议层失败
- 也没有出现之前那种重复连接导致 session 丢失的问题

### 2. 没有看到服务端显式报错或丢帧

从 `PLAY` 到连接结束这段期间，没有出现以下异常：

- `Invalid video bitstream`
- `pull_frame failed`
- `Pull result: -1`
- `fifo_drops > 0`
- video `pull_timeout > 0`

说明：

- 服务端持续拿到了视频帧
- 码流本身未见明显错误
- FIFO 没有发生丢帧

### 3. 视频发送节奏存在持续但不致命的 lag

RTSP 周期统计中，视频侧的发送滞后逐步升高：

- 第 1 秒：`lag_us_avg=8528.52`，`lag_us_max=21236`
- 第 2 秒：`lag_us_avg=13072.64`，`lag_us_max=23089`
- 第 3 秒：`lag_us_avg=13792.00`，`lag_us_max=32820`
- 第 4 秒：`lag_us_avg=15292.83`，`lag_us_max=40224`
- 第 6 秒：`lag_us_avg=18672.95`，`lag_us_max=40731`

同时可见：

- `frame_ms_max=20.25`
- 某些周期 `consumed` 低于 `produced`
- video FIFO 深度从 `0/60` 上升到 `4/60`

这说明：

- 服务端虽然还在稳定送流，但视频发送节奏不是完全贴住 30fps 理想节拍
- 中后段出现了轻微积压和调度滞后
- 这种现象可能表现为客户端端上的“出帧不均匀”或“轻微抖动”

### 4. 当前日志没有证据表明发生了严重服务器端故障

最终统计：

- `produced=192`
- `consumed=188`
- `avg_produced=30.12 fps`
- `avg_consumed=29.50 fps`
- `fifo_drop=0`

说明：

- 服务器整体还在接近 30fps 地持续发送
- 这更像“节奏不够平滑”，不是“服务端断流/乱序/大量丢帧”

### 5. 连接结束时的 `Connection error` 更像客户端主动断开

结束时仅有一条：

- `E/RTSP Connection error`

随后立即进入：

- `onSessionClosed`
- `MediaSession stopped`

这更像客户端退出或中断连接后的收尾日志，不像抖动根因。

## 结论

从这次服务端日志看：

- **没有发现致命异常**
- **没有发现明显码流错误、拉流失败或服务器端丢帧**
- **唯一比较可疑的是视频发送侧存在持续的 pacing lag 和轻微积压**

因此当前更准确的判断是：

- 服务端日志没有直接坐实“抖动”的根因
- 但日志确实暴露出“发送节奏不够平滑”的迹象，这一项值得继续追

## 建议

建议按下面顺序继续验证：

1. 优先区分 Android client 是走 `TCP interleaved` 还是 `UDP`
2. 若客户端支持，做一组 `UDP` 对照测试，看抖动是否明显改善
3. 在服务端补充更细的观测：
   - RTP timestamp 增量
   - 实际发送时间间隔
   - 是否存在 burst 发送
4. 若 Android 端一直只走 `TCP`，重点继续排查当前 RTSP pacing 与 event-loop 调度是否导致帧发送聚簇

## 关键日志位置

- 握手与 `PLAY`：`app.log` 28-49 行
- 首帧：`app.log` 65-67 行
- 视频 pacing 统计：`app.log` 71、76、81、87、91、97 行
- 结束收尾：`app.log` 98-102 行
