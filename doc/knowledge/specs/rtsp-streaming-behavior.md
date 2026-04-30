# RTSP 推流行为规格

## 1. 目的

定义 `t32_cam` 当前 RTSP (Real-Time Streaming Protocol) 推流链路在仓库层面的权威行为描述，覆盖：
- 服务启动与播放启动时序
- 真机与仿真模式下的统一行为
- 音视频媒体会话 (MediaSession) 的启动/停止语义
- FIFO 缓冲与丢帧策略
- 已确认问题与仍待验证项

本规格优先以当前代码行为为依据，而不是历史方案文档的原始表述。

## 2. 当前状态

- 状态：已完成一轮代码校准
- 校准范围：`src/media/rtsp/*`、`src/media/fifo/*`、`src/app/main_app.cpp`、相关 RTSP 历史分析/方案文档
- 仍待验证：
  - 真机端长时间推流稳定性
  - 更多 RTSP 客户端兼容性
  - 混合 start code 兼容修复在更多码流样本上的回归

## 3. 当前权威代码入口

- `src/app/main_app.cpp`
- `src/media/rtsp/RtspServer.h`
- `src/media/rtsp/RtspServer.cpp`
- `src/media/rtsp/MediaSession.h`
- `src/media/rtsp/MediaSession.cpp`
- `src/media/fifo/MediaFIFO.h`
- `src/media/rtsp/rtsp.c`

历史参考但不作为当前行为唯一依据：
- `doc/rtsp_delayed_startup_solution.md`
- `doc/fifo_analysis_and_fix_report.md`
- `doc/solution/20260304-rtsp-simu-hal-linkage-guide.md`
- `doc/analysis/20260326-rtsp-simu-first-frame-delay-root-cause-and-review.md`

## 4. 服务启动与播放启动规则

### 4.1 命令入口
当前主入口支持通过 `htc_main_app -rs` 启动 RTSP 服务；命令解析在 `src/app/main_app.cpp`。

### 4.2 服务启动阶段
当前 `RtspServer::start_internal()` 的行为不是“完全空转等待客户端后再准备视频”，而是：
1. `videoSession_` 必须已经可用，否则启动失败
2. 服务启动前会先短暂启动 `videoSession_`，尝试从视频流中预提取 SPS/PPS 以写入 SDP
3. 预提取结束后会停止该 `videoSession_`
4. 随后创建 RTSP server、注册 pull/release/onSessionPlay/onSessionClosed 回调并启动监听

因此，历史文档中“服务器启动时完全不启动任何媒体会话”的说法已经不准确。
当前更准确的描述是：
- 服务启动时会进行一次有限的 video preopen，用于获取 SPS/PPS
- 正式持续推流要等到客户端 `PLAY` 触发 `onSessionPlay()` 后才开始

### 4.3 播放启动阶段
当客户端进入播放阶段，`rtsp.c` 会回调 `RtspServer::onSessionPlay()`：
- 设置 `streamingEnabled_ = true`
- 若 video/audio session 缺失，会尝试重新初始化
- 若 session 存在但未运行，则调用 `start()`
- video session 启动后会调用 `requestIDR()` 请求关键帧

这说明当前正式推流起点是 `onSessionPlay()`，而不是第一次 `pullFrame()`。
因此，旧文档中“通过 pullFrame 首次调用检测客户端连接并延迟启动 session”的描述已过时。

### 4.4 会话关闭阶段
RTSP 会话关闭时，会通过 `onSessionClosed()` 参与清理；上层应用还可通过 `registerOnsessionClosedCallback()` 注册附加回调。
当前仓库中 `main_app.cpp` 已注册 session closed log callback，用于记录重新等待连接的状态。

## 5. 媒体会话规则

### 5.1 Video session
当前 `initVideo()`：
- 通过 `hal::HalProvider::createVideo()` 获取 HAL video
- 创建 video stream
- 配置为：
  - `H264`
  - `1280x720`
  - `30fps`
  - `CBR`
- 用 `VideoSource` 封装后生成 `MediaSession`
- 默认 FIFO 大小为 60

### 5.2 Audio session
当前 `initAudio()`：
- 通过 `hal::HalProvider::createAudio()` 获取 HAL audio
- 配置为：
  - `G711A / PCMA`
  - `8000Hz`
  - `mono`
  - `320 samples/frame`
  - 约 40ms/frame
- 根据 frame duration 计算 polling timeout
- 用 `AudioSource` 封装后生成 `MediaSession`
- 默认 FIFO 大小为 80

