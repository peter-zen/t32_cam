# T32 拍照/录影功能实现分析

> 分析日期：2026-05-04
> 分析范围：T32 DSP应用层代码（src/）及厂商SDK（sdk/）
> 关联文档：[camera-photo-recording-implementation-status.md](camera-photo-recording-implementation-status.md)（T32实现现状 + 差距分析）

## 一、整体架构分层

```
┌─────────────────────────────────────────────────┐
│  Layer 4: HTTP API (CivetWeb, 端口80)            │  ← 我们实现
│  src/service/http_server/http_api_v1.cpp         │
├─────────────────────────────────────────────────┤
│  Layer 3: Service 层                             │  ← 我们实现
│  ICameraService / CameraServiceT32               │
│  CameraPropertyService / PhotoJobManager         │
├─────────────────────────────────────────────────┤
│  Layer 2: Media 层                               │  ← 我们实现
│  ImageSnap (拍照) / VideoRecorder (录影)          │
│  RtspServer (RTSP流) / MediaFIFO                 │
├─────────────────────────────────────────────────┤
│  Layer 1: HAL 抽象层                             │  ← 我们实现
│  IVideo / IVideoStream / IAudio / IAudioStream   │
│  HalProvider 工厂 (编译时切换 Sim/Ingenic)        │
├─────────────────────────────────────────────────┤
│  Layer 0: Ingenic IMP SDK                        │  ← 厂商提供
│  libimp.so + 头文件 (imp_encoder.h 等)           │
└─────────────────────────────────────────────────┘
```

---

## 二、SDK 原生提供的能力 (Layer 0)

T32使用 **君正 (Ingenic) T32 芯片**，厂商SDK（`sdk/` 目录）以预编译库 + C头文件形式提供，核心库为 `libimp.so`。

### SDK 提供的拍照相关API:

| API | 功能 |
|-----|------|
| `IMP_FrameSource_GetFrame()` | 从传感器获取原始YUV帧 |
| `IMP_FrameSource_SnapFrame()` | 快照方式获取一帧 |
| `IMP_Encoder_CreateGroup/CreateChn` | 创建编码器组/通道 |
| `IMP_Encoder_StartRecvPic()` | 启动JPEG编码（单帧） |
| `IMP_Encoder_PollingStream/GetStream` | 轮询并获取编码后的JPEG数据 |
| `IMP_Encoder_RequestIDR()` | 请求I帧 |

### SDK 提供的录影相关API:

| API | 功能 |
|-----|------|
| `IMP_Encoder_StartRecvPic()` | 启动H.264/H.265编码（连续） |
| `IMP_Encoder_PollingStream()` | 持续轮询编码流 |
| `IMP_Encoder_GetStream/ReleaseStream` | 获取/释放编码帧 |
| `IMP_Audio_*` 系列 | 音频采集 |

### SDK 提供的ISP控制:

| API | 功能 |
|-----|------|
| `IMP_ISP_Tuning_SetISPRunningMode` | 日夜模式切换 |
| `IMP_System_Bind/Unbind` | 管线绑定 (FrameSource → Encoder) |

### SDK 提供的物理通道:

每个传感器有 **3个物理通道**：
- **Channel 0** = 高清 (HD) 流
- **Channel 1** = 标清 (SD/IVS) 流
- **Channel 2** = JPEG 编码通道

**总结：SDK只提供"从传感器取帧"和"编码"这两个原子能力，不提供文件存储、MP4封装、业务逻辑等任何上层功能。**

---

## 三、我们在SDK之上增加的功能

### Layer 1 - HAL抽象层 (`src/hal/`)

- **抽象接口** (`IVideo`, `IVideoStream`, `IAudio`, `IAudioStream`)：将SDK的C API封装为C++面向对象接口
- **SimVideo/SimAudio**：模拟实现，从文件读取测试数据，支持PC端仿真
- **HalProvider工厂**：编译时自动切换真机/模拟实现

