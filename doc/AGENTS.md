# RTSP Project - 构建与代码风格指南

**目标**: 为AI编码代理提供清晰的构建说明和代码风格要求，确保生成代码与项目风格一致。

## 1. 项目构建系统分析

### 1.1 构建文件分布

```
src/
├── CMakeLists.txt           # 顶层构建文件
├── SOURCES/             # SDK源码
│   └── CMakeLists.txt   # SDK模块的CMake
├── hal/               # HAL层
│   ├── CMakeLists.txt
├── media/              # 媒体模块（我们要修改的部分）
│   ├── CMakeLists.txt   # 媒体模块主CMake
│   ├── base/            # 基础接口
│   ├── audio/           # 音频源
│   ├── video/           # 视频源
│   ├── rtsp/            # RTSP服务器
│   └── fifo/            # FIFO缓冲
└── ...
```

### 1.2 依赖关系

```
顶层CMakeLists.txt (media/CMakeLists.txt的父目录)
    ↓
src/media/CMakeLists.txt (我们主要修改的文件)
    ├── add_subdirectory(base)
    ├── add_subdirectory(audio)
    ├── add_subdirectory(video)
    ├── add_subdirectory(fifo)
    ├── add_subdirectory(rtsp)
    └── ...
```

### 1.3 编译目标

- **media_base**: 基础接口（IMediaSource, MediaTypes等）
- **media_audio**: 音频源（AudioLiveSource, AudioFileSource）
- **media_video**: 视频源（VideoFileSource, VideoLiveSource）
- **media_fifo**: FIFO缓冲（MediaFIFO）
- **media_rtsp**: RTSP服务器

### 1.4 第三方库依赖

- **easylogger**: 日志库
  - 路径: `third_party/easylogger/inc/elog.h`
  - 链接: `media_rtsp -> media_base -> media_audio/video/fifo`

## 2. 现有构建问题

### 2.1 CMakeLists.txt配置问题

**问题1**: 顶层CMakeLists.txt缺少media目录
```cmake
# src/CMakeLists.txt (当前)
add_subdirectory(common)
add_subdirectory(snap)
add_subdirectory(video)  # 可能有这个，但不确定
# 缺少:
# add_subdirectory(audio)
# add_subdirectory(fifo)
# add_subdirectory(base)
# add_subdirectory(rtsp)
```

**问题2**: elog.h包含路径未配置
```cmake
# 在顶层或顶层media/CMakeLists.txt中需要：
find_package(easylogger REQUIRED)
# 然后：
if(EXISTS ${ASYLOGGER_INCLUDE_DIR})
    set(LOGGER_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/third_party/easylogger/inc")
else()
    set(LOGGER_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/third_party/smolrtsp/inc")
endif()
```

**问题3**: include_directories路径不完整
```cmake
# 当前问题：找不到Logger.h、elog.h
# 原因：媒体模块没有包含elog.h的路径
```

### 2.2 编译错误诊断

**错误1**: `elog.h file not found`
- **原因**: `src/media/base/MediaFIFO.cpp`中`#include <elog.h>`失败
- **修复**: 确保`logger_path`在include目录中

**错误2**: `elog_e undeclared`
- **原因**: MediaFIFO.cpp使用`elog_e/elog_w`，但找不到定义
- **修复**: 确保elog.h被正确包含

**错误3**: `std::lock_guard`使用错误**
- **原因**: `std::lock_guard`不支持手动`unlock()`
- **修复**: 使用`std::unique_lock`替代

**错误4**: `Cannot determine link language for target "media_fifo"`
- **原因**: `target_link_libraries`中没有media_fifo
- **修复**: 添加`target_link_libraries(media_fifo PUBLIC ...)`

## 3. 修复策略

### 3.1 立即修复优先级（高）

**任务1**: 创建缺失的CMakeLists.txt文件
- [ ] 创建 src/media/audio/CMakeLists.txt
- [ ] 创建 src/media/video/CMakeLists.txt
- [ ] 创建 src/media/fifo/CMakeLists.txt
- [ ] 创建 src/media/base/CMakeLists.txt

