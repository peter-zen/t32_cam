# RTSP 仿真样本与客户端参考

## 1. 目的

汇总当前 `t32_cam` 仓库中与 RTSP 仿真验证直接相关的：
- 默认 simu 资源配置
- 本地样本资产位置
- 常用验证客户端
- 仓库内现有验证脚本
- 已知注意事项与不应误判的点

这篇文档是参考资料 (references)，不是规格本身。

## 2. 当前默认 simu 资源配置

当前 `src/hal/simu/res/config.json` 内容表明：
- audio.g711a = `full_frame_camera_g711a.alaw`
- video.h264 = `full_frame_camera_no_b_30s.h264`
- video.h265 = `sample_video.h265`

也就是说，截至本轮校准，RTSP simu 默认 H.264 样本已经指向：
- `full_frame_camera_no_b_30s.h264`

这点很重要，因为很多旧文档仍以 `full_frame_camera.h264` 作为默认前提；那已经不再稳定成立。

## 3. 样本资产来源与位置

### 3.1 仓库内 local_assets 说明
`local_assets/README.md` 中已记录 RTSP 相关本地资产组织：
- `local_assets/rtsp/video/full_frame_camera.h264`
- `local_assets/rtsp/video/full_frame_camera_no_b_30s.h264`
- `local_assets/rtsp/audio/full_frame_camera_g711a.alaw`

其中：
- `full_frame_camera.h264`
  - 原始 H.264 源，可包含 B 帧
- `full_frame_camera_no_b_30s.h264`
  - no-B 30 秒测试片段
- `full_frame_camera_g711a.alaw`
  - 可选外部音频资源

### 3.2 当前默认样本选择的含义
默认 `config.json` 指向 `full_frame_camera_no_b_30s.h264`，说明当前仿真链路默认更偏向：
- 降低 B-frame 对调试的干扰
- 让 RTSP 首画面、时序和回归行为更容易复现与比较

因此后续如果复现问题时换回 `full_frame_camera.h264`，必须在记录中明确说明，否则测试结论不可直接对比。

## 4. 仿真资源加载路径注意事项

根据现有 RTSP simu 说明文档与当前仓库结构，应记住：
- RTSP 仿真数据最终不是直接由 `tests/assets/configs/rtsp_config.ini` 驱动
- 当前主链路是：
  - `RtspServer`
  - `HalProvider`
  - `SimVideo / SimAudio`
  - `res/config.json`
  - 样本媒体文件

因此当出现“样本明明换了但行为没变”时，应首先检查：
1. 当前运行目录是否正确
2. `build_sim/bin/res/config.json` 是否与源配置一致
3. 样本文件是否已复制/可访问

## 5. 常用客户端

### 5.1 ffplay
仓库多处文档和脚本都把 `ffplay` 作为最常用播放验证客户端。
典型命令：
```bash
ffplay rtsp://localhost:554/live
```

常见变体：
```bash
timeout 10s ffplay -nodisp -autoexit -loglevel info rtsp://localhost:554/live
```

适用场景：
- 快速验证能否正常播放
- 观察首画面是否明显延迟
- 做短时间 headless 播放回归

### 5.2 ffprobe
`ffprobe` 在仓库脚本中大量用于：
- 拉取流信息
- 检查视频/音频 stream 是否存在
- 分析 frame / pts / dts 相关输出

典型命令：
```bash
ffprobe -show_streams -show_format rtsp://localhost:554/live
```

或：
```bash
ffprobe -v info -show_streams -show_frames rtsp://localhost:554/live
```

适用场景：
- 自动化检查是否同时存在 video/audio
- 对比 stream 元信息
- 长时间采集 frame 级日志

### 5.3 VLC
历史分析文档中明确提到 VLC，用于验证：
- 首画面延迟问题
- mixed start code 修复前后的行为差异

虽然仓库脚本主要围绕 FFmpeg 工具链，但 VLC 仍然是高价值对照客户端。
原因很简单：
- 它与 ffplay 的行为并不完全等价
- 某些 RTSP 起播/缓冲问题，只有多客户端对照才有意义

## 6. 仓库内现有验证脚本

当前与 RTSP 仿真验证直接相关的脚本至少包括：
- `script/quick_verify.sh`
- `script/test_rtsp_sync.sh`
- `script/test_av_full.sh`
- `script/test_av_sync_30s.sh`
- `script/test_fifo_30s.sh`
- `script/test_fifo_30s_final.sh`
- `script/test_new_media.sh`
- `script/verify_config.sh`

可按用途粗分：

### 6.1 快速流信息检查
- `script/quick_verify.sh`
  - 用 `ffprobe` 快速看 stream 信息

### 6.2 播放/同步检查
- `script/test_rtsp_sync.sh`
- `script/test_av_full.sh`
- `script/test_av_sync_30s.sh`

### 6.3 FIFO / producer 稳定性检查
- `script/test_fifo_30s.sh`
- `script/test_fifo_30s_final.sh`

### 6.4 资源与配置检查
- `script/test_new_media.sh`
- `script/verify_config.sh`

## 7. 已知注意事项

### 7.1 旧文档里的默认端口并不总一致
仓库旧文档里同时出现过：
- `554`
- `8554`

而当前 `AGENTS.md` 与脚本大量使用：
- `rtsp://localhost:554/live`

所以后续写文档时不要再随手写死 `8554`，除非明确说明那是某一历史阶段或某个特定脚本/测试环境。

### 7.2 旧文档中的默认视频样本前提可能已过时
很多历史文档默认使用：
- `full_frame_camera.h264`

但当前 `src/hal/simu/res/config.json` 默认是：
- `full_frame_camera_no_b_30s.h264`

因此引用历史测试结论时，必须标注样本名，否则结论会漂移。

### 7.3 `--no-audio` 不能当成已确认有效能力
当前代码校准已确认：
- `main_app.cpp` 能解析 `--no-audio`
- 但尚未看到完整的 `RtspServer::enableAudio_` 传递链

所以后续使用该参数做验证时，应明确写成：
- “已解析命令行参数，但当前实现是否真正关闭音频仍需再确认”

## 8. 推荐测试组合

如果只做最小回归，建议至少保留两组：

### 8.1 快速健康检查
- 服务：`./build_sim/bin/htc_main_app -rs`
- 元信息：`ffprobe -show_streams -show_format rtsp://localhost:554/live`
- 播放：`ffplay rtsp://localhost:554/live`

### 8.2 首画面/兼容性检查
- 样本：确认当前 `config.json` 指向的具体 H.264 文件
- 客户端 A：`ffplay`
- 客户端 B：`VLC`

如果只用一个客户端就下结论，很容易误判。

## 9. 后续维护建议

当以下任一项变化时，应优先更新本参考文档：
- 默认 `config.json` 指向的样本变了
- 新增/废弃了常用验证脚本
- 确认 `--no-audio` 已真正生效
- 某个客户端被证明有独特兼容性问题