关键文件：
- `src/hal/include/IVideo.h` — 视频抽象接口
- `src/hal/include/IAudio.h` — 音频抽象接口
- `src/hal/include/HalProvider.h` — 工厂
- `src/hal/ingenic/IngenicVideo.h/.cpp` — 真机实现
- `src/hal/simu/SimVideo.h/.cpp` — 模拟实现

### Layer 2 - Media层 (`src/media/`)

- **ImageSnap** (`src/media/snap/ImageSnap.cpp`)：
  - 封装拍照完整流程：创建视频流 → 配置JPEG编码(FIXQP, quality=40) → 启动 → 轮询帧 → 写文件 → 停止
  - 支持单拍、连拍、异步回调拍照
  - 管理日夜模式切换
- **VideoRecorder** (`src/media/video/VideoRecorder.cpp`)：
  - 封装录影完整流程：创建H.264编码流(CBR) + 可选AAC音频流
  - 使用 **minimp4** 库进行MP4封装（fMP4格式）
  - 音频采集在独立线程，通过线程安全队列与视频帧同步
  - 支持定时录影、异步回调
- **RtspServer** (`src/media/rtsp/`)：RTSP实时流服务器（端口554）

### Layer 3 - Service层 (`src/service/camera/`)

- **ICameraService接口** (`ICameraService.h`)：定义完整的拍照/录影业务API
- **CameraServiceT32** (`impl/CameraServiceT32.cpp`)：真机实现
  - 管理拍照/录影状态机
  - 生成文件路径：`/sdcard/DCIM/IMG_<时间戳>.jpg` / `VID_<时间戳>.mp4`
  - 从CameraPropertyService读取录影参数（分辨率、帧率、码率）
- **CameraPropertyService**：相机参数管理，JSON持久化到 `setting.json`
- **PhotoJobManager** (`http_server/PhotoJobManager.h`)：HTTP异步拍照任务队列
- **CameraParameterRegistry**：定义所有CPS参数（含CAM_Mode）

### Layer 4 - HTTP API层 (`src/service/http_server/`)

RESTful API端点：

| 方法 | 路径 | 功能 |
|------|------|------|
| POST | `/api/v1/camera/photo` | 拍照（同步/异步） |
| GET | `/api/v1/camera/photo/status` | 拍照任务状态 |
| POST | `/api/v1/camera/photo/burst` | 连拍 |
| POST | `/api/v1/camera/photo/timer` | 定时拍照（启动/停止） |
| POST | `/api/v1/camera/video/start` | 开始录影 |
| POST | `/api/v1/camera/video/stop` | 停止录影 |
| GET | `/api/v1/camera/video/status` | 录影状态 |
| GET | `/api/v1/camera/preview` | 实时预览（JPEG/MJPEG） |
| GET | `/api/v1/camera/properties` | 获取所有参数 |
| POST | `/api/v1/camera/properties` | 设置参数 |
| POST | `/api/v1/camera/presets` | 预设切换 |

通信通道：
- **HTTP REST API**（端口80）：主要控制通道
- **RTSP**（端口554）：实时视频流
- **TCP事件推送**（端口5000）：异步事件通知（拍照完成等）
- **mDNS**（`_t32cam._tcp`）：设备发现

### API调用链路

**拍照调用链：**
```
HTTP POST /api/v1/camera/photo
  → http_api_v1.cpp: api_v1_camera_photo()
    → CameraServiceT32::takePhoto()
      → media::ImageSnap::snap(filename)
        → hal::IVideoStream::configure/start/polling/getFrame
          → hal::IngenicVideoStream (真机)
            → T32 SDK: IMP_Encoder_* 调用
```

**录影调用链：**
```
HTTP POST /api/v1/camera/video/start
  → CameraServiceT32::startRecord()
    → CameraPropertyService::getVideoRecordConfig()
    → media::VideoRecorder::record(path, callback, duration)
      → hal::IVideoStream + hal::IAudioStream
        → T32 SDK: IMP 编码器 + 音频采集
        → minimp4: MP4封装
```

