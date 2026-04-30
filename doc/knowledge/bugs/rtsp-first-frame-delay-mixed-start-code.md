# RTSP 首画面延迟：mixed start code 根因

## 1. 现象

在 simu 环境中，客户端连接后：
- 音频几乎立即开始
- 视频首画面约数秒后才出现

该现象在历史分析中已通过 VLC / ffmpeg 等复现。

## 2. 已确认根因

最终根因不是 `PLAY` 时序本身，也不是“首帧一定不是 IDR”。

已确认根因是：
- `rtsp.c` 旧版 NAL 遍历逻辑对同一 AU (Access Unit) 内混用 `3-byte` / `4-byte` start code 的 H.264 码流兼容不足
- 结果导致首帧里明明已经存在 IDR，但 RTSP 启动门控没在首个 AU 内识别出来
- 客户端于是等到后续 GOP 的关键帧才真正出画

## 3. 证据来源

主要参考：
- `doc/analysis/20260326-rtsp-simu-first-frame-delay-root-cause-and-review.md`
- 当前 `src/media/rtsp/rtsp.c`

历史分析中已经记录：
- 首帧 AU 中包含 SEI / SPS / PPS / IDR
- 其中起始码混用了 `00 00 00 01` 与 `00 00 01`
- 旧扫描逻辑只沿用首个推断出的 start code tester，导致后续不同长度的起始码未被识别

## 4. 当前修复结论

必须保留的核心修复是：
- `rtsp.c` 中 mixed start code 兼容扫描

可作为增强但不是唯一根因修复的内容：
- 启动期整帧 IDR 预判
- 对 non-VCL prefix NAL 的更合理放行
- deferred PLAY 之类的协议鲁棒性改动

## 5. 工程结论

后续如果再次出现“音频很快出、视频明显晚出”的问题，不要先把锅甩给播放器或网络。
正确第一检查项应是：
1. 当前码流首个 AU 是否已包含 IDR
2. AU 内是否混用了不同长度的 start code
3. `rtsp.c` 当前扫描逻辑是否被回退或被新改动破坏

## 6. 回归建议

至少覆盖：
- simu 默认测试源
- 首帧含 mixed start code 的样本
- ffmpeg / ffplay / VLC 三类客户端中的至少两类

## 7. 状态

- 已纳入当前 RTSP 规格治理
- 后续如需更细粒度过程记录，应继续补充实测日志与回归样本说明
