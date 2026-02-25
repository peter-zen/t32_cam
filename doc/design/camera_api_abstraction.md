# 相机业务层抽象设计 (Camera Service Abstraction)

## 1. 背景与问题
当前 `ImageSnap` 和 `VideoRecorder` 等核心业务类直接依赖 Ingenic T32 SDK (`IMP_*` 函数)，导致无法在 PC 环境下运行和调试 HTTP API。
为了支持在 PC 模拟环境 (Simu) 开发和测试 HTTP API，需要引入一层**业务抽象层 (Service Layer)**，将 HTTP 接口与底层硬件实现解耦。

## 2. 架构设计

### 2.1 整体分层
```
┌─────────────────────────────┐
│       HTTP API Handlers     │ (src/service/http_server)
│ (Controller, Parse JSON...) │
└──────────────┬──────────────┘
               │ 调用接口
               ▼
┌─────────────────────────────┐
│      ICameraService         │ (Interface)
│   (Pure Virtual Class)      │
└──────┬───────────────┬──────┘
       │               │
       ▼               ▼
┌──────────────┐ ┌──────────────┐
│CameraService │ │CameraService │
│     T32      │ │     Sim      │
└──────┬───────┘ └───────┬──────┘
       │                 │
       ▼                 ▼
┌──────────────┐ ┌──────────────┐
│  ImageSnap   │ │  MockSnap    │
│VideoRecorder │ │ MockRecorder │
│  (T32 SDK)   │ │ (File Ops)   │
└──────────────┘ └──────────────┘
```

### 2.2 核心接口定义 (ICameraService)

`src/service/camera/ICameraService.h`

```cpp
class ICameraService {
public:
    virtual ~ICameraService() = default;

    // --- 拍照业务 ---
    // 返回: 0 成功, 非0 错误码
    virtual int takePhoto(int channel, bool save, const std::string& format, int quality, PhotoResult& result) = 0;
    virtual int startBurstPhoto(int count, int interval, const std::string& jobId) = 0;
    virtual PhotoStatus getPhotoStatus() = 0;

    // --- 录像业务 ---
    virtual int startRecord(int channel, int duration, bool audio, const std::string& recordId) = 0;
    virtual int stopRecord() = 0;
    virtual RecordStatus getRecordStatus() = 0;

    // --- 属性管理 ---
    virtual int setProperty(const std::string& key, const std::string& value) = 0;
    virtual std::string getProperty(const std::string& key) = 0;
    virtual std::string getAllPropertiesJson() = 0; // 或者返回对象结构

    // --- 文件/数据库 ---
    virtual std::string getMediaDatabasePath() = 0;
    virtual std::string getThumbnailDatabasePath() = 0;
    
    // --- 系统 ---
    virtual int factoryReset() = 0;
};
```

## 3. 环境差异定义

| 功能点 | T32 真机环境 (Target) | PC 模拟环境 (Simu) |
| :--- | :--- | :--- |
| **文件存储路径** | `/sdcard/media/...` | `./simulation/sdcard/media/...` |
| **数据库路径** | `/sdcard/data/db/` | `./simulation/data/db/` |
| **拍照实现** | 调用 `ImageSnap` -> `IMP_Encoder` 抓取真实 Sensor 数据 | 复制预置的 `test.jpg` 到目标路径，并生成缩略图 |
| **录像实现** | 调用 `VideoRecorder` -> `IMP_System` 编码 H.264 | 创建空文件或复制预置 `test.mp4`，模拟耗时和状态变化 |
| **属性配置** | 真正修改 ISP/Sensor 参数 (如曝光、白平衡) | 仅在内存/数据库中更新配置值，打印日志 |
| **缩略图生成** | 使用 T32 硬件缩放或软解 | 使用 OpenCV 或简单的文件拷贝模拟 |

## 4. 详细实现方案

### 4.1 工厂模式
使用 `CameraServiceFactory` 根据编译宏决定实例化哪个版本。

```cpp
// src/service/camera/CameraServiceFactory.h
class CameraServiceFactory {
public:
    static std::shared_ptr<ICameraService> create();
};

// src/service/camera/CameraServiceFactory.cpp
std::shared_ptr<ICameraService> CameraServiceFactory::create() {
#ifdef PLATFORM_T32
    return std::make_shared<CameraServiceT32>();
#else
    return std::make_shared<CameraServiceSim>();
#endif
}
```

### 4.2 模拟层 (CameraServiceSim) 实现策略
*   **状态模拟**: 使用成员变量 `is_recording_`, `recording_start_time_` 等模拟真实状态。
*   **异步模拟**: 录像和连拍通常是异步的。Sim 实现可以使用 `std::thread` 模拟后台任务，例如"连拍"就是启动一个线程，每隔 1秒 copy 一个文件。
*   **数据生成**:
    *   在 `doc/assets/` 或 `simulation/assets/` 下放置 `sample.jpg` 和 `sample.mp4`。
    *   当 API 请求拍照时，将 `sample.jpg` 拷贝到 `DCIM` 目录，重命名为 `IMG_Timestamp.jpg`。
    *   同时往 SQLite 数据库插入一条记录。

### 4.3 真机层 (CameraServiceT32) 实现策略
*   封装现有的 `ImageSnap` 和 `VideoRecorder`。
*   管理这些对象的生命周期（单例或按需创建）。
*   处理线程安全（防止同时拍照和录像，如果硬件不支持）。

## 5. 实施步骤

1.  **定义接口**: 创建 `src/service/camera/ICameraService.h`。
2.  **实现 Sim 版本**: 创建 `src/service/camera/impl/CameraServiceSim.cpp`。
    *   实现基础的文件拷贝逻辑。
    *   实现 SQLite 数据库操作（复用现有的 `MetadataDao`，确保 Dao 层也是可移植的）。
3.  **集成 HTTP**: 修改 HTTP Handler，不再直接调用 `ImageSnap`，而是调用 `CameraServiceFactory::create()->takePhoto(...)`。
4.  **PC 调试**: 在 PC 上编译运行 HTTP Server，验证 API 逻辑、JSON 格式、数据库同步是否正常。
5.  **实现 T32 版本**: 创建 `src/service/camera/impl/CameraServiceT32.cpp`，接入真实的硬件逻辑。

## 6. 带来的收益
*   **开发效率**: 90% 的 HTTP API 逻辑（参数解析、鉴权、数据库查询、错误处理）可以在 PC 上开发验证，无需频繁烧录板子。
*   **稳定性**: 可以在 PC 上进行压力测试和内存泄漏检测（Valgrind）。
*   **解耦**: 硬件 SDK 的变动不会影响 API 层代码。
