# 设计文档

## 概述

本设计文档描述了 t32 嵌入式相机固件的 HAL（硬件抽象层）重构方案。通过建立 HAL 层，实现业务逻辑与硬件的解耦，使大部分代码能够在 PC 环境下开发和调试。

### 设计目标

1. **硬件无关性**: 业务逻辑代码不直接依赖硬件，通过 HAL 接口访问硬件功能
2. **双平台支持**: 同一套代码支持真机（Ingenic T32）和 PC 模拟两种运行环境
3. **渐进式重构**: 保留现有已验证代码，逐步封装到 HAL 层
4. **最小侵入性**: 不修改现有模块的内部实现，仅添加适配层

### 参考设计

本设计参考 `ref/t32_camera` 项目的 HAL 架构，该项目已实现完整的 HAL 接口定义和 PC 模拟实现。

## 架构

### 整体架构图

```
┌─────────────────────────────────────────────────────────────┐
│                      应用层 (Application)                    │
│  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐           │
│  │ 录像模块 │ │ 抓拍模块 │ │ RTSP服务│ │ 其他模块 │           │
│  └────┬────┘ └────┬────┘ └────┬────┘ └────┬────┘           │
└───────┼──────────┼──────────┼──────────┼───────────────────┘
        │          │          │          │
┌───────┴──────────┴──────────┴──────────┴───────────────────┐
│                    HAL 接口层 (Interface)                    │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐       │
│  │hal_system│ │hal_encoder│ │ hal_gpio │ │ hal_mcu  │       │
│  └──────────┘ └──────────┘ └──────────┘ └──────────┘       │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐                    │
│  │  hal_isp │ │hal_frame │ │ hal_kv   │                    │
│  │          │ │ source   │ │          │                    │
│  └──────────┘ └──────────┘ └──────────┘                    │
└─────────────────────────┬───────────────────────────────────┘
                          │
        ┌─────────────────┴─────────────────┐
        │                                   │
┌───────┴───────┐                   ┌───────┴───────┐
│  真机实现      │                   │  PC 模拟实现   │
│  (ingenic/)   │                   │  (sim/)       │
│               │                   │               │
│ ┌───────────┐ │                   │ ┌───────────┐ │
│ │ libimp    │ │                   │ │ 文件读取   │ │
│ │ GPIO sysfs│ │                   │ │ 内存模拟   │ │
│ │ I2C/MCU   │ │                   │ │ 日志输出   │ │
│ └───────────┘ │                   │ └───────────┘ │
└───────────────┘                   └───────────────┘
```

### 目录结构

根据方案文档 4.2 节的建议，采用更完整的目录重组方案：

```
src/
├── app/                        # 应用层 (保持)
│   ├── main_app.cpp
│   ├── daemon_app.cpp
│   ├── media_app.cpp
│   └── ...
├── hal/                        # 【新增】硬件抽象层
│   ├── interface/              # HAL 接口定义
│   │   ├── hal_common.h        # 公共定义 (错误码等)
│   │   ├── hal_system.h        # 系统接口
│   │   ├── hal_isp.h           # ISP 接口
│   │   ├── hal_framesource.h   # 帧源接口
│   │   ├── hal_encoder.h       # 编码器接口
│   │   ├── hal_gpio.h          # GPIO 接口
│   │   └── hal_mcu.h           # MCU 接口
│   ├── ingenic/                # 真机实现 (封装现有代码)
│   │   ├── hal_system_ingenic.cpp
│   │   ├── hal_gpio_ingenic.cpp
│   │   ├── hal_mcu_ingenic.cpp
│   │   └── ...
│   ├── sim/                    # PC 模拟实现
│   │   ├── hal_system_sim.c
│   │   ├── hal_gpio_sim.c
│   │   ├── hal_mcu_sim.c
│   │   └── ...
│   └── CMakeLists.txt
├── media/                      # 媒体处理 (保持，内部重构)
│   ├── common/
│   ├── video/
│   ├── snap/
│   └── rtsp/
├── hardware/                   # 【重组】硬件相关模块
│   ├── mcu/                    # MCU 通信 (从 src/mcu 移入)
│   ├── gpio/                   # GPIO 控制 (从 src/gpio 移入)
│   ├── daynight/               # 日夜切换 (从 src/daynight 移入)
│   └── power/                  # 电源管理 (从 src/power 移入)
├── service/                    # 【新增】服务层
│   ├── http_server/            # HTTP REST API (新增)
│   └── rtsp_server/            # RTSP 服务 (从 media/rtsp 移入，可选)
├── network/                    # 网络通信 (逐步移除 TCP Client)
├── common/                     # 通用模块 (保持)
├── config/                     # 【重组】配置相关
│   ├── setting/                # (从 src/setting 移入)
│   ├── devconf/                # (从 src/devconf 移入)
│   └── env/                    # (从 src/env 移入)
└── CMakeLists.txt
```

### 目录调整说明

