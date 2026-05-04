# Service Interface 分层分析：覆盖度、合理性与演进方向

## 1. 背景

在 HTTP API 与 Camera Service 分层（见 `http-api-v1-and-camera-service-layering.md`）基础上，新增了四个 Service 接口以覆盖 device/sensor/storage/system 域。本文档分析当前全部 Service 接口的覆盖度、粒度合理性，以及 HTTP 层中仍未被接口收口的部分。

## 2. 当前全部 Service 接口一览

### 2.1 ICameraService（重接口，12 个方法）

```
src/service/camera/ICameraService.h
```

| 方法 | HTTP 端点 | simu/T32 差异 |
|------|----------|--------------|
| `takePhoto()` | `POST /camera/photo` | 路径、文件源 |
| `startBurstPhoto()` | `POST /camera/photo/burst` | 路径 |
| `getPhotoStatus()` | `GET /camera/photo/status` | 状态来源 |
| `startTimerPhoto()` | `POST /camera/photo/timer` | 路径 |
| `stopTimerPhoto()` | `POST /camera/photo/timer` | 同上 |
| `getTimerPhotoStatus()` | `POST /camera/photo/timer` | 同上 |
| `capturePreviewFrame()` | `GET /camera/preview` | 帧来源 |
| `startRecord()` | `POST /camera/video/start` | 路径、编码器 |
| `stopRecord()` | `POST /camera/video/stop` | 同上 |
| `getRecordStatus()` | `GET /camera/video/status` | 状态来源 |
| `setProperty()` / `getProperty()` / `getAllPropertiesJson()` | 通过 CameraPropertyService 间接使用 | 存储路径 |
| `getMediaDatabasePath()` | `GET /camera/database/*` | DB 路径 |
| `getThumbnailDatabasePath()` | `GET /camera/database/thumbnail` | DB 路径 |
| `getMediaList()` | 未直接使用（HTTP 层走 MetadataDao） | — |
| `deleteFile()` | `POST /camera/files/delete` | 路径校验 |
| `factoryReset()` | `POST /camera/properties/factory-reset` | 配置路径 |

### 2.2 IDeviceService（轻接口，1 个方法）

```
src/service/device/IDeviceService.h
```

| 方法 | HTTP 端点 | simu/T32 差异 |
|------|----------|--------------|
| `getDeviceInfo()` | `GET /device/info` | PID/固件版本来源（MCU vs 硬编码） |

### 2.3 ISensorService（轻接口，1 个方法）

```
src/service/sensor/ISensorService.h
```

| 方法 | HTTP 端点 | simu/T32 差异 |
|------|----------|--------------|
| `getSensorData()` | `GET /device/sensors` | 电池/温湿度/气压/CDS 来源（MCU vs 硬编码） |

### 2.4 IStorageService（轻接口，2 个方法）

```
src/service/storage/IStorageService.h
```

| 方法 | HTTP 端点 | simu/T32 差异 |
|------|----------|--------------|
| `getStorageInfo()` | `GET /storage/info` | 容量来源（Disk::getInfo vs 硬编码） |
| `formatStorage()` | `POST /storage/format` | 格式化操作 |

### 2.5 ISystemService（轻接口，2 个方法）

```
src/service/system/ISystemService.h
```

| 方法 | HTTP 端点 | simu/T32 差异 |
|------|----------|--------------|
| `setDatetime()` | `POST /system/datetime` | MCU RTC + settimeofday |
| `setWorkMode()` | `POST /system/workmode` | MCU 工作模式控制 |

### 2.6 不需要 simu/T32 区分的组件

| 组件 | 端点 | 说明 |
|------|------|------|
| `CameraPropertyService` | `GET/POST /camera/properties/*` | 属性 schema/校验/持久化，两个平台一致 |
| `CameraStatusService` | `GET /camera/status` | 状态聚合，两个平台一致 |
| `PhotoJobManager` | 异步 photo job 队列 | 平台无关的 job 调度 |
| `MetadataDao` | `GET /camera/video/list`, `GET /camera/photos` | 直接查 SQLite，两个平台一致 |
| Presets（硬编码） | `GET/POST /camera/presets` | 预设配置数据，目前在 HTTP handler 内 |
| 文件访问 | `GET /camera/thumbnail`, `GET /camera/files/download` | 路径解析 + 文件 I/O |

## 3. 覆盖度全景图

```
HTTP 端点总数: 27

├── 通过 ICameraService:      13 端点 (photo/video/preview/delete/database)
├── 通过 CameraPropertyService: 7 端点 (properties/reset/factory-reset)
├── 通过 CameraStatusService:   1 端点  (status)
├── 通过 新增 Service 接口:      6 端点  (device/sensor/storage/system)
├── 通过 MetadataDao 直查:      2 端点  (video/list, photos)
├── 硬编码在 HTTP handler:      2 端点  (presets, thumbnail)
└── PhotoJobManager:           1 端点  (photo/status, 异步 job)
```

