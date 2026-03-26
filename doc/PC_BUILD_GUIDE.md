# PC 模拟版本编译指南

## 快速开始

```bash
# 1. 配置 (首次或修改 CMakeLists.txt 后)
cmake -DBUILD_FOR_SIMULATION=ON -B build_sim -S .

# 2. 编译
cmake --build build_sim -j$(nproc)

# 3. 运行
./build_sim/bin/htc_main_app -h
./build_sim/bin/htc_main_app -rs
```

## 详细说明

### 编译选项

| 选项 | 说明 |
|------|------|
| `-DBUILD_FOR_SIMULATION=ON` | 启用 PC 模拟模式 (必须) |
| `-B build_sim` | 指定构建目录 |
| `-S .` | 指定源码目录 |

### 输出文件

编译完成后，可执行文件位于：
```
build_sim/
├── bin/
│   ├── htc_main_app      # 主程序
│   ├── htc_media_app     # 媒体程序
│   ├── htc_daemon_app    # 守护程序
│   └── test_http_server  # HTTP 服务器测试程序
└── lib/
    └── *.so              # 动态库
```

### 运行前准备

PC 模拟版本使用相对路径，需要在项目根目录下运行：

```bash
# 确保在项目根目录
cd /path/to/t32

# 运行程序
./build_sim/bin/htc_main_app -rs

# 查看rtsp端口占用
sudo ss -tlnp | grep 554

# 结束程序
sudo pkill -9 htc_main_app
```

配置文件位置：
- `./res/env.ini` - 环境配置
- `./res/config.ini` - 设备配置
- `./sim_sdcard/` - 模拟 SD 卡目录 (自动创建)

## 可用命令

```bash
# 查看帮助
./build_sim/bin/htc_main_app -h

# 启动 RTSP 服务器
./build_sim/bin/htc_main_app -rs

# 启动移动网络模式 (含 HTTP Server)
./build_sim/bin/htc_main_app -m

# 读取 RTC 时间
./build_sim/bin/htc_main_app -grtc

# 拍照测试
./build_sim/bin/htc_main_app -s

# HTTP Server 独立测试
./build_sim/bin/test_http_server
```

## 使用 No-B RTSP 测试源

如果要对比 Android RTSP 客户端在 `B-frame` / `no-B-frame` 下的表现，建议使用仓库内辅助脚本切换 simulation 视频源：

```bash
# 先准备本地大资源目录（不纳入 Git）
mkdir -p local_assets/rtsp/video local_assets/rtsp/audio

# 放入原始视频源
# local_assets/rtsp/video/full_frame_camera.h264

# 可选：放入外部音频源
# local_assets/rtsp/audio/full_frame_camera_g711a.alaw

# 生成 30 秒 no-B H.264 片段，并切换 build_sim/bin/res/config.json 到该源
./script/use_rtsp_no_b_source.sh

# 从 build_sim/bin 启动，确保 simulation 资源路径正确
cd build_sim/bin
./htc_main_app -rs
```

恢复默认测试源：

```bash
./script/use_rtsp_no_b_source.sh --restore-default
```

说明：

- 大体积音视频资源统一放在 [local_assets/README.md](/home/zengping/project/huntcam/code/t32_yb/local_assets/README.md) 约定的本地目录
- 该目录默认被 `.gitignore` 忽略，不纳入版本控制
- 当前脚本采用 fail-fast 策略：缺失原始 H.264 源时直接失败，不再静默回退到仓库内 sample 视频

## PC 模拟特性

| 功能 | PC 模拟行为 |
|------|-------------|
| GPIO | 内存模拟，打印操作日志 |
| SD 卡挂载 | 创建本地目录 `./sim_sdcard/` |
| poweroff/reboot | 只打印日志，不执行 |
| RTC | 使用系统时间 |
| MCU (I2C) | 返回模拟数据 |
| 视频编码 | SDK stub 模拟 |

## 清理重建

```bash
# 清理构建目录
rm -rf build_sim

# 重新配置和编译
cmake -DBUILD_FOR_SIMULATION=ON -B build_sim -S .
cmake --build build_sim -j$(nproc)
```

## 与真机编译对比

| 项目 | PC 模拟 | 真机 |
|------|---------|------|
| 命令 | `cmake -DBUILD_FOR_SIMULATION=ON ...` | `cmake ...` |
| 构建目录 | `build_sim/` | `build/` |
| 工具链 | 本机 GCC | MIPS 交叉编译器 |
| 配置路径 | `./res/` | `/config/htc/` |
| SD 卡路径 | `./sim_sdcard/` | `/mnt/sdcard/` |
