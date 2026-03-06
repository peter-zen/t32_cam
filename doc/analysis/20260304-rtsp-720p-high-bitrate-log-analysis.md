# RTSP 720P 高码率测试日志分析（2026-03-04 09:07:58 ~ 09:08:25）

## 背景与目标
- 背景：已替换为 720P 源文件后进行一次 RTSP 测试，主诉“bitrate 仍偏高”。
- 目标：基于日志确认本次链路表现、主要问题与可能根因。
- 日志：`build_sim/sdcard/logs/app.log`

## 关键现象

### 1) 视频仍存在持续积压，最终触发 FIFO 满并丢帧
- 过程：
  - 初期：`fifo=1/60`（`produced=30/s consumed=29/s`）
  - 中后期：`fifo` 持续上升到 `55/60`
  - 末期：`FIFO full(60/60)`，开始 GOP drop
- 证据：
  - `09:07:59`：`fifo=1/60`  
  - `09:08:23`：`fifo=55/60`  
  - `09:08:24`：`FIFO full(60/60), dropping incoming P-frame...`
- 会话总结：
  - `VIDEO MediaSession stopped: produced=802 consumed=742 fifo_drop=1 avg_consumed=27.50 fps`

### 2) 720P 源下视频帧大小波动很大，出现明显高峰
- `VIDEO sent` 采样（26 个点）统计：
  - `min=1319 bytes`
  - `max=93534 bytes`
  - `avg=13775.8 bytes`
- 高峰帧示例：
  - `size=32694 / 37481 / 26758 / 25708 / 93534`
- 按 30fps 对应瞬时码率示例（仅按当下帧大小换算）：
  - `37481 bytes -> 9.00 Mbps`
  - `93534 bytes -> 22.45 Mbps`

### 3) 音频链路本次基本稳定
- 音频统计长期接近 `25/s`，`fifo_drop=0`，发送包稳定 `size=320`。
- 会话总结：
  - `AUDIO MediaSession stopped: produced=675 consumed=672 fifo_drop=0 avg_consumed=24.86 pps`

### 4) 退出阶段仍有设置文件路径错误（与推流主路径无关）
- `Failed to get setting file path`
- `Failed to open settings file for writing`

## 结论（本次）

- 720P 并不等于低码率：本次源的压缩参数/关键帧峰值仍高，导致视频发送侧负载偏重。
- 视频消费速度持续低于生产速度（约 27.5 vs 29.7~30fps），FIFO 累积后触发 1 次丢帧。
- 主问题仍在视频路径，音频整体稳定。

## 可能根因（按置信度）

1. **高码率 + 大峰值帧**导致 RTP 打包发送开销上升（高置信度）
2. **RTSP 视频发送回调是 NAL 驱动且有 1ms retry 调度**，在大帧/多 NAL 时更容易拉低有效消费速率（中高置信度）
3. 网络拥塞迹象不明显（日志未见 backpressure 相关告警），更像发送侧处理能力问题（中置信度）

## 建议

1. 先限制源码率并压平峰值（建议固定 GOP、限制 `maxrate/bufsize`），再复测 FIFO 是否仍上升。
2. 同步检查源是否“多切片（multi-slice）”编码；若是，改为单帧少切片可降低回调/打包开销。
3. 若目标仍是 30fps，建议继续优化视频发送回调的调度策略；仅增大 FIFO 只能延后问题触发。

