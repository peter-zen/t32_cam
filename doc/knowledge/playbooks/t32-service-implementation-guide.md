# T32 Service 接口实现与验证指南

## 1. 总览

本文档是 T32 真机环境下实现各 Service 接口的参考指南。涵盖每个接口的实现要点、硬件 API 调用方式、单位换算、已知问题和验证方法。

### 当前实现状态

| 接口 | T32 实现状态 | 文件 |
|------|-------------|------|
| ICameraService | **大部分已实现** | `src/service/camera/impl/CameraServiceT32.cpp` |
| IDeviceService | 骨架（返回默认值） | `src/service/device/impl/DeviceServiceT32.cpp` |
| ISensorService | 骨架（返回默认值） | `src/service/sensor/impl/SensorServiceT32.cpp` |
| IStorageService | 骨架（返回默认值） | `src/service/storage/impl/StorageServiceT32.cpp` |
| ISystemService | 骨架（返回默认值） | `src/service/system/impl/SystemServiceT32.cpp` |

---

## 2. ICameraService（已有实现，需补全）

### 2.1 已完成的方法

| 方法 | 实现要点 |
|------|---------|
| `takePhoto()` | 路径 `/sdcard/DCIM/IMG_*.jpg`，调用 `ImageSnap::snap()` |
| `startTimerPhoto()` | 完整定时器线程，循环调用 `takePhoto()` |
| `stopTimerPhoto()` | 条件变量通知 + thread join |
| `getTimerPhotoStatus()` | 返回 timer_status_ 成员 |
| `capturePreviewFrame()` | 拍照到 `/sdcard/.preview/`，读回二进制，删除临时文件 |
| `startRecord()` | 路径 `/sdcard/DCIM/VID_*.mp4`，配置 VideoParams + AudioParams |
| `stopRecord()` | 调用 `video_recorder_->stopRecorder()` |
| `setProperty()` / `getProperty()` | 走 `CameraPropertyService` |
| `getMediaDatabasePath()` | 返回 `/sdcard/data/db/media_file.db` |
| `getThumbnailDatabasePath()` | 返回 `/sdcard/data/db/media_thumb.db` |
| `getMediaList()` | 走 `MetadataDao` |
| `deleteFile()` | 走 `MetadataDao` + `remove()` |

### 2.2 需要补全的方法

#### `startBurstPhoto()`

当前返回 `-1`（未实现）。

实现方案：
- 复用 `takePhoto()` 逻辑，在独立线程中循环 `count` 次
- 每次间隔 `interval` ms
- 参考 `startTimerPhoto()` 的线程模型

#### `getPhotoStatus()`

当前永远返回 `IDLE`。

需要：
- 增加 `std::atomic<PhotoState> photo_state_` 成员
- 在 `takePhoto()` 入口设为 `CAPTURING`，完成后设为 `IDLE`
- 进度追踪可选（单次拍照进度意义不大，burst 场景下有价值）

#### `getRecordStatus()`

当前永远返回 `IDLE`。

需要：
- 增加 `std::atomic<RecordState> record_state_` 成员
- 在 `startRecord()` 设为 `RECORDING`，`stopRecord()` 设为 `IDLE`
- 可从 `VideoRecorder` 查询当前录制时长

#### `factoryReset()`

当前只打日志。

需要：
- 删除 `/sdcard/data/db/*.db`
- 重置 `Settings` 和 `DeviceConfig` 到默认值
- 参考 `CameraPropertyService::resetFactoryProperties()` 的实现

---

## 3. IDeviceService（需实现）

### 3.1 接口定义

```cpp
// src/service/device/IDeviceService.h
virtual DeviceInfo getDeviceInfo() = 0;
```

### 3.2 实现映射

