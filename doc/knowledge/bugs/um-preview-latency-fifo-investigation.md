# um 预览秒级延迟根因分析（RTSP video FIFO 堆积）

**问题**：`um` 起来后，低延迟客户端预览 RTSP 主码流，画面持续落后现实约 **秒级（实测 ~1.6–1.9 s）**。
**排查时间**：2026-06-30
**状态**：**根因已确认（日志证据）**。修复方向已给出，**未实施**（本次范围为只诊断+确认）。

---

## TL;DR — 工程参考卡

> **秒级延迟来自 video MediaSession 的 FIFO（深度 60）被持续填满。** 编码器稳定产出 ~30 fps，
> 但 RTP 消费者只 ~26–28 fps；产销差 ~2–3 fps/s 让 FIFO 单调爬升到 60 上限后掉帧再涨，
> 稳态深度 ~50–58/60 → **延迟 ≈ depth / fps ≈ 58/31 ≈ 1.87 s**。FIFO 深度 60 是把"小幅产销差"
> 放大成"秒级延迟"的放大器。
>
> **编码器没问题**（produced≈31/s，pull_timeout=0，不卡顿）——排除"GC4653/低 fps"假设。
> **音频源是死的**（produced=0）——独立 bug，与视频延迟无关。

### 关键证据（稳态每秒 Stats，`MediaSession.cpp:311`，`HTC_LOG_DEBUG=1`）

| 时刻 | produced/s | consumed/s | pull_timeout | fifo |
|------|-----------|-----------|--------------|------|
| 43:12 | 30 | 19 | 0 | 24/60 |
| 43:14 | 31 | 28 | 0 | 30/60 |
| 43:17 | 31 | 28 | 0 | 39/60 |
| 43:20 | 31 | 28 | 0 | 50/60 |
| 43:22 | 31 | 27 | 0 | **58/60** |
| 43:23 | 15 | 29 | 0 | 44/60（fifo_drops=1，撞 60 上限丢帧）|
| 43:24 | 31 | 28 | 0 | 47/60（再涨）|

整段 14.28 s 会话汇总：`produced=405 consumed=368 → avg_produced=28.4 fps avg_consumed=25.8 fps`。

---

## 1. 现象与判定

- **延迟类型**：稳态端到端（glass-to-glass），非首帧、非启动。客户端为可控低延迟播放器，
  故"秒级"主要为**服务端**贡献。
- **数据通路**：sensor → IMP encode → `VideoSource::pullData`（`polling(100)`→`getFrame`）
  → **`MediaSession` 生产者 → FIFO(60) → 消费者(RTP 发送)** → 客户端。
- `onSessionPlay` 已 `requestIDR()`（`RtspServer.cpp:430`）→ 首帧不等待 GOP，排除 GOP 因素。

## 2. 嫌疑链与判定

| # | 嫌疑 | 判定 | 证据 |
|---|------|------|------|
| 1 | **video FIFO 深度 60 被填满** | ✅ **根因** | fifo 24→58 单调爬升；produced>consumed；depth/fps≈1.87s |
| 2 | 编码器实际 fps<30（GC4653/bitrate 未设）| ❌ 排除 | produced≈31/s，pull_timeout=0，稳定 30fps 不卡 |
| 3 | 音频 FIFO / AV-sync | ⚠️ 独立问题 | 音频 produced=0（源死），未参与视频延迟 |
| 4 | 客户端 jitter buffer | 已隔离 | 用可控低延迟客户端 |

## 3. 根因机理

1. 生产者按 `fps=30` pacing（`MediaSession.cpp:173`），从编码器拉帧 ~30–31 fps。
2. 消费者（RTP 发送，TCP over WiFi）实际 ~26–28 fps —— 比 30 fps 慢 ~2–3 fps/s。
3. `MediaSession` FIFO（`RtspServer.cpp:499` 建会话传 `fifoSize=60`）在产销差下**单调填满**，
   到 60 上限触发丢帧（`fifo_drops`），然后继续涨 —— 稳态停在 ~50–58/60。
4. 每帧在 FIFO 里蹲 `depth × (1/fps)` 秒 → **~1.6–1.9 s 的固定延迟线**。
   即用户看到的"画面落后现实秒级"。

**为什么是"秒级"而不是更小**：FIFO 深度 60 @ 30fps = 上限 2.0 s。深度是延迟放大器。
即便产销差只有 ~2–3 fps，深 FIFO 也让稳态延迟顶到 ~1.9 s。

