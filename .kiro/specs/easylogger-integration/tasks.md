# Implementation Plan: EasyLogger Integration

## Overview

本实现计划将 EasyLogger 日志库集成到 HTC 固件项目，包括库集成、平台移植、兼容层实现和测试。实现采用 C/C++ 语言，构建系统使用 CMake。

## Tasks

- [x] 1. 集成 EasyLogger 源码到项目
  - [x] 1.1 下载并添加 EasyLogger 源码到 third_party/easylogger/
    - 从 GitHub 获取 EasyLogger 核心文件（elog.h, elog.c, elog_cfg.h）
    - 创建目录结构：inc/, src/, port/
    - _Requirements: 1.1_

  - [x] 1.2 创建 EasyLogger 的 CMakeLists.txt
    - 配置静态库编译
    - 设置 include 路径
    - 支持 BUILD_FOR_SIMULATION 条件编译
    - _Requirements: 1.2, 1.3, 1.4, 1.5_

  - [x] 1.3 更新 third_party/CMakeLists.txt 添加 easylogger 子目录
    - _Requirements: 1.1_

- [x] 2. 实现平台移植层
  - [x] 2.1 创建 elog_cfg.h 配置文件
    - 配置日志输出格式（级别、时间戳、标签）
    - 配置缓冲区大小
    - 配置默认日志级别为 INFO
    - _Requirements: 5.4, 6.1, 6.3_

  - [x] 2.2 实现 elog_port.c 移植层
    - 实现 elog_port_init() 初始化函数
    - 实现 elog_port_output() 终端输出
    - 实现 elog_port_output_lock/unlock() 互斥锁
    - 实现 elog_port_get_time() 毫秒级时间戳
    - 实现 elog_port_get_p_info() 进程信息
    - 实现 elog_port_get_t_info() 线程信息
    - _Requirements: 2.1, 2.2, 2.3, 2.4, 2.5, 2.6, 2.7, 3.1, 3.2, 7.2_

  - [ ]* 2.3 编写移植层单元测试
    - 测试时间戳格式
    - 测试输出功能
    - _Requirements: 2.3, 3.1_

  - [ ]* 2.4 编写属性测试：时间戳格式正确性
    - **Property 1: Timestamp Format Correctness**
    - **Validates: Requirements 2.3, 3.1, 3.2**

  - [ ]* 2.5 编写属性测试：时间戳精度
    - **Property 2: Timestamp Accuracy**
    - **Validates: Requirements 3.3**

- [x] 3. Checkpoint - 验证 EasyLogger 基础功能
  - 确保 EasyLogger 可以编译
  - 确保基本日志输出正常
  - 如有问题请询问用户

- [x] 4. 实现兼容层
  - [x] 4.1 修改 src/logger/Logger.h
    - 包含 elog.h
    - 修改 LogLevel 枚举映射到 EasyLogger 级别
    - 添加 [[deprecated]] 属性
    - _Requirements: 8.1, 8.2, 8.5, 8.6_

  - [x] 4.2 修改 src/logger/Logger.cpp
    - 实现 Logger::log() 转发到 elog_x() 函数
    - 实现 Logger::setLogLevel() 转发到 elog_set_filter_lvl()
    - _Requirements: 8.3, 8.4_

  - [x] 4.3 更新 src/logger/CMakeLists.txt
    - 添加 EasyLogger 库依赖
    - _Requirements: 8.1_

  - [ ]* 4.4 编写属性测试：LogLevel 枚举映射
    - **Property 5: LogLevel Enum Mapping**
    - **Validates: Requirements 8.2**

- [x] 5. 实现初始化模块
  - [x] 5.1 创建 src/logger/ElogInit.h 和 ElogInit.cpp
    - 实现 elog_init_default() 默认初始化
    - 实现 elog_init_with_config() 配置初始化
    - 支持终端和文件输出配置
    - _Requirements: 9.1, 9.2, 9.3, 9.4, 4.1, 4.2, 4.3, 4.4, 4.5_

  - [x] 5.2 添加文件输出支持
    - 实现日志文件写入
    - 实现文件输出失败的错误处理
    - _Requirements: 4.2, 4.3, 4.6_

- [x] 6. Checkpoint - 验证兼容层和初始化
  - 确保现有代码使用 Logger 可以编译
  - 确保日志输出包含毫秒时间戳
  - 如有问题请询问用户

- [x] 7. 实现日志级别过滤
  - [x] 7.1 配置运行时日志级别修改
    - 确保 elog_set_filter_lvl() 可用
    - _Requirements: 5.1, 5.2, 5.3_

  - [ ]* 7.2 编写属性测试：日志级别过滤
    - **Property 3: Log Level Filtering**
    - **Validates: Requirements 5.2**

- [x] 8. 实现线程安全
  - [x] 8.1 验证互斥锁实现
    - 确保 elog_port_output_lock/unlock 正确实现
    - _Requirements: 7.1, 7.2_

  - [ ]* 8.2 编写属性测试：多线程并发安全
    - **Property 4: Thread-Safe Concurrent Logging**
    - **Validates: Requirements 7.1, 7.3**

- [x] 9. 集成测试
  - [x] 9.1 在 main_app.cpp 中添加 EasyLogger 初始化
    - 在应用启动时调用 elog_init_default()
    - _Requirements: 9.1, 9.2_

  - [x] 9.2 验证 RTSP 模块日志输出
    - 确保 RtspServer.cpp 等文件的日志正常输出
    - 确保时间戳包含毫秒
    - _Requirements: 3.1, 8.3_

- [x] 10. Final Checkpoint - 完整功能验证
  - 确保所有测试通过
  - 确保 PC 模拟和真机编译都正常
  - 如有问题请询问用户

## Notes

- Tasks marked with `*` are optional and can be skipped for faster MVP
- 属性测试使用 C++ 属性测试库（如 RapidCheck）
- 每个属性测试运行至少 100 次迭代
- 兼容层设计为过渡方案，后续应逐步迁移到原生 EasyLogger API
