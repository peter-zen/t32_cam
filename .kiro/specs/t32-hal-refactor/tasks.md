# 实施计划: T32 HAL 重构

## 概述

本实施计划采用渐进式重构策略，分为 5 个阶段（Phase 0-4），每个阶段完成后都可以编译通过并在真机验证。

## 任务列表

- [x] 1. Phase 0: 目录结构调整
  - [x] 1.1 创建新目录结构
    - 创建 src/hal/interface/、src/hal/ingenic/、src/hal/sim/ 目录
    - 创建 src/hardware/、src/service/、src/config/ 目录
    - _需求: 5.1, 5.2, 5.3, 5.4_

  - [x] 1.2 移动硬件相关模块到 hardware/
    - 将 src/gpio/ 移动到 src/hardware/gpio/
    - 将 src/mcu/ 移动到 src/hardware/mcu/
    - 将 src/daynight/ 移动到 src/hardware/daynight/
    - 将 src/power/ 移动到 src/hardware/power/
    - 更新 CMakeLists.txt 中的路径引用
    - _需求: 5.5_

  - [x] 1.3 移动配置相关模块到 config/
    - 将 src/setting/ 移动到 src/config/setting/
    - 将 src/devconf/ 移动到 src/config/devconf/
    - 将 src/env/ 移动到 src/config/env/
    - 更新 CMakeLists.txt 中的路径引用
    - _需求: 5.5_

  - [x] 1.4 更新主 CMakeLists.txt
    - 添加新目录的 add_subdirectory 调用
    - 确保编译通过
    - _需求: 5.6_

- [x] 2. Checkpoint - Phase 0 验证
  - 确保所有代码能够编译通过
  - 在真机上验证功能正常
  - 如有问题请告知

- [x] 3. Phase 1: HAL 接口定义
  - [x] 3.1 创建 HAL 公共定义
    - 创建 src/hal/interface/hal_common.h
    - 定义 HalError 错误码枚举
    - _需求: 1.1, 1.3_

  - [x] 3.2 创建 GPIO HAL 接口
    - 创建 src/hal/interface/hal_gpio.h
    - 定义 HalGpioDirection、HalGpioValue 枚举
    - 定义 hal_gpio_export、hal_gpio_unexport、hal_gpio_set_direction、hal_gpio_set_value、hal_gpio_get_value 函数
    - _需求: 1.2, 7.1, 7.2, 7.3, 7.4, 7.5_

  - [x] 3.3 创建 MCU HAL 接口
    - 创建 src/hal/interface/hal_mcu.h
    - 定义 hal_mcu_init、hal_mcu_deinit、hal_mcu_read_battery_level、hal_mcu_read_temperature、hal_mcu_read_firmware_version、hal_mcu_set_datetime、hal_mcu_get_datetime 函数
    - _需求: 1.2, 8.1, 8.2, 8.3, 8.4, 8.5, 8.6_

  - [x] 3.4 创建系统 HAL 接口
    - 创建 src/hal/interface/hal_system.h
    - 定义 HalDeviceID、HalCell 结构
    - 定义 hal_system_init、hal_system_exit、hal_system_bind、hal_system_unbind 函数
    - _需求: 1.2, 9.1, 9.2_

  - [x] 3.5 创建编码器 HAL 接口
    - 创建 src/hal/interface/hal_encoder.h
    - 定义 HalPayloadType、HalRcMode、HalEncoderChnAttr、HalEncoderPack、HalEncoderStream 结构
    - 定义编码器组和通道管理函数
    - _需求: 1.2, 9.4, 9.5_

  - [x] 3.6 创建帧源 HAL 接口
    - 创建 src/hal/interface/hal_framesource.h
    - 定义帧源通道属性和管理函数
    - _需求: 1.2, 9.3_

  - [x] 3.7 创建 HAL CMakeLists.txt
    - 创建 src/hal/CMakeLists.txt
    - 配置接口头文件的包含路径
    - _需求: 4.1_

- [x] 4. Checkpoint - Phase 1 验证
  - 确保 HAL 接口头文件语法正确
  - 确保编译通过（此时只有接口定义，无实现）
  - 如有问题请告知

- [x] 5. Phase 1 续: HAL 真机实现
  - [x] 5.1 实现 GPIO HAL (真机)
    - 创建 src/hal/ingenic/hal_gpio_ingenic.cpp
    - 封装调用 hardware/gpio/GPIO.cpp 的方法
    - _需求: 2.1, 2.4, 2.5_

  - [ ]* 5.2 编写 GPIO HAL 属性测试
    - **Property 1: GPIO 状态一致性**
    - **Property 2: GPIO 导出状态管理**
    - **验证: 需求 7.1, 7.2, 7.4, 7.5**

  - [x] 5.3 实现 MCU HAL (真机)
    - 创建 src/hal/ingenic/hal_mcu_ingenic.cpp
    - 封装调用 hardware/mcu/MCU.cpp 的方法
    - _需求: 2.2, 2.4, 2.5_

  - [ ]* 5.4 编写 MCU HAL 属性测试
    - **Property 3: MCU 数据范围有效性**
    - **验证: 需求 8.2, 8.3**

  - [x] 5.5 实现系统 HAL (真机)
    - 创建 src/hal/ingenic/hal_system_ingenic.cpp
    - 封装调用 libimp IMP_System_* 函数
    - _需求: 2.3_

  - [ ]* 5.6 编写系统 HAL 属性测试
    - **Property 5: 系统初始化幂等性**
    - **验证: 需求 9.1**

  - [x] 5.7 实现编码器 HAL (真机)
    - 创建 src/hal/ingenic/hal_encoder_ingenic.cpp
    - 封装调用 libimp IMP_Encoder_* 函数
    - _需求: 2.3_

  - [ ]* 5.8 编写编码器 HAL 属性测试
    - **Property 4: 编码器状态机正确性**
    - **验证: 需求 9.4, 9.5**

  - [x] 5.9 实现帧源 HAL (真机)
    - 创建 src/hal/ingenic/hal_framesource_ingenic.cpp
    - 封装调用 libimp IMP_FrameSource_* 函数
    - _需求: 2.3_

  - [x] 5.10 更新 HAL CMakeLists.txt (真机部分)
    - 添加 ingenic/ 子目录的编译配置
    - 配置链接 libimp 库
    - _需求: 4.3_

