# RTSP Simu `missing start code` 根因分析

## 背景与目标

现象：

- RTSP 服务启动后，客户端可连上并完成 `OPTIONS/DESCRIBE/SETUP/PLAY`
- 音频正常出包
- 视频从首帧开始持续报：
  - `E/RTSP [VIDEO] Invalid video bitstream (missing start code)`

目标：

- 判断问题是在 RTSP 视频打包链路，还是 simu 视频源本身
- 记录可复现条件、根因和后续建议

## 现状描述

本次分析基于以下文件与运行结果：

- 日志检查点 [src/media/rtsp/rtsp.c](/home/zengping/project/huntcam/code/t32_yb/src/media/rtsp/rtsp.c)
- simu 视频源 [src/hal/simu/SimVideo.cpp](/home/zengping/project/huntcam/code/t32_yb/src/hal/simu/SimVideo.cpp)
- simu 资源配置 [src/hal/simu/res/config.json](/home/zengping/project/huntcam/code/t32_yb/src/hal/simu/res/config.json)
- 运行资源目录 [build_sim/bin/res/config.json](/home/zengping/project/huntcam/code/t32_yb/build_sim/bin/res/config.json)
- 运行资源文件 [build_sim/bin/res/full_frame_camera.h264](/home/zengping/project/huntcam/code/t32_yb/build_sim/bin/res/full_frame_camera.h264)

关键实现点：

- `SimVideoStream::start()` 固定用相对路径 `res/config.json`
- 如果配置读取失败，则回退到相对路径 `src/hal/simu/res/sample_video.h264`
- H.264/H.265 文件未打开成功时，`src_ == nullptr`
- 此时 `buildSampleBitstream()` 不会填充视频数据，后续 `VideoSource::pullData()` 会返回 `size=0` 的空视频帧
- RTSP 侧对空帧执行 `smolrtsp_determine_start_code()` 时自然得到 `NULL`

## 问题与发现

### 1. 当前日志形态更符合“持续空帧”

异常日志从首帧开始连续出现：

- `[VIDEO] Pull result: 0, capture_ts=0`
- 紧接着就是 `[VIDEO] Invalid video bitstream (missing start code)`

这说明：

- RTSP 的 `pull_frame` 回调没有失败
- 但拿到的数据缓冲区不满足 Annex-B 起始码要求
- 如果每一帧都这样，优先怀疑不是偶发码流损坏，而是上游稳定地产出空包或错误包

### 2. `SimVideo` 的资源路径依赖当前工作目录

`SimVideoStream::start()` 中的关键路径逻辑：

- 先尝试打开 `res/config.json`
- 再解析其中的 `h264` 文件名
- 最终拼出相对路径，例如 `res/full_frame_camera.h264`

这段实现没有基于可执行文件路径做解析，而是直接依赖进程当前工作目录。

### 3. 实际资源位于 `build_sim/bin/res/`

本地构建产物中，真实资源目录是：

- [build_sim/bin/res/config.json](/home/zengping/project/huntcam/code/t32_yb/build_sim/bin/res/config.json)
- [build_sim/bin/res/full_frame_camera.h264](/home/zengping/project/huntcam/code/t32_yb/build_sim/bin/res/full_frame_camera.h264)

而不是：

- `build_sim/res/config.json`

所以当进程从 `build_sim` 目录启动时：

- `res/config.json` 实际解析为 `build_sim/res/config.json`
- 文件不存在
- 回退路径 `src/hal/simu/res/sample_video.h264` 也会相对 `build_sim` 解析
- 该路径同样不存在

最终结果就是：

- 视频文件未加载
- simu 持续产生空视频帧
- RTSP 持续报 `missing start code`

### 4. 对照实验已经把根因坐实

使用同一份二进制、同一份配置，仅改变工作目录：

#### 实验 A：`cwd=build_sim`

启动命令：

```bash
CONFIG_FILE=/tmp/t32_rtsp_8554.ini SIM_LOG_DIR=/tmp/t32_rtsp_bad_logs ./bin/htc_main_app -rs
```

结果：

- `PLAY` 后立即反复出现 `Invalid video bitstream (missing start code)`
- `DESCRIBE: video=1 sps_len=0 pps_len=0`

说明：

- 预打开阶段都没拿到有效 SPS/PPS
- 视频源并未正确加载文件

#### 实验 B：`cwd=build_sim/bin`

启动命令：

```bash
CONFIG_FILE=/tmp/t32_rtsp_8554.ini SIM_LOG_DIR=/tmp/t32_rtsp_good_logs ./htc_main_app -rs
```

结果：

- 预打开阶段出现：
  - `Found SPS, length: 30`
  - `Found PPS, length: 4`
  - `Injected SPS/PPS to RTSP: sps=30, pps=4`
- 播放阶段出现：
  - `[VIDEO] First frame: capture_ts=0 us, rtp_ts=0`
- 整个会话中不再出现 `Invalid video bitstream (missing start code)`

说明：

- 同一套代码在 `build_sim/bin` 下工作正常
- 根因不是 RTSP 封包逻辑，而是 simu 资源路径解析依赖 `cwd`

## 结论

本次 `Invalid video bitstream (missing start code)` 的根因是：

- **simulation 模式下 `SimVideo` 使用相对路径加载 `res/config.json` 和视频文件，导致从 `build_sim` 启动时找不到真实资源目录 `build_sim/bin/res/`**

连锁结果是：

1. 视频文件加载失败
2. simu 持续返回空视频帧
3. RTSP 在空帧上检测不到 NAL start code
4. 因而从首帧开始持续报 `missing start code`

## 建议

### 立即可用的规避方式

- simulation 验证时，从 `build_sim/bin` 目录启动 `htc_main_app`

例如：

```bash
cd build_sim/bin
CONFIG_FILE=/abs/path/config.sim.ini ./htc_main_app -rs
```

### 推荐修正方向

由于问题位于 HAL simu 实现，且 `src/hal/**` 有明确 ownership 约束，当前建议先由 PIC 确认后再改。

建议修正内容：

- 让 `SimVideo` 与 `SimAudio` 基于可执行文件路径定位资源目录，而不是依赖当前工作目录
- 或者把资源根目录显式配置为绝对路径
- 同时在文件打开失败时增加明确错误日志，避免后续只在 RTSP 层看到“missing start code”这类次生症状

### 建议的 HAL 变更范围

- [src/hal/simu/SimVideo.cpp](/home/zengping/project/huntcam/code/t32_yb/src/hal/simu/SimVideo.cpp)
- [src/hal/simu/SimAudio.cpp](/home/zengping/project/huntcam/code/t32_yb/src/hal/simu/SimAudio.cpp)

## 附：与重连问题的关系

这次 `missing start code` 与前面“第二次连接无法播放”的问题是两条独立问题链：

- 重连失败问题：已在 RTSP 会话生命周期中修复
- `missing start code`：本质是 simu 资源路径问题，只在当前 simulation 启动方式下触发
