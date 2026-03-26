# RTSP Simu 测试源排查

## 背景与目标

排查 2026-03-25 在 PC simu 环境下启动 RTSP 服务时，实际播放的仍像是旧测试视频源，而不是 3 月音视频同步测试时使用的新源。

目标：

- 明确当前 `-rs` 启动链路到底从哪里加载视频/音频源
- 判断是否与启动方式有关
- 给出最可能的根因

## 现状描述

当前 RTSP simu 启动链路为：

1. `src/app/main_app.cpp` 启动 `RtspServer`
2. `src/media/rtsp/RtspServer.cpp` 在 simulation 模式下通过 `HalProvider` 创建 `SimVideo` / `SimAudio`
3. `src/hal/simu/SimVideo.cpp` 和 `src/hal/simu/SimAudio.cpp` 在 `start()` 中读取相对路径 `res/config.json`

关键事实：

- 当前 RTSP 模块不再直接读取 `tests/assets/configs/rtsp_config.ini`
- `tests/assets/configs/rtsp_config.ini` 自 2026-02-25 创建后，没有在 3 月被更新
- 2026-03-13 的 `ea9af02 fix(sim): add fallback sample assets and gpio link` 给 `SimVideo.cpp` 增加了 fallback 逻辑

## 问题与发现

### 1. `rtsp_config.ini` 不是当前 `-rs` 启动的主配置入口

代码位置：

- `src/media/rtsp/RtspServer.cpp`
- `src/hal/simu/SimVideo.cpp`
- `src/hal/simu/SimAudio.cpp`

当前 `RtspServer` 走 HAL 模拟流，不读取 `tests/assets/configs/rtsp_config.ini`。因此即使当时为了音视频同步测试替换过 `tests/assets/*`，今天手工启动 `./build_sim/bin/htc_main_app -rs` 也不一定会用到那套资源。

### 2. 测试源依赖当前工作目录

`SimVideo.cpp` / `SimAudio.cpp` 将配置路径写死为：

```cpp
std::string config_path = "res/config.json";
```

这是相对当前工作目录解析，不是相对可执行文件路径解析。

因此不同启动方式命中不同路径：

- 仓库根目录启动：`./build_sim/bin/htc_main_app -rs`
- `build_sim/` 目录启动：`cd build_sim && ./bin/htc_main_app -rs`
- `build_sim/bin/` 目录启动：`cd build_sim/bin && ./htc_main_app -rs`

三种方式会读到不同的 `res/config.json`，甚至读不到。

### 3. 根目录启动时，会落到 fallback 的旧 sample 资源

仓库根目录下不存在：

- `./res/config.json`
- `./res/sample_video.h264`

但存在：

- `./src/hal/simu/res/sample_video.h264`

所以从仓库根目录启动 `./build_sim/bin/htc_main_app -rs` 时，会因为找不到 `res/config.json`，进入 `SimVideo.cpp` 中 2026-03-13 增加的 fallback：

- `src/hal/simu/res/sample_video.h264`
- `src/hal/simu/res/sample_video.h265`
- `src/hal/simu/res/sample_image.jpeg`

这解释了为什么今天会看到像是旧测试源的内容。

### 4. `build_sim/bin/res/config.json` 实际指向的也不是 `full_frame_camera`

当前 `build_sim/bin/res/config.json` 内容仍是：

- `sample_video.h264`
- `sample_video.h265`
- `sample_audio.g711a`
- `sample_audio.g711u`

并不是：

- `tests/assets/video/full_frame_camera.h264`
- `tests/assets/audio/full_frame_camera.pcm`

所以即使从 `build_sim/bin` 启动，命中的也还是 sample 资源，而不是 3 月用于同步验证的 `full_frame_camera.*`。

### 5. `tests/assets/rtsp_config.ini` 与 sample 资源不是同一套

文件大小对比：

- `src/hal/simu/res/sample_video.h264`：6261762 bytes
- `build_sim/bin/res/sample_video.h264`：6261762 bytes
- `tests/assets/video/full_frame_camera.h264`：191880566 bytes
- `tests/assets/video/test.h264`：35390748 bytes

可以确认当前 sample 资源与 `full_frame_camera.h264` 不是同一个文件。

## 结论与建议

结论：

- 你今天看到的“还是之前的测试视频源”，大概率不是启动错了 RTSP 功能本身，而是启动时命中了 simu HAL 的 sample/fallback 资源链路。
- 3 月你做音视频同步验证时使用的新源，更像是通过测试脚本和 `tests/assets/*` 组织起来的文件源测试路径，不是今天手工 `-rs` 启动默认命中的路径。
- 当前 simu RTSP 的测试源选择对 cwd 非常敏感，存在明显的路径设计问题。

建议：

1. 如果要复现 3 月的同步测试，先确认当时使用的是哪个脚本，以及脚本是否有复制 `tests/assets/*` 到运行目录。
2. 如果希望 `-rs` 手工启动稳定命中新测试源，应统一 `SimVideo.cpp` / `SimAudio.cpp` 的配置路径，改为基于可执行文件目录或 `projectRootPath`。
3. 如果要快速验证当前命中的源，优先检查：
   - 当前 shell 的 cwd
   - 是否存在 `res/config.json`
   - `build_sim/bin/res/config.json` 指向的文件名
   - 是否落入 `src/hal/simu/res/sample_video.h264` fallback
