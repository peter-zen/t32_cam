# 需求文档

## 简介

本项目旨在对现有 t32 嵌入式相机固件进行重构，建立硬件抽象层（HAL），实现与硬件无关的开发环境。目标是让大部分业务逻辑能够在 PC 环境下开发和调试，同时保留现有已验证的真机代码，采用渐进式重构策略降低风险。

## 术语表

- **HAL (Hardware Abstraction Layer)**: 硬件抽象层，提供统一的接口屏蔽底层硬件差异
- **libimp**: 君正（Ingenic）T32 芯片的多媒体处理库
- **FrameSource**: 视频帧源模块，负责从 ISP 获取原始图像
- **Encoder**: 视频编码器模块，负责 H.264/H.265/JPEG 编码
- **ISP (Image Signal Processor)**: 图像信号处理器
- **PC_Simulation**: PC 模拟环境，用于在 PC 上模拟硬件行为
- **Ingenic_Implementation**: 真机实现，封装现有代码调用 libimp
- **Cell-Bind**: libimp 的数据流绑定机制

## 需求

### 需求 1: HAL 接口层设计

**用户故事:** 作为开发者，我希望有一套统一的 HAL 接口定义，以便在不同平台（真机/PC模拟）上使用相同的 API 进行开发。

#### 验收标准

1. THE HAL_Interface SHALL 提供 C 语言风格的接口定义，支持 C++ 代码调用
2. THE HAL_Interface SHALL 包含以下核心模块接口：
   - hal_system.h: 系统初始化和数据流绑定
   - hal_isp.h: ISP 图像处理控制
   - hal_framesource.h: 视频帧源管理
   - hal_encoder.h: 视频编码器管理
   - hal_gpio.h: GPIO 控制
   - hal_mcu.h: MCU 通信
3. WHEN 定义 HAL 接口时，THE HAL_Interface SHALL 使用统一的错误码枚举（HalError）
4. THE HAL_Interface SHALL 与现有 libimp API 保持概念对应，便于封装

### 需求 2: 真机实现封装

**用户故事:** 作为开发者，我希望将现有已验证的代码封装到 HAL 层，以便保留同事的调试成果，降低重构风险。

#### 验收标准

1. THE Ingenic_Implementation SHALL 封装现有 GPIO 类（src/gpio/GPIO.cpp）到 hal_gpio 接口
2. THE Ingenic_Implementation SHALL 封装现有 MCU 类（src/mcu/MCU.cpp）到 hal_mcu 接口
3. THE Ingenic_Implementation SHALL 封装现有 libimp 调用到 hal_system、hal_isp、hal_framesource、hal_encoder 接口
4. WHEN 封装现有代码时，THE Ingenic_Implementation SHALL 不修改原有代码逻辑，仅添加适配层
5. THE Ingenic_Implementation SHALL 确保封装后功能与原有代码行为一致

### 需求 3: PC 模拟实现

**用户故事:** 作为开发者，我希望在 PC 环境下模拟硬件行为，以便在没有真机的情况下进行业务逻辑开发和调试。

#### 验收标准

1. THE PC_Simulation SHALL 实现 hal_system 接口的模拟版本
2. THE PC_Simulation SHALL 实现 hal_gpio 接口的模拟版本，使用内存状态模拟 GPIO 操作
3. THE PC_Simulation SHALL 实现 hal_mcu 接口的模拟版本，返回模拟数据
4. THE PC_Simulation SHALL 实现 hal_framesource 接口的模拟版本，从 H.264 文件读取帧数据
5. THE PC_Simulation SHALL 实现 hal_encoder 接口的模拟版本，模拟编码器输出
6. WHEN 模拟 GPIO 操作时，THE PC_Simulation SHALL 打印日志显示操作内容
7. WHEN 模拟视频流时，THE PC_Simulation SHALL 支持从本地 H.264 文件循环读取 NAL 单元

### 需求 4: 编译系统支持

**用户故事:** 作为开发者，我希望通过编译开关选择目标平台，以便在同一代码库中支持真机和 PC 模拟两种构建。

#### 验收标准

1. THE CMake_Build_System SHALL 提供 BUILD_FOR_SIMULATION 选项控制构建目标
2. WHEN BUILD_FOR_SIMULATION=ON 时，THE CMake_Build_System SHALL 编译 PC 模拟实现
3. WHEN BUILD_FOR_SIMULATION=OFF 时，THE CMake_Build_System SHALL 编译真机实现
4. THE CMake_Build_System SHALL 定义 SIMULATION_MODE 宏供代码条件编译使用
5. THE CMake_Build_System SHALL 确保两种构建模式下应用层代码无需修改

### 需求 5: 目录结构重组

**用户故事:** 作为开发者，我希望代码目录结构清晰，以便更好地组织 HAL 层和业务逻辑代码。

