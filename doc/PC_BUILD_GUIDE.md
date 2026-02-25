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
