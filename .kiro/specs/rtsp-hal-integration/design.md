# 设计文档

## 概述

本设计文档描述了如何让 RtspServer 在 PC 模拟环境下使用 HAL 层接口获取视频数据。通过完善 HAL FrameSource 和 Encoder 的模拟实现，并修改 RtspServer 支持条件编译，实现 PC 环境下完整的 RTSP 流媒体功能。

### 设计目标

1. **完善 HAL 模拟实现**: 参考 ref/t32_camera 项目，实现真实的 H.264 文件读取和 NAL 解析
2. **最小侵入性修改**: 通过条件编译让 RtspServer 支持双模式，不影响真机代码
3. **数据流一致性**: PC 模拟环境下的数据流与真机保持一致的接口和行为

### 参考设计

本设计参考 `ref/t32_camera/src/hal/impl_sim/` 目录下的实现：
- `hal_framesource_sim.c` - FrameSource 模拟实现
- `hal_encoder_sim.c` - Encoder 模拟实现
- `hal_video_file.c` - H.264 文件读取工具

## 架构

### 数据流架构图

```
┌─────────────────────────────────────────────────────────────────────┐
│                         RtspServer                                   │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │                    pullFrameThread                           │   │
│  │  ┌─────────────────────────────────────────────────────┐    │   │
│  │  │ #ifdef SIMULATION_MODE                               │    │   │
│  │  │   hal_enc_polling_stream() → hal_enc_get_stream()   │    │   │
│  │  │ #else                                                │    │   │
│  │  │   IMP_Encoder_PollingStream() → IMP_Encoder_GetStream()│   │   │
│  │  │ #endif                                               │    │   │
│  │  └─────────────────────────────────────────────────────┘    │   │
│  └─────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────────┐
│                    HAL Encoder (hal_encoder_sim.c)                   │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │ hal_enc_get_stream():                                        │   │
│  │   1. 获取绑定的 FrameSource 通道                              │   │
│  │   2. 调用 hal_fs_pop_nal() 从队列获取 NAL                     │   │
│  │   3. 封装为 HalEncoderStream 返回                            │   │
│  └─────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────────┐
│                 HAL FrameSource (hal_framesource_sim.c)              │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │ fs_thread_func():                                            │   │
│  │   1. 从 H.264 文件读取下一个 NAL                              │   │
│  │   2. 放入 NAL 队列                                           │   │
│  │   3. 按帧率间隔循环                                          │   │
│  └─────────────────────────────────────────────────────────────┘   │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │ NAL Queue:                                                   │   │
│  │   [NAL1] → [NAL2] → [NAL3] → ... → [NALn]                   │   │
│  └─────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────────┐
│                      H.264 Video File                                │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │ sim_sdcard/video/test.h264                                   │   │
│  │ [SPS][PPS][IDR][P][P][P]...[IDR][P][P][P]...                 │   │
│  └─────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────┘
```

### 模块关系图

```
┌─────────────────────────────────────────────────────────────────────┐
│                          src/media/rtsp/                             │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │ RtspServer.cpp                                               │   │
│  │   - 条件编译: SIMULATION_MODE                                │   │
│  │   - PC 模式: 调用 hal_enc_* 接口                             │   │
│  │   - 真机模式: 调用 IMP_Encoder_* 接口                        │   │
│  └─────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────┘
                                │
        ┌───────────────────────┴───────────────────────┐
        │                                               │
        ▼                                               ▼
┌───────────────────────┐                   ┌───────────────────────┐
│   src/hal/sim/        │                   │   src/hal/ingenic/    │
│   (PC 模拟实现)        │                   │   (真机实现)           │
│                       │                   │                       │
│ hal_system_sim.c      │                   │ hal_system_ingenic.cpp│
│ hal_framesource_sim.c │                   │ hal_framesource_*.cpp │
│ hal_encoder_sim.c     │                   │ hal_encoder_*.cpp     │
└───────────────────────┘                   └───────────────────────┘
```

## 组件和接口

### HAL FrameSource 模拟实现 (hal_framesource_sim.c)

需要添加的核心功能：

```c
/* 文件缓冲区结构 */
typedef struct {
    uint8_t* data;      /* 文件数据 */
    size_t size;        /* 文件大小 */
    size_t pos;         /* 当前读取位置 */
} FileBuffer;

/* NAL 单元结构 */
typedef struct NalUnit {
    uint8_t* data;      /* NAL 数据 (包含起始码) */
    size_t size;        /* NAL 大小 */
    bool keyframe;      /* 是否为关键帧 */
    struct NalUnit* next;
} NalUnit;

/* NAL 队列结构 */
typedef struct {
    NalUnit* head;
    NalUnit* tail;
    int count;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} NalQueue;

/* 通道结构 */
typedef struct {
    FsChnState state;
    HalFSChnAttr attr;
    pthread_t thread;
    bool thread_running;
    NalQueue queue;
} FsChannel;

/* 内部函数 */
static bool load_file(const char* path, FileBuffer* buf);
static bool next_nal(FileBuffer* buf, uint8_t** data, size_t* size, bool* keyframe);
static void queue_push(NalQueue* q, uint8_t* data, size_t size, bool keyframe);
static bool queue_pop(NalQueue* q, uint8_t** data, size_t* size, bool* keyframe, uint32_t timeout_ms);

/* 导出给 Encoder 使用的内部函数 */
bool hal_fs_pop_nal(int chn_id, uint8_t** data, size_t* size, bool* keyframe, uint32_t timeout_ms);
```