### 5.3 真机与仿真的统一抽象
`RtspServer` 不直接区分文件源还是硬件源，而是统一经由 HAL (Hardware Abstraction Layer, HAL)：
- 仿真模式：`SimVideo / SimAudio`
- 真机模式：`IngenicVideo / IngenicAudio`

仓库层产品语义是：
- RTSP 只依赖统一媒体源接口
- 具体数据来自 simu 还是硬件，由 HAL 提供者决定

## 6. FIFO 与生产消费规则

### 6.1 生产者线程
`MediaSession::start()` 会：
- `source_->open()`
- 初始化统计与 session 类型
- 启动 producer thread

producer loop 会按媒体类型决定 pacing：
- video：根据 `videoFrameRate` 推导 pacing interval
- audio：根据 `audioFrameDurationUs` 推导 pacing interval

这意味着当前实现已经不是历史文档里描述的“完全无节制生产”。
历史分析里关于“3265fps 无限制生产”的结论，反映的是旧问题阶段，不代表当前代码状态。

### 6.2 视频丢帧策略
当前 video FIFO 满时不是简单“丢当前帧”，而是更复杂的 GOP 感知策略：
- 普通情况下，若新来的 P 帧无法入队，会丢弃该 P 帧并进入 droppingGop 状态
- droppingGop 状态下，直到遇到下一关键帧才恢复尝试入队
- 若关键帧到来但 FIFO 已满，会优先删除队列中最老的非关键帧
- 如果队列里只剩关键帧，则再强制丢弃最老关键帧

因此当前准确规则是：
- video FIFO 目标是优先保留关键帧与较新 GOP，避免阻塞生产者
- 其实现不是简单的一句“drop old”，而是带关键帧优先级的丢弃策略

### 6.3 音频丢帧策略
audio 路径更简单：
- FIFO 满时直接丢弃新音频帧
- 不阻塞生产者
- 通过限频日志报告 FIFO 满与 drop 统计

因此不要把 video 的 GOP-aware drop 规则套用到 audio。

### 6.4 消费者超时
`MediaSession::start()` 会依据媒体类型计算 consumer pop timeout：
- audio：通常是 frameMs 一半，范围 5~20ms
- video：通常是 frameMs 一半，范围 3~12ms

这是当前生产者/消费者解耦的一部分，不应再用旧文档中的固定经验值替代。

## 7. 首画面与码流兼容规则

根据历史分析与当前收敛结论，已确认：
- RTSP 曾存在对同一 AU (Access Unit) 内混合 `3-byte` / `4-byte` start code 的识别问题
- 该问题会导致首帧虽然包含 IDR，但服务端未能在首个 AU 内正确识别到 IDR，从而出现“声音很快出来、画面数秒后才出现”的现象
- 当前修复核心在 `rtsp.c` 的 mixed start code 兼容扫描逻辑

因此当前产品级规则应写成：
- RTSP 服务必须能正确处理首个视频 AU 内混合 `3-byte` / `4-byte` start code 的 H.264 码流
- 启动门控不能错误跳过首个已含 IDR 的 AU

不要再把该问题表述成“PLAY 时序就是最终根因”；那是错误归因。

## 8. 当前明确约束

- `RtspServer` 当前默认视频编码按 `H264` 配置；尽管 `start_internal()` 兼容 `H265` 参数选择，当前 `initVideo()` 默认配置仍是 H264
- 音频是否启用由 `enableAudio_` 与 `audioSession_` 是否存在共同决定
- 上层 `main_app.cpp` 可以在 mobile 模式与独立 `-rs` 模式下启动 RTSP
- RTSP 与 mDNS 端口存在配置关联，`main_app.cpp` 会从配置中取 `rtsp_port`

## 9. 仍待验证项

以下内容暂不写成已确认产品事实：
- 真机端断连重连后的资源释放是否在所有工作模式都完全一致
- 所有客户端对当前 mixed start code 修复后的兼容性是否一致
- H265 在当前默认路径下是否被完整长期验证
- `--no-audio` 参数在所有启动路径上是否都完整传递到 `RtspServer`

## 10. 推荐后续拆分

如果后续继续治理 RTSP 文档，应进一步拆成：
- `decisions/rtsp-session-startup-model.md`
- `bugs/rtsp-first-frame-delay-mixed-start-code.md`
- `playbooks/rtsp-simu-verification.md`

当前这篇规格先承担统一入口作用，避免后续继续从旧 `doc/` 散读。