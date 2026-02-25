# Design Document: EasyLogger Integration

## Overview

本设计文档描述了将 EasyLogger 日志库集成到 HTC 固件项目的技术方案。设计目标是：
1. 将 EasyLogger 作为第三方库集成到现有构建系统
2. 实现跨平台移植层（PC x86_64 和 Ingenic T32 MIPS）
3. 提供毫秒级时间戳支持
4. 保持与现有 Logger API 的兼容性，支持渐进式迁移

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                      Application Layer                          │
│  ┌──────────────────┐    ┌──────────────────────────────────┐  │
│  │  Legacy Code     │    │  New Code                        │  │
│  │  Logger::log()   │    │  elog_i(), elog_d(), elog_e()    │  │
│  └────────┬─────────┘    └────────────────┬─────────────────┘  │
│           │                               │                     │
│           ▼                               │                     │
│  ┌──────────────────┐                     │                     │
│  │ Compatibility    │                     │                     │
│  │ Layer (Logger.h) │─────────────────────┤                     │
│  └────────┬─────────┘                     │                     │
│           │                               │                     │
└───────────┼───────────────────────────────┼─────────────────────┘
            │                               │
            ▼                               ▼
┌─────────────────────────────────────────────────────────────────┐
│                    EasyLogger Kernel                            │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────────┐ │
│  │ Log Filter  │  │ Formatter   │  │ Output Manager          │ │
│  │ (Level/Tag) │  │ (Time/File) │  │ (Terminal/File)         │ │
│  └─────────────┘  └─────────────┘  └─────────────────────────┘ │
└─────────────────────────────────────────────────────────────────┘
            │
            ▼
┌─────────────────────────────────────────────────────────────────┐
│                    Platform Port Layer                          │
│  ┌─────────────────────────┐  ┌─────────────────────────────┐  │
│  │  PC Simulation Port     │  │  Target Hardware Port       │  │
│  │  (POSIX APIs)           │  │  (Ingenic T32 APIs)         │  │
│  │  - gettimeofday()       │  │  - gettimeofday()           │  │
│  │  - pthread_mutex        │  │  - pthread_mutex            │  │
│  │  - printf()             │  │  - printf()                 │  │
│  └─────────────────────────┘  └─────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

## Components and Interfaces

### 1. EasyLogger 核心库

EasyLogger 源码放置在 `third_party/easylogger/` 目录：

```
third_party/easylogger/
├── CMakeLists.txt           # CMake 构建配置
├── inc/
│   ├── elog.h               # 主头文件
│   └── elog_cfg.h           # 配置头文件（项目定制）
├── src/
│   └── elog.c               # 核心实现
└── port/
    └── elog_port.c          # 平台移植层实现
```

### 2. 移植层接口

```c
// elog_port.c - 需要实现的移植接口

/**
 * EasyLogger port initialize
 * @return result
 */
ElogErrCode elog_port_init(void);

/**
 * EasyLogger port deinitialize
 */
void elog_port_deinit(void);

/**
 * output log port interface
 * @param log output of log
 * @param size log size
 */
void elog_port_output(const char *log, size_t size);

/**
 * output lock
 */
void elog_port_output_lock(void);

/**
 * output unlock
 */
void elog_port_output_unlock(void);

/**
 * get current time interface
 * @return current time string (format: "YYYY-MM-DD HH:MM:SS.mmm")
 */
const char *elog_port_get_time(void);

/**
 * get current process info interface
 * @return current process info string
 */
const char *elog_port_get_p_info(void);

/**
 * get current thread info interface
 * @return current thread info string
 */
const char *elog_port_get_t_info(void);
```

### 3. 兼容层接口

修改现有 `src/logger/Logger.h`：

```cpp
#ifndef LOGGER_H
#define LOGGER_H

#include <string>
#include <elog.h>

// 保持原有枚举，映射到 EasyLogger 级别
enum class LogLevel { 
    VERBOSE = ELOG_LVL_VERBOSE,
    DEBUG = ELOG_LVL_DEBUG, 
    INFO = ELOG_LVL_INFO, 
    WARNING = ELOG_LVL_WARN, 
    ERROR = ELOG_LVL_ERROR 
};

// [[deprecated]] 标记，提示迁移到 EasyLogger 原生 API
class [[deprecated("Use EasyLogger API (elog_i, elog_d, etc.) instead")]] Logger {
public:
    static void setLogLevel(LogLevel level);
    static void log(LogLevel level, const std::string &message);
    static void log(LogLevel level, const char *format, ...);
    
private:
    static const char* TAG;  // 默认标签 "LEGACY"
};

#endif // LOGGER_H
```

### 4. 初始化接口

```cpp
// src/logger/ElogInit.h
#ifndef ELOG_INIT_H
#define ELOG_INIT_H

#include <string>

struct ElogConfig {
    bool enableTerminal = true;      // 终端输出
    bool enableFile = false;         // 文件输出
    std::string logFilePath;         // 日志文件路径
    int logLevel = ELOG_LVL_INFO;    // 默认日志级别
};

/**
 * Initialize EasyLogger with configuration
 * @param config Configuration options
 * @return true on success, false on failure
 */
bool elog_init_with_config(const ElogConfig& config);

/**
 * Initialize EasyLogger with default settings
 * @return true on success, false on failure
 */
bool elog_init_default(void);

#endif // ELOG_INIT_H
```

