# RTSP 会话启动模型决策

## 1. 决策主题

为 `t32_cam` 的 RTSP 推流链路明确当前采用的会话启动模型，以及为什么不是以下两种极端方案：
- 方案 A：服务一启动就持续打开媒体源并常驻生产
- 方案 B：直到第一次 `pullFrame()` 才真正准备所有媒体链路

当前仓库实际采用的是折中模型：
- 初始化阶段先建立 video/audio session 对象
- 服务启动前对 video 做一次有限 preopen，用于提取 SPS/PPS 写入 SDP
- 正式持续推流由 RTSP `PLAY` 流程触发的 `onSessionPlay()` 启动
- 会话关闭时停止 session 生产，但尽量复用已存在的 session 对象，等待后续重连

## 2. 证据基础

本决策不是从旧方案文档推导，而是基于当前代码：
- `src/media/rtsp/RtspServer.cpp`
- `src/media/rtsp/MediaSession.cpp`
- `src/media/rtsp/VideoSource.cpp`
- `src/media/rtsp/AudioSource.cpp`
- `src/media/rtsp/rtsp.c`

## 3. 已放弃的极端方案

### 3.1 方案 A：服务启动即常驻推流准备
旧问题阶段，这种模式会带来明显副作用：
- 无客户端时持续占用设备/文件源
- producer thread 持续运行
- FIFO 容易在无消费时堆积、告警、丢帧
- 真机场景下不符合“按需激活摄像头/音频”的嵌入式预期

因此该方案不适合作为长期模型。

### 3.2 方案 B：完全等到首次 `pullFrame()` 再启动所有媒体会话
这个方案听起来最省资源，但当前仓库并未采用，主要原因是：
- RTSP 服务在正式播放前就需要尽早准备 SDP 所需的 SPS/PPS
- 若完全不预开 video，会把首包准备、参数提取、关键帧等待等责任压到更晚阶段
- 历史实现也已演化 away from “pullFrame first-call lazy start”

所以它也不是当前权威模型。

## 4. 当前采用的模型

### 4.1 初始化阶段
`RtspServer` 构造时会调用 `initialize()`：
- `initVideo()`
- 若启用音频则 `initAudio()`

这一步已经建立：
- HAL stream 封装
- `VideoSource` / `AudioSource`
- `MediaSession` 对象

注意：
- 这里建立的是 session 对象与底层 stream 封装
- 不是说 producer thread 已经常驻运行

### 4.2 服务启动前的 video preopen
`start_internal()` 中当前有一个关键设计：
- 若 `videoSession_` 存在，则先 `videoSession_->start()`
- 在最多约 500ms 的窗口内拉取视频数据
- 尝试 `extractSpsPps()`
- 成功后把 SPS/PPS 写入 RTSP server 参数
- 然后 `videoSession_->stop()`

这个 preopen 只对 video 生效，目的非常明确：
- 让 SDP 在服务对外可用前就有更完整的视频参数
- 降低后续客户端起播阶段的首包不确定性

因此当前设计不是“完全懒加载”，而是“有限预热 + 正式按需启动”。

### 4.3 正式推流启动
正式播放并不是由 `pullFrame()` 首次调用触发，而是：
- `rtsp.c` 在 `PLAY` 处理链路中使用 deferred play
- 随后触发 `FUNC_ID_ON_SESSION_PLAY`
- `RtspServer::onSessionPlay()` 负责：
  - 标记 `streamingEnabled_ = true`
  - 若 session 缺失则重新初始化
  - 若 session 未运行则调用 `start()`
  - video session 启动后主动 `requestIDR()`

这个模型的意义是：
- 把“真正开始连续推流”的职责绑定到 RTSP 协议语义上的 `PLAY`
- 而不是绑定到某个底层 frame callback 的偶然首次调用

### 4.4 会话关闭
当前 `onSessionClosed()` 做的不是彻底摧毁 session 对象，而是：
- 停止 video/audio session
- 清掉 `streamingEnabled_`
- 保留 session 对象，以便下次 `PLAY` 时复用

这是一种“停生产、不必全量重建对象”的折中模型。

## 5. 为什么当前模型合理

### 5.1 兼顾 SDP 准备与按需推流
如果没有 preopen：
- SPS/PPS 准备会更晚
- 启播阶段不确定性更大

如果全程常驻生产：
- 无客户端时浪费资源
- 更容易引入空转与 FIFO 噪声

当前模型通过“服务启动时有限 preopen + PLAY 时正式启动”平衡了两者。

### 5.2 更贴近 RTSP 语义
RTSP 协议层真正的播放开始应绑定 `PLAY`，而不是绑定内部某个 pull callback。
因此 `onSessionPlay()` 作为正式启动点，比“首次 `pullFrame()` 触发启动”更清晰、更可维护。

### 5.3 便于重连
当前会话关闭后只停止 producer，而不是每次都彻底销毁对象：
- 简化重连路径
- 保持对象结构稳定
- 让下次 `PLAY` 可直接 restart 现有 session，必要时再补 reinit

## 6. 当前模型的代价

这个模型不是零代价，也需要明确承认：
- 服务启动时依然会对 video 做一次短暂 preopen，不是绝对零资源占用
- `onSessionClosed()` 选择 stop 而非完全 reset，对“彻底释放到底层资源”的语义不如完全销毁那样直接
- audio 开关语义还不够干净：`main_app.cpp` 能解析 `--no-audio`，但从当前代码校准看，并未看到明确传递到 `RtspServer::enableAudio_` 的完整链路

所以当前模型是“工程上合理的折中”，不是“理论上最纯粹”的方案。

## 7. 决策结论

当前仓库 RTSP 的正式启动模型定义为：
1. 初始化时建立 session 与 source 封装
2. 服务启动前允许 video 做短暂 preopen，以提取 SPS/PPS
3. 正式持续推流由 `PLAY -> onSessionPlay()` 启动
4. 会话关闭时停止 producer，保留可复用 session 对象，等待重连

后续若文档或代码再出现下面两种说法，应视为不准确：
- “RTSP 服务器现在完全等首次 pullFrame 才启动媒体”
- “RTSP 服务器一启动就一直常驻生产媒体流”

这两种都不是当前实际模型。

## 8. 后续建议

- 若要进一步收口，可补一篇 `refs/rtsp-simu-sample-assets-and-clients.md`
- 若后续决定真正支持 `--no-audio`，应把命令行解析到 `RtspServer` 的传参链补完整，并同步更新 spec 与本决策文档
