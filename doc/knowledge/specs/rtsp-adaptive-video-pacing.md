# RTSP 视频自适应步速（adaptive pacing）设计

**状态**：✅ 设计已 grill 定型（2026-06-30），⏳ **未实施**（待用户发话动代码；按项目 git 政策不擅自提交）。
**范围**：`um` 预览主码流（TCP interleaved，单客户端）。
**解决**：`um-preview-latency-fifo-investigation.md` 的"治本（B）"方向——消费者端产销失配导致 video FIFO 堆积 → 秒级延迟。

---

## 1. 背景与问题（一句话）

video `MediaSession` FIFO（深度 60）被持续填满：生产者 ~30fps，消费者因 libevent 唤醒抖动 + `NO_SKIP=0` 丢槽只 ~27fps，产销差让 depth 单调爬到 ~58/60 → **延迟 ≈ depth/fps ≈ 1.9s**。
根因与证据详见 [`../bugs/um-preview-latency-fifo-investigation.md`](../bugs/um-preview-latency-fifo-investigation.md)。

本设计是消费者端的**闭环控制**：按 FIFO depth 在两个发送步速间带迟滞地切换，把 depth 钉在低位 → 延迟钉在百毫秒级。

## 2. 机制概述（TL;DR）

```
depth ≥ 5  →  drain 档（35fps / 28571µs）—— 主动排空积压
depth ≤ 2  →  calm 档  （30fps / 33333µs）—— 匹配生产者
(2, 5) 死区 →  保持当前档（迟滞，防抖振）
```

- 采样点：新帧 pull 之前（`rtsp.c:1702`，`ctx->video` 为空时）。
- 生效点：帧完成时 `video_schedule_next_deadline`（`:1893`）读 `pace_interval_us` → 只管**帧间间隔**，不碰帧内 NALU 的 `au_retry_us`。
- `pace_interval_us` 本身就是"档位"，不需要额外 mode 字段；死区就是迟滞带。
- `NO_SKIP` **恒 ON**（load-bearing，见 §4.5）。

## 3. 关键事实（代码确认，决定设计的硬约束）

| 事实 | 出处 | 含义 |
|---|---|---|
| 编码器 `fps=30/1`；生产者/消费者 pacing 都来自 `1000000/30` | `RtspServer.cpp:470`；`MediaSession.cpp:173`；`rtsp.c:1597` | 生产者=消费者基准=**30fps**；日志里的"31"是 1s 窗口取整 |
| 消费者的 pull 是 **blocking**（`popBlocking` 12ms），跑在单线程 `event_base_dispatch` 上 | `MediaSession.cpp:333`；`rtsp.c:1648/2096` | FIFO 空 → 拉帧 → **整个事件循环卡最多 12ms**（=抖动源）|
| `MediaFIFO` 已有非阻塞 `pop()` 与 `size()` | `MediaFIFO.h:37,42` | depth 查询 / 非阻塞 pull 几乎零成本可得 |
| TCP `is_full` = libevent 输出缓冲 > 512KB，已在用 | `tcp.c:80`；`rtsp.c:1324/1691` | TCP 背压**可读**，已门控 |
| UDP `is_full` 硬编码 `return false` | `udp.c:82` | UDP 背压**不可读**——本机制对 UDP 等于无门控加速（可能加剧丢包）|
| RTP ts 取生产者 capture ts，与 send pacing 无关 | `rtsp.c:1767` | 改发送步速**不影响**客户端节律（ts 稳定）|

## 4. 设计决策与权衡

### 4.1 自适应双速 vs 恒定略高于生产者
替代方案：消费者恒定 32–33fps（略高于生产者 30），FIFO 自然排到 ~0，无需测 depth/阈值/plumbing。
**否决理由**：恒定略高 → FIFO 趋空 → 频繁撞空 → blocking pull 的 12ms 卡顿**重新引入**正是当初导致 27fps 的唤醒抖动。
**自适应的优势**恰是让 FIFO **保持非空**（冷静期从 depth 往上填、排空期从 depth>2 往下排）→ 几乎不触发 blocking pull → 循环干净。代价是带一条 ~2-5 帧的延迟缓冲（~70-160ms，预览不可感知）。

