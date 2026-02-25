# Design Document: PC Simulation Build

## Overview

本设计实现PC模拟环境的编译系统，使项目能够在PC（x86_64 Linux）上编译和运行。核心思路是：
1. 创建PC原生编译的toolchain配置文件
2. 修改主CMakeLists.txt支持根据选项选择不同toolchain
3. 处理SDK库依赖，为PC环境提供stub实现
4. 确保两个编译环境完全隔离

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        CMakeLists.txt                           │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │  if(BUILD_FOR_SIMULATION)                                │   │
│  │    → toolchain_sim.cmake (PC native)                     │   │
│  │  else()                                                  │   │
│  │    → toolchain.cmake (MIPS cross-compile)                │   │
│  └─────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
                              │
              ┌───────────────┴───────────────┐
              ▼                               ▼
    ┌─────────────────┐             ┌─────────────────┐
    │   build_sim/    │             │     build/      │
    │  (PC x86_64)    │             │  (MIPS T32)     │
    │                 │             │                 │
    │  hal_sim        │             │  hal_ingenic    │
    │  sdk_stub       │             │  sdk (real)     │
    └─────────────────┘             └─────────────────┘
```

## Components and Interfaces

### 1. toolchain_sim.cmake - PC原生编译配置

```cmake
# PC Simulation Toolchain Configuration
# 不设置CMAKE_SYSTEM_NAME，使用本地编译器

# 使用系统默认编译器 (gcc/g++)
# 不需要显式设置CMAKE_C_COMPILER和CMAKE_CXX_COMPILER

# PC编译优化选项
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2 -g -ffunction-sections -fdata-sections")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -g -ffunction-sections -fdata-sections")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--gc-sections")

# 定义模拟模式
add_definitions(-DSIMULATION_MODE)
```

### 2. CMakeLists.txt 修改 - Toolchain选择逻辑

修改主CMakeLists.txt，在加载toolchain之前检查BUILD_FOR_SIMULATION选项：

```cmake
cmake_minimum_required(VERSION 3.16)

# 在project()之前处理toolchain选择
# BUILD_FOR_SIMULATION必须通过cmake命令行传入: -DBUILD_FOR_SIMULATION=ON
if(BUILD_FOR_SIMULATION)
    message(STATUS "Configuring for PC Simulation")
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/toolchain_sim.cmake")
else()
    message(STATUS "Configuring for Target (Ingenic T32)")
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/toolchain.cmake")
endif()

project(htc_firmware VERSION 1.0)
# ... 其余配置
```

### 3. 平台特定编译选项处理

移除硬编码的MIPS选项，改为条件编译：

```cmake
# 平台特定编译选项
if(NOT BUILD_FOR_SIMULATION)
    # 仅在真机编译时添加MIPS选项
    add_definitions(
        -O2
        -Wall
        -march=mips32r2
    )
else()
    # PC模拟编译选项
    add_definitions(
        -O2
        -Wall
    )
endif()
```

### 4. SDK Stub库 - PC环境替代实现

创建 `src/sdk_stub/` 目录，提供SDK库的stub实现：

```
src/sdk_stub/
├── CMakeLists.txt
├── system_call_stub.c    # system_call库的stub
├── imp_stub.c            # imp库的stub  
└── alog_stub.c           # alog库的stub
```

### 5. 应用程序链接配置修改

修改 `src/app/CMakeLists.txt`，根据编译模式选择不同的库：

```cmake
if(BUILD_FOR_SIMULATION)
    # PC模拟模式：使用stub库
    target_link_libraries(htc_main_app 
        PRIVATE
        # ... 其他库
        sdk_stub    # 替代 system_call, imp, alog
        pthread
        rt
    )
else()
    # 真机模式：使用真实SDK库
    target_link_libraries(htc_main_app 
        PRIVATE
        # ... 其他库
        system_call
        imp
        alog
        pthread
        rt
        gcc
        stdc++
    )
endif()
```

## Data Models

### 编译配置选项

| 选项 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| BUILD_FOR_SIMULATION | BOOL | OFF | 是否为PC模拟编译 |
| USE_UCLIBC | BOOL | 1 | 是否使用uclibc（仅真机有效） |

### 构建目录结构

```
project_root/
├── build/              # 真机编译输出
│   ├── bin/           # 可执行文件 (MIPS)
│   └── lib/           # 库文件
├── build_sim/          # PC模拟编译输出
│   ├── bin/           # 可执行文件 (x86_64)
│   └── lib/           # 库文件
├── toolchain.cmake     # MIPS交叉编译配置
└── toolchain_sim.cmake # PC原生编译配置
```

## Correctness Properties

*A property is a characteristic or behavior that should hold true across all valid executions of a system-essentially, a formal statement about what the system should do. Properties serve as the bridge between human-readable specifications and machine-verifiable correctness guarantees.*

### Property 1: Toolchain选择正确性

*For any* BUILD_FOR_SIMULATION配置值，当设置为ON时，CMake应选择PC原生编译器（gcc/g++），当设置为OFF或未设置时，应选择MIPS交叉编译器。

**Validates: Requirements 1.1, 3.3, 3.4**

### Property 2: 编译标志正确性

*For any* PC模拟构建，编译命令中不应包含MIPS特定标志（如`-march=mips32r2`），且应定义`SIMULATION_MODE`宏；对于真机构建，应包含MIPS标志且不定义`SIMULATION_MODE`宏。

**Validates: Requirements 1.2, 4.1, 4.3**

### Property 3: 库链接正确性

*For any* PC模拟构建，应链接`hal_sim`库和`sdk_stub`库；对于真机构建，应链接`hal_ingenic`库和真实SDK库。

**Validates: Requirements 4.2, 5.1**

### Property 4: 环境隔离性

*For any* 构建环境切换操作，一个环境的构建不应影响另一个环境的构建目录内容，两个构建目录应能独立存在和编译。

**Validates: Requirements 2.3, 2.4**

## Error Handling

1. **Toolchain文件缺失**: 如果指定的toolchain文件不存在，CMake将报错并终止配置
2. **编译器不可用**: 如果PC上没有安装gcc/g++，配置阶段将失败并提示安装
3. **SDK库缺失**: PC模拟模式下，如果sdk_stub未正确实现，链接阶段将报错

## Testing Strategy

### 单元测试
- 验证toolchain_sim.cmake正确配置PC编译器
- 验证CMakeLists.txt正确选择toolchain
- 验证sdk_stub库提供所需的符号

### 集成测试
- 在PC上完整编译项目并运行基本功能测试
- 验证两个构建目录可以独立编译
- 验证环境切换不会相互影响

### 属性测试
- 使用CMake的`--trace`选项验证toolchain选择逻辑
- 使用`file`命令验证生成的可执行文件架构
- 使用`nm`命令验证库链接正确性

### 构建命令示例

```bash
# PC模拟编译
mkdir -p build_sim && cd build_sim
cmake -DBUILD_FOR_SIMULATION=ON ..
make -j$(nproc)

# 真机交叉编译
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```