### HAL Encoder 模拟实现 (hal_encoder_sim.c)

需要修改的核心功能：

```c
/* 外部声明 - 从 FrameSource 获取 NAL */
extern bool hal_fs_pop_nal(int chn_id, uint8_t** data, size_t* size, bool* keyframe, uint32_t timeout_ms);

int hal_enc_get_stream(int chn_id, HalEncoderStream* stream, bool block) {
    /* 1. 获取绑定的 FrameSource 通道 */
    int fs_chn = get_bound_fs_channel(chn_id);
    
    /* 2. 从 FrameSource 队列获取 NAL */
    uint8_t* nal_data = NULL;
    size_t nal_size = 0;
    bool keyframe = false;
    
    if (!hal_fs_pop_nal(fs_chn, &nal_data, &nal_size, &keyframe, timeout_ms)) {
        return HAL_ERR_TIMEOUT;
    }
    
    /* 3. 封装为 HalEncoderStream */
    HalEncoderPack* pack = malloc(sizeof(HalEncoderPack));
    pack->data = nal_data;
    pack->length = nal_size;
    pack->keyframe = keyframe;
    pack->timestamp = get_current_timestamp_us();
    
    stream->pack = pack;
    stream->pack_count = 1;
    stream->seq = channel->seq++;
    
    return HAL_SUCCESS;
}
```

### HAL System 绑定机制 (hal_system_sim.c)

需要添加的绑定管理：

```c
/* 绑定关系表 */
typedef struct {
    HalDeviceID src;    /* 源设备 (FrameSource) */
    HalDeviceID dst;    /* 目标设备 (Encoder) */
    bool active;
} BindEntry;

static BindEntry g_bindings[MAX_BINDINGS];

int hal_system_bind(const HalCell* src, const HalCell* dst) {
    /* 记录绑定关系 */
    /* 通知 Encoder 绑定的 FrameSource 通道 */
    hal_enc_set_bound_fs_chn(dst->group_id, src->chn_id);
    return HAL_SUCCESS;
}
```

### RtspServer 条件编译修改

```cpp
// RtspServer.cpp

#ifdef SIMULATION_MODE
#include "hal_system.h"
#include "hal_framesource.h"
#include "hal_encoder.h"
#endif

bool RtspServer::start(int chnNum, int payloadType) {
    // ... 初始化代码 ...
    
#ifdef SIMULATION_MODE
    ret = hal_enc_start_recv_pic(chnNum);
#else
    ret = IMP_Encoder_StartRecvPic(chnNum);
#endif
    
    while (this->pullFrameThreadRun) {
#ifdef SIMULATION_MODE
        ret = hal_enc_polling_stream(chnNum, 1000);
        if (ret != HAL_SUCCESS) {
            continue;
        }
        
        HalEncoderStream stream;
        ret = hal_enc_get_stream(chnNum, &stream, true);
        if (ret != HAL_SUCCESS) {
            continue;
        }
        
        // 处理 stream.pack 数据
        // ...
        
        hal_enc_release_stream(chnNum, &stream);
#else
        ret = IMP_Encoder_PollingStream(chnNum, 1000);
        if (ret < 0) {
            continue;
        }
        
        IMPEncoderStream stream;
        ret = IMP_Encoder_GetStream(chnNum, &stream, 1);
        if (ret < 0) {
            continue;
        }
        
        // 处理 stream.pack 数据
        // ...
        
        IMP_Encoder_ReleaseStream(chnNum, &stream);
#endif
    }
}
```

## 数据模型

### NAL 单元类型

```c
/* H.264 NAL 类型 */
#define NAL_TYPE_SLICE      1   /* 非 IDR 图像片 */
#define NAL_TYPE_DPA        2   /* 数据分区 A */
#define NAL_TYPE_DPB        3   /* 数据分区 B */
#define NAL_TYPE_DPC        4   /* 数据分区 C */
#define NAL_TYPE_IDR        5   /* IDR 图像 (关键帧) */
#define NAL_TYPE_SEI        6   /* 补充增强信息 */
#define NAL_TYPE_SPS        7   /* 序列参数集 */
#define NAL_TYPE_PPS        8   /* 图像参数集 */
#define NAL_TYPE_AUD        9   /* 访问单元分隔符 */
```

### 视频文件配置

```c
/* 默认配置 */
#define DEFAULT_H264_PATH       "sim_sdcard/video/test.h264"
#define DEFAULT_FPS             25
#define DEFAULT_QUEUE_MAX_SIZE  60

/* 配置结构 */
typedef struct {
    char h264_path[256];    /* H.264 文件路径 */
    int fps;                /* 帧率 */
    int queue_max_size;     /* 队列最大长度 */
} SimVideoConfig;
```

