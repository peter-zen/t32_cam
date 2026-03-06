# RTSP 优化验证：Pace No-Skip 追赶策略（2026-03-05）

## 背景与目标
- 背景：已确认主要瓶颈在 `pace` 路径回调滞后（非 retry 主导）。
- 目标：验证“晚触发时不跳拍、快速追赶”是否能提升 `consumed` 并降低 FIFO 积压。

## 优化点
- 新增开关：`RTSP_VIDEO_PACE_NO_SKIP`
  - `0`：原有策略（晚触发时按既有逻辑跳到未来节拍）
  - `1`：晚触发时快速追赶（不跳过 pacing slot）
- 代码：`src/media/rtsp/rtsp.c`

## 测试方法
- 程序：`build_sim/bin/htc_main_app -rs`
- 客户端：`ffmpeg -rtsp_transport tcp -i rtsp://127.0.0.1:8554/live -t 20 -an -f null -`
- 固定：`RTSP_VIDEO_AU_RETRY_US=1000`
- 对照：
  - Case A：`RTSP_VIDEO_PACE_NO_SKIP=0`（baseline）
  - Case B：`RTSP_VIDEO_PACE_NO_SKIP=1`（优化）

## 结果对比

### Case A（no_skip=0）
- `avg_consumed=29.44 fps`（`produced=704 consumed=691`）
- `stats`: `delta_avg=0.52`, `fifo_avg=8.43`, `fifo_max=12`
- `obs`: `lag_us_avg=9068`, `pace_lag_us_avg=9074`, `retry_lag_us_avg=1190`

### Case B（no_skip=1）
- `avg_consumed=29.94 fps`（`produced=693 consumed=692`）
- `stats`: `delta_avg=0.05`, `fifo_avg=1.00`, `fifo_max=2`
- `obs`: `lag_us_avg=8498`, `pace_lag_us_avg=9085`, `retry_lag_us_avg=1000`
- 注意：`pull_timeout=53`（消费者更积极追赶后出现一定空等）

## 结论
1. 开启 `Pace No-Skip` 后，视频消费速率明显更接近 30FPS，FIFO 积压显著下降。  
2. 该策略可有效缓解“回调滞后导致的长期产消差”。  
3. 代价是可能增加一定 `pull_timeout`（积极追赶带来的空等），但本次未见溢出或背压告警。  

## 建议
1. 在 SIMU 场景优先开启：`RTSP_VIDEO_PACE_NO_SKIP=1`。  
2. 继续观察真实网络下 `pull_timeout` 与 CPU 占用；若副作用可接受，可考虑作为默认策略。  