```cpp
// src/service/device/impl/DeviceServiceT32.cpp

#include <MCU.h>
#include <EnvManager.h>

DeviceInfo DeviceServiceT32::getDeviceInfo() {
    auto mcu = MCU::getInstance();

    DeviceInfo info;
    info.pid = mcu->readPID();                          // 32字节 ASCII，如 "C154E001M4500046"
    info.firmwareVersion = mcu->readFirmwareVersion();   // DSP 固件版本，格式 "X.Y.Z"
    info.model = "T32";                                  // 编译时常量
    info.buildDate = __DATE__;                           // 编译日期宏
    info.mcuVersion = mcu->convertVersion(mcu->readVersion()); // MCU 固件版本，格式 "V%02d.%03d"

    return info;
}
```

### 3.3 关键注意事项

| 项目 | 说明 |
|------|------|
| PID 读取失败 | `readPID()` 返回空字符串 `""`，需处理 |
| DSP 版本 vs MCU 版本 | `readFirmwareVersion()` 返回 DSP 版本（默认 `"1.0.0"`），`readVersion()` + `convertVersion()` 返回 MCU 版本（格式 `"V10.002"`） |
| model 字段 | 当前硬编码 `"T32"`，后续如需支持多型号，可从配置读取 |

### 3.4 HTTP 映射

```
DeviceInfo → JSON:
  pid             → "pid"
  firmwareVersion → "camera_ver"
  model           → "camera_model"
  buildDate       → "camera_build"
  mcuVersion      → "mcu_ver"
```

### 3.5 验证方法

```bash
# 对比 MCU 直读和 HTTP 返回
curl -s http://<device_ip>/api/v1/device/info | python3 -m json.tool

# 预期：pid 为实际设备 PID，mcu_ver 为 "Vxx.xxx" 格式
```

---

## 4. ISensorService（需实现）

### 4.1 接口定义

```cpp
// src/service/sensor/ISensorService.h
virtual SensorData getSensorData() = 0;
```

### 4.2 实现映射

```cpp
// src/service/sensor/impl/SensorServiceT32.cpp

#include <MCU.h>
#include <Disk.h>
#include <ctime>

SensorData SensorServiceT32::getSensorData() {
    auto mcu = MCU::getInstance();

    SensorData data;

    // 电池
    data.batteryVoltage = mcu->readBatteryVoltage();     // 单位：分伏(decivolt)，126 = 12.6V
    data.batteryType = mcu->readBatteryType();           // 电池类型标识
    data.batteryLevel = mcu->readBatteryLevel();         // ⚠️ 当前死代码，永远返回 0
    data.externalVoltage = mcu->readExternalVoltage();   // 单位：分伏(decivolt)

    // 存储（Disk 返回 MB）
    DiskInfo disk = Disk::getInfo("/sdcard");
    data.sdcardCapacity = disk.total;                    // MB
    data.sdcardUsed = disk.total - disk.free;            // MB

    // 传感器
    data.cds = mcu->readCds();                           // ⚠️ 当前死代码，永远返回 0
    data.temperature = mcu->readTemperature();           // 已是摄氏度（原始值 - 125）
    data.humidity = mcu->readHumidity();                 // 百分比 0-100
    data.pressure = mcu->readAtmosPressure();            // hPa（原始值 / 10）

    // 时间
    struct tm t = mcu->getDatetime();
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.000",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
             t.tm_hour, t.tm_min, t.tm_sec);
    data.datetime = buf;

    return data;
}
```

### 4.3 关键注意事项（重要）

| 项目 | 说明 | 影响 |
|------|------|------|
| `readBatteryLevel()` | 函数开头有 `return 0;` 死代码，I2C 读取路径永远不执行 | battery_level 永远为 0 |
| `readCds()` | 同上，函数开头有 `return 0;` 死代码 | cds 永远为 0 |
| `readBatteryVoltage()` | 返回 decivolt，不是 mV | HTTP API 当前字段名是 `"battery"`，simu 返回 3700（mV），需要决定单位统一策略 |
| `readAtmosPressure()` | 返回 hPa，但原始寄存器值需要除 10 | 已在 MCU 内部处理 |
| `readTemperature()` | 返回摄氏度，原始值已减 125 | 直接使用，无需额外转换 |
| `getDatetime()` | 返回 `struct tm`，年 = 实际年 - 1900，月 = 0-11 | 转 ISO 字符串时需 +1900、+1 |
| `Disk::getInfo()` | 返回 MB | 与 simu 一致 |