## Data Models

### 日志级别映射

| 原 LogLevel | EasyLogger Level | 数值 | 说明 |
|-------------|------------------|------|------|
| ERROR | ELOG_LVL_ERROR | 1 | 错误 |
| WARNING | ELOG_LVL_WARN | 2 | 警告 |
| INFO | ELOG_LVL_INFO | 3 | 信息 |
| DEBUG | ELOG_LVL_DEBUG | 4 | 调试 |
| VERBOSE | ELOG_LVL_VERBOSE | 5 | 详细 |

### 时间戳格式

```
格式: "YYYY-MM-DD HH:MM:SS.mmm"
示例: "2026-01-14 15:30:45.123"
长度: 23 字符 + null terminator
```

### 日志输出格式

默认格式配置（elog_cfg.h）：
```
[级别] [时间戳] [标签] 消息
示例: [I] [2026-01-14 15:30:45.123] [RTSP] Client connected
```



## Correctness Properties

*A property is a characteristic or behavior that should hold true across all valid executions of a system—essentially, a formal statement about what the system should do. Properties serve as the bridge between human-readable specifications and machine-verifiable correctness guarantees.*

### Property 1: Timestamp Format Correctness

*For any* call to `elog_port_get_time()`, the returned string SHALL match the format "YYYY-MM-DD HH:MM:SS.mmm" where:
- YYYY is a 4-digit year (2000-2099)
- MM is a 2-digit month (01-12)
- DD is a 2-digit day (01-31)
- HH is a 2-digit hour (00-23)
- MM is a 2-digit minute (00-59)
- SS is a 2-digit second (00-59)
- mmm is a 3-digit millisecond (000-999)

**Validates: Requirements 2.3, 3.1, 3.2**

### Property 2: Timestamp Accuracy

*For any* call to `elog_port_get_time()`, the returned timestamp SHALL be within 10 milliseconds of the actual system time at the moment of the call.

**Validates: Requirements 3.3**

### Property 3: Log Level Filtering

*For any* log level filter setting L and any log call with level M:
- IF M <= L (numerically, where ERROR=1, WARN=2, INFO=3, DEBUG=4, VERBOSE=5), THEN the log SHALL be output
- IF M > L, THEN the log SHALL NOT be output

**Validates: Requirements 5.2**

### Property 4: Thread-Safe Concurrent Logging

*For any* set of N threads (N >= 2) concurrently calling log functions, each complete log message in the output SHALL:
- Contain exactly one timestamp
- Contain exactly one log level indicator
- Contain exactly one complete message without interleaving from other threads

**Validates: Requirements 7.1, 7.3**

### Property 5: LogLevel Enum Mapping

*For any* value of the legacy LogLevel enum, the mapping to EasyLogger level SHALL be:
- LogLevel::ERROR → ELOG_LVL_ERROR (1)
- LogLevel::WARNING → ELOG_LVL_WARN (2)
- LogLevel::INFO → ELOG_LVL_INFO (3)
- LogLevel::DEBUG → ELOG_LVL_DEBUG (4)
- LogLevel::VERBOSE → ELOG_LVL_VERBOSE (5)

**Validates: Requirements 8.2**

## Error Handling

### 初始化错误

| 错误场景 | 处理方式 |
|---------|---------|
| EasyLogger 初始化失败 | 返回错误码，输出到 stderr |
| 日志文件路径无效 | 禁用文件输出，继续终端输出 |
| 日志文件写入失败 | 记录错误，继续终端输出 |

### 运行时错误

| 错误场景 | 处理方式 |
|---------|---------|
| 日志消息过长 | 截断到最大长度 |
| 格式化参数错误 | 输出原始格式字符串 |
| 内存分配失败 | 使用静态缓冲区 |

## Testing Strategy

### 单元测试

使用 Google Test 框架进行单元测试：

1. **移植层测试**
   - 测试 `elog_port_get_time()` 返回格式
   - 测试 `elog_port_output()` 输出功能
   - 测试互斥锁功能

2. **兼容层测试**
   - 测试 LogLevel 映射
   - 测试 Logger::log() 转发
   - 测试 Logger::setLogLevel() 转发

3. **集成测试**
   - 测试完整日志流程
   - 测试文件输出
   - 测试多线程场景

### 属性测试

使用 RapidCheck 或类似的 C++ 属性测试库：

1. **Property 1**: 生成随机时间点，验证格式
2. **Property 2**: 比较返回时间与系统时间
3. **Property 3**: 生成随机级别组合，验证过滤行为
4. **Property 4**: 启动多线程并发日志，验证输出完整性
5. **Property 5**: 遍历所有 LogLevel 值，验证映射

### 测试配置

- 每个属性测试运行至少 100 次迭代
- 测试标签格式: **Feature: easylogger-integration, Property N: {property_text}**