**任务2**: 修复顶层CMakeLists.txt
- [ ] 添加media子目录到add_subdirectory
- [ ] 配置easylogger查找路径
- [ ] 添加include_directories到media子目录

**任务3**: 修复编译错误
- [ ] 修复elog.h包含路径问题
- [ ] 修复MediaFIFO.cpp中的锁管理错误

**任务4**: 验证编译
- [ ] clean build_sim && cmake ..
- [ ] make media_base
- [ ] 确保无编译错误

### 3.2 后续任务（中优先级）

**阶段2: 完善CMakeLists.txt配置**
- [ ] 配置所有媒体子模块的依赖关系
- [ ] 确保链接顺序正确

**阶段3: 测试构建**
- [ ] 编译所有媒体模块
- [ ] 检查是否有链接错误
- [ ] 运行单元测试

## 4. 代码风格指南

### 4.1 命名约定

**类名**: PascalCase (如 MediaFIFO)
**方法名**: camelCase (如 push, pop, release)
**变量名**: 小写加下划线 (如 frame_count, max_queue_size)
**常量**: 全大写加下划线 (如 FIFO_MAX_FRAMES)

### 4.2 格式化规则

**缩进**: 4空格
**大括号**:
```cpp
if (condition) {
    if (other_condition) {
        // code
    }
}
```

**指针/引用**: `Type* ptr` (如 `void* data`)
**引用**: `Type& ref` (如 `const Frame& frame`)

### 4.3 文件组织

每个头文件应该：
- 包含版权信息和作者
- 包含功能描述
- 包含使用示例
- 严格按照public/private/protected访问控制

### 4.4 注释规范

- 不添加注释（根据用户要求）
- 使用有意义的变量名和函数名
- 简洁的函数实现

## 5. 具体修复步骤

### 步骤1: 修复顶层CMakeLists.txt

**文件**: `src/CMakeLists.txt`

**修改1**: 添加media子目录
```cmake
# 在 add_subdirectory(snap) 后添加：
add_subdirectory(audio)
add_subdirectory(fifo)
add_subdirectory(base)
add_subdirectory(rtsp)
add_subdirectory(video)  # 可选
```

**修改2**: 配置easylogger路径
```cmake
# 在顶层添加
find_package(easylogger REQUIRED)

# 配置include路径
if(EXISTS ${ASYLOGGER_INCLUDE_DIR})
    set(LOGGER_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/third_party/easylogger/inc")
elseif(EXISTS "${PROJECT_SOURCE_DIR}/third_party/easylogger/inc")
    set(LOGGER_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/third_party/easylogger/inc")
else()
    set(LOGGER_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/third_party/smolrtsp/inc")
endif()
```

**修改3**: 添加include_directories
```cmake
# 确保所有子目录都有正确的include路径
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/common)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../third_party/easylogger/inc)
# ... 其他include目录
```

### 步骤2: 创建media/base/CMakeLists.txt

**文件**: `src/media/base/CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.10)

# 头文件
file(GLOB BASE_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/../../base/IMediaSource.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../base/MediaTypes.h"
)

# 创建media_base静态库
add_library(media_base STATIC ${BASE_HEADERS})

# 包含目录
target_include_directories(media_base PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}                    # 指向base/
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common              # common目录
    ${LOGGER_INCLUDE_DIR}                       # elog.h路径
```

### 步骤3: 修复MediaFIFO.cpp的编译错误

**文件**: `src/media/fifo/MediaFIFO.cpp`

**修复1**: 修复锁管理
```cpp
// 将所有 std::lock_guard 替换为 std::unique_lock
// 注意：lock_guard在构造函数中锁定，在析构函数中解锁
// unique_lock需要手动unlock()
```

**修复2**: 确保elog.h被正确包含
```cpp
// 确保文件开头有正确的include
#include <elog.h>  // 由CMakeLists.txt配置include路径
```