## 正确性属性

*正确性属性是一种特征或行为，应该在系统的所有有效执行中保持为真——本质上是关于系统应该做什么的形式化陈述。属性作为人类可读规范和机器可验证正确性保证之间的桥梁。*

### Property 1: NAL 数据完整性

*对于任意* 从 hal_enc_get_stream 获取的 HalEncoderStream，其中的 NAL 数据应该以有效的 H.264 起始码（0x00000001 或 0x000001）开头，且 NAL 类型字节应该在有效范围内（1-31）。

**验证: 需求 1.2, 2.3**

### Property 2: 队列数据流一致性

*对于任意* 放入 FrameSource 队列的 NAL 单元，通过 Encoder 获取时应该保持数据内容不变，且获取顺序与放入顺序一致（FIFO）。

**验证: 需求 1.3, 2.1**

### Property 3: 循环读取正确性

*对于任意* H.264 文件，当读取位置到达文件末尾后，下一次读取应该从文件开头重新开始，且读取的第一个 NAL 应该与首次读取时相同。

**验证: 需求 1.4**

### Property 4: 关键帧识别正确性

*对于任意* 从 Encoder 获取的 NAL 单元，如果 NAL 类型为 5（IDR），则 HalEncoderPack.keyframe 应该为 true；如果 NAL 类型不为 5，则 keyframe 应该为 false。

**验证: 需求 2.4**

### Property 5: IDR 请求响应

*对于任意* 调用 hal_enc_request_idr 后的下一次 hal_enc_get_stream 调用，返回的帧应该是关键帧（keyframe = true）。

**验证: 需求 2.6**

### Property 6: 绑定关系正确性

*对于任意* 通过 hal_system_bind 建立的 FrameSource-Encoder 绑定，Encoder 应该只能从其绑定的 FrameSource 获取数据，不同的绑定关系应该相互独立。

**验证: 需求 3.3, 3.4**

### Property 7: 错误码一致性

*对于任意* HAL 函数调用返回的错误码，应该是 HalError 枚举中定义的值（HAL_SUCCESS, HAL_ERR_INVALID_PARAM, HAL_ERR_NOT_INIT, HAL_ERR_TIMEOUT 等）。

**验证: 需求 6.2**

## 错误处理

### 错误场景和处理

| 错误场景 | 错误码 | 处理方式 |
|---------|--------|---------|
| H.264 文件不存在 | HAL_ERR_IO | 输出错误日志，hal_fs_enable_channel 返回失败 |
| 文件读取失败 | HAL_ERR_IO | 输出错误日志，返回失败 |
| 内存分配失败 | HAL_ERR_NO_MEM | 输出错误日志，返回失败 |
| 队列为空且超时 | HAL_ERR_TIMEOUT | hal_enc_get_stream 返回超时 |
| 通道未初始化 | HAL_ERR_NOT_INIT | 返回未初始化错误 |
| 参数无效 | HAL_ERR_INVALID_PARAM | 返回参数无效错误 |

### 日志输出

```c
/* 日志级别 */
#define HAL_LOG_ERROR   1
#define HAL_LOG_WARN    2
#define HAL_LOG_INFO    3
#define HAL_LOG_DEBUG   4

/* 日志宏 */
#define HAL_LOGE(fmt, ...) printf("[HAL ERROR] " fmt "\n", ##__VA_ARGS__)
#define HAL_LOGW(fmt, ...) printf("[HAL WARN] " fmt "\n", ##__VA_ARGS__)
#define HAL_LOGI(fmt, ...) printf("[HAL INFO] " fmt "\n", ##__VA_ARGS__)
#define HAL_LOGD(fmt, ...) printf("[HAL DEBUG] " fmt "\n", ##__VA_ARGS__)
```

## 测试策略

### 单元测试

1. **文件读取测试**: 测试 load_file 函数能否正确读取 H.264 文件
2. **NAL 解析测试**: 测试 next_nal 函数能否正确解析 NAL 单元
3. **队列操作测试**: 测试 queue_push/queue_pop 的基本功能
4. **绑定测试**: 测试 hal_system_bind/unbind 的基本功能

### 属性测试

使用属性测试框架验证正确性属性：

1. **NAL 完整性测试**: 生成随机 H.264 数据，验证解析结果的有效性
2. **队列一致性测试**: 生成随机 NAL 序列，验证 FIFO 顺序
3. **关键帧识别测试**: 生成包含不同 NAL 类型的数据，验证 keyframe 标志

### 集成测试

1. **端到端测试**: 启动 RtspServer，使用 VLC 或 ffplay 连接验证视频流
2. **压力测试**: 长时间运行验证内存泄漏和稳定性

### 测试配置

- 属性测试最少运行 100 次迭代
- 每个属性测试需要标注对应的设计属性编号
- 测试框架: C 语言使用 Unity 或自定义测试框架

