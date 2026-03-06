# RTSP 根因锁定验证：Pace Lag vs Retry Lag（2026-03-05）

## 背景与目标
- 背景：此前已确认“目标 30FPS 但实际偶尔达不到 30FPS”与回调滞后相关。
- 目标：进一步确认滞后主要来自哪条调度路径：
  1. `pace` 调度（33.333ms 节拍）；
  2. `retry` 调度（AU 后重试，默认 1ms）。

## 方法
- 在 `rtsp.c` 增加分离统计字段：
  - `pace_lag_us_avg / pace_lag2ms`
  - `retry_lag_us_avg / retry_lag2ms`
- 测试配置：
  - Case A：`RTSP_VIDEO_AU_RETRY_US=1000`
  - Case B：`RTSP_VIDEO_AU_RETRY_US=0`
  - 客户端：`ffmpeg -rtsp_transport tcp -i rtsp://127.0.0.1:8554/live -t 20 -an -f null -`

## 结果

### Case A（retry=1000）
- `avg_consumed=29.77 fps`（`produced=696 consumed=691`）
- `obs` 聚合：
  - `lag_us_avg=8844.1`
  - `pace_lag_us_avg=8893.0`
  - `retry_lag_us_avg=1152.0`
  - `lag2=20.18/s`，`pace_lag2=20.05/s`，`retry_lag2=0.14/s`
- `Stats` 聚合：
  - `delta_avg(produced-consumed)=0.22`
  - `fifo_avg=3.74/60`，`fifo_max=5`

### Case B（retry=0）
- `avg_consumed=29.79 fps`（`produced=696 consumed=692`）
- `obs` 聚合：
  - `lag_us_avg=8598.3`
  - `pace_lag_us_avg=8598.3`
  - `retry_lag_us_avg=0`
  - `lag2=19.77/s`，`pace_lag2=19.77/s`，`retry_lag2=0`
- `Stats` 聚合：
  - `delta_avg=0.17`
  - `fifo_avg=2.87/60`，`fifo_max=4`

## 结论
1. **主导因素是 pace 路径的回调滞后，而不是 retry 路径。**
   - `retry=1000` 时，`pace_lag` 与 `overall lag` 几乎重合；
   - `retry_lag` 量级和发生频次都很小。
2. 关闭 retry 仅带来小幅改善（`delta_avg`、`fifo` 轻微下降），不改变主要矛盾。
3. 因此“发送达不到 30FPS”的核心仍是 pace 调度触发时序（event loop 回调触发晚），不是频率公式计算错误，也不是 retry 逻辑本身。

## 说明
- 本轮本机回环测试未出现 FIFO 溢出；但在高负载/真实网络/更重源条件下，`pace lag -> consumed下降 -> FIFO抬升` 会更容易放大到可见卡顿。

