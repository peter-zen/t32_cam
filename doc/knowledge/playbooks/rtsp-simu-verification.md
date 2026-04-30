# RTSP 仿真验证手册

## 1. 目的

用于在 `BUILD_FOR_SIMULATION=ON` 模式下，对 RTSP 推流能力做快速验证与问题分诊。

## 2. 前置阅读

建议先读：
1. `doc/knowledge/specs/rtsp-streaming-behavior.md`
2. `doc/knowledge/bugs/rtsp-first-frame-delay-mixed-start-code.md`
3. `doc/solution/20260304-rtsp-simu-hal-linkage-guide.md`

## 3. 前提条件

- 已完成仿真构建
- `build_sim/bin/htc_main_app` 可执行
- `build_sim/bin/res/` 下有 simu 资源
- 机器上可用 `ffplay` / `ffprobe` / `ffmpeg` 中的至少一种

典型构建命令：
```bash
mkdir -p build_sim && cd build_sim
cmake -DBUILD_FOR_SIMULATION=ON ..
make -j$(nproc)
```

## 4. 最小验证路径

### 4.1 启动服务
仓库内已有历史脚本可直接利用：
- `script/test_av_full.sh`
- `script/quick_verify.sh`
- `script/test_rtsp_sync.sh`
- `script/test_fifo_30s_final.sh`

如果手动启动：
```bash
./build_sim/bin/htc_main_app -rs
```

### 4.2 客户端连接
最常用：
```bash
ffplay rtsp://localhost:554/live
```

快速查看流信息：
```bash
ffprobe -show_streams -show_format rtsp://localhost:554/live
```

### 4.3 重点观察项
- 是否能正常建连
- 视频是否快速首帧出画
- 是否存在只有音频、视频晚数秒的异常
- 日志中是否反复出现 FIFO 满
- 断开后重连是否恢复正常

## 5. 建议排查顺序

### 5.1 服务根本没起来
先看：
- 端口占用
- 日志中的 `Failed to start RTSP server`
- 仿真资源路径是否正确

### 5.2 有音频无视频或首画面很慢
优先检查：
- 是否命中 mixed start code 相关回归
- 当前测试样本首个 AU 是否包含 IDR
- `rtsp.c` 是否仍保留兼容扫描逻辑

### 5.3 FIFO 持续告警
优先检查：
- 当前是 video 还是 audio FIFO 打满
- 是否是长时间客户端消费不足
- 是否存在新的 pacing 回退
- 是否是异常日志过度放大而非实际功能退化

## 6. 仿真链路理解

当前仿真模式下 RTSP 不直接从 `tests/assets/configs/rtsp_config.ini` 取数据源，而是：
- `RtspServer`
- `HalProvider`
- `SimVideo / SimAudio`
- `res/config.json`
- 样本媒体文件

因此看到 RTSP 问题时，不要只盯 `script/` 或测试配置；要同时检查 HAL 仿真资源路径与 `res/config.json`。

## 7. 当前不应再重复的错误判断

- 不要默认认为“只要有延迟就是 PLAY 时序问题”
- 不要默认认为“FIFO full 一定说明客户端太慢”
- 不要把旧分析文档中的阶段性现象当作当前代码事实

## 8. 推荐后续沉淀

如果某一类问题反复出现，应继续沉淀为：
- `bugs/`：单一已确认问题的根因文档
- `decisions/`：为什么 RTSP 采用当前 startup / FIFO / IDR 策略
- `refs/`：测试样本、客户端兼容性记录
