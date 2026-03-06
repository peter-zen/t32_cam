# RTSP 发送频率机制与回调滞后验证（2026-03-05）

## 背景与目标
- 目标：确认“发送达不到 30FPS”是否由频率计算错误导致，还是由调度回调晚触发导致。
- 关注点：
  1. 发送频率如何计算与调度；
  2. 日志能否直接证明问题来源；
  3. 通过定向测试验证因果关系。

## 机制核对结论
1. 目标节拍计算逻辑正确：`interval_us = 1000000 / fps`。  
   - 30FPS 对应 `33333us`。  
2. 日志可见：`[VIDEO] pacing=33333 us`。  
3. 调度器在回调晚于 deadline 时会“跳过已过期节拍”（catch-up skip），因此即使目标是 30FPS，实际执行可能低于 30FPS。  

## 定向测试设计
- 程序：`build_sim/bin/htc_main_app -rs`
- 客户端：`ffmpeg -rtsp_transport tcp -i rtsp://127.0.0.1:8554/live -t 25 -an -f null -`
- 统一设置：`RTSP_VIDEO_AU_RETRY_US=1000`
- Case A（idle）：空闲 CPU
- Case B（stress）：4 个 busy loop 施压 CPU
- 日志：
  - `/tmp/rtsp_sched_validation/idle.log`
  - `/tmp/rtsp_sched_validation/stress.log`

## 测试结果

### Case A（idle）
- `MediaSession stopped`: `avg_consumed=29.38 fps`
- `obs` 聚合：
  - `lag_us_avg=10441`
  - `lag2ms=19.75 /s`
- `Stats` 聚合：
  - `delta_avg(produced-consumed)=0.57`
  - `fifo_avg=10.18/60`，`fifo_max=16`
  - 无 `FIFO full`、无 `Network buffer full`

### Case B（stress）
- `MediaSession stopped`: `avg_consumed=29.09 fps`
- `obs` 聚合：
  - `lag_us_avg=8829`
  - `lag2ms=20.04 /s`
- `Stats` 聚合：
  - `delta_avg(produced-consumed)=0.89`
  - `fifo_avg=18.54/60`，`fifo_max=25`
  - 无 `FIFO full`、无 `Network buffer full`

### 相关性分析（按秒配对 `Stats` 与 `obs`）
- idle：`corr(delta, lag) = 0.540`
  - `lag >= 12ms` 时 `delta_avg=0.88`
  - `lag < 12ms` 时 `delta_avg=0.00`
- stress：`corr(delta, lag) = 0.824`
  - `lag >= 12ms` 时 `delta_avg=2.88`
  - `lag < 12ms` 时 `delta_avg=0.10`

## 结论
1. 本轮未发现“频率计算错误”证据；节拍公式与日志一致。  
2. 日志能明确支持：**回调滞后（deadline lag）与 `produced-consumed` 差值正相关**。  
3. 当 lag 变大时，消费速率掉速、FIFO 上升更明显；这是“目标 30FPS 但实际低于 30FPS”的主要路径。  

## 备注
- 本轮在本机回环流中未复现到 FIFO 溢出，但“lag->delta->fifo 抬升”的趋势稳定存在。  
- 在真实网络/更重源场景下，该路径会更容易放大到溢出与可感知卡顿。

