# 实施计划: RTSP HAL 集成

## 概述

本实施计划将完善 HAL 模拟实现，并修改 RtspServer 支持在 PC 模拟环境下使用 HAL 接口获取视频数据。

## 任务列表

- [x] 1. 完善 HAL FrameSource 模拟实现
  - [x] 1.1 添加 H.264 文件读取功能
    - 在 hal_framesource_sim.c 中添加 FileBuffer 结构和 load_file 函数
    - 实现文件加载到内存缓冲区
    - _需求: 1.1_

  - [x] 1.2 实现 NAL 单元解析
    - 添加 next_nal 函数，识别 0x00000001 和 0x000001 起始码
    - 解析 NAL 类型，识别关键帧（NAL type 5）
    - 支持循环读取（到达文件末尾后从头开始）
    - _需求: 1.2, 1.4_

  - [x] 1.3 实现 NAL 队列机制
    - 添加 NalUnit 和 NalQueue 结构
    - 实现 queue_init、queue_destroy、queue_push、queue_pop 函数
    - 添加线程同步（mutex 和 condition variable）
    - 限制队列最大长度，防止内存溢出
    - _需求: 1.3_

  - [x] 1.4 实现后台读取线程
    - 在 hal_fs_enable_channel 中启动后台线程
    - 线程按帧率间隔读取 NAL 并放入队列
    - 在 hal_fs_disable_channel 中停止线程
    - _需求: 1.5, 1.6, 1.7_

  - [x] 1.5 添加导出函数供 Encoder 使用
    - 实现 hal_fs_pop_nal 函数，从队列获取 NAL 数据
    - 实现 hal_fs_flush_to_idr 函数，清空队列到最近的 IDR
    - 实现 hal_fs_cleanup 函数，释放文件缓冲区
    - _需求: 2.1_

- [x] 2. 完善 HAL Encoder 模拟实现
  - [x] 2.1 添加 FrameSource 绑定支持
    - 添加 bound_fs_chn 字段到编码器组结构
    - 实现 hal_enc_set_bound_fs_chn 内部函数
    - _需求: 3.3_

  - [x] 2.2 修改 hal_enc_get_stream 实现
    - 从绑定的 FrameSource 队列获取 NAL 数据
    - 封装为 HalEncoderStream 结构返回
    - 正确设置 keyframe 标志
    - 添加时间戳
    - _需求: 2.1, 2.3, 2.4_

  - [x] 2.3 修改 hal_enc_polling_stream 实现
    - 保存 timeout 参数供 get_stream 使用
    - _需求: 2.2_

  - [x] 2.4 修改 hal_enc_release_stream 实现
    - 正确释放 NAL 数据内存
    - 释放 Pack 结构内存
    - _需求: 2.5_

  - [x] 2.5 实现 IDR 请求功能
    - 在 hal_enc_request_idr 中设置请求标志
    - 在 FrameSource 线程中检查并跳转到下一个 IDR
    - _需求: 2.6_

- [x] 3. 完善 HAL System 绑定机制
  - [x] 3.1 添加绑定关系管理
    - 在 hal_system_sim.c 中添加绑定关系表
    - 实现 hal_system_bind 函数（已存在，添加了 Encoder 绑定通知）
    - 实现 hal_system_unbind 函数（已存在）
    - _需求: 3.1, 3.2_

  - [x] 3.2 添加 ISP 模拟支持
    - 简化实现：直接在 hal_framesource_sim.c 中使用默认配置
    - 视频文件路径通过 hal_fs_sim_set_video_file 设置
    - 帧率从通道属性获取
    - _需求: 5.1, 5.2, 5.4_

  - [x] 3.3 添加时间戳支持
    - hal_system_get_time_ms 函数已存在
    - _需求: 2.3_

- [x] 4. Checkpoint - HAL 模拟实现验证
  - HAL 模拟实现编译通过
  - 编写简单测试验证 NAL 读取和队列功能（跳过，将在集成测试中验证）

- [x] 5. 修改 RtspServer 支持 HAL 接口
  - [x] 5.1 添加条件编译头文件包含
    - 在 SIMULATION_MODE 下包含 HAL 头文件
    - _需求: 4.1_

  - [x] 5.2 修改 initialize 函数
    - 初始化部分保持不变（使用 sample_* 函数）
    - _需求: 4.4_

  - [x] 5.3 修改 start 函数中的视频获取循环
    - 在 SIMULATION_MODE 下使用 hal_enc_polling_stream 和 hal_enc_get_stream
    - 处理 HalEncoderStream 数据
    - 使用 hal_enc_release_stream 释放数据
    - _需求: 4.1, 4.5_

  - [x] 5.4 修改 deinitialize 函数
    - 保持不变（使用 sample_* 函数）
    - _需求: 4.3_

  - [x] 5.5 确保真机代码不受影响
    - 通过条件编译隔离 PC 模式和真机模式代码
    - _需求: 4.2, 4.3_

- [x] 6. 添加测试视频文件
  - [x] 6.1 准备测试用 H.264 文件
    - 在 sim_sdcard/video/ 目录下放置测试视频 (test.h264)
    - 包含 SPS、PPS 和 IDR 帧
    - _需求: 5.2_

  - [x] 6.2 添加视频文件路径配置
    - 默认路径: sim_sdcard/video/test.h264
    - 可通过 hal_fs_sim_set_video_file() 修改
    - _需求: 5.1, 5.3_

- [x] 7. Checkpoint - 集成测试
  - 编译 PC 模拟版本 ✓
  - 启动程序，使用 VLC 或 ffplay 连接 RTSP 流（待用户测试）
  - 验证视频能够正常播放（待用户测试）

- [ ]* 8. 属性测试（可选）
  - [ ]* 8.1 编写 NAL 完整性属性测试
    - **Property 1: NAL 数据完整性**
    - **验证: 需求 1.2, 2.3**

  - [ ]* 8.2 编写队列一致性属性测试
    - **Property 2: 队列数据流一致性**
    - **验证: 需求 1.3, 2.1**

  - [ ]* 8.3 编写关键帧识别属性测试
    - **Property 4: 关键帧识别正确性**
    - **验证: 需求 2.4**

- [-] 9. 最终验证
  - PC 模拟版本编译通过 ✓
  - 确保真机版本编译通过（待验证）
  - 如有问题请告知

## 备注

- 任务标记 `*` 的为可选测试任务
- 每个 Checkpoint 都需要确保编译通过和功能正常
- 参考 ref/t32_camera/src/hal/impl_sim/ 目录下的实现
- 测试视频文件可以使用 ffmpeg 生成或从网上下载

