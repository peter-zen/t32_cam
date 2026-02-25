# 基于 t32 重构方案分析

## 1. 方案对比概览

| 维度 | 方案A: 基于 t32_camera | 方案B: 重构 t32 (新方案) |
|------|------------------------|--------------------------|
| 起点 | 新架构，功能不完整 | 已调试OK的生产代码 |
| 真机验证 | ❌ 需要从头验证 | ✅ 已验证 |
| 协作影响 | 🔴 同事代码需重写 | 🟢 保留同事工作成果 |
| 重构风险 | 低 (新代码) | 中 (改动已有代码) |
| 工作量 | 大 (功能迁移) | 中 (架构调整) |
| PC模拟支持 | ✅ 已有 | ⚠️ 需要添加 |

**结论：方案B更务实，推荐采用**

---

## 2. 方案B 可行性分析

### 2.1 优势

1. **保留已验证代码** - 同事的调试成果不会浪费
2. **降低协作冲突** - 渐进式重构，不影响现有功能
3. **风险可控** - 每一步都可以在真机验证
4. **快速见效** - 不需要重新实现已有功能

### 2.2 挑战

1. **代码耦合** - t32 模块间有一定耦合，需要逐步解耦
2. **C++ 代码** - 添加 C 风格 HAL 层需要适配
3. **TCP Client 移除** - 需要替换为 HTTP Server

### 2.3 可行性评估：✅ 可行

t32 代码结构已经有一定模块化 (20+ 子目录)，重构基础较好。

---

## 3. 重构路线图

```
Phase 0: 目录结构调整 (1周)
    ↓
Phase 1: HAL 抽象层建立 (2周)
    ↓
Phase 2: PC 模拟环境适配 (2周)
    ↓
Phase 3: HTTP Server 添加 (2周)
    ↓
Phase 4: TCP Client 移除 (1周)
```

---

## 4. Phase 0: 目录结构调整

### 4.1 当前结构

```
t32/src/
├── app/            # 应用入口
├── media/          # 媒体处理 (视频、抓拍、RTSP)
├── network/        # 网络通信 (TCP Client - 待移除)
├── mcu/            # MCU 通信
├── gpio/           # GPIO 控制
├── daynight/       # 日夜切换
├── power/          # 电源管理
├── time/           # RTC 时间
├── setting/        # 配置管理
├── ... (其他模块)
└── CMakeLists.txt
```

### 4.2 建议调整后结构

```
t32/src/
├── app/                    # 应用层 (保持)
│   ├── main_app.cpp
│   └── ...
├── hal/                    # 【新增】硬件抽象层
│   ├── interface/          # HAL 接口定义
│   │   ├── hal_system.h
│   │   ├── hal_isp.h
│   │   ├── hal_encoder.h
│   │   ├── hal_gpio.h
│   │   └── hal_mcu.h
│   ├── ingenic/            # 真机实现 (封装现有代码)
│   │   ├── hal_system_ingenic.cpp
│   │   ├── hal_gpio_ingenic.cpp
│   │   └── ...
│   └── sim/                # 【新增】PC 模拟实现
│       ├── hal_system_sim.cpp
│       └── ...
├── media/                  # 媒体处理 (保持，内部重构)
├── hardware/               # 【重组】硬件相关
│   ├── mcu/                # MCU 通信 (从 src/mcu 移入)
│   ├── gpio/               # GPIO 控制 (从 src/gpio 移入)
│   ├── daynight/           # 日夜切换 (从 src/daynight 移入)
│   └── power/              # 电源管理 (从 src/power 移入)
├── service/                # 【新增】服务层
│   ├── http_server/        # HTTP REST API
│   └── rtsp_server/        # RTSP (从 media 移入)
├── network/                # 网络通信 (逐步移除 TCP Client)
├── common/                 # 通用模块 (保持)
├── config/                 # 【重组】配置相关
│   ├── setting/
│   ├── devconf/
│   └── env/
└── CMakeLists.txt
```

### 4.3 调整原则

1. **渐进式** - 不一次性大改，分步骤调整
2. **保持编译通过** - 每次调整后确保能编译
3. **保持功能正常** - 每次调整后在真机验证

---

## 5. Phase 1: HAL 抽象层建立

### 5.1 策略：封装而非重写

不重写现有代码，而是在其上建立抽象层：

```cpp
// hal/interface/hal_gpio.h
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HAL_GPIO_DIR_INPUT,
    HAL_GPIO_DIR_OUTPUT,
} HalGpioDirection;

int hal_gpio_export(int pin);
int hal_gpio_set_direction(int pin, HalGpioDirection dir);
int hal_gpio_set_value(int pin, int value);
int hal_gpio_get_value(int pin, int* value);

#ifdef __cplusplus
}
#endif
```

```cpp
// hal/ingenic/hal_gpio_ingenic.cpp
#include "hal_gpio.h"
#include "gpio/GPIO.h"  // 现有代码

extern "C" {

int hal_gpio_export(int pin) {
    return GPIO::exportPin(pin);  // 调用现有实现
}

int hal_gpio_set_value(int pin, int value) {
    return GPIO::setValue(pin, value ? GPIO::HIGH : GPIO::LOW);
}

// ... 其他函数类似封装

}
```

### 5.2 需要建立 HAL 的模块

