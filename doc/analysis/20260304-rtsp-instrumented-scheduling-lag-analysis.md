# RTSP 插桩复测分析：consumer < 30FPS 的主因定位（2026-03-04 09:56）

## 背景与目标
- 背景：已在 `rtsp.c` 增加观测点（每帧 NAL 数、回调耗时、deadline lag），再次复测仍有卡顿。
- 目标：确认 `consumer` 低于 30FPS 是否由 bitrate 导致，还是发送调度链路导致。
- 日志：`build_sim/sdcard/logs/app.log`
- 会话：`09:56:29.500`（PLAY）到 `09:56:55.783`（TEARDOWN）

## 关键结论
- 结论 1：**bitrate 不是主因**。本次主要矛盾是视频发送调度存在持续 `deadline lag`，导致消费速率长期低于生产速率。
- 结论 2：编码包化复杂度不是主要瓶颈。`NAL` 数量和回调执行耗时都很低，不足以解释 `consumer` 掉到 `25~28/s`。
- 结论 3：视频 FIFO 持续积压并最终溢出，卡顿点与溢出一致。

## 证据

### 1) 视频链路确实长期“产大于消”
- `VIDEO Stats`（26 个采样）：
  - `produced - consumed`：平均 `+2.08 fps`
  - `17/26` 秒为 `produced > consumed`
  - `10/26` 秒差值达到 `>=4 fps`
- FIFO 水位从 `1/60` 持续升到 `57/60`，后续出现：
  - `FIFO full(60/60), dropping incoming P-frame and starting GOP drop`

### 2) 复杂度指标低，排除“回调太慢/每帧NAL过多”
- `obs{}` 汇总（25 个采样窗口）：
  - `frame(avg)=28.20`（对应每秒实际发送帧数）
  - `nal_avg(avg)=1.024`，`nal_max(max)=3`
  - `frame_ms_avg(avg)=0.167 ms`，`frame_ms_max(max)=21.370 ms`
  - `cb_us_avg(avg)=118.52 us`，`cb_us_max(max)=1173 us`
- 解释：
  - 大多数窗口里每帧仅约 `1` 个 NAL，NAL 维度不重。
  - 回调自身执行仅百微秒级，远小于 33ms 帧预算。
  - 即使存在偶发 `frame_ms_max=21.37ms`，也未超过 33ms，不足以单独导致长期掉到 25~28fps。

### 3) 时序指标异常，调度 lag 持续存在
- `obs{}` 汇总：
  - `lag_us_avg(avg)=11626.62 us`（约 `11.6ms`）
  - `lag_us_max(max)=42155 us`（约 `42.2ms`）
  - `lag2ms(avg)=19.60`（每秒约 20 次回调晚于 deadline 超 2ms）
- 同时日志中：
  - `Network buffer full` = `0`
  - `VIDEO pull_frame failed/stream ended` = `0`
  - `VIDEO Stats pull_timeout!=0` = `0`
- 解释：
  - 不是网络背压，也不是取帧失败。
  - 主要是 RTSP 发送事件回调触发时机长期偏晚，造成节拍损失。

## 根因判断（当前）
- 当前更接近“调度层节拍问题”而非“码率问题”：
  - 回调执行本身快，但触发时机常晚（deadline lag 高）；
  - 导致视频消费端节拍落后，FIFO 长期抬升并最终触发 GOP drop。

## 建议（按优先级）
1. 先做调度策略验证：临时去掉/减小 `AU 后 1ms retry`，或改为单次回调内尽量完成当前帧发送，观察 `lag_us_avg` 和 `consumed` 是否回升至 30。
2. 增加“按帧截止时间驱动”的严格节拍：以 `capture_ts`/目标帧间隔作为主时钟，避免多次 event 往返造成节拍损失。
3. 复测时固定传输方式（优先 UDP）并记录 `obs{}`，确认是否存在 TCP 模式特有放大效应。
4. 若需保守兜底，再考虑把目标帧率短期降至 25fps；这属于缓解，不是根治。