### 4.2 calm = 30（匹配生产者），不是 31
`calm=31` 会比生产者快 1fps → FIFO 向**空**排空 → 撞 0 → blocking 卡顿（或改非阻塞则 200Hz 自旋）。
正确方向：calm **≤ 生产者**，让 depth 自然趋势**向上**（走进 drain 的怀抱），而非向下撞 0。
且 `calm=30` 与现实自洽：**原 bug（FIFO 涨到 60）本身就是消费端天然 fill-bias 的证据**（libevent 抖动 → 略晚触发 → 略低于 30fps → depth 向上漂）。所以 calm=30 继承这个向上趋势 → 触发 drain → 清到 2 → 循环，趋势**远离 0**，blocking pull 几乎不发生。不是赌未验证的漂移方向，是用已测到的方向。

### 4.3 水位 high=5 / low=2
- `high=5`（不是早期讨论的 15）：15 帧 @30fps = 容忍 0.5s 才反应；5 帧 ≈ 0.16s 上限。
- `low=2`（不是 0）：留小缓冲，避免排空到 0 触发 blocking。
- 死区 `(2,5)` 即迟滞带，防 calm↔drain 抖振。

### 4.4 drain = 35fps
drain 净排空速率 = 35 − 30 = 5fps；从 high=5 清到 low=2（3 帧）≈ 0.6s。够快、够温和，且 28.5ms interval 在 NO_SKIP=1 下可达（原测 30fps 时 pace_lag 仅 ~5ms，余量充足）。

### 4.5 `NO_SKIP` 恒 ON（load-bearing）—— ⚠️ 非"可选"
**NO_SKIP 与 adaptive 是两个正交的轴**：adaptive 设定 *目标 interval*；`NO_SKIP` 决定 *单次触发晚到时是否丢槽*（`rtsp.c:619`：`=1` 补发 `next_deadline=now+1` / `=0` 跳槽 `+= skipped×interval`）。

adaptive 说"按 35fps 发"，但**能否真做到取决于每次触发是否都发出了一帧**——而丢槽正是 `NO_SKIP=0` 的行为。更糟的是**反向耦合**：drain 把 interval 收紧 → `pace_lag > interval` 概率变高 → `NO_SKIP=0` 丢槽更多 → 有效 fps 更低 → **drain 越紧越排不空，适得其反**。

经验证据：`NO_SKIP=0`→27fps（丢槽），`NO_SKIP=1`→30fps（补发）。drain 要 >30fps，前提正是 `NO_SKIP=1` 把丢掉的 ~3fps 补回来。

> **结论**：adaptive 设定目标，`NO_SKIP` 让目标可达。去掉 `NO_SKIP`，drain 不但不工作还会反噬；两者协同（drain 模式下 `NO_SKIP=1` 晚到→`now+1`，比 drain interval 还激进，等于 depth 触发之外再叠 per-fire 补发）。
> **决定**：删掉 `RTSP_VIDEO_PACE_NO_SKIP` env gate（`rtsp.c:1596`），`pace_no_skip` 恒真（`rtsp.c:1617`）。env 默认 0 是脚枪——ship 了 adaptive 却没开它就触发反噬。

唯一能让 NO_SKIP"显得冗余"的世界是唤醒抖动为 0；实测 avg 14ms / max 43ms，非 0。

### 4.6 保持 blocking pull（不改 pull 契约）
配合 calm≤生产者 + low=2 缓冲，稳态 depth ∈ [2,5]，几乎不撞 0 → blocking pull 几乎不触发。无需改成非阻塞（避免 200Hz 重试自旋）。pull 契约不变 = 改动面更小。

### 4.7 单客户端范围
每个 RTSP 客户端有自己的 `VideoCtx`，但都从**同一个** `MediaSession` FIFO pop。两个自适应 drainer 会对共享 depth 抢。`um` 预览=单客户端，本机制限定单客户端语义；第二客户端行为不在范围（不专门处理）。

### 4.8 断连背压死锁 —— 出范围（独立 bug）
本机制**不修**那个非恢复性背压死锁（transport 满 → 排不进满管）。现有 `:1691` 的 `is_full` 门已保证：transport 满时跳过 pull，自适应不介入、不会让死锁更糟。该死锁单独排查。

## 5. 实现方案（实施时的改动清单）

**非 hal/，仍按 git 政策不擅自提交；动代码前待用户发话。**

### 5.1 新增 depth 查询通路（plumbing）
- `MediaSession`：加 `size_t fifoDepth() const { return fifo_ ? fifo_->size() : 0; }`（`size()` 互斥保护，30/s 近零开销）。
- `RtspServer`：加 `queryVideoDepth()` 指向 `videoSession_->fifoDepth()`。
- 暴露给 `rtsp.c`：**推荐用专用函数指针**（`size_t (*query_video_depth)(void*)` 挂在 server struct，与 `register_function` 并列设置）——避开 `func_t` 的 `int` 返回签名不匹配（`rtsp.h:73`）。
  - 备选：新增 `FUNC_ID_QUERY_VIDEO_DEPTH`，depth 经 `size_t*` out-param 带回（复用现有 `funcs[]` 注册表，但语义略 hacky）。