### 4.4 已知问题：单位不一致

Simu 环境中 `SensorData` 的字段值：
- `batteryVoltage = 3700`（暗示 mV）
- `externalVoltage = 12000`（暗示 mV）

T32 真机 MCU 返回的是 **decivolt**：
- `readBatteryVoltage()` 返回 `37` 表示 3.7V
- `readExternalVoltage()` 返回 `120` 表示 12.0V

**需要在实现时统一**。两种方案：
1. T32 实现中转换为 mV：`data.batteryVoltage = mcu->readBatteryVoltage() * 100;`
2. Simu 实现中改为 decivolt：`data.batteryVoltage = 37;`

建议采用方案 1（T32 转换），因为 HTTP API 的当前 JSON 输出格式（`"battery": 3700`）不应改变。

### 4.5 HTTP 映射

```
SensorData → JSON:
  batteryVoltage  → "battery"        (mV)
  batteryType     → "battery_type"
  batteryLevel    → "battery_level"  (⚠️ 当前永远 0)
  externalVoltage → "ext_power"      (mV)
  sdcardCapacity  → "sdcard_capacity" (MB)
  sdcardUsed      → "sdcard_used"    (MB)
  cds             → "cds"            (⚠️ 当前永远 0)
  temperature     → "temp"           (摄氏度，字符串)
  pressure        → "press"          (hPa，字符串)
  humidity        → "rh"             (百分比，字符串)
  datetime        → "datetime"       (ISO 8601)
```

### 4.6 验证方法

```bash
curl -s http://<device_ip>/api/v1/device/sensors | python3 -m json.tool

# 验证项：
# - battery 在合理范围（3V-4.2V 锂电池 → 3000-4200 mV）
# - ext_power 在合理范围（USB 5V → 5000，12V 适配器 → 12000）
# - temp 在合理范围（-20 ~ 60 摄氏度）
# - press 在合理范围（海平面约 1013 hPa）
# - rh 在合理范围（0-100%）
# - sdcard_capacity > 0，sdcard_used < sdcard_capacity
# - datetime 为合理的 ISO 时间
# - battery_level = 0（已知死代码，待修复）
# - cds = 0（已知死代码，待修复）
```

---

## 5. IStorageService（需实现）

### 5.1 接口定义

```cpp
// src/service/storage/IStorageService.h
virtual StorageInfo getStorageInfo() = 0;
virtual FormatResult formatStorage() = 0;
```

### 5.2 实现映射

```cpp
// src/service/storage/impl/StorageServiceT32.cpp

#include <Disk.h>

StorageInfo StorageServiceT32::getStorageInfo() {
    DiskInfo disk = Disk::getInfo("/sdcard");

    StorageInfo info;
    info.total = disk.total;               // MB
    info.free = disk.free;                 // MB
    info.used = disk.total - disk.free;    // MB
    return info;
}

FormatResult StorageServiceT32::formatStorage() {
    // TODO: 调用实际格式化命令
    // system("mkfs.ext4 /dev/mmcblk0p1") 或类似操作
    // 需要考虑：格式化前卸载、格式化后重新挂载、通知 MediaScanner 重新扫描
    FormatResult result;
    result.accepted = true;
    result.status = "pending";
    return result;
}
```

### 5.3 关键注意事项

| 项目 | 说明 |
|------|------|
| `Disk::getInfo()` | 返回 MB，与 simu 一致 |
| 路径 | 使用 `/sdcard`（T32 的 SD 卡挂载点） |
| `formatStorage()` | 实际格式化是破坏性操作，需要谨慎实现，建议先做功能验证再实现格式化 |
| 错误处理 | `Disk::getInfo()` 失败返回 `{0, 0}`，HTTP 层可据此判断存储不可用 |

