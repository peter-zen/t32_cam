# 需求文档

## 简介

本项目旨在解决 PC 模拟环境下 RTSP 功能不正常的问题。当前 RtspServer 直接调用 libimp API，而 PC 模拟环境下的 imp_stub 只是空壳，没有真实视频数据。通过让 RtspServer 在 PC 模式下使用 HAL 层接口，可以从 H.264 文件读取真实的视频数据，实现完整的 RTSP 流媒体功能。

## 术语表

- **HAL (Hardware Abstraction Layer)**: 硬件抽象层，提供统一的接口屏蔽底层硬件差异
- **libimp**: 君正（Ingenic）T32 芯片的多媒体处理库
- **RTSP (Real Time Streaming Protocol)**: 实时流传输协议
- **NAL (Network Abstraction Layer)**: H.264/H.265 视频编码的网络抽象层单元
- **FrameSource**: 视频帧源模块，负责从 ISP 或文件获取图像数据
- **Encoder**: 视频编码器模块，负责 H.264/H.265 编码
- **PC_Simulation**: PC 模拟环境，用于在 PC 上模拟硬件行为
- **imp_stub**: 当前 PC 模拟环境下的 libimp 空壳实现

## 需求

### 需求 1: 完善 HAL FrameSource 模拟实现

**用户故事:** 作为开发者，我希望 HAL FrameSource 模拟实现能够从 H.264 文件读取 NAL 单元，以便在 PC 环境下提供真实的视频数据。

#### 验收标准

1. THE hal_framesource_sim SHALL 从配置的 H.264 文件路径读取视频数据
2. THE hal_framesource_sim SHALL 解析 H.264 文件中的 NAL 单元（识别 0x00000001 或 0x000001 起始码）
3. THE hal_framesource_sim SHALL 将解析的 NAL 单元放入内部队列
4. THE hal_framesource_sim SHALL 支持循环读取，当文件读取完毕后从头开始
5. THE hal_framesource_sim SHALL 根据配置的帧率控制 NAL 单元的输出速度
6. WHEN hal_fs_enable_channel 被调用时，THE hal_framesource_sim SHALL 启动后台线程读取视频数据
7. WHEN hal_fs_disable_channel 被调用时，THE hal_framesource_sim SHALL 停止后台线程

### 需求 2: 完善 HAL Encoder 模拟实现

**用户故事:** 作为开发者，我希望 HAL Encoder 模拟实现能够从 FrameSource 队列获取 NAL 数据，以便模拟编码器输出。

#### 验收标准

1. THE hal_encoder_sim SHALL 从绑定的 FrameSource 通道队列获取 NAL 数据
2. THE hal_encoder_sim SHALL 在 hal_enc_polling_stream 中等待数据就绪
3. THE hal_encoder_sim SHALL 在 hal_enc_get_stream 中返回包含 NAL 数据的 HalEncoderStream 结构
4. THE hal_encoder_sim SHALL 正确识别关键帧（NAL type 5 为 IDR 帧）
5. THE hal_encoder_sim SHALL 在 hal_enc_release_stream 中释放已获取的流数据
6. WHEN hal_enc_request_idr 被调用时，THE hal_encoder_sim SHALL 跳转到下一个 IDR 帧

### 需求 3: HAL System 绑定机制

**用户故事:** 作为开发者，我希望 HAL System 能够管理 FrameSource 和 Encoder 之间的数据流绑定，以便模拟 libimp 的 Cell-Bind 机制。

#### 验收标准

1. THE hal_system_sim SHALL 提供 hal_system_bind 函数绑定 FrameSource 和 Encoder
2. THE hal_system_sim SHALL 提供 hal_system_unbind 函数解除绑定
3. WHEN FrameSource 和 Encoder 绑定后，THE hal_encoder_sim SHALL 能够从对应的 FrameSource 获取数据
4. THE hal_system_sim SHALL 维护绑定关系表，支持多通道绑定

### 需求 4: RtspServer HAL 集成

**用户故事:** 作为开发者，我希望 RtspServer 在 PC 模拟环境下使用 HAL 接口获取视频数据，以便在 PC 上测试 RTSP 功能。

#### 验收标准

1. WHEN SIMULATION_MODE 宏定义时，THE RtspServer SHALL 使用 HAL 接口（hal_enc_*）获取视频流
2. WHEN SIMULATION_MODE 未定义时，THE RtspServer SHALL 保持使用 libimp API（IMP_Encoder_*）
3. THE RtspServer SHALL 通过条件编译实现双模式支持，不影响真机代码
4. THE RtspServer SHALL 在 PC 模式下正确初始化 HAL 系统、FrameSource 和 Encoder
5. THE RtspServer SHALL 在 PC 模式下正确处理 HAL 返回的 HalEncoderStream 数据

### 需求 5: 视频文件配置

**用户故事:** 作为开发者，我希望能够配置模拟视频文件的路径，以便使用不同的测试视频。

#### 验收标准

1. THE PC_Simulation SHALL 支持通过配置文件或环境变量指定 H.264 文件路径
2. THE PC_Simulation SHALL 提供默认的视频文件路径（如 sim_sdcard/video/test.h264）
3. IF 配置的视频文件不存在，THEN THE PC_Simulation SHALL 输出错误日志并返回失败
4. THE PC_Simulation SHALL 支持配置模拟帧率（默认 25fps）

### 需求 6: 错误处理和日志

**用户故事:** 作为开发者，我希望 HAL 模拟实现有完善的错误处理和日志输出，以便调试问题。

#### 验收标准

1. THE hal_sim SHALL 在关键操作处输出日志（初始化、启动、停止、错误）
2. THE hal_sim SHALL 使用统一的 HAL 错误码返回错误
3. IF 文件读取失败，THEN THE hal_sim SHALL 返回 HAL_ERR_IO
4. IF 内存分配失败，THEN THE hal_sim SHALL 返回 HAL_ERR_NO_MEM
5. IF 操作超时，THEN THE hal_sim SHALL 返回 HAL_ERR_TIMEOUT

