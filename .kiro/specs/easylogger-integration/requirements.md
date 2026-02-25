# Requirements Document

## Introduction

本文档定义了将 EasyLogger 日志库集成到 HTC 固件项目的需求。目标是替换现有的简单 Logger 类，提供更强大的日志功能，支持毫秒级时间戳、文件输出、终端输出，以满足 RTSP 音画同步调试等场景的需求。项目需要同时支持嵌入式真机（Ingenic T32 MIPS）和 PC 模拟环境（x86_64）。

## Glossary

- **EasyLogger**: 一款超轻量级（ROM<1.6K, RAM<0.3K）、高性能的 C/C++ 日志库
- **Elog**: EasyLogger 的简称，也是其 API 前缀
- **Log_Level**: 日志级别，包括 ASSERT、ERROR、WARN、INFO、DEBUG、VERBOSE
- **Tag**: 日志标签，用于标识日志来源模块
- **Port_Layer**: EasyLogger 的移植层，需要针对不同平台实现
- **Simulation_Mode**: PC 模拟编译模式，通过 BUILD_FOR_SIMULATION 宏控制
- **Target_Mode**: 真机交叉编译模式，针对 Ingenic T32 MIPS 平台

## Requirements

### Requirement 1: EasyLogger 库集成

**User Story:** As a developer, I want to integrate EasyLogger into the project build system, so that I can use its logging capabilities.

#### Acceptance Criteria

1. THE Build_System SHALL include EasyLogger source code in the `third_party/easylogger/` directory
2. THE Build_System SHALL compile EasyLogger as a static library
3. THE Build_System SHALL support both Simulation_Mode and Target_Mode compilation
4. WHEN compiling for Simulation_Mode, THE Build_System SHALL define `BUILD_FOR_SIMULATION` macro for EasyLogger
5. WHEN compiling for Target_Mode, THE Build_System SHALL use MIPS cross-compiler for EasyLogger

### Requirement 2: 平台移植层实现

**User Story:** As a developer, I want EasyLogger to work on both PC and embedded platforms, so that I can debug on PC and deploy to target hardware.

#### Acceptance Criteria

1. THE Port_Layer SHALL implement `elog_port_init()` for platform initialization
2. THE Port_Layer SHALL implement `elog_port_output()` for log output to terminal
3. THE Port_Layer SHALL implement `elog_port_get_time()` to return timestamp string with millisecond precision
4. THE Port_Layer SHALL implement `elog_port_get_p_info()` to return process information
5. THE Port_Layer SHALL implement `elog_port_get_t_info()` to return thread information
6. WHEN running in Simulation_Mode, THE Port_Layer SHALL use POSIX APIs for time and thread functions
7. WHEN running in Target_Mode, THE Port_Layer SHALL use platform-specific APIs compatible with Ingenic T32

### Requirement 3: 毫秒级时间戳

**User Story:** As a developer debugging AV sync issues, I want log timestamps with millisecond precision, so that I can accurately trace timing-related problems.

#### Acceptance Criteria

1. THE Elog SHALL output timestamps in format "YYYY-MM-DD HH:MM:SS.mmm" where mmm represents milliseconds
2. WHEN `elog_port_get_time()` is called, THE Port_Layer SHALL return current time with millisecond precision
3. THE Timestamp SHALL be accurate within 1 millisecond of actual system time

### Requirement 4: 多输出目标支持

**User Story:** As a developer, I want logs to be output to both terminal and file, so that I can view real-time logs and analyze historical logs.

#### Acceptance Criteria

1. THE Elog SHALL support output to terminal (stdout/stderr)
2. THE Elog SHALL support output to log file
3. WHEN file output is enabled, THE Elog SHALL write logs to a configurable file path
4. THE Elog SHALL allow enabling/disabling terminal output independently
5. THE Elog SHALL allow enabling/disabling file output independently
6. IF file output fails, THEN THE Elog SHALL continue terminal output without crashing

### Requirement 5: 日志级别过滤

**User Story:** As a developer, I want to filter logs by level, so that I can focus on relevant information during debugging.

#### Acceptance Criteria

1. THE Elog SHALL support log levels: ASSERT, ERROR, WARN, INFO, DEBUG, VERBOSE
2. WHEN a log level filter is set, THE Elog SHALL only output logs at or above that level
3. THE Elog SHALL allow runtime modification of log level filter
4. THE Default_Log_Level SHALL be INFO

### Requirement 6: 日志格式配置

**User Story:** As a developer, I want configurable log format, so that I can include relevant context information in logs.

#### Acceptance Criteria

1. THE Elog SHALL support configurable output format including: level, timestamp, tag, file path, line number, function name
2. THE Elog SHALL allow different format configurations for different log levels
3. THE Default_Format SHALL include: level, timestamp, tag, and message

### Requirement 7: 线程安全

**User Story:** As a developer working with multi-threaded RTSP code, I want thread-safe logging, so that logs from different threads don't corrupt each other.

#### Acceptance Criteria

1. THE Elog SHALL be thread-safe for concurrent log calls from multiple threads
2. THE Port_Layer SHALL implement mutex lock/unlock functions for thread synchronization
3. WHEN multiple threads call log functions simultaneously, THE Elog SHALL serialize output without data corruption

### Requirement 8: 现有代码兼容层（过渡方案）

**User Story:** As a developer, I want a compatibility wrapper that wraps EasyLogger, so that existing code compiles without changes while I gradually migrate to native EasyLogger API.

#### Acceptance Criteria

1. THE Compatibility_Layer SHALL modify existing Logger class to internally use EasyLogger
2. THE Compatibility_Layer SHALL map existing LogLevel enum (VERBOSE, DEBUG, INFO, WARNING, ERROR) to EasyLogger levels
3. THE Compatibility_Layer SHALL forward `Logger::log()` calls to corresponding EasyLogger functions (elog_v, elog_d, elog_i, elog_w, elog_e)
4. WHEN existing code calls `Logger::setLogLevel()`, THE Compatibility_Layer SHALL update EasyLogger's filter level via `elog_set_filter_lvl()`
5. THE Compatibility_Layer SHALL be marked as deprecated with compiler warnings to encourage migration
6. THE Compatibility_Layer SHALL preserve existing Logger.h include path for backward compatibility
7. AFTER migration is complete, THE Old_Logger_API SHALL be removable without affecting EasyLogger functionality

### Requirement 9: 初始化和配置

**User Story:** As a developer, I want simple initialization, so that I can quickly enable logging in my application.

#### Acceptance Criteria

1. THE Elog SHALL provide a single initialization function to set up logging
2. THE Initialization SHALL configure default output format, log level, and output targets
3. WHEN initialization fails, THE Elog SHALL return an error code and log to stderr
4. THE Elog SHALL support re-initialization to change configuration at runtime