| 调整类型 | 原位置 | 新位置 | 说明 |
|---------|--------|--------|------|
| 新增 | - | src/hal/ | HAL 抽象层 |
| 重组 | src/mcu, src/gpio, src/daynight, src/power | src/hardware/ | 硬件相关模块集中 |
| 新增 | - | src/service/ | 服务层 (HTTP Server) |
| 重组 | src/setting, src/devconf, src/env | src/config/ | 配置相关模块集中 |
| 保持 | src/app, src/media, src/common | 不变 | 核心模块保持位置 |

### 调整原则

1. **渐进式** - 不一次性大改，分步骤调整
2. **保持编译通过** - 每次调整后确保能编译
3. **保持功能正常** - 每次调整后在真机验证
4. **最小影响** - 优先调整独立性强的模块

## 组件和接口

### HAL 公共定义 (hal_common.h)

```c
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief HAL 错误码定义
 */
typedef enum {
    HAL_SUCCESS = 0,
    HAL_ERR_INVALID_PARAM = -1,
    HAL_ERR_NOT_INIT = -2,
    HAL_ERR_ALREADY_INIT = -3,
    HAL_ERR_NO_MEM = -4,
    HAL_ERR_BUSY = -5,
    HAL_ERR_NOT_FOUND = -6,
    HAL_ERR_TIMEOUT = -7,
    HAL_ERR_IO = -8,
    HAL_ERR_NOT_SUPPORTED = -9,
} HalError;

#ifdef __cplusplus
}
#endif
```

### GPIO HAL 接口 (hal_gpio.h)

```c
#pragma once

#include "hal_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HAL_GPIO_DIR_INPUT = 0,
    HAL_GPIO_DIR_OUTPUT = 1,
} HalGpioDirection;

typedef enum {
    HAL_GPIO_LOW = 0,
    HAL_GPIO_HIGH = 1,
} HalGpioValue;

/**
 * @brief 导出 GPIO 引脚
 * @param pin GPIO 引脚编号
 * @return HAL_SUCCESS 成功
 */
int hal_gpio_export(int pin);

/**
 * @brief 取消导出 GPIO 引脚
 * @param pin GPIO 引脚编号
 * @return HAL_SUCCESS 成功
 */
int hal_gpio_unexport(int pin);

/**
 * @brief 设置 GPIO 方向
 * @param pin GPIO 引脚编号
 * @param dir 方向 (输入/输出)
 * @return HAL_SUCCESS 成功
 */
int hal_gpio_set_direction(int pin, HalGpioDirection dir);

/**
 * @brief 设置 GPIO 输出值
 * @param pin GPIO 引脚编号
 * @param value 输出值 (高/低)
 * @return HAL_SUCCESS 成功
 */
int hal_gpio_set_value(int pin, HalGpioValue value);

/**
 * @brief 读取 GPIO 输入值
 * @param pin GPIO 引脚编号
 * @param value 输出参数，存储读取的值
 * @return HAL_SUCCESS 成功
 */
int hal_gpio_get_value(int pin, HalGpioValue* value);

#ifdef __cplusplus
}
#endif
```

### MCU HAL 接口 (hal_mcu.h)

```c
#pragma once

#include "hal_common.h"
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 MCU 通信
 * @return HAL_SUCCESS 成功
 */
int hal_mcu_init(void);

/**
 * @brief 关闭 MCU 通信
 * @return HAL_SUCCESS 成功
 */
int hal_mcu_deinit(void);

/**
 * @brief 读取电池电量
 * @param level 输出参数，电量百分比 (0-100)
 * @return HAL_SUCCESS 成功
 */
int hal_mcu_read_battery_level(int* level);

/**
 * @brief 读取温度
 * @param temperature 输出参数，温度值 (摄氏度)
 * @return HAL_SUCCESS 成功
 */
int hal_mcu_read_temperature(int* temperature);

/**
 * @brief 读取 MCU 固件版本
 * @param version 输出缓冲区
 * @param size 缓冲区大小
 * @return HAL_SUCCESS 成功
 */
int hal_mcu_read_firmware_version(char* version, int size);

/**
 * @brief 设置 RTC 时间
 * @param time 时间结构
 * @return HAL_SUCCESS 成功
 */
int hal_mcu_set_datetime(const struct tm* time);

/**
 * @brief 获取 RTC 时间
 * @param time 输出参数，时间结构
 * @return HAL_SUCCESS 成功
 */
int hal_mcu_get_datetime(struct tm* time);

/**
 * @brief 读取湿度
 * @param humidity 输出参数，湿度百分比
 * @return HAL_SUCCESS 成功
 */
int hal_mcu_read_humidity(int* humidity);

/**
 * @brief 读取外部电压
 * @param voltage_mv 输出参数，电压值 (毫伏)
 * @return HAL_SUCCESS 成功
 */
int hal_mcu_read_external_voltage(int* voltage_mv);

#ifdef __cplusplus
}
#endif
```

### 系统 HAL 接口 (hal_system.h)

参考 `ref/t32_camera/src/hal/interface/hal_system.h`，提供系统初始化和数据流绑定功能。

### 编码器 HAL 接口 (hal_encoder.h)

