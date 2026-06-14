# Device & System API

本文档覆盖本轮（McuService 集成）填实 / 改造的 4 个 HTTP v1 端点。

## 1. 通用约定

- Base path：`/api/v1`
- 默认端口：8080（sim build）；80 / 配置文件 MDNS_CTRL_PORT（T32 build）
- Content-Type：`application/json`（除二进制端点外）
- 响应包络：
  ```json
  {"code": 0, "message": "success", "data": <payload>}
  ```
- 错误包络：
  ```json
  {"code": <int>, "message": <string>, "data": <null|object>}
  ```
- CORS：`Access-Control-Allow-Origin: *`

## 2. 端点清单

| Method | Path | 端到端 | 来源 |
|--------|------|--------|------|
| GET | `/api/v1/device/info` | 本轮填实 | `IDeviceService::getDeviceInfo()` → McuService |
| GET | `/api/v1/device/sensors` | 本轮填实 | `ISensorService::getSensorData()` → McuService |
| POST | `/api/v1/system/datetime` | 本轮填实 | `ISystemService::setDatetime()` → McuService::setDatetime → settimeofday + `MCU::setDatetime` |
| POST | `/api/v1/system/workmode` | 改返 501 | `ISystemService::setWorkMode()` 返 accepted=false；handler 改返 501 |

---

## 3. `GET /api/v1/device/info`

读取设备识别信息。`mcu_ver` / `pid` / `camera_ver` 现经由 `McuService` 取自 MCU 寄存器（sim 下为 I2C bypass 值；T32 下为真实 I2C 读）。

### 请求

无 body；无 query。

### 响应 200

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "pid": "T32-CAM-001",
    "camera_ver": "1.0.0",
    "camera_model": "T32",
    "camera_build": "2025-01-06",
    "mcu_ver": "MCU-1.0.0"
  }
}
```

字段说明：

| 字段 | 来源 | T32 行为 |
|---|---|---|
| `pid` | `McuService::getPID()` → `MCU::readPID()` | I2C 读 PID 寄存器 |
| `camera_ver` | `McuService::getFirmwareVersion()` → `MCU::readFirmwareVersion()` | I2C 读固件版本寄存器（注意：当前 `MCU.cpp` 该方法为 stub，返 "1.0.0"） |
| `camera_model` | 硬编码 `"T32"` |  |
| `camera_build` | （暂无） | 后续可填 `cmake` 编译期注入的 build date |
| `mcu_ver` | `McuService::getMcuFirmwareVersion()` → `MCU::convertVersion(MCU::readVersion())` | `readVersion()` 返 int；`convertVersion()` 格式化为 `"V1.104"` 形式 |

### 错误码

- 405：方法不是 GET

---

## 4. `GET /api/v1/device/sensors`

读取设备传感器快照。`battery*` / `ext_power` / `cds` / `temp` / `press` / `rh` 现经由 `McuService` 取自 MCU 寄存器。`sdcard_*` 仍由 storage service 提供（本轮不接）。

### 请求

无 body；无 query。

### 响应 200

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "battery": 3700,
    "battery_type": 1,
    "battery_level": 85,
    "ext_power": 12000,
    "sdcard_capacity": 32000,
    "sdcard_used": 8000,
    "cds": 500,
    "temp": 25,
    "press": 1013,
    "rh": 60,
    "datetime": "2026-06-13T10:39:34.000"
  }
}
```

字段说明：

| 字段 | 类型 | 来源 | T32 行为 |
|---|---|---|---|
| `battery` | int (mV) | `McuService::getBattery1Voltage()` | `MCU::readBattery1Voltage()` |
| `battery_type` | int | `McuService::getBatteryType()` | `MCU::readBatteryType()` |
| `battery_level` | int (%) | `McuService::getBatteryLevel()` | `MCU::readBatteryLevel()`（当前 MCU.cpp stub） |
| `ext_power` | int (mV) | `McuService::getExternalVoltage()` | `MCU::readExternalVoltage()` |
| `sdcard_capacity` | int (MB) | storage service（未改） | `Disk::getInfo()` |
| `sdcard_used` | int (MB) | storage service（未改） | `Disk::getInfo()` |
| `cds` | int | `McuService::getCds()` | `MCU::readCds()`（光敏电阻 raw 值） |
| `temp` | int (°C) | `McuService::getTemperature()` | `MCU::readTemperature()` |
| `press` | int (hPa) | `McuService::getAtmosPressure()` | `MCU::readAtmosPressure()` |
| `rh` | int (%) | `McuService::getHumidity()` | `MCU::readHumidity()` |
| `datetime` | string | `McuService::getDatetime()` → ISO 8601 | `MCU::getDatetime()` 格式化 |