### 5.4 验证方法

```bash
curl -s http://<device_ip>/api/v1/storage/info | python3 -m json.tool

# 验证项：
# - total > 0（SD 卡已插入且已挂载）
# - free > 0
# - used = total - free
# - 数值与 `df -h /sdcard` 输出一致
```

---

## 6. ISystemService（需实现）

### 6.1 接口定义

```cpp
// src/service/system/ISystemService.h
virtual SetDatetimeResult setDatetime(const std::string& datetime) = 0;
virtual WorkModeResult setWorkMode(int mode) = 0;
```

### 6.2 实现映射

#### `setDatetime()`

```cpp
#include <MCU.h>
#include <ctime>
#include <sys/time.h>
#include <cstring>

SetDatetimeResult SystemServiceT32::setDatetime(const std::string& datetime) {
    SetDatetimeResult result;

    // 1. 解析 ISO 8601 字符串 "2025-01-06T12:30:00.000"
    struct tm t;
    memset(&t, 0, sizeof(t));
    if (sscanf(datetime.c_str(), "%d-%d-%dT%d:%d:%d",
               &t.tm_year, &t.tm_mon, &t.tm_mday,
               &t.tm_hour, &t.tm_min, &t.tm_sec) < 6) {
        result.accepted = false;
        return result;
    }
    t.tm_year -= 1900;  // struct tm 年份偏移
    t.tm_mon -= 1;      // struct tm 月份 0-11

    // 2. 写入 MCU RTC
    auto mcu = MCU::getInstance();
    if (!mcu->setDatetime(&t)) {
        result.accepted = false;
        return result;
    }

    // 3. 同步系统时钟
    struct timeval tv;
    tv.tv_sec = mktime(&t);
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);

    result.accepted = true;
    result.datetime = datetime;
    return result;
}
```

#### `setWorkMode()`

```cpp
WorkModeResult SystemServiceT32::setWorkMode(int mode) {
    // 注意：MCU::readWorkingMode() 只读，没有 writeWorkingMode()
    // 实际切换工作模式可能需要：
    //   - 通过 MCU I2C 写寄存器
    //   - 或通过 Power::requestChangeMode() 触发模式切换
    //   - 或写入配置文件后重启
    // 需要根据实际硬件方案确定

    WorkModeResult result;
    result.accepted = true;
    result.mode = mode;
    return result;
}
```

### 6.3 关键注意事项

| 项目 | 说明 |
|------|------|
| ISO 解析 | 输入格式 `"2025-01-06T12:30:00.000"`，用 sscanf 简单解析即可 |
| MCU 时间偏移 | `setDatetime()` 内部会自动加 YEAR_OFFSET(1900) 和 MONTH_OFFSET(1) |
| 双重设置 | 需要同时写 MCU RTC 和 `settimeofday()` 系统时钟 |
| 工作模式 | MCU 只有 `readWorkingMode()` 没有写方法，切换方式待确认 |
| 模式值 | 0=仅拍照, 1=拍照+上传, 2=仅上传, 3=测试, 4=UVC |

### 6.4 验证方法

```bash
# 设置时间
curl -X POST http://<device_ip>/api/v1/system/datetime \
  -H 'Content-Type: application/json' \
  -d '{"datetime":"2025-06-15T10:30:00.000"}'

# 验证：
# 1. HTTP 返回 {"accepted": true, "datetime": "2025-06-15T10:30:00.000"}
# 2. 设备上执行 date 命令，确认系统时间已更新
# 3. 设备重启后时间是否保持（MCU RTC 持久化）

# 切换工作模式
curl -X POST http://<device_ip>/api/v1/system/workmode \
  -H 'Content-Type: application/json' \
  -d '{"mode":0}'

# 验证：
# 1. HTTP 返回 {"accepted": true, "mode": 0}
# 2. 设备行为符合模式定义（mode=0 时只拍照不上传）
```