参考 `ref/t32_camera/src/hal/interface/hal_encoder.h`，提供视频编码功能。

## 数据模型

### GPIO 状态模型 (PC 模拟)

```c
typedef struct {
    bool exported;           // 是否已导出
    HalGpioDirection dir;    // 方向
    HalGpioValue value;      // 当前值
} GpioState;

// 全局 GPIO 状态表
static GpioState g_gpio_states[MAX_GPIO_NUM];
```

### MCU 模拟数据模型

```c
typedef struct {
    int battery_level;       // 电池电量 (0-100)
    int temperature;         // 温度 (摄氏度)
    int humidity;            // 湿度 (%)
    int external_voltage;    // 外部电压 (mV)
    char firmware_version[32]; // 固件版本
    struct tm datetime;      // RTC 时间
} McuSimData;
```

### 编码器状态模型

```c
typedef enum {
    ENC_CHN_STATE_NONE = 0,
    ENC_CHN_STATE_CREATED,
    ENC_CHN_STATE_REGISTERED,
    ENC_CHN_STATE_STARTED,
} EncChnState;

typedef struct {
    EncChnState state;
    HalEncoderChnAttr attr;
    int group_id;
    uint32_t seq;
} EncChannel;
```

## 正确性属性

*正确性属性是一种特征或行为，应该在系统的所有有效执行中保持为真——本质上是关于系统应该做什么的形式化陈述。属性作为人类可读规范和机器可验证正确性保证之间的桥梁。*

### Property 1: GPIO 状态一致性

*对于任意* GPIO 引脚，如果调用 hal_gpio_set_value 设置值后，立即调用 hal_gpio_get_value 应该返回相同的值（当方向为输出时）。

**验证: 需求 7.4, 7.5**

### Property 2: GPIO 导出状态管理

*对于任意* GPIO 引脚，在调用 hal_gpio_export 之前调用其他 GPIO 操作函数应该返回错误；在调用 hal_gpio_unexport 之后调用其他 GPIO 操作函数也应该返回错误。

**验证: 需求 7.1, 7.2**

### Property 3: MCU 数据范围有效性

*对于任意* MCU 读取操作，返回的数据应该在合理范围内：
- 电池电量: 0-100
- 温度: -40 到 85 摄氏度
- 湿度: 0-100%

**验证: 需求 8.2, 8.3**

### Property 4: 编码器状态机正确性

*对于任意* 编码器通道，状态转换必须遵循: NONE → CREATED → REGISTERED → STARTED，不能跳过中间状态。

**验证: 需求 9.4, 9.5**

### Property 5: 系统初始化幂等性

*对于任意* 系统状态，多次调用 hal_system_init 应该返回 HAL_ERR_ALREADY_INIT（第二次及以后），而不是重复初始化。

**验证: 需求 9.1**

### Property 6: 视频流数据完整性

*对于任意* 从 hal_encoder 获取的视频流，NAL 单元应该是完整的，以 0x00000001 或 0x000001 起始码开头。

**验证: 需求 9.5, 9.6**

## 错误处理

### 错误码设计

所有 HAL 函数使用统一的错误码返回值：

| 错误码 | 值 | 含义 |
|--------|-----|------|
| HAL_SUCCESS | 0 | 操作成功 |
| HAL_ERR_INVALID_PARAM | -1 | 参数无效 |
| HAL_ERR_NOT_INIT | -2 | 模块未初始化 |
| HAL_ERR_ALREADY_INIT | -3 | 模块已初始化 |
| HAL_ERR_NO_MEM | -4 | 内存不足 |
| HAL_ERR_BUSY | -5 | 资源忙 |
| HAL_ERR_NOT_FOUND | -6 | 资源不存在 |
| HAL_ERR_TIMEOUT | -7 | 操作超时 |
| HAL_ERR_IO | -8 | IO 错误 |

### 错误处理策略

1. **参数校验**: 所有函数入口处检查参数有效性
2. **状态检查**: 操作前检查模块/资源状态
3. **资源清理**: 错误发生时确保已分配资源被释放
4. **日志记录**: 错误发生时记录详细日志便于调试

## 测试策略

### 单元测试

- 测试每个 HAL 接口函数的基本功能
- 测试边界条件和错误处理
- 测试状态转换的正确性

### 属性测试

使用属性测试框架验证正确性属性：

1. **GPIO 属性测试**: 生成随机 GPIO 操作序列，验证状态一致性
2. **MCU 属性测试**: 验证返回数据在有效范围内
3. **编码器属性测试**: 验证状态机转换正确性

### 集成测试

1. **PC 模拟环境**: 验证完整的视频流程（从文件读取 → 编码器输出）
2. **真机环境**: 验证 HAL 封装与原有功能一致

### 测试框架

- **C 语言**: 使用 Unity 或 CMocka 进行单元测试
- **属性测试**: 使用 theft (C 语言属性测试库) 或自定义随机测试
- **构建验证**: CMake CTest 集成

### 测试配置

- 属性测试最少运行 100 次迭代
- 每个属性测试需要标注对应的设计属性编号
