# HTTP→MCU 真机联调测试手册

## 1. 目的

验证 commit `6906b3f`（feat(mcu): add McuService and wire device/sensor/system HTTP endpoints）
把 MCU 数据接到 HTTP V1 表层后，**在 T32 真机上确实返回真实 MCU 值**，而不只是返回
stub/占位值。

这篇手册聚焦「真机 MCU 真实性」，与下列已有手册互补、不重叠：

- `http-api-simu-test.md` —— 用 sim 构建 + `test_http_server` 验证 API 契约（返回的是 stub）
- `device-storage-http-probe.md` —— 验证 device/storage 字段结构（明确「不适用于证明值来自真实硬件」）
- `../decisions/mcu-service-architecture.md` —— McuService 同步读取的架构决策
- `../specs/http-api-reference.md` —— 完整接口手册

## 2. 适用范围

**适用于：**

- 在 T32 真机上验证 `/device/info`、`/device/sensors`、`/system/datetime`、`/camera/status`
  中的 MCU 绑定属性确实来自 I2C/MCU 读取。
- 验证 MCU 时钟写入闭环。

**不适用于：**

- 仅验 API 路由 / JSON 结构（用 `http-api-simu-test.md`）。
- 在 PC sim 上验证 MCU 真实值——sim 下 `device/info`、`device/sensors` 走 Sim stub，
  `/camera/status` 走 McuService 但 I2C 被绕过返回 0（见下表）。

## 3. 关键前提：sim vs 真机行为差异

`ServiceProvider` 用 `#ifdef SIMULATION_MODE` 切换服务实现。HTTP→MCCU 真链路
（`Device/Sensor/SystemService → McuService → MCU → I2C`）**只在 T32 真机生效**。

| 端点 | T32 真机（`-m`） | PC sim 构建 |
|------|------------------|-------------|
| `/device/info`、`/device/sensors` | 真实 MCU 值 | Sim stub（写死值，**不走 McuService**） |
| `/camera/status` 的 MCU 绑定属性 | 真实 MCU 值 | 走 McuService，I2C 绕过 → **0 / 空** |
| `/system/datetime`（POST） | 真写 MCU + 系统时钟 | `SystemServiceSim`，**不碰 MCU** |

**结论**：要真正测到 HTTP→MCU，必须用测试 PC 去 curl 一台同网段、运行
`htc_main_app -m`（T32 构建）的 T32 设备。PC 本地跑 sim 只能验契约。

sim 下 `/device/sensors` 的 stub 值（来自 `SensorServiceSim`，供对照识别）：
`battery=3700 battery_type=1 battery_level=85 ext_power=12000 cds=500
temperature=25 pressure=1013 humidity=60`，`datetime` 为本机当前时间。

## 4. 环境准备

```bash
# 设备侧（T32 真机，T32 构建）：启动 mobile 模式
./bin/htc_main_app -m            # HTTP 默认 80，RTSP 默认 8554

# 测试 PC 上设好目标
IP=192.168.0.198; PORT=80

# 连通性自检
curl -s http://$IP:$PORT/api/v1/device/info | head
```

如需在 PC 本地跑 sim 对照（验契约）：

```bash
cmake -DBUILD_FOR_SIMULATION=ON -B build_sim -S .
cmake --build build_sim -j$(nproc)
./build_sim/bin/htc_main_app -m   # 80 端口可能需要 sudo，或改 config.sim.ini 的 ctrl port
```

## 5. MCU 相关端点清单

| # | 方法 | 路径 | MCU 来源字段 | 真机期望 | sim 期望 |
|---|------|------|-------------|---------|---------|
| ① | GET | `/api/v1/device/info` | `pid`、`camera_ver`、`mcu_ver` | 真实 PID / 版本 | stub |
| ② | GET | `/api/v1/device/sensors` | `battery`、`battery_type`、`battery_level`、`ext_power`、`cds`、`temp`、`press`、`rh`、`datetime` | 真实值 | 写死 stub |
| ③ | POST | `/api/v1/system/datetime` | 写 MCU + 系统时钟 | `accepted=true` | 不碰 MCU |
| ④ | GET | `/api/v1/camera/status?group=all` | Device/Signal/Sensor 下的 MCU 绑定属性（见 5.4） | 真实值 | 0 / 空 |
| ⑤ | GET | `/api/v1/camera/properties/{name}` | 单个 MCU 绑定属性 | 真实值 | 0 / 空 |
| 附 | POST | `/api/v1/system/workmode` | 只读，应拒绝 | **501** | 501 |

### 5.1 `GET /api/v1/device/info`

```bash
curl -s http://$IP:$PORT/api/v1/device/info
```