## 4. 合理性分析

### 4.1 ICameraService 为什么重

ICameraService 有 12+ 方法，这是**合理的**，原因：

1. **Camera 域本身就是复杂域**。拍照、录像、预览、文件管理是不同子能力，但它们共享：
   - 同一个媒体根路径（`/sdcard/DCIM` vs `sim_sdcard_runtime/DCIM`）
   - 同一套 HAL（ImageSnap, VideoRecorder）
   - 同一个数据库（MetadataDao）
   - 同一个配置源（Settings, DeviceConfig）

2. **这些子能力在 simu/T32 之间有统一的差异模式**：路径前缀不同、文件源不同、编码器不同。用一个接口收口比拆成 5 个小接口更清晰——拆开后每个接口都需要同样的路径/数据库依赖，反而增加耦合。

3. **符合 ISP/DVR 领域的自然边界**。在嵌入式摄像设备中，"Camera Service" 天然覆盖拍摄全生命周期。

### 4.2 新增接口为什么轻

IDeviceService / ISensorService / IStorageService / ISystemService 各只有 1-2 个方法，这是**合理的**，原因：

1. **这些端点本身就很薄**。`GET /device/info` 就是读一次 MCU 返回数据，没有状态机、没有异步、没有文件 I/O。接口方法数 = 端点复杂度，没有过度抽象。

2. **关键价值不在于方法数，而在于隔离差异**。真机上 `getSensorData()` 需要调 5 个 MCU API（`readBatteryVoltage`, `readTemperature`, `readHumidity`, `readAtmosPressure`, `readCds`）+ `Disk::getInfo()`，这些硬件调用全部封装在 `SensorServiceT32` 内部，HTTP 层完全不感知。

3. **独立可测试**。即使只有 1 个方法，有了接口就可以：
   - 在 simu 上跑通 HTTP 全链路
   - 在真机上单独调试 `SensorServiceT32`（不启动 HTTP server）
   - 未来扩展更多传感器读取时只改实现，不动接口

### 4.3 不对称是否合理

ICameraService（12 方法）vs IDeviceService（1 方法）的不对称是**合理的**，因为：

- **对称性应该来自领域复杂度，不是来自接口数量**
- Camera 域有 13 个 HTTP 端点、3 个配套组件（PropertyService, StatusService, PhotoJobManager），复杂度远高于其他域
- 如果强行把 IDeviceService 也做成"重接口"（加入 firmware update、MCU heartbeat 等），反而是过度设计——这些功能还不存在

### 4.4 不在任何接口内的部分

以下端点的业务逻辑直接在 `http_api_v1.cpp` 中，没有 Service 接口：

| 端点 | 当前实现 | 是否需要接口 |
|------|---------|------------|
| `GET /camera/video/list` | `MetadataDao::getTimelineByType()` | **暂不需要**。SQLite 查询在 simu/T32 上行为一致。如果未来需要从不同数据源读取（如云端），再抽象 |
| `GET /camera/photos` | `MetadataDao::getTimelineByType()` | 同上 |
| `GET /camera/thumbnail` | 文件系统路径解析 + 读取 | **暂不需要**。路径差异已由 `ICameraService::getMediaDatabasePath()` 隐式覆盖 |
| `GET /camera/files/download` | 路径校验 + `mg_send_mime_file()` | **暂不需要**。安全校验是 HTTP 层职责 |
| `GET/POST /camera/presets` | 硬编码预设配置 | **暂不需要**。纯配置数据，两个平台一致。如果未来需要从设备读取预设，可加到 ICameraService |

## 5. 当前抽象的边界定义

```
┌─────────────────────────────────────────────────┐
│                HTTP 层职责                        │
│  路由 · 参数解析 · JSON 序列化 · 错误码 · 文件传输   │
├─────────────────────────────────────────────────┤
│                Service 层职责                     │
│  业务逻辑 · simu/T32 差异隔离 · 状态管理           │
├─────────────────────────────────────────────────┤
│                不需要 Service 接口的部分            │
│  MetadataDao 直查 · Preset 配置 · 文件路径解析      │
│  （这些在两个平台上行为一致，或差异已被其他接口覆盖）   │
└─────────────────────────────────────────────────┘
```

## 6. 演进建议

### 6.1 短期（当前阶段）

- 保持现有 5 个接口不变
- T32 骨架实现逐步填充真实硬件 API
- MetadataDao / Presets / Thumbnail 维持现状

### 6.2 中期（真机联调后）

- 如果 `ISensorService` 需要支持单传感器查询（如只读温度），可以扩展为 `getSensorData(SensorQuery)` 或增加 `getSensorValue(key)` 方法
- 如果 `IDeviceService` 需要支持 OTA 版本检查、MCU 心跳等，按需增加方法
- 如果 `IStorageService` 需要支持 SD 卡热插拔检测，增加 `getSDCardStatus()` 方法

### 6.3 长期（多入口场景）

