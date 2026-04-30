# Device / Storage 代码入口与字段来源参考

## 1. 目的

给后续会话提供最小充分入口，用于快速回答：
- 这几个接口代码在哪
- 哪些字段是写死的
- 哪些底层能力已经存在
- 当前最容易误读的点是什么

## 2. 当前最重要代码入口

### 2.1 HTTP handler
- `src/service/http_server/http_api_v1.cpp`

重点看：
- `build_device_info_json()`
- `build_sensor_data_json()`
- `build_storage_info_json()`
- `api_v1_device_info()`
- `api_v1_device_sensors()`
- `api_v1_storage_info()`
- `api_v1_storage_format()`

### 2.2 底层能力线索
- `src/hardware/disk/Disk.h`
- `src/hardware/disk/Disk.cpp`
- `src/hardware/mcu/MCU.h`
- `src/hardware/mcu/MCU.cpp`

## 3. 当前字段来源要点

### 3.1 `device/info`
当前字段主要是固定字符串：
- `pid`
- `camera_ver`
- `camera_model`
- `camera_build`
- `mcu_ver`

### 3.2 `device/sensors`
当前除 `datetime` 外，主要也是固定构造值：
- `battery`
- `battery_level`
- `battery_type`
- `ext_power`
- `sdcard_capacity`
- `sdcard_used`
- `cds`
- `temp`
- `press`
- `rh`

### 3.3 `storage/info`
当前固定构造：
- `total`
- `free`
- `used`

### 3.4 `storage/format`
当前返回：
- `accepted=true`
- `status="scheduled"`

但没有真实执行链证据。

## 4. 当前已存在的底层能力线索

### 4.1 `Disk::getInfo()`
已存在：
- `DiskInfo { total, free }`
- `Disk::getInfo(path)`

### 4.2 MCU 电池读取
已存在 MCU 电池电压相关读取实现。

但要注意：
- “仓库里存在底层能力” 不等于 “HTTP V1 已接过去”

## 5. 当前最容易犯的理解错误

- 不要把 reference 里的示例值当成真实采样值
- 不要把字段稳定误写成后端闭环已完成
- 不要把底层能力存在误写成 HTTP 已接线
- 不要把 device / storage 域成熟度写成和 camera 域一样

## 6. 推荐阅读顺序

进入本主题时建议按下面顺序：
1. `doc/knowledge/specs/device-and-storage-surface-behavior.md`
2. `doc/knowledge/decisions/device-and-storage-http-surface-vs-real-backend-depth.md`
3. 本文
4. 再看：
   - `src/service/http_server/http_api_v1.cpp`
   - `src/hardware/disk/Disk.cpp`
   - `src/hardware/mcu/MCU.cpp`
