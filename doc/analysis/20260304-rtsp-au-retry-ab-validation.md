# RTSP 调度验证：AU Retry 对 consumer 的影响（A/B）

## 背景与目标
- 背景：为确认 `consumer < 30FPS` 的根因，新增可配置项：
  - `RTSP_VIDEO_AU_RETRY_US`（默认 `1000`）
  - `0` 表示在同一回调内继续处理（不通过 1ms retry 拆分）
- 目标：验证“`1ms retry` 是否是主要根因”。

## 验证方法
- 环境：`t32_yb/build_sim/bin/htc_main_app -rs`
- 客户端：`ffmpeg -rtsp_transport tcp -i rtsp://127.0.0.1:8554/live -t 20 -an -f null -`
- A 组（baseline）：`RTSP_VIDEO_AU_RETRY_US=1000`
- B 组（noretry）：`RTSP_VIDEO_AU_RETRY_US=0`
- 日志：
  - `/tmp/ab_baseline_app.log`
  - `/tmp/ab_noretry_app.log`

## 结果对比

### 会话汇总
- baseline：
  - `produced=702 consumed=691 fifo_drop=0 avg_consumed=29.50 fps`
- noretry：
  - `produced=698 consumed=691 fifo_drop=0 avg_consumed=29.68 fps`

### 关键观测（obs 聚合）
- `cbpf_avg`（每帧回调次数）：
  - baseline `1.009`
  - noretry `1.000`
- `cbpf_max`：
  - baseline `2`
  - noretry `1`
- `frame_ms_max_max`：
  - baseline `20.20 ms`
  - noretry `0.66 ms`
- `lag_us_avg`：
  - baseline `8969 us`
  - noretry `8222 us`
- `lag2ms`：
  - baseline `19.09 /s`
  - noretry `19.50 /s`

### `VIDEO Stats` 聚合
- `produced-consumed` 平均差值：
  - baseline `+0.48 fps`
  - noretry `+0.30 fps`
- `fifo_avg`：
  - baseline `7.04/60`
  - noretry `5.87/60`
- 两组均未出现：
  - `FIFO full(60/60)`
  - `Network buffer full`
  - `pull_timeout != 0`

## 结论
1. `AU retry=1ms` 不是主因。关闭后确有轻微改善，但幅度有限，不足以解释“明显积压到溢出”的场景。
2. `AU retry` 更像次要放大因素：在多 NAL/复杂帧时会增加回调拆分成本，但不是决定性瓶颈。
3. 主要矛盾仍在调度时序层（deadline lag 长期存在）与业务负载耦合：当源更重（更高峰值/更复杂）时，易从“轻微差值”放大成持续积压。

## 下一步建议
1. 增加“严格按帧 deadline 的单次回调预算”验证（区分 pace 触发与 retry 触发的 lag）。
2. 在用户真实复现源（非本机轻负载）上重复 A/B，确认在高压力下的放大路径。
3. 若要先缓解，保留 `RTSP_VIDEO_AU_RETRY_US` 作为可调开关，默认可考虑下调或关闭。

