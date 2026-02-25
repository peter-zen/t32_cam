# 日志系统文档

## 概述

本项目使用 [EasyLogger](https://github.com/armink/EasyLogger) 作为日志系统，支持毫秒级时间戳、多输出目标、线程安全等特性。同时保留了原有 Logger 类的兼容层，支持渐进式迁移。

## 架构

```
┌─────────────────────────────────────────────────────────────────┐
│                      应用层 (Application)                        │
│  ┌──────────────────┐    ┌──────────────────────────────────┐  │
│  │  旧代码           │    │  新代码                          │  │
│  │  Logger::log()   │    │  elog_i(), elog_d(), elog_e()    │  │
│  └────────┬─────────┘    └────────────────┬─────────────────┘  │
│           │                               │                     │
│           ▼                               │                     │
│  ┌──────────────────┐                     │                     │
│  │ 兼容层            │                     │                     │
│  │ (Logger.h/cpp)   │─────────────────────┤                     │
│  └────────┬─────────┘                     │                     │
└───────────┼───────────────────────────────┼─────────────────────┘
            │                               │
            ▼                               ▼
┌─────────────────────────────────────────────────────────────────┐
│                    EasyLogger 核心层                             │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────────┐ │
│  │ 日志过滤     │  │ 格式化器     │  │ 输出管理器              │ │
│  │ (级别/标签)  │  │ (时间/文件)  │  │ (终端/文件)             │ │
│  └─────────────┘  └─────────────┘  └─────────────────────────┘ │
└─────────────────────────────────────────────────────────────────┘
            │
            ▼
┌─────────────────────────────────────────────────────────────────┐
│                    平台移植层 (elog_port.c)                      │
│  - elog_port_get_time()    毫秒级时间戳                         │
│  - elog_port_output()      终端/文件输出                        │
│  - elog_port_output_lock() 线程安全互斥锁                       │
└─────────────────────────────────────────────────────────────────┘
```

## 目录结构

```
third_party/easylogger/
├── CMakeLists.txt           # CMake 构建配置
├── inc/
│   ├── elog.h               # EasyLogger 主头文件
│   └── elog_cfg.h           # 配置文件
├── src/
│   ├── elog.c               # 核心实现
│   └── elog_utils.c         # 工具函数
└── port/
    └── elog_port.c          # 平台移植层

src/logger/
├── Logger.h                 # 兼容层头文件
├── Logger.cpp               # 兼容层实现
├── ElogInit.h               # 初始化接口
└── ElogInit.cpp             # 初始化实现
```

## 日志级别

| 级别 | EasyLogger 宏 | 旧 API | 说明 |
|------|---------------|--------|------|
| ASSERT | `elog_a()` | - | 断言失败 |
| ERROR | `elog_e()` | `LogLevel::ERROR` | 错误 |
| WARN | `elog_w()` | `LogLevel::WARNING` | 警告 |
| INFO | `elog_i()` | `LogLevel::INFO` | 信息 |
| DEBUG | `elog_d()` | `LogLevel::DEBUG` | 调试 |
| VERBOSE | `elog_v()` | `LogLevel::VERBOSE` | 详细 |

## 使用方法

### 1. 初始化

项目中 EasyLogger 在 `main_app.cpp` 的 `main()` 函数开始处初始化：

```cpp
#include "ElogInit.h"

int main(int argc, char* argv[])
{
    // ... 环境变量解析
    
#ifdef BUILD_FOR_SIMULATION
    // PC 模拟环境：启用终端和文件日志
    ElogConfig elog_config;
    elog_config.enableTerminal = true;
    elog_config.enableFile = true;
    elog_config.logFilePath = "sim_sdcard/log/app.log";
    elog_config.logLevel = ELOG_LVL_DEBUG;
    if (!elog_init_with_config(elog_config)) {
        fprintf(stderr, "Failed to initialize EasyLogger\n");
    }
#else
    // 真机环境：仅终端输出
    if (!elog_init_default()) {
        fprintf(stderr, "Failed to initialize EasyLogger\n");
    }
#endif
    
    // ... 应用代码
}
```

### 2. 模拟环境日志文件

在 PC 模拟环境 (`BUILD_FOR_SIMULATION`) 下：
- 终端输出：启用
- 文件输出：启用，保存到 `sim_sdcard/log/app.log`
- 日志级别：DEBUG（输出所有级别）

日志文件位置：
```
sim_sdcard/
└── log/
    └── app.log    # 日志文件
```

### 3. 新代码 - 使用 EasyLogger 原生 API（推荐）

```cpp
#include <elog.h>

// 定义模块标签
#define LOG_TAG "RTSP"

void example() {
    // 使用 elog_x(tag, format, ...) 格式
    elog_i("RTSP", "Server started on port %d", 8554);
    elog_d("RTSP", "Client connected, session_id=%s", session_id);
    elog_e("RTSP", "Failed to bind socket: %s", strerror(errno));
    
    // 或使用 log_x(...) 简写（需要先定义 LOG_TAG）
    log_i("Server started");
    log_d("Processing frame %d", frame_num);
}
```

### 4. 旧代码 - 兼容层（逐步迁移）

旧代码无需修改即可编译运行，但会产生 deprecated 警告：

```cpp
#include "Logger.h"

void legacy_code() {
    // 旧 API 仍然可用，但建议迁移
    Logger::log(LogLevel::INFO, "This is a legacy log");
    Logger::log(LogLevel::DEBUG, "Value: %d", value);
    Logger::setLogLevel(LogLevel::DEBUG);
}
```

### 4. 运行时配置

```cpp
// 设置日志级别过滤
elog_set_filter_lvl(ELOG_LVL_DEBUG);

// 启用/禁用终端输出
elog_set_terminal_output(true);

// 设置日志文件
elog_set_file_output("/tmp/debug.log");
```

## 日志输出格式

```
I/RTSP            [2026-01-14 13:55:04.113] Client connected
│ │                │                        │
│ │                │                        └── 日志消息
│ │                └── 毫秒级时间戳
│ └── 标签 (TAG)
└── 级别 (I=INFO, D=DEBUG, E=ERROR, W=WARN, V=VERBOSE)
```

## 迁移指南

将旧代码迁移到 EasyLogger 原生 API：

| 旧 API | 新 API |
|--------|--------|
| `Logger::log(LogLevel::INFO, "msg")` | `elog_i("TAG", "msg")` |
| `Logger::log(LogLevel::DEBUG, "x=%d", x)` | `elog_d("TAG", "x=%d", x)` |
| `Logger::log(LogLevel::ERROR, "err")` | `elog_e("TAG", "err")` |
| `Logger::log(LogLevel::WARNING, "warn")` | `elog_w("TAG", "warn")` |
| `Logger::setLogLevel(LogLevel::DEBUG)` | `elog_set_filter_lvl(ELOG_LVL_DEBUG)` |

## 配置选项

编辑 `third_party/easylogger/inc/elog_cfg.h` 可修改：

- `ELOG_OUTPUT_LVL` - 编译时最大输出级别
- `ELOG_LINE_BUF_SIZE` - 单行日志缓冲区大小
- `ELOG_COLOR_ENABLE` - 终端颜色输出
- `ELOG_FMT_USING_FUNC` - 输出函数名
- `ELOG_FMT_USING_LINE` - 输出行号

## 线程安全

EasyLogger 通过 pthread_mutex 实现线程安全，多线程环境下可安全使用。