#### 验收标准

1. THE Directory_Structure SHALL 在 src/ 下创建 hal/ 目录存放 HAL 相关代码
2. THE Directory_Structure SHALL 在 src/hal/ 下创建 interface/ 子目录存放接口定义
3. THE Directory_Structure SHALL 在 src/hal/ 下创建 ingenic/ 子目录存放真机实现
4. THE Directory_Structure SHALL 在 src/hal/ 下创建 sim/ 子目录存放 PC 模拟实现
5. WHEN 调整目录结构时，THE Directory_Structure SHALL 保持现有模块位置不变，仅新增 HAL 层
6. THE Directory_Structure SHALL 确保每次调整后代码能够正常编译

### 需求 6: 渐进式重构策略

**用户故事:** 作为开发者，我希望采用渐进式重构策略，以便每一步都可以在真机验证，降低重构风险。

#### 验收标准

1. THE Refactor_Strategy SHALL 分阶段进行：Phase 0 目录结构 → Phase 1 HAL 层 → Phase 2 PC 模拟 → Phase 3 HTTP Server → Phase 4 移除 TCP Client
2. WHEN 完成每个阶段时，THE Refactor_Strategy SHALL 确保代码能够编译通过
3. WHEN 完成每个阶段时，THE Refactor_Strategy SHALL 确保真机功能正常运行
4. THE Refactor_Strategy SHALL 允许 HAL 层与现有代码并存，逐步迁移
5. IF 某阶段出现问题，THEN THE Refactor_Strategy SHALL 支持回退到上一阶段

### 需求 7: GPIO HAL 模块

**用户故事:** 作为开发者，我希望通过 HAL 接口控制 GPIO，以便在 PC 环境下模拟 GPIO 操作。

#### 验收标准

1. THE hal_gpio SHALL 提供 hal_gpio_export() 函数导出 GPIO 引脚
2. THE hal_gpio SHALL 提供 hal_gpio_unexport() 函数取消导出 GPIO 引脚
3. THE hal_gpio SHALL 提供 hal_gpio_set_direction() 函数设置 GPIO 方向（输入/输出）
4. THE hal_gpio SHALL 提供 hal_gpio_set_value() 函数设置 GPIO 输出值
5. THE hal_gpio SHALL 提供 hal_gpio_get_value() 函数读取 GPIO 输入值
6. WHEN 在真机实现中调用 hal_gpio 时，THE hal_gpio SHALL 调用现有 GPIO 类的对应方法
7. WHEN 在 PC 模拟中调用 hal_gpio 时，THE hal_gpio SHALL 使用内存变量模拟状态并打印日志

### 需求 8: MCU HAL 模块

**用户故事:** 作为开发者，我希望通过 HAL 接口与 MCU 通信，以便在 PC 环境下模拟 MCU 数据读取。

#### 验收标准

1. THE hal_mcu SHALL 提供 hal_mcu_init() 函数初始化 MCU 通信
2. THE hal_mcu SHALL 提供 hal_mcu_read_battery_level() 函数读取电池电量
3. THE hal_mcu SHALL 提供 hal_mcu_read_temperature() 函数读取温度
4. THE hal_mcu SHALL 提供 hal_mcu_read_firmware_version() 函数读取 MCU 固件版本
5. THE hal_mcu SHALL 提供 hal_mcu_set_datetime() 函数设置 RTC 时间
6. THE hal_mcu SHALL 提供 hal_mcu_get_datetime() 函数获取 RTC 时间
7. WHEN 在真机实现中调用 hal_mcu 时，THE hal_mcu SHALL 调用现有 MCU 类的对应方法
8. WHEN 在 PC 模拟中调用 hal_mcu 时，THE hal_mcu SHALL 返回预设的模拟数据

### 需求 9: 视频 HAL 模块

**用户故事:** 作为开发者，我希望通过 HAL 接口管理视频采集和编码，以便在 PC 环境下使用文件模拟视频流。

#### 验收标准

1. THE hal_system SHALL 提供 hal_system_init() 和 hal_system_exit() 函数管理系统生命周期
2. THE hal_system SHALL 提供 hal_system_bind() 和 hal_system_unbind() 函数管理数据流绑定
3. THE hal_framesource SHALL 提供创建、销毁、启用、禁用通道的函数
4. THE hal_encoder SHALL 提供创建、销毁编码器组和通道的函数
5. THE hal_encoder SHALL 提供获取和释放编码流的函数
6. WHEN 在真机实现中调用视频 HAL 时，THE Video_HAL SHALL 调用 libimp 对应的 API
7. WHEN 在 PC 模拟中调用视频 HAL 时，THE Video_HAL SHALL 从 H.264 文件读取 NAL 单元模拟编码输出