如果出现非 HTTP 入口（CLI、TCP 命令、MCU 事件触发）需要相同业务逻辑：
- Camera 域：`ICameraService` 已可复用
- Device/Sensor 域：新增的接口也可复用
- 属性域：`CameraPropertyService` 已是独立单例，可复用

## 7. 非 Camera 接口的拆分逻辑分析

### 7.1 当前拆分的实际标准

当前四个接口的拆分，实际上**照抄了 HTTP URL 路径结构**：

```
/device/info     → IDeviceService
/device/sensors  → ISensorService
/storage/info    → IStorageService
/system/datetime → ISystemService
```

这不是一个有深度的架构决策，而是"URL 怎么分，接口就怎么分"。

### 7.2 真机数据来源揭示的问题

如果按数据来源看，四个接口的依赖关系并不支持当前的拆分：

```
IDeviceService::getDeviceInfo()     → MCU::readPID(), MCU::readFirmwareVersion()
ISensorService::getSensorData()     → MCU::readBatteryVoltage(), readTemperature(),
                                      readHumidity(), readAtmosPressure(), readCds(),
                                      Disk::getInfo()
IStorageService::getStorageInfo()   → Disk::getInfo()
ISystemService::setDatetime()       → MCU::setDatetime() + settimeofday()
ISystemService::setWorkMode()       → MCU::readWorkingMode()
```

IDeviceService 和 ISensorService 都从 MCU 读数据，ISensorService 和 IStorageService 都调 Disk::getInfo()。按数据来源分，它们不应该拆成四个。

### 7.3 更有说服力的拆分标准

#### 按"变化原因"拆分（Single Responsibility Principle）

| 接口 | 变化原因 | 合理性 |
|------|---------|--------|
| IDeviceService | 设备固件升级、型号变更 | 弱 — 设备身份几乎不变 |
| ISensorService | 硬件传感器增减、MCU 固件升级 | 合理 — 传感器集合可能扩展 |
| IStorageService | 存储介质变化（SD卡→eMMC）、分区方案变化 | 合理 — 存储是独立硬件 |
| ISystemService | RTC 芯片变化、工作模式定义变化 | 合理 — 系统控制是独立关注点 |

#### 按"调用频率/生命周期"拆分

| 接口 | 典型调用频率 | 生命周期 |
|------|------------|---------|
| IDeviceService | 启动时读一次 | 设备运行期间不变 |
| ISensorService | 周期性轮询（秒级） | 持续变化 |
| IStorageService | 低频查询 | 缓慢变化 |
| ISystemService | 用户操作触发 | 事件驱动 |

这个维度上确实有区分度：设备信息是静态的，传感器是动态的，存储是准静态的，系统操作是命令式的。

### 7.4 替代方案（未采用，记录供后续参考）

#### 方案 A：按数据来源合并

```
IMcuService         = IDeviceService + ISensorService + ISystemService
IStorageService     = 保持不变
```

优点：接口数少，数据来源内聚。缺点：IMcuService 会变成杂合体（既读设备信息又读传感器又设置时间）。

#### 方案 B：按读/写语义拆分

```
IDeviceQueryService   = IDeviceService + ISensorService + IStorageService::getStorageInfo()
IDeviceCommandService = ISystemService + IStorageService::formatStorage()
```

优点：读写分离清晰。缺点：查询接口会膨胀，命令接口混合不同域。

### 7.5 当前方案的实际好处

保留四个独立接口的最大实际好处是：**每个 T32 实现文件职责单一、体积小。**

- `SensorServiceT32.cpp` 只关心传感器读取
- `StorageServiceT32.cpp` 只关心磁盘
- 如果合成一个 `McuServiceT32.cpp`，它会变成一个既读设备信息又读传感器又设置时间的杂合体

### 7.6 决策

当前拆分的真正逻辑是 **URL 域名 + 实现文件的内聚性**，而不是严格的数据来源或变化原因分析。作为第一刀够用。

**后续观察点**：如果在真机接入过程中发现 IDeviceService 和 ISensorService 总是一起变化（因为都依赖 MCU），合并为 IMcuService 是完全合理的。当前阶段保留四个接口，根据实际接入情况再决定是否优化。

## 8. 结论

当前抽象**逻辑上合理**：

1. **ICameraService 重是应该的** —— Camera 域复杂度高，需要统一收口
2. **其他接口轻也是应该的** —— 对应端点本身简单，接口方法数 = 业务复杂度
3. **不对称是自然的** —— 反映了嵌入式摄像设备中 Camera 域与其他域的真实复杂度差异
4. **MetadataDao / Presets 不在接口内是合理的** —— 这些在两个平台上行为一致，不需要 simu/T32 抽象
5. **非 Camera 接口的拆分标准是 URL 域名 + 实现内聚性** —— 非严格架构分析，但作为第一刀够用，后续可按实际变化模式合并

核心原则：**接口的存在理由是隔离 simu/T32 差异，而不是追求结构对称。** 有差异的地方才需要接口，没有差异的地方直接用即可。