| 模块 | 现有代码 | HAL 接口 |
|------|----------|----------|
| System | libimp 直接调用 | hal_system.h |
| ISP | libimp 直接调用 | hal_isp.h |
| Encoder | libimp 直接调用 | hal_encoder.h |
| FrameSource | libimp 直接调用 | hal_framesource.h |
| GPIO | gpio/GPIO.cpp | hal_gpio.h |
| MCU | mcu/MCU.cpp | hal_mcu.h |

### 5.3 工作量估算

- HAL 接口定义：2-3 天
- 真机实现封装：3-4 天
- 编译调试：2-3 天
- **总计：约 2 周**

---

## 6. Phase 2: PC 模拟环境适配

### 6.1 编译开关设计

```cmake
# CMakeLists.txt
option(BUILD_FOR_SIMULATION "Build for PC simulation" OFF)

if(BUILD_FOR_SIMULATION)
    add_definitions(-DSIMULATION_MODE)
    add_subdirectory(hal/sim)
else()
    add_subdirectory(hal/ingenic)
endif()
```

### 6.2 模拟实现示例

```cpp
// hal/sim/hal_gpio_sim.cpp
#include "hal_gpio.h"
#include <map>

static std::map<int, int> g_gpio_values;

extern "C" {

int hal_gpio_export(int pin) {
    g_gpio_values[pin] = 0;
    printf("[SIM] GPIO %d exported\n", pin);
    return 0;
}

int hal_gpio_set_value(int pin, int value) {
    g_gpio_values[pin] = value;
    printf("[SIM] GPIO %d = %d\n", pin, value);
    return 0;
}

}
```

### 6.3 视频模拟

可以借鉴 t32_camera 的模拟实现：
- 从 H.264 文件读取帧数据
- 模拟编码器输出

---

## 7. Phase 3: HTTP Server 添加

### 7.1 策略

1. 新增 HTTP Server 模块，与现有 TCP Client 并存
2. 逐步将功能迁移到 HTTP API
3. 最后移除 TCP Client

### 7.2 HTTP Server 实现

可以直接复用 t32_camera 的 http_server.c，或使用轻量级库如 mongoose。

```cpp
// service/http_server/http_server.cpp
class HttpServer {
public:
    void start(int port);
    void stop();
    
    // 注册 API 处理器
    void registerHandler(const std::string& path, Handler handler);
};
```

### 7.3 API 设计

```
GET  /api/device/info        → 设备信息
GET  /api/sensor/data        → 传感器数据
GET  /api/record/start       → 开始录像
GET  /api/record/stop        → 停止录像
GET  /api/daynight/status    → 日夜状态
POST /api/config             → 设置配置
```

---

## 8. 两方案详细对比

### 8.1 工作量对比

| 工作项 | 方案A (t32_camera) | 方案B (重构t32) |
|--------|-------------------|-----------------|
| HAL 真机实现 | 从头实现 (3周) | 封装现有代码 (2周) |
| 功能迁移 | 全部迁移 (6周) | 不需要 (0周) |
| PC 模拟 | 已有 (0周) | 新增 (2周) |
| HTTP Server | 已有 (0周) | 新增 (2周) |
| TCP Client 移除 | 不需要 (0周) | 需要 (1周) |
| 真机调试 | 全面调试 (2周) | 增量调试 (1周) |
| **总计** | **~11周** | **~8周** |

### 8.2 风险对比

| 风险 | 方案A | 方案B |
|------|-------|-------|
| 真机功能异常 | 🔴 高 (未验证) | 🟢 低 (已验证) |
| 协作冲突 | 🔴 高 (代码重写) | 🟢 低 (渐进重构) |
| 进度延期 | 🟠 中 | 🟢 低 |
| 技术难度 | 🟠 中 | 🟢 低 |

### 8.3 长期维护对比

| 维度 | 方案A | 方案B |
|------|-------|-------|
| 代码整洁度 | ✅ 高 (全新设计) | ⚠️ 中 (历史包袱) |
| 可扩展性 | ✅ 高 | ✅ 高 (重构后) |
| 团队熟悉度 | ⚠️ 需要学习 | ✅ 已熟悉 |

---

## 9. 最终建议

### 9.1 推荐方案B：基于 t32 重构

**理由：**
1. **保护已有投入** - 同事的调试工作不会浪费
2. **降低风险** - 基于已验证代码，风险可控
3. **更快见效** - 不需要重新实现已有功能
4. **便于协作** - 渐进式重构，不影响现有开发

### 9.2 实施建议

1. **先做目录结构调整** - 让代码组织更清晰
2. **逐步建立 HAL 层** - 封装而非重写
3. **并行添加 HTTP Server** - 与 TCP Client 共存过渡
4. **最后移除 TCP Client** - 确保 HTTP API 完善后再移除

### 9.3 t32_camera 的价值

虽然不直接使用 t32_camera，但其设计可以作为参考：
- HAL 接口设计
- Video Pipeline 架构
- HTTP API 设计
- PC 模拟实现

---

## 10. 下一步行动

1. **团队确认** - 确认采用方案B
2. **制定详细计划** - 细化每个 Phase 的任务
3. **启动 Phase 0** - 开始目录结构调整
4. **同步协作** - 与同事沟通重构计划，避免冲突
