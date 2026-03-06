# RTSP 测试日志问题分析（2026-03-04）

## 背景与目标
- 目标：分析 `t32_yb` simulation 模式下，本次 RTSP 测试日志中可见的问题。
- 日志文件：`build_sim/sdcard/logs/app.log`
- 会话区间：约 `2026-03-04 08:04:30` 到 `08:05:16`（约 46.5 秒）

## 现状描述
- 音频配置：G711A / 8000Hz / 单声道，目标约 25 pps。
- 视频配置：H264 / 30fps，MediaSession FIFO 深度 60。
- 会话开始后，音视频均正常启动，持续推流，最终 TEARDOWN 正常。

## 问题与发现

### 1. 视频侧存在持续积压，最终触发 FIFO 满
- 证据：
  - 视频统计长期表现为 `produced=30/s`，而 `consumed` 多次低于 30（如 26~29/s），FIFO 占用从 `1/60` 持续涨到 `58/60`。
  - 关键告警：
    - `W/MED_SESSION ... [VIDEO] FIFO full(60/60), dropping incoming P-frame and starting GOP drop`
    - 位置：`app.log` 第 249 行左右。
- 结论：
  - 视频消费侧吞吐在该时段略低于生产侧，导致缓冲区水位不断上升并最终溢出一次。

### 2. FIFO 满后出现 GOP Drop 行为，视频短时抖动/卡顿风险高
- 证据：
  - 触发告警后，视频统计短时出现：
    - `produced=22/s consumed=27/s`（第 252 行）
    - `produced=0/s consumed=28/s`（第 257 行）
    - `produced=6/s consumed=29/s`（第 262 行）
  - 同时该阶段发送帧大小出现异常偏小（如 `size=91/82/66`，第 253/258/264 行）。
  - 拉帧时间戳出现明显跳变（`capture_ts` 从约 `39032943` 跳到 `42099579`，第 257~262 行附近）。
- 结论：
  - 本次存在一次“视频明显不连续”窗口，用户侧可能感知为短时画面卡顿或突跳。

### 3. 音频链路本次整体稳定，未见服务端丢帧/空包
- 证据：
  - 音频统计基本保持 `produced=25/s consumed=25/s`，FIFO 始终约 `1/80`，`fifo_drops=0`。
  - RTP 音频发送包大小稳定 `size=320`，`expected_pps=25`。
  - 会话总计：`avg_produced=24.96 pps`, `avg_consumed=24.90 pps`（第 285 行）。
- 结论：
  - 这次“声音偶发卡顿”在服务端日志中没有直接证据，主问题集中在视频路径。

### 4. 退出阶段有非推流核心错误（配置路径）
- 证据：
  - `Failed to get setting file path`
  - `Failed to open settings file for writing`
  - 位置：`app.log` 末尾第 291、295 行附近。
- 结论：
  - 不影响本次 RTSP 推流主流程，但属于收尾流程缺陷，需要单独修复。

## 结论与建议
- 主要问题：视频消费速率在一段时间内低于生产速率，导致 FIFO 逐步堆积并触发一次 overflow；overflow 后 GOP drop 导致短时视频不连续。
- 次要问题：退出时设置文件路径处理仍有错误日志。
- 当前结论：本次 A/V 体验风险主要来自视频侧（而非音频侧）。

