# Device 与 Storage 表层行为规格

## 1. 目的

定义 `t32_cam` 当前 `device` / `storage` 相关 HTTP 表层能力的仓库级权威描述，覆盖：
- `/api/v1/device/info`
- `/api/v1/device/sensors`
- `/api/v1/storage/info`
- `/api/v1/storage/format`
- 当前代码中的真实数据来源深度
- 已确认事实与仍待验证项

本规格以当前代码为准，不以 reference 文档中的字段列表或产品期待为准。

## 2. 当前状态

- 状态：已完成一轮代码校准
- 当前接口表面稳定：路由、方法、JSON 包装已稳定
- 当前数据深度偏浅：多数仍是构造型/占位型返回
- 当前真实硬件能力线索已存在，但尚未接进这些 V1 handler

## 3. 当前权威代码入口

- `src/service/http_server/http_api_v1.cpp`
- `src/hardware/disk/Disk.h`
- `src/hardware/disk/Disk.cpp`
- `src/hardware/mcu/MCU.h`
- `src/hardware/mcu/MCU.cpp`

历史参考但不作为唯一依据：
- `doc/reference/20260324-http-api-reference.md`
- `doc/reference/20260324-http-api-client-quick-reference.md`

## 4. 当前代码结构判断

### 4.1 HTTP 表层路由已经固定
`http_api_v1.cpp` 当前已注册：
- `GET /api/v1/device/info`
- `GET /api/v1/device/sensors`
- `GET /api/v1/storage/info`
- `POST /api/v1/storage/format`

从协议层看，这些入口已经稳定存在。

### 4.2 但 handler 当前主要是本地构造 JSON
当前 `http_api_v1.cpp` 中：
- `build_device_info_json()` 直接写死固定字符串
- `build_sensor_data_json()` 直接写死示例数值，并只把 `datetime` 用当前时间动态生成
- `build_storage_info_json()` 直接写死 `total/free/used`
- `api_v1_storage_format()` 仅返回 `accepted=true` 与 `status=scheduled`

因此当前更准确的结论是：
- device / storage 域的协议外观稳定
- 但后端接线深度明显弱于 camera 域

## 5. Device 域当前行为

### 5.1 `/api/v1/device/info`
当前 `build_device_info_json()` 返回固定字段：
- `pid = "T32-CAM-001"`
- `camera_ver = "1.0.0"`
- `camera_model = "T32"`
- `camera_build = "2025-01-06"`
- `mcu_ver = "MCU-1.0.0"`

当前没有看到它去：
- 读取真实设备 PID
- 读取真实相机版本
- 读取真实 MCU version
- 读取构建时间/版本元数据

因此当前它是：
- 结构稳定的设备信息占位接口
- 不是完整真实设备身份信息接口

### 5.2 `/api/v1/device/sensors`
当前 `build_sensor_data_json()` 返回：
- `battery = 3700`
- `battery_type = 1`
- `battery_level = 85`
- `ext_power = 12000`
- `sdcard_capacity = 32000`
- `sdcard_used = 8000`
- `cds = 500`
- `temp = "25"`
- `press = "1013"`
- `rh = "60"`
- `datetime = 当前服务端时间字符串`

这里面唯一明显动态生成的是：
- `datetime`

其余数值按当前代码都是固定构造值，不是实时读取。

因此不能把 `/api/v1/device/sensors` 写成“当前已真实接所有传感器、电池、存储状态”。
那是错的。

## 6. Storage 域当前行为

### 6.1 `/api/v1/storage/info`
当前 `build_storage_info_json()` 返回固定：
- `total = 32000`
- `free = 24000`
- `used = 8000`

当前没有看到它实际调用：
- `Disk::getInfo()`
- 或其他存储模块

因此当前它仍然是：
- 容量结构稳定
- 数据值占位

### 6.2 `/api/v1/storage/format`
当前 `api_v1_storage_format()`：
- 只接受 `POST`
- 解析 JSON body
- 不使用请求内容
- 记录 `Requested storage format`
- 返回：
  - `accepted = true`
  - `status = "scheduled"`

当前没有看到它：
- 真正调用格式化逻辑
- 调度后台格式化任务
- 广播格式化事件
- 更新 storage 状态

因此当前正确描述只能是：
- “格式化请求已接收/已排队语义”接口
- 不是“真实格式化过程已完成接线”的接口

## 7. 当前真实硬件能力线索

### 7.1 Disk 能力已存在，但未接到 HTTP V1
`src/hardware/disk/` 当前已有：
- `DiskInfo { total, free }`
- `Disk::getInfo(const std::string& path = "/")`

这说明：
- 仓库里并不是完全没有存储容量读取能力
- 只是当前 `storage/info` handler 还没接过去

### 7.2 MCU 电池能力已存在，但未体现在 V1 sensors handler
`MCU.h / MCU.cpp` 中可见：
- `readWorkingMode()`
- 电池电压读取相关实现

这说明：
- 仓库里存在部分电池/MCU 侧真实读取能力
- 但 `/api/v1/device/sensors` 当前并未以此为真实数据源

因此这里不能简单写成“项目没有硬件能力”，更准确的说法是：
- 硬件能力存在部分线索
- V1 handler 尚未完成接线

## 8. 当前与历史 reference 的关系

### 8.1 reference 中的“字段稳定”基本成立
历史 reference 里对于这些接口的字段与示例，和当前代码基本对齐。
因此字段命名层面可以继续沿用。

### 8.2 但不能把示例值误写成真实采集值
reference 中展示的：
- battery
- sdcard_capacity
- used/free
- mcu_ver
等内容

在当前代码里多数还是构造值。

所以文档里必须明确区分：
- 字段形状稳定
- 真实接线深度不足

## 9. 当前明确结论

### 9.1 可以明确写成事实的
- device / storage 路由和方法约束已稳定
- `device/info` 当前是固定设备信息 JSON
- `device/sensors` 当前除 `datetime` 外主要是固定占位值
- `storage/info` 当前是固定容量占位值
- `storage/format` 当前是 accepted/scheduled 语义
- 仓库中存在 `Disk::getInfo()` 与 MCU 电池读取等底层能力线索
- 当前这些底层能力尚未完全接入 V1 handler

### 9.2 不能写得过满的
- `device/sensors` 已全面反映实时传感器状态
- `storage/info` 已直接读取真实磁盘容量
- `storage/format` 已完成真实格式化调度闭环
- device / storage 域当前成熟度与 camera 域相当

这些说法都会过度承诺。

## 10. 仍待验证项

- `Disk::getInfo()` 在真机和 simu 下的实际返回行为
- MCU 电池电压与 battery_level 的真实换算链路是否已在别处存在
- storage format 的真实执行路径是否在仓库其他位置已有草稿或旧链路
- `device/info` 所需版本元数据能否从构建系统或 MCU 统一拉取

## 11. 推荐后续拆分

后续如果继续治理，建议补：
- `decisions/device-and-storage-http-surface-vs-real-backend-depth.md`
- `refs/device-storage-code-entry-and-field-source-notes.md`
- `playbooks/device-storage-http-probe.md`
- `bugs/device-storage-placeholder-data-and-unwired-format-path.md`

当前这篇规格先承担当前表层行为权威入口，避免继续把字段稳定性误写成真实数据闭环。