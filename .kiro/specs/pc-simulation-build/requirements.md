# Requirements Document

## Introduction

本功能旨在建立PC模拟环境的编译系统，使项目能够在PC（x86_64 Linux）上编译和运行，用于开发调试和功能测试。同时保持与现有交叉编译环境的兼容，实现两种编译环境的方便切换。

## Glossary

- **Build_System**: CMake构建系统，负责管理编译配置和生成Makefile
- **Cross_Compiler**: MIPS交叉编译工具链，用于编译真机（Ingenic T32）可执行文件
- **Native_Compiler**: PC本地编译器（gcc/g++），用于编译PC可执行文件
- **HAL**: Hardware Abstraction Layer，硬件抽象层
- **Simulation_Mode**: PC模拟模式，使用模拟HAL实现替代真实硬件接口
- **Target_Mode**: 真机模式，使用Ingenic HAL实现与真实硬件交互

## Requirements

### Requirement 1: PC原生编译支持

**User Story:** As a developer, I want to compile the project using native PC compiler, so that I can run and debug the application on my development machine.

#### Acceptance Criteria

1. WHEN `BUILD_FOR_SIMULATION` is set to ON, THE Build_System SHALL use the native PC compiler (gcc/g++) instead of the cross compiler
2. WHEN building for PC simulation, THE Build_System SHALL NOT include MIPS-specific compiler flags (e.g., `-march=mips32r2`)
3. WHEN building for PC simulation, THE Build_System SHALL produce x86_64 Linux executables
4. THE Build_System SHALL link against PC-compatible libraries when in simulation mode

### Requirement 2: 编译环境隔离

**User Story:** As a developer, I want separate build directories for PC and target builds, so that I can switch between environments without conflicts.

#### Acceptance Criteria

1. THE Build_System SHALL support using `build/` directory for target (cross-compiled) builds
2. THE Build_System SHALL support using `build_sim/` directory for PC simulation builds
3. WHEN switching between build environments, THE Build_System SHALL NOT require cleaning the other environment's build directory
4. THE Build_System SHALL allow both build directories to coexist independently

### Requirement 3: 便捷的环境切换

**User Story:** As a developer, I want simple commands to switch between PC and target build environments, so that I can quickly test on both platforms.

#### Acceptance Criteria

1. THE Build_System SHALL provide a clear method to configure PC simulation build (e.g., cmake option)
2. THE Build_System SHALL provide a clear method to configure target cross-compilation build
3. WHEN configuring for PC simulation, THE Build_System SHALL automatically select the native toolchain
4. WHEN configuring for target build, THE Build_System SHALL automatically select the cross-compilation toolchain

### Requirement 4: 条件编译兼容

**User Story:** As a developer, I want the build system to handle platform-specific code correctly, so that the same codebase works on both PC and target.

#### Acceptance Criteria

1. WHEN building for PC simulation, THE Build_System SHALL define `SIMULATION_MODE` macro
2. WHEN building for PC simulation, THE Build_System SHALL link the `hal_sim` library instead of `hal_ingenic`
3. WHEN building for target, THE Build_System SHALL NOT define `SIMULATION_MODE` macro
4. THE Build_System SHALL handle SDK library dependencies appropriately for each platform
5. IF SDK libraries are not available for PC, THEN THE Build_System SHALL provide stub implementations or skip those components

### Requirement 5: 第三方库兼容

**User Story:** As a developer, I want third-party libraries to compile correctly on PC, so that all application features work in simulation mode.

#### Acceptance Criteria

1. WHEN building for PC simulation, THE Build_System SHALL compile third-party libraries using the native compiler
2. THE Build_System SHALL handle platform-specific third-party library configurations
3. IF a third-party library has platform-specific code, THEN THE Build_System SHALL use appropriate conditional compilation