## 4. 修复方向（**本次未实施**，供后续）

> **✅ 已定型（2026-06-30）**：B 方向已 grill 出完整设计 —— **RTSP 视频自适应步速**
> （按 FIFO depth 在 calm=30/drain=35 两档间带迟滞切换，`NO_SKIP` 恒 ON，depth 钉在 [2,5]）。
> 完整决策、实现清单、验收标准见 [`../specs/rtsp-adaptive-video-pacing.md`](../specs/rtsp-adaptive-video-pacing.md)。
> 下面的 A/B/C 候选保留作背景；A（改小 FIFO 深度）已不再是首选——自适应步速能在不动 FIFO 深度（仍 60）的前提下把稳态 depth 钉到 [2,5]。

按"延迟 vs 平滑度"权衡，候选（需独立验证，不在本诊断范围）：

- **A（最直接）**：大幅调小 video FIFO 深度（60 → ~2–3），把延迟上限压到 ~0.1 s。
  `RtspServer.cpp:499` `MediaSession(videoSource, 60)` → 改小。代价：网络抖动时更易丢帧/卡顿。
- **B（治本）**：让消费者跟上 30 fps（查 RTP/TCP 发送瓶颈 —— WiFi 链路、smolrtsp 发送循环、
  `popTimeoutMs_=5`）。产销匹配后 FIFO 不积压。
- **C（策略）**：FIFO 满时"丢最旧非关键帧"已部分实现（`MediaSession.cpp:228-269`），
  但只在撞 60 上限时触发；可改为更激进的低水位丢旧，让稳态深度不顶到上限。

建议先做 A 的小步验证（如 60→10）量化延迟下降，再决定 B/C。

## 5. 顺带发现

### 5.1 `HTC_LOG_DEBUG=1` 在 um 里失效（顺序 bug，**已修**）
- 原 `um_app.cpp:140` 在 `commonStartup`（145）**之前**调 `setLogLevel(DEBUG)`，
  被 `commonStartup` 内 `elog_init_with_config(logLevel=INFO)`（`ProcessLifecycle.cpp:344`）覆盖。
- `wm_app.cpp:192-193` 在 `commonStartup`（171）**之后**调 —— um 是回归。
- **已修**：把 um 的 `HTC_LOG_DEBUG` 块移到 `commonStartup` 之后（镜像 wm），并加注释防回归。
  本次正是靠这个修复才拿到 `elog_d` Stats。**已提交**（随 `6e797c1` Slice 1b-1 并入主干；当前 `um_app.cpp:142-148`）。

### 5.2 音频源 dead
- RTSP 音频会话 `produced=0 pull_timeout=584`（14.4 s 内一帧没出）—— 音频编码器/源未出帧。
  独立 bug，需单独排查（不在本延迟诊断范围）。

## 6. 复现方法

```sh
# 设备侧（devctl over serial，company env）
tools/devctl/devctl bringup                      # SD→WiFi→NFS(noac)
tools/devctl/verify_deploy.sh um                 # md5 校验
tools/devctl/devctl run '> /mnt/huntcam/logs/app.log; \
  HTC_LOG_DEBUG=1 HTC_TEST_NO_POWEROFF=1 HTC_UM_IDLE_TIMEOUT_MS=1800000 \
  LD_LIBRARY_PATH=/mnt/huntcam/lib:$LD_LIBRARY_PATH /mnt/huntcam/bin/um >/dev/null 2>&1 & echo PID=$!'

# 主机侧（同子网，低延迟拉流 ≥10s）
ffmpeg -rtsp_transport tcp -fflags nobuffer -flags low_delay \
  -i rtsp://<device-ip>:8554/ -an -t 12 -f null /dev/null

# 读 Stats（NFS log = 主机 build/logs/app.log）
grep -a "Stats:" build/logs/app.log | grep VIDEO   # 看 fifo depth / produced / consumed
```

## 7. 关联

- [`T32-recording-fps-17-investigation.md`](T32-recording-fps-17-investigation.md) — 录影 fps 根因（本 bug 的"编码器 fps"嫌疑据此排除：720p 预览流编码器实测达标 30fps）
- [`../specs/um-app-spec.md`](../specs/um-app-spec.md) — um 规格
- 代码：`src/media/rtsp/MediaSession.cpp`（FIFO + pacing + Stats）、`src/media/rtsp/RtspServer.cpp:499`（fifoSize=60）