---

## 7. MCU API 速查表

### 7.1 设备信息

| API | 返回 | 单位/格式 | 错误值 |
|-----|------|----------|--------|
| `readPID()` | string | ASCII 32字节，如 `"C154E001M4500046"` | `""` |
| `readFirmwareVersion()` | string | DSP 版本 `"X.Y.Z"` | 默认 `"1.0.0"` |
| `readVersion()` | int | MCU 版本原始整数 | — |
| `convertVersion(int)` | string | 格式 `"V%02d.%03d"` | — |

### 7.2 传感器

| API | 返回 | 单位 | 错误值 | 备注 |
|-----|------|------|--------|------|
| `readTemperature()` | int | 摄氏度 | 0（歧义） | 原始值已减 125 |
| `readHumidity()` | int | 百分比 % | 0 | 0-100 |
| `readAtmosPressure()` | int | hPa | 0 | 原始值已除 10 |
| `readCds()` | int | 原始值 | 0 | **⚠️ 死代码，永远返回 0** |
| `readBatteryVoltage()` | int | decivolt | 0 | 126 = 12.6V，×100 得 mV |
| `readBatteryLevel()` | int | 等级 0-3 | 0 | **⚠️ 死代码，永远返回 0** |
| `readExternalVoltage()` | int | decivolt | 0 | 同 batteryVoltage |
| `readBatteryType()` | int | 类型标识 | — | — |

### 7.3 时间

| API | 返回 | 格式 | 错误值 |
|-----|------|------|--------|
| `getDatetime()` | struct tm | 年=实际年-1900, 月=0-11 | 部分填充 |
| `setDatetime(tm*)` | bool | 同上 | false |

### 7.4 系统

| API | 返回 | 值域 | 错误值 |
|-----|------|------|--------|
| `readWorkingMode()` | int | 0=仅拍照, 1=拍照+上传, 2=仅上传, 3=测试, 4=UVC | -1 |

### 7.5 存储

| API | 返回 | 单位 | 错误值 |
|-----|------|------|--------|
| `Disk::getInfo(path)` | DiskInfo{total, free} | MB | {0, 0} |

---

## 8. 实现优先级建议

### Phase 1：基础数据通道（验证 MCU I2C 通信）

1. **ISensorService** — 调用最多的 MCU API，验证 I2C 通道
2. **IDeviceService** — 简单，验证 PID/版本读取

### Phase 2：系统控制

3. **ISystemService::setDatetime()** — 验证 MCU RTC 写入 + 系统时钟同步
4. **ISystemService::setWorkMode()** — 需先确认写入方式

### Phase 3：存储完善

5. **IStorageService** — 验证 Disk::getInfo 路径正确性
6. **IStorageService::formatStorage()** — 最后实现（破坏性操作）

### Phase 4：Camera 补全

7. **ICameraService::startBurstPhoto()** — 复用 timer 模式
8. **ICameraService 状态追踪** — getPhotoStatus / getRecordStatus
9. **ICameraService::factoryReset()** — 需谨慎

---

## 9. 已知问题清单

| 问题 | 位置 | 影响 | 建议处理时机 |
|------|------|------|-------------|
| `readBatteryLevel()` 死代码 | MCU.cpp:285 `return 0;` | battery_level 永远为 0 | Phase 1，需要修复 MCU 代码 |
| `readCds()` 死代码 | MCU.cpp:452 `return 0;` | cds 永远为 0 | Phase 1，需要修复 MCU 代码 |
| 电压单位不一致 | SensorServiceSim (mV) vs MCU API (decivolt) | T32 实现需做 ×100 转换 | Phase 1 实现时处理 |
| `readWorkingMode()` 无写方法 | MCU.h | setWorkMode 实现方式待定 | Phase 2 |
| `formatStorage()` 未实现 | StorageServiceT32 | 格式化功能不可用 | Phase 3 |
