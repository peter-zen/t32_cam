# RTSP 720P 降码率复测日志分析（2026-03-04 09:18:13 ~ 09:18:55）

## 背景与目标
- 背景：在 720P 场景下进一步降低视频码率后再次测试，主诉“仍有卡顿”。
- 目标：确认本次是否改善、仍存问题点及根因判断。
- 日志：`build_sim/sdcard/logs/app.log`

## 关键观察

### 1) 码率峰值明显下降，但问题未根除
- `VIDEO sent` 采样（41 个点）：
  - `min=58 bytes`
  - `max=35197 bytes`
  - `avg=5409.4 bytes`
- 与上次（高码率 720P）对比：
  - 上次：`max=93534`、`avg=13775.8`
  - 本次：`max=35197`、`avg=5409.4`
- 结论：降码率是有效的，但仍有较大突发帧（如 `35197`），峰值压力仍存在。

### 2) 视频 FIFO 仍持续抬升并触发一次溢出
- 早期开始即存在 `produced > consumed`，FIFO 逐步累积：
  - `fifo=4/60 -> 9/60 -> ... -> 59/60`
- 到 `09:18:52.994` 触发：
  - `FIFO full(60/60), dropping incoming P-frame and starting GOP drop`
- 结论：核心问题仍是“长期小幅欠消费 + 偶发突发帧”。

### 3) 溢出后出现典型 GOP Drop 恢复段，用户可感知为卡顿/顿挫
- 告警后短时间统计：
  - `produced=15/s consumed=28/s`
  - `produced=0/s consumed=27/s`
- 同时发送帧出现极小值（`size=139/67/58`），属于恢复窗口特征。

### 4) 音频链路仍稳定
- 会话结束统计：
  - `AUDIO produced=1051 consumed=1049 fifo_drop=0 avg_consumed=24.91 pps`
- 未见音频丢帧相关告警。

### 5) 会话总览
- `VIDEO MediaSession stopped: produced=1192 consumed=1192 pull_timeout=4 fifo_drop=1 avg=28.34 fps`
- 虽然最终 produced/consumed 一致，但存在一次显式丢帧 + 恢复段，足以造成主观卡顿。

## 结论
- 降码率后“突发大小”已明显改善，但**视频消费侧能力边界仍不足以覆盖 30fps 稳态 + 峰值抖动**。
- 因此仍会在高水位（50+/60）运行，最终被一次突发帧推到满队列，触发 GOP drop，表现为卡顿。
- 主矛盾依旧在视频路径，非音频路径。

## 建议（按优先级）
1. 降低源的关键帧峰值与突发：固定 GOP、收紧 `maxrate/bufsize`、避免超大 I 帧。
2. 若可接受，短期把 RTSP 输出从 30fps 降到 25fps 验证是否消除 FIFO 抬升。
3. 中期优化 RTSP 视频发送路径调度（当前 NAL 驱动 + 1ms retry 在峰值场景下仍偏吃紧）。