`data` 字段：`pid`、`camera_ver`、`camera_model`、`camera_build`、`mcu_ver`。
MCU 来源：`pid`、`camera_ver`、`mcu_ver`（经 `DeviceServiceT32 → McuService`）。

### 5.2 `GET /api/v1/device/sensors`

```bash
curl -s http://$IP:$PORT/api/v1/device/sensors
```

`data` 字段：`battery`、`battery_type`、`battery_level`、`ext_power`、`sdcard_capacity`、
`sdcard_used`、`cds`、`temp`、`press`、`rh`、`datetime`。
注意：`temp`/`press`/`rh` 为字符串，其余为数字；`sdcard_capacity`/`sdcard_used` 由 storage 负责，
当前为 0。其余几乎全部 MCU 来源。

### 5.3 `POST /api/v1/system/datetime`（写 MCU 时钟）

```bash
curl -s -X POST http://$IP:$PORT/api/v1/system/datetime \
  -H 'Content-Type: application/json' \
  -d '{"datetime":"2026-06-16T12:00:00"}'
```

`data`：`{datetime, accepted}`，期望 `accepted=true`。回读验证：

```bash
curl -s http://$IP:$PORT/api/v1/device/sensors   # 看 datetime 是否更新
```

### 5.4 `GET /api/v1/camera/status`（MCU 绑定属性，覆盖最全）

```bash
curl -s 'http://$IP:$PORT/api/v1/camera/status?group=all'
# 或按组：?group=Device | ?group=Signal | ?group=Sensor
```

经 `CameraParameterRegistry::readMcuValue → McuService` 的属性：

- **Device 组**：`MCU_Version`、`Battery_Type`、`Battery1`、`Battery2`、`EPower`
- **Signal 组**：`Signal_Type`、`Signal_CF`、`Signal_TP`、`Signal_RSSI`、`Signal_RSRP`、
  `Signal_RSRQ`、`Signal_RL`、`Signal_SNR`、`Signal_TD`
  （`Signal_BW` 为 placeholder，**不走** MCU）
- **Sensor 组**：`Sensor_CDS`、`Sensor_TEMPS`、`Sensor_RHS`、`Sensor_APS`

### 5.5 `GET /api/v1/camera/properties/{name}`（单个 MCU 属性）

```bash
curl -s 'http://$IP:$PORT/api/v1/camera/properties/Battery1'
curl -s 'http://$IP:$PORT/api/v1/camera/properties/MCU_Version'
```

### 5.6 `POST /api/v1/system/workmode`（应正确拒绝）

```bash
curl -s -X POST http://$IP:$PORT/api/v1/system/workmode \
  -H 'Content-Type: application/json' -d '{"mode":1}'
```

期望 HTTP **501**：work mode 由 GPIO / MCU 固件决定，不可经 I2C 写入。验证它正确拒绝、
不产生副作用。

## 6. 通过标准

- **所有读端点**：`code=0`，`data` 字段齐全、JSON 结构正确。
- **真机 MCU 真实性**：MCU 来源字段值非 0，且随物理状态变化：
  - 遮挡 CDS 传感器 → `cds` / `Sensor_CDS` 变化。
  - 拔/接外部电源 → `ext_power` / `EPower` 变化。
  - 写入 `datetime` 后能从 `/device/sensors` 回读到新值。
- **workmode**：返回 501，设备工作模式未改变。

## 7. 已知噪音（可忽略）

- `E/LEGACY Audio init failed, continue without audio` —— 环境问题，RTSP 照常起。
- `mDNS model CXXX is invalid, fallback to T32`、`auto switch thread is not running`
  —— 已降级为 debug（commit `3d854ab`），默认 INFO 级不再出现。
- mDNS TXT 中 `model=T32` 是 `PModel=CXXX` 占位值降级，不影响设备发现。

## 8. 一键冒烟脚本

```bash
IP=192.168.0.198; PORT=80; B=http://$IP:$PORT/api/v1
for ep in "GET device/info" "GET device/sensors" "GET camera/status?group=all"; do
  m=${ep%% *}; p=${ep#* }
  echo "=== $m /$p ==="; curl -s -X $m "$B/$p"; echo
done
echo "=== POST system/datetime ==="
curl -s -X POST "$B/system/datetime" -H 'Content-Type: application/json' \
  -d '{"datetime":"2026-06-16T12:00:00"}'; echo
```

## 9. 相关文档

- `../decisions/mcu-service-architecture.md` —— McuService 同步读取架构
- `../decisions/device-and-storage-http-surface-vs-real-backend-depth.md` —— 表层 vs 真后端深度
- `../specs/http-api-reference.md` —— 完整接口手册
- `http-api-simu-test.md` —— sim 契约测试
- `device-storage-http-probe.md` —— device/storage 字段结构探针