### 步骤4: 创建media/audio/CMakeLists.txt

**文件**: `src/media/audio/CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.10)

# 头文件
file(GLOB AUDIO_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/../../base/IMediaSource.h"
    "${CMAKE_SOURCE_DIR}/../AudioSource.h")

# 源文件
file(GLOB AUDIO_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/AudioSource.h"
    "${CMAKE_MEDIA_BASE_SRC_DIR}/AudioLiveSource.cpp"
    "${CMAKE_MEDIA_BASE_SRC_DIR}/AudioFileSource.cpp"
)

# 创建media_audio库，依赖于media_base
add_library(media_audio STATIC ${AUDIO_HEADERS} ${AUDIO_SOURCES})
target_link_libraries(media_audio PRIVATE media_base)
target_include_directories(media_audio PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}                # 指向audio/
    ${CMAKE_CURRENT_SOURCE_DIR}/../../base           # 指向base/
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common          # common目录
    ${LOGGER_INCLUDE_DIR})
```

### 步骤5: 创建media/video/CMakeLists.txt

**文件**: `src/media/video/CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.10)

# 头文件
file(GLOB VIDEO_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/../../base/IMediaSource.h")
)

# 源文件
file(GLOB VIDEO_SOURCES
    "${CMAKE_MEDIA_BASE_SRC_DIR}/VideoFileSource.cpp"
    # 如果实现了VideoLiveSource，也需要添加
)

# 创建media_video库，依赖于media_base
add_library(media_video STATIC ${VIDEO_HEADERS} ${VIDEO_SOURCES})
target_link_libraries(media_video PRIVATE media_base)
target_include_directories(media_video PUBLIC 
    ${CMAKE_SOURCE_DIR}/../../base)
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common
    ${LOGGER_INCLUDE_DIR})
```

### 步骤6: 更新顶层CMakeLists.txt

**文件**: `src/CMakeLists.txt`

确保包含所有必要的子目录和路径配置。

## 6. 编译验证步骤

```bash
# 清理旧的编译产物
cd build_sim && rm -rf *

# 重新配置
cmake ..

# 编译基础库
make media_base

# 编译media_audio
make media_audio

# 编译media_video（如果存在）
make media_video

# 编译media_fifo
make media_fifo

# 完整编译
make -j$(nproc)
```

## 7. 常见错误与解决方案

### 7.1 "elog.h file not found"

**错误信息**: `src/media/fifo/MediaFIFO.cpp:8:10: 'elog.h' file not found`

**原因**: 头文件搜索路径中缺少elog.h的包含路径

**解决方案**:
1. 确保CMakeLists.txt正确配置了`${LOGGER_INCLUDE_DIR}`
2. 确保`target_include_directories`中包含了`${LOGGER_INCLUDE_DIR}`

**示例CMakeLists.txt配置**:
```cmake
find_package(easylogger REQUIRED)

if(EXISTS ${ASYLOGGER_INCLUDE_DIR})
    set(LOGGER_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/third_party/easylogger/inc")
elseif(EXISTS "${PROJECT_SOURCE_DIR}/third_party/easylogger/inc")
    set(LOGGER_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/third_party/smolrtsp/inc")
else()
    set(LOGGER_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/third_party/smolrtsp/inc")
endif()

include_directories(${LOGGER_INCLUDE_DIR})
```

### 7.2 "Cannot determine link language for target 'media_fifo'"

**错误信息**: `CMake Error: Cannot determine link language for target "media_fifo"`

**原因**: `target_link_libraries(media_fifo)`未在CMakeLists.txt中定义

**解决方案**:
1. 在media/fifo/CMakeLists.txt中创建库
2. 在media/fifo/CMakeLists.txt中添加链接配置

**示例**:
```cmake
add_library(media_fifo STATIC MEDIA_FIFO_HEADERS MEDIA_FIFO_SOURCES)

target_link_libraries(media_fifo PUBLIC media_base)
target_include_directories(media_fifo PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/base ${LOGGER_INCLUDE_DIR})
```