### 5.2 `VideoCtx` + 步速切换（`rtsp.c`）
- `VideoCtx`（`:167`）加 `func_t`/函数指针 `query_depth`；`play_video`（`:1589`）接线。
- 在 pull 点（`:1702`，`ctx->video` 为空、`pull_frame` 之前）插入迟滞切速：
  ```c
  #define VIDEO_CALM_US  33333u   /* 30fps */
  #define VIDEO_DRAIN_US 28571u   /* 35fps */
  #define VIDEO_DEPTH_HIGH 5
  #define VIDEO_DEPTH_LOW  2

  size_t depth = query_depth(ctx);                 /* 新通路 */
  if      (depth >= VIDEO_DEPTH_HIGH) ctx->pace_interval_us = VIDEO_DRAIN_US;
  else if (depth <= VIDEO_DEPTH_LOW ) ctx->pace_interval_us = VIDEO_CALM_US;
  /* else 死区：保持 */
  ```
  `pace_interval_us` 初值仍取 `:1597` 的 `1000000/safe_fps`（=calm）。`video_schedule_next_deadline`（`:612`）每次触发读它，自动用新值。
- **`pace_no_skip` 恒真**：删 `:1596` 的 env 解析，`:1617` 直接 `.pace_no_skip = true`。

### 5.3 可观测（每秒 stats，`:1797-1843`）
在那行 `elog_d` 补打 `depth` 与当前 `pace_interval_us`（已算出，不新增字段），首轮验证才能看见 calm↔drain 切换与 depth 轨迹。

### 5.4 不动
- `:1691` 的 `is_full` 背压门（保留）。
- 帧内 NALU 的 `au_retry_us` / `video_schedule_retry`（步速只影响帧间间隔）。
- pull 契约（仍 `popBlocking`）。
- RTP ts 来源（仍生产者 capture ts）。
- `MediaSession` FIFO 深度 60（`RtspServer.cpp:499`）——本机制把 depth 钉在 [2,5]，60 仅作突发上限，无需改。

## 6. 验收标准（实施后真机验证）

从 `build/logs/app.log` 的每秒 stats 行核对：

| 指标 | 通过条件 |
|---|---|
| FIFO depth | 稳态在 **[2,5]** 内振荡，**不再**爬到 50-60 |
| 消费者 fps | calm ~30；drain 期短暂 ~35 |
| `pace_lag` | 保持低位（`NO_SKIP=1` 效果不退化）|
| blocking stall | 稳态 stats 无持续 depth=0（无 12ms 卡顿迹象）|
| 端到端预览延迟 | 从 ~1.9s 降到 **≤ ~0.16s**（5 帧×33ms）稳态，通常更低 |
| calm↔drain | stats 行能看见切换（drain 仅在突发/追帧时短暂出现）|
| 双平台编译 | `BUILD_FOR_SIMULATION=ON/OFF` 均过 |

复现/拉流方法同 investigation 文档 §6（`HTC_LOG_DEBUG=1` 起 um + ffmpeg TCP 拉流 ≥10s + 读 stats）。

## 7. 风险与回退

- **若实测消费端竟呈 drain-bias**（depth 反向撞 0）：退路是把 calm 降到 29（显式 fill-bias），或改非阻塞 pull（接受 200Hz 自旋）。但原 bug 证据表明方向是 fill-bias，预期不会触发。
- **drain 达不到 35fps**：若 `pace_lag` 在 drain 档下失控（>28.5ms 频繁），说明抖动比预期大；退路是放宽 drain 到 33fps（仍 > 生产者 30，净排空 3fps）。
- env gate 删除后若需临时关 `NO_SKIP` 调试：临时改回 env 即可（但生产必须 ON）。

## 8. 关联

- [`../bugs/um-preview-latency-fifo-investigation.md`](../bugs/um-preview-latency-fifo-investigation.md) — 根因与证据（本设计的"问题侧"）
- [`rtsp-streaming-behavior.md`](rtsp-streaming-behavior.md) — RTSP 流行为规格
- [`um-app-spec.md`](um-app-spec.md) — um 规格
- 代码：`src/media/rtsp/rtsp.c`（步速/迟滞/NO_SKIP）、`MediaSession.cpp`（`fifoDepth`）、`RtspServer.cpp`（查询通路注册）、`MediaFIFO.h`（`size()`）