---

## 四、工作模式 (workingMode) — 硬件级启动模式

定义在 `src/app/workmode/WorkMode.h`，由 **MCU寄存器** 或 **GPIO引脚** 在启动时一次性决定。

| 值 | 模式 | MCU寄存器值 | 启动后行为 |
|----|------|------------|-----------|
| 0 | `WORKING_MODE_SNAP_ONLY` | 0 | 仅拍照，拍完关机 |
| 1 | `WORKING_MODE_SNAP_UPLOAD` | 1 (或-1默认) | 拍照 → 联网 → 上传 |
| 2 | `WORKING_MODE_UPLOAD_ONLY` | 2 | 仅联网上传已有照片 |
| 3 | `WORKING_MODE_TEST_ONLY` | 3 | 测试/手机配对模式 |
| 4 | `WORKING_MODE_UVC` | 4 | USB视频类/RTSP流模式 |

### 工作模式 → 命令映射（main_app.cpp）

| Work Mode | 命令标志 | 行为 |
|-----------|---------|------|
| SNAP_ONLY | `CMD_SNAP` | 仅拍照 |
| SNAP_UPLOAD | `CMD_SNAP \| CMD_CONN_NET \| CMD_DHCP \| CMD_NTP \| CMD_UPLOAD` | 拍照+联网+上传 |
| UPLOAD_ONLY | `CMD_CONN_NET \| CMD_DHCP \| CMD_NTP \| CMD_UPLOAD` | 联网+上传 |
| TEST_ONLY | `CMD_MOBILE` | 启动HTTP/RTSP/mDNS服务 |
| UVC | `CMD_CONN_NET \| CMD_DHCP \| CMD_RTSP_SERVER` | RTSP流 |

**注意**：workingMode 是已实现的，main_app.cpp 中有 switch/case 根据模式执行对应命令。运行时切换需要MCU重新设置寄存器后重启。

---

## 五、拍摄模式 (CAM_Mode) — 拍照/录影组合

定义在 `CameraParameterRegistry.cpp`，通过APP/云端设置，存储在 `Settings::cameraMode`。

| 值 | 模式 | 设计意图 |
|----|------|---------|
| **0** | 仅拍照 | PIR/定时触发时只拍照片 |
| **1** | 拍照+录像 | PIR触发时先拍照再录一段视频 |
| **2** | 仅录像 | PIR/定时触发时只录视频 |
| **3** | 同步拍录 | 同时进行拍照和录像（边录边拍） |
| **4** | 智感录像 | 运动检测触发的智能录像（需工厂开关`Smart_DCVR_EN`） |
| **5** | 全时录像 | 持续不间断录像（需工厂开关`Video_ATR_EN`） |

### 实现状态：未实现（仅定义）

**CAM_Mode 是一个"死设置"** — 存在完整的存储/传输链路，但没有任何代码读取它来做业务分发。

`cameraMode` 的所有出现位置：
- `Settings.h` 字段定义（line 56-57）
- `Settings.cpp` JSON序列化/反序列化
- `RemoteCtrlClient.cpp` 网络收发（接收CAM_Mode → 存储；发送状态 → 导出）
- `MgmtServClient.cpp` 网络收发（同上）
- `CameraPropertyService.cpp` 属性getter/setter
- `CameraParameterRegistry.cpp` 参数定义（选项0-5）
- `MCU.cpp` I2C寄存器映射（`PARAM_CAM_MODE` 定义但从未读写）
- `test_camera_properties.cpp` 单元测试

**不存在任何**：
- `switch(cameraMode)` 或 `if(cameraMode == ...)` 条件分发
- PIR触发事件处理函数
- 定时触发事件处理函数中读取cameraMode
- 将cameraMode映射到拍照/录影动作的代码

### 相关配置参数现状

以下参数同样只是存储/传输，无业务逻辑读取：