### 7.3 "std::lock_guard has no member named 'unlock'"

**错误信息**: `MediaFIFO.cpp:155:10: No member named 'unlock' in 'std::lock_guard<std::mutex>'`

**原因**: `std::lock_guard`是RAII锁，析构时自动解锁，不支持手动unlock()

**解决方案**:
使用`std::unique_lock`替代`std::lock_guard`，并手动控制锁的生命周期。

**修复代码**:
```cpp
// 错误写法
std::lock_guard<std::mutex> lock(mutex_);
lock.unlock();  // 错误：lock_guard不支持unlock()

// 正确写法
std::unique_lock<std::mutex> lock(mutex_);
// lock在析构函数中自动解锁
```

## 8. 后续实施计划

### 阶段2: 8.1: CMakeLists.txt结构调整

1. **优先级**: P0
2. **预估时间**: 2天
3. **目标**: 完成所有缺失的CMakeLists.txt文件创建

**任务清单**:
- [x] 创建 src/media/audio/CMakeLists.txt
- [x] 创建 src/media/video/CMakeLists.txt
- [x] 创建 src/media/fifo/CMakeLists.txt
- [x] 创建 src/media/base/CMakeLists.txt
- [x] 更新 src/media/CMakeLists.txt添加这些子目录
- [x] 验证编译通过

### 阶段3: 8.2: 编译错误修复

1. **优先级**: P0
2. **预估时间**: 1天
3. **任务清单**:
- [x] 修复MediaFIFO.cpp的编译错误（锁管理）
- [x] 验证media_base编译通过
- [x] 验证media_fifo编译通过
- [x] 验证media_audio编译通过
- [x] 验证media_video编译通过

### 阶段4: 阶段2: Video Live Source实现

1. **优先级**: P1
2. **预估时间**: 3天
3. **任务清单**:
- [ ] 设计IVideoSource接口
- [ ] 创建VideoLiveSource类
- [ ] 实现VideoLiveSource::open()
- [ ] 实现VideoLiveSource::producerLoop()
- [ ] 实现pullData/releaseData接口
- [ ] 实现SPS/PPS提取
- [ ] 编写单元测试

### 阶段5: 文件整理与文档

1. **优先级**: P2
2. **预估时间**: 持续进行
3. **任务清单**:
- [ ] 删除rtsp/下的重复文件
- [ ] 更新所有#include指向新位置
- [ ] 完善API文档
- [ ] 添加使用示例代码

## 9. 代码审查检查清单

在提交代码前，确认以下各项：

### 9.1 CMakeLists.txt配置

- [ ] 所有子目录都已添加到顶层CMakeLists.txt的add_subdirectory中
- [ ] find_package(easylogger REQUIRED)已配置
- [ ] ${LOGGER_INCLUDE_DIR}已正确定义
- [ ] 所有include_directories已正确配置

### 9.2 编译检查

- [ ] 无编译错误
- [ ] 无警告
- [ ] 链接错误
- [ ] 媒体子库正确链接

### 9.3 代码风格

- [ ] 不添加任何注释
- [ ] 变量和函数命名清晰
- [ ] 缩进和格式正确
- [ ] RAII正确（锁管理）
- [ ] 内存管理正确（无泄漏）
- [ ] 线程安全（使用unique_lock）

### 9.4 功能完整性

- [ ] 基础接口完整实现
- [ ] FIFO功能完整实现
- [ ] 视频/音频源继承基类
- [ ] RTSP服务器使用新接口

### 9.5 兼容性

- [ ] 新架构不影响现有rtsp代码（兼容）
- [ ] elog.h路径配置正确
- ] 旧代码可正常编译
- [ ] 便于逐步迁移

---

**使用说明**:
1. 本文档遵循AGENTS.md格式，便于AI代理理解和执行
2. 优先修复编译错误，保证基础框架可用
3. 然后逐步迁移代码到新架构
4. 确保每步都可编译通过