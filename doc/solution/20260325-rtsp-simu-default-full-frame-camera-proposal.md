# RTSP Simu 默认测试源切换为 `full_frame_camera.*` 变更提案

## 背景与目标

当前在 `build_sim/bin` 下执行 `./htc_main_app -rs` 时，默认使用的是 `sample_video.h264` / `sample_audio.g711a` 这一套 sample 资源，而不是此前用于音视频同步验证的 `full_frame_camera.*`。

目标：

- 让 `build_sim/bin` 环境下直接执行 `./htc_main_app -rs` 时，默认命中稳定、可追溯的 `full_frame_camera.*` 测试源
- 避免再次依赖手工修改 `build_sim/bin/res/config.json`

## 现状

1. `build_sim/bin/res/config.json` 不是 git 管理文件。
2. 它由 [src/hal/CMakeLists.txt](/home/zengping/project/huntcam/code/t32_yb/src/hal/CMakeLists.txt#L34) 在构建后从 [src/hal/simu/res](/home/zengping/project/huntcam/code/t32_yb/src/hal/simu/res) 自动复制生成。
3. 真正受源码管理的 [src/hal/simu/res/config.json](/home/zengping/project/huntcam/code/t32_yb/src/hal/simu/res/config.json) 从创建开始一直指向 sample 资源。
4. 2026-03-13 的 [SimVideo.cpp](/home/zengping/project/huntcam/code/t32_yb/src/hal/simu/SimVideo.cpp#L73) 还增加了 sample fallback，使“重新看到旧源”更容易发生。

结论：

- 没有证据表明有人把源码里的默认配置从 `full_frame_camera.*` 改回 sample。
- 更可能是之前手工改过 `build_sim/bin/res/config.json`，后续重新编译时被自动覆盖。

## 推荐方案

### 推荐方案 A：恢复为“完整 AV 同步测试源”

范围：

- 修改 [src/hal/simu/res/config.json](/home/zengping/project/huntcam/code/t32_yb/src/hal/simu/res/config.json)
- 修改 [src/media/rtsp/RtspServer.cpp](/home/zengping/project/huntcam/code/t32_yb/src/media/rtsp/RtspServer.cpp)

做法：

1. 在 `config.json` 中把 simu 默认视频源改为 `tests/assets/video/full_frame_camera.h264`
2. 把 simu 默认音频源改为 `tests/assets/audio/full_frame_camera.pcm`
3. 将 simulation 下 RTSP 音频配置从当前的 `G711A / 8000Hz / 320 samples` 调整为与测试资产匹配的 `PCM16 / 16000Hz / mono`

理由：

- 这是最接近你 3 月音视频同步验证时使用链路的方案
- 视频和音频都来自同一组 `full_frame_camera.*`
- 不需要再依赖 `build_sim/bin/res` 里非受管的大文件

影响：

- `build_sim/bin/htc_main_app -rs` 的 simulation 默认音频格式会发生变化
- 相关 RTSP 客户端测试结果会以 `PCM16/16k` 为基准，而不是当前 `PCMA/8k`

### 备选方案 B：只固定视频源

范围：

- 修改 [src/hal/simu/res/config.json](/home/zengping/project/huntcam/code/t32_yb/src/hal/simu/res/config.json)

做法：

1. 默认视频改为 `tests/assets/video/full_frame_camera.h264`
2. 音频仍保持当前 sample `G711A`

理由：

- 变更最小
- 不改变当前 RTSP audio codec 行为

问题：

- 无法完全恢复你当时“音视频同步验证”那套资产和参数
- 只能保证视频源一致，不能保证整套 AV 行为一致

## 推荐结论

推荐采用 **方案 A**。

原因：

- 你的目标不是单纯换一个视频文件，而是恢复“当时用于卡顿和音视频同步验证的那套默认测试源”
- 当前 simulation RTSP 音频参数与 `full_frame_camera.pcm` 不匹配，仅改视频文件不够

## 需要 PIC 确认的 HAL 变更

由于本方案会修改 `src/hal/**`，按当前仓库协作规则，需要 PIC 确认后再落代码。

拟修改点：

- [src/hal/simu/res/config.json](/home/zengping/project/huntcam/code/t32_yb/src/hal/simu/res/config.json)

可能同时修改：

- [src/media/rtsp/RtspServer.cpp](/home/zengping/project/huntcam/code/t32_yb/src/media/rtsp/RtspServer.cpp)

## 验证方式

确认后实施时，建议按以下方式验证：

1. 重新编译 `build_sim`
2. 在 `build_sim/bin` 下执行 `./htc_main_app -rs`
3. 查看启动日志，确认命中的不是 sample 资源
4. 用 `ffprobe` 检查 RTSP 流的分辨率、帧率、音频采样率和 codec