| 参数 | 说明 | 现状 |
|------|------|------|
| `pirEn` | PIR传感器开关 | 仅存储 |
| `ckPirSensitivity` | PIR灵敏度 | 仅存储 |
| `trigInterval` | 触发间隔 | 仅存储 |
| `timerEn` | 定时开关 | 仅存储 |
| `timerLapse` | 定时间隔 | 仅存储 |
| `continuous_record` | 连续录影策略 | 仅存储 |
| `Smart_DCVR_EN` | 智感录像工厂开关 | 仅定义（默认0） |
| `Video_ATR_EN` | 全时录像工厂开关 | 仅定义（默认0） |

### 6种模式的实现状态总结

| 值 | 模式 | 状态 | 说明 |
|----|------|------|------|
| 0 | 仅拍照 | 未实现 | 无触发-分发逻辑 |
| 1 | 拍照+录像 | 未实现 | 无触发-分发逻辑 |
| 2 | 仅录像 | 未实现 | 无触发-分发逻辑 |
| 3 | 同步拍录 | 未实现 | 无触发-分发逻辑 |
| 4 | 智感录像 | 未实现 | 无触发-分发逻辑，工厂开关也是死配置 |
| 5 | 全时录像 | 未实现 | 无触发-分发逻辑，工厂开关也是死配置 |

**已实现的能力**：通过HTTP API可以独立调用 `takePhoto()` 和 `startRecord()`，但这些是手动调用，与cameraMode无关。

---

## 六、关键结论

1. **SDK只提供原子能力**：取帧 + 编码，没有任何业务逻辑。
2. **所有拍照/录影基础功能都是我们实现的**：JPEG保存、MP4封装(minimp4)、音频混流、文件路径管理、状态机、HTTP API。
3. **workingMode（启动模式）已实现**：5种模式在 `main_app.cpp` 中有实际的switch/case分发。
4. **CAM_Mode（拍摄模式）6种组合全部未实现**：参数定义、存储、网络传输都完整，但缺少核心的"触发→读取模式→分发动作"逻辑。
5. **PIR/定时触发逻辑不在DSP应用层**：MCU I2C寄存器中定义了PIR相关参数，但DSP端没有对应的事件处理。这部分逻辑可能在MCU固件侧，或者尚未实现。
6. **其他未实现**：`startBurstPhoto()` 返回-1，`SystemServiceT32::setWorkMode()` 是stub。

---

## 七、关键文件索引

| 文件 | 角色 |
|------|------|
| `src/app/workmode/WorkMode.h/.cpp` | 启动模式定义与检测 |
| `src/app/media_app.cpp` | 入口：读取启动模式，执行quick_snap，启动main_app |
| `src/app/main_app.cpp` | 启动模式→命令映射（已实现） |
| `src/hal/include/IVideo.h` | 视频抽象接口 |
| `src/hal/include/IAudio.h` | 音频抽象接口 |
| `src/hal/include/HalProvider.h` | HAL工厂 |
| `src/hal/ingenic/IngenicVideo.h/.cpp` | 真机视频实现（调用SDK） |
| `src/media/snap/ImageSnap.h/.cpp` | 拍照实现 |
| `src/media/video/VideoRecorder.h/.cpp` | 录影实现（minimp4封装） |
| `src/media/video/VideoParams.h` | 录影参数 |
| `src/service/camera/ICameraService.h` | 相机服务接口 |
| `src/service/camera/impl/CameraServiceT32.cpp` | 相机服务真机实现 |
| `src/service/camera/CameraPropertyService.h` | 参数管理 |
| `src/service/camera/CameraParameterRegistry.cpp` | CPS参数定义（含CAM_Mode） |
| `src/service/http_server/http_api_v1.cpp` | HTTP API端点 |
| `src/service/http_server/PhotoJobManager.h` | 异步拍照任务 |
| `src/config/setting/Settings.h` | 所有运行时设置 |
| `res/setting.json` | 设置持久化文件 |
| `res/config.ini` | 设备启动配置 |
| `doc/knowledge/refs/CPS-CS-SET1-camera-parameter-settings-spec.md` | CPS参数规范 |