### 错误码

- 405：方法不是 GET

---

## 5. `POST /api/v1/system/datetime`

把传入的 UTC 时间同步到系统时钟与 MCU RTC。

### 请求

```json
{ "datetime": "2026-06-13T12:00:00" }
```

格式要求：ISO 8601 `YYYY-MM-DDTHH:MM:SS`（秒精度；不含时区后缀，解释为 UTC）。

### 响应 200

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "datetime": "2026-06-13T12:00:00",
    "accepted": true
  }
}
```

### 错误码

| HTTP | code | 触发条件 |
|---|---|---|
| 400 | 400 | body 解析失败 / 缺 `datetime` 字段 / `datetime` 不是 string |
| 405 | 405 | 方法不是 POST |
| 200 + accepted:false | 0 | `McuService::setDatetime` 内部解析失败（非法 ISO 格式） |

注意：sim 下 `settimeofday` 会因权限失败，但不影响 `accepted:true`（写 RTC 仍走 I2C bypass 返成功；前端只需要看 `accepted` 字段判断业务结果）。

### 流程

`McuService::setDatetime(iso8601)`：
1. `strptime(iso, "%Y-%m-%dT%H:%M:%S", &tm)` 解析
2. `timegm(&tm)` → UTC epoch 秒
3. `settimeofday(tv)` 写系统时钟
4. `MCU::getInstance()->setDatetime(&tm)` 写 MCU RTC

---

## 6. `POST /api/v1/system/workmode`

**本轮改造**：因 MCU 模块没有 I2C 路径写工作模式（work mode 由 GPIO / MCU 固件侧切换），端点改为返回 **HTTP 501**。

### 请求

```json
{ "mode": 1 }
```

`mode` 字段：int，工作模式编号。当前仅诊断时使用——T32 设备启动时由 `WorkMode::getWorkingMode()` 读出实际模式。

### 响应 501

```json
{
  "code": 501,
  "message": "work mode is firmware-set via GPIO/MCU firmware, not writable via I2C",
  "data": {
    "mode": 1,
    "accepted": false,
    "reason": "work mode is firmware-set via GPIO/MCU firmware, not writable via I2C"
  }
}
```

### 错误码

| HTTP | code | 触发条件 |
|---|---|---|
| 400 | 400 | body 解析失败 / 缺 `mode` 字段 / `mode` 不是 int |
| 405 | 405 | 方法不是 POST |
| 501 | 501 | 任何合法请求均返 501（work mode firmware-only） |

### 替代路径

如需查询当前 work mode，使用：
- `htc_media_app` 启动时调 `WorkMode::getWorkingMode()` → 写入 `htc_main_app` argv `-wm`
- 当前 `htc_main_app` 没有 GET workmode 端点；如需可加 `/api/v1/system/workmode` GET（独立 future 任务）

---

## 7. 错误码汇总

| code | HTTP | 含义 |
|---|---|---|
| 0 | 200 | 成功 |
| 400 | 400 | 请求格式 / 字段错误 |
| 404 | 404 | 端点不存在 |
| 405 | 405 | 方法不允许 |
| 501 | 501 | 业务不支持（work mode 写） |
| 1001 | 200 | 拍照 / burst 失败 |
| 1002 | 200 | 定时拍照已在运行 |
| 1005 | 200 | 录影已开始 |
| 1006 | 200 | 录影未在运行 |
| 1007 | 200 | 任务 / preview 失败 |
| 1008 | 200 | 缩略图失败 |

## 8. 示例会话

```bash
# 读设备信息
curl -s http://localhost:8080/api/v1/device/info | jq .

# 读传感器
curl -s http://localhost:8080/api/v1/device/sensors | jq .

# 写 datetime
curl -s -X POST -d '{"datetime":"2026-06-13T12:00:00"}' \
     -H 'Content-Type: application/json' \
     http://localhost:8080/api/v1/system/datetime

# 写 workmode（会返 501）
curl -s -X POST -d '{"mode":1}' \
     -H 'Content-Type: application/json' \
     -w 'HTTP %{http_code}\n' \
     http://localhost:8080/api/v1/system/workmode

# 读 status（带 MCU 字段）
curl -s 'http://localhost:8080/api/v1/camera/status?group=device' | jq '.data.status.device[] | select(.raw_name | test("MCU|Battery|EPower"))'
curl -s 'http://localhost:8080/api/v1/camera/status?group=signal' | jq '.data.status.signal[] | select(.source == "real") | {name, value}'
```