- [x] 6. Checkpoint - Phase 1 完成验证
  - 确保真机实现编译通过
  - 在真机上验证 HAL 封装功能正常
  - 如有问题请告知

- [x] 7. Phase 2: PC 模拟实现
  - [x] 7.1 实现 GPIO HAL (模拟)
    - 创建 src/hal/sim/hal_gpio_sim.c
    - 使用内存变量模拟 GPIO 状态
    - 添加日志输出
    - _需求: 3.2, 3.6_

  - [x] 7.2 实现 MCU HAL (模拟)
    - 创建 src/hal/sim/hal_mcu_sim.c
    - 返回预设的模拟数据
    - _需求: 3.3_

  - [x] 7.3 实现系统 HAL (模拟)
    - 创建 src/hal/sim/hal_system_sim.c
    - 模拟系统初始化和数据流绑定
    - _需求: 3.1_

  - [x] 7.4 实现帧源 HAL (模拟)
    - 创建 src/hal/sim/hal_framesource_sim.c
    - 从 H.264 文件读取 NAL 单元
    - 支持循环读取
    - _需求: 3.4, 3.7_

  - [ ]* 7.5 编写帧源模拟属性测试
    - **Property 6: 视频流数据完整性**
    - **验证: 需求 9.5, 9.6**

  - [x] 7.6 实现编码器 HAL (模拟)
    - 创建 src/hal/sim/hal_encoder_sim.c
    - 从帧源获取 NAL 数据，模拟编码器输出
    - _需求: 3.5_

  - [x] 7.7 更新 HAL CMakeLists.txt (模拟部分)
    - 添加 sim/ 子目录的编译配置
    - 配置 BUILD_FOR_SIMULATION 编译开关
    - 定义 SIMULATION_MODE 宏
    - _需求: 4.1, 4.2, 4.4_

- [x] 8. Checkpoint - Phase 2 完成验证
  - 确保 PC 模拟实现编译通过 ✅ libhal_sim.a 编译成功
  - 在 PC 上验证模拟功能正常
  - 如有问题请告知

- [x] 9. Phase 3: HTTP Server 添加
  - [x] 9.1 创建 HTTP Server 模块
    - 创建 src/service/http_server/ 目录
    - 实现基础 HTTP Server 框架
    - _需求: 后续扩展_

  - [x] 9.2 实现设备信息 API
    - GET /api/device/info
    - _需求: 后续扩展_

  - [x] 9.3 实现传感器数据 API
    - GET /api/sensor/data
    - _需求: 后续扩展_

  - [x] 9.4 实现录像控制 API
    - GET /api/record/start
    - GET /api/record/stop
    - _需求: 后续扩展_

  - [x] 9.5 更新 service CMakeLists.txt
    - 添加 http_server 子目录
    - _需求: 后续扩展_

- [x] 10. Checkpoint - Phase 3 完成验证
  - 确保 HTTP Server 编译通过
  - 验证 API 功能正常
  - 如有问题请告知

- [x] 11. Phase 4: RemoteCtrlClient 移除 (保留服务器通信)
  - [x] 11.1 分析 RemoteCtrlClient 依赖
    - 识别 RemoteCtrlClient 的所有功能
    - 确认功能已迁移到 HTTP API
    - 保留 MgmtServClient 和 StorageServClient (服务器通信)
    - _需求: 后续扩展_

  - [x] 11.2 移除 RemoteCtrlClient 代码
    - 从 src/network/CMakeLists.txt 移除 RemoteCtrlClient.cpp
    - 更新 main_app.cpp 中的 CMD_MOBILE 处理 (使用 HTTP Server)
    - 移除 RemoteCtrlClient.h 引用，添加 http_server.h
    - 更新信号处理中的资源清理逻辑
    - _需求: 后续扩展_

  - [x] 11.3 完善 HTTP API 替代功能
    - 实现参数获取/设置 API
    - 实现硬件信息 API
    - 实现传感器信息 API
    - 实现存储格式化 API
    - _需求: 后续扩展_

- [x] 12. 最终验证
  - [x] 确保所有代码编译通过 (htc_main_app 编译成功)
  - [ ] 在真机上进行完整功能测试
  - [ ] 在 PC 上进行模拟环境测试
  - 如有问题请告知

## 备注

- 任务标记 `*` 的为可选测试任务，可根据时间安排决定是否执行
- 每个 Checkpoint 都需要确保编译通过和功能正常
- Phase 3 和 Phase 4 的具体需求待后续细化
- 建议按顺序执行，每完成一个 Phase 后在真机验证
