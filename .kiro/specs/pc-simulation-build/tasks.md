# Implementation Plan: PC Simulation Build

## Overview

本实现计划将项目的编译系统改造为支持PC模拟和真机交叉编译两种模式，通过CMake选项实现环境切换，确保两种编译环境完全隔离。

## Tasks

- [x] 1. 创建PC原生编译toolchain配置
  - [x] 1.1 创建 `toolchain_sim.cmake` 文件
    - 配置PC原生编译器选项
    - 设置适合PC的编译优化标志
    - 定义SIMULATION_MODE宏
    - _Requirements: 1.1, 1.2, 4.1_

- [x] 2. 修改主CMakeLists.txt支持双环境
  - [x] 2.1 修改toolchain选择逻辑
    - 在project()之前添加BUILD_FOR_SIMULATION判断
    - 根据选项选择对应的toolchain文件
    - _Requirements: 3.1, 3.2, 3.3, 3.4_
  - [x] 2.2 条件化MIPS特定编译选项
    - 将`-march=mips32r2`移入条件判断
    - PC模式下使用通用编译选项
    - _Requirements: 1.2_

- [x] 3. 创建SDK Stub库
  - [x] 3.1 创建 `src/sdk_stub/` 目录和CMakeLists.txt
    - 设置stub库的编译配置
    - 仅在BUILD_FOR_SIMULATION时编译
    - _Requirements: 4.4, 4.5_
  - [x] 3.2 实现 `system_call_stub.c`
    - 提供system_call库的空实现或模拟实现
    - _Requirements: 4.5_
  - [x] 3.3 实现 `imp_stub.c`
    - 提供imp库的空实现或模拟实现
    - _Requirements: 4.5_
  - [x] 3.4 实现 `alog_stub.c`
    - 提供alog库的空实现或模拟实现
    - _Requirements: 4.5_

- [x] 4. 修改src/CMakeLists.txt添加sdk_stub子目录
  - 在BUILD_FOR_SIMULATION时添加sdk_stub子目录
  - _Requirements: 4.4_

- [x] 5. 修改应用程序链接配置
  - [x] 5.1 修改 `src/app/CMakeLists.txt`
    - 根据BUILD_FOR_SIMULATION选择链接库
    - PC模式链接sdk_stub，真机模式链接真实SDK库
    - 处理gcc/stdc++库的条件链接
    - _Requirements: 4.2, 4.4, 5.1_

- [x] 6. Checkpoint - 验证基础编译配置
  - 清理build_sim目录并重新配置
  - 确认CMake正确选择PC编译器
  - 确认编译选项不包含MIPS特定标志

- [x] 7. 处理其他模块的SDK依赖
  - [x] 7.1 检查并修改依赖SDK的其他CMakeLists.txt
    - 检查media、hardware等模块的SDK依赖
    - 添加条件编译处理
    - _Requirements: 4.4, 5.2_

- [x] 8. 更新.gitignore
  - 确保build_sim目录被忽略
  - _Requirements: 2.2_

- [x] 9. Final Checkpoint - 完整编译测试
  - 在build_sim目录执行完整编译
  - 验证生成的可执行文件为x86_64架构
  - 确保两个编译环境可以独立工作
  - _Requirements: 1.3, 2.3, 2.4_

## Notes

- 任务按依赖顺序排列，需要顺序执行
- SDK Stub库只需要提供符号定义，不需要完整功能实现
- 编译测试时注意检查CMake输出，确认toolchain选择正确
