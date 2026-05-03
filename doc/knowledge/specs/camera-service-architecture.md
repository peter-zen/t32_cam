# Camera Service 架构

## 分层结构

```
┌─────────────────────────────┐
│       HTTP API Handlers     │  src/service/http_server/http_api_v1.cpp
│  (CivetWeb, JSON parse)     │
└──────────────┬──────────────┘
               │
               ▼
┌─────────────────────────────┐
│      ICameraService         │  src/service/camera/ICameraService.h
│   (Pure Virtual Class)      │
└──────┬───────────────┬──────┘
       │               │
       ▼               ▼
┌──────────────┐ ┌──────────────┐
│CameraService │ │CameraService │
│     T32      │ │     Sim      │
│ (真机硬件)    │ │ (PC 模拟)     │
└──────────────┘ └──────────────┘
```

## 平台选择

`CameraServiceFactory::create()` 根据编译宏选择实现：

```cpp
#ifdef SIMULATION_MODE
    return std::make_shared<CameraServiceSim>();
#else
    return std::make_shared<CameraServiceT32>();
#endif
```

**注意**：属性 get/set 不经过 `ICameraService`，而是直接走 `CameraPropertyService` 单例，两个平台完全一致。

## ICameraService 接口

负责拍照、录像、文件管理等**平台相关**操作：

| 方法 | 说明 |
|---|---|
| `takePhoto()` | 单次拍照 |
| `startBurstPhoto()` | 连拍 |
| `getPhotoStatus()` | 拍照状态查询 |
| `startRecord()` / `stopRecord()` | 录像控制 |
| `getRecordStatus()` | 录像状态查询 |
| `getMediaDatabasePath()` | 媒体数据库路径 |
| `getThumbnailDatabasePath()` | 缩略图数据库路径 |

## 属性系统 (平台无关)

`CameraPropertyService` 是单例，不区分平台：

| 组件 | 文件 | 职责 |
|---|---|---|
| `CameraParameterRegistry` | `CameraParameterRegistry.cpp` | 100+ 参数声明式目录 |
| `CameraPropertyService` | `CameraPropertyService.cpp` | 校验、读写、持久化、JSON 导出 |
| `CameraStatusService` | `CameraStatusService.cpp` | STATUS 分类只读查询 |
| `CameraFactoryConfigImporter` | `CameraFactoryConfigImporter.cpp` | SD 卡工厂配置 JSON 导入 |

## 环境差异

| 功能点 | T32 真机 | PC 模拟 |
|---|---|---|
| 文件路径 | `/sdcard/media/...` | `./sim_sdcard/...` |
| 拍照实现 | `IMP_Encoder` 抓取 Sensor | 复制预置 `test.jpg` |
| 录像实现 | `IMP_System` 编码 H.264 | 创建文件模拟 |
| 属性配置 | 同一 `CameraPropertyService` | 同一 `CameraPropertyService` |
| HTTP Server | CivetWeb (port from DeviceConfig) | 同左，无平台限制 |
| 媒体存储 | `main_app.cpp` 初始化 | `ensure_sim_media_storage_ready()` |

## HTTP 端点路由

所有端点在 `http_api_v1.cpp:1690-1727` 注册，无 `SIMULATION_MODE` 保护：

| 路由 | 说明 |
|---|---|
| `/api/v1/camera/properties` | GET: 按组列出 / POST: 批量设置 |
| `/api/v1/camera/properties/item` | GET: registry 单属性读取 |
| `/api/v1/camera/properties/set` | POST: registry 单属性写入 |
| `/api/v1/camera/properties/reset` | POST: 重置为默认值 |
| `/api/v1/camera/properties/factory-reset` | POST: 出厂恢复 |
| `/api/v1/camera/status` | GET: STATUS 参数只读 |
| `/api/v1/camera/presets` | GET/POST: 预设方案 |
| `/api/v1/camera/photo*` | 拍照相关 |
| `/api/v1/camera/video*` | 录像相关 |
