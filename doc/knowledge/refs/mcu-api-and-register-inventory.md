# MCU API 清单与 HTTP 使用对照

> 范围：T32 平台上通过 I2C 与外置 MCU 通信的全部能力。本文回答三个问题——
> (1) MCU 给我们 list 了哪些功能；(2) HTTP API 中 MCU 相关功能用了其中哪些；
> (3) 哪些 MCU API 完全没有被使用。给出一一对照。
>
> 真相源：`src/hardware/mcu/MCU.h`（API 声明）、`src/hardware/mcu/MCUParams.h`（寄存器映射）、
> `src/hardware/mcu/MCU.cpp`（实现）、`src/service/mcu/McuService.{h,cpp}`（service facade）。
> 调用点经全仓 grep 复核（MCU 仅经 `MCU::getInstance()` / `McuService::getInstance()` 访问，
> 无其它实例）。建立日期 2026-06-16，基线 commit `3d854ab`。

---

## 1. 总览结论（先读这段）

| 维度 | 数量 | 说明 |
|---|---|---|
| MCU 类公开方法（不含 ctor/dtor/getInstance） | **132** | 逐项见 §4 |
| 其中**被任何调用方使用** | **45** | 含读+写；调用点见 §2/§3 |
| └ 其中 **HTTP API 触达** | **23** | 4 端点 + camera/status，见 §5 |
| └ 其中 **仅 app/网络等非 HTTP 路径** | **22** | main_app / MgmtServClient / RemoteCtrl / WorkMode |
| 其中**无调用方（未用）** | **87** | 详见 §6，已 grep 复核为零调用（含 getDatetime 休眠、readAllTestData 测试辅助；纯死 85） |
| 寄存器参数（`MCUParams.h`） | **71** | 6 个地址段，见 §3 |
| HTTP 端点（device/system/sensor 域） | **4** | 仅 `device/info`、`device/sensors`、`system/datetime`、`system/workmode`；**无** `/sensor/*`、无按字段拆分的 device 端点 |
| CameraParameterRegistry 字段 | **142** | 其中 **18 个 STATUS 字段** MCU 绑定（REAL） |

**一句话**：MCU 通过 I2C 暴露了 132 个方法（覆盖电池/信号/传感器/GPS/RTC/PIR/Timer/探测器/PID 等 11 大类），
但 HTTP API 只用了其中 **23 个**（全部走 `McuService` 这个 facade），另有 22 个方法被主程序/网络心跳/远程控制等**非 HTTP 路径**使用，
剩下 **87 个方法**（含全部 PIR/Timer/TDS 设置类、全部 ESOR 探测器类、全部 SOR 扩展传感器类）当前没有任何调用方——属于 V104 固件提供但应用层尚未消费的能力。

---

## 2. 调用架构（数据如何从 I2C 流到 HTTP）

```
┌─────────────┐   I2C /dev/i2c-*   ┌──────────────────┐
│  外置 MCU    │◄──────────────────►│  IIC (传输层)     │  IIC.h/cpp —— read(reg,buf,n)/write(reg,buf,n)
│ (V104 固件)  │   寄存器 R/W         │  src/hardware/    │  mutex 串行化总线
└─────────────┘                    │  mcu/IIC.*        │
                                   └────────┬─────────┘
                                            │
                            ┌───────────────▼────────────────┐
                            │  MCU (硬件 API 单例)            │  MCU.h —— ~132 方法
                            │  src/hardware/mcu/MCU.*         │  每个 read*/write* 封装一次 I2C 读/写
                            │  MCU::getInstance()             │  PARAM_MCU_* 寄存器定义见 MCUParams.h
                            └──────┬──────────────────┬───────┘
                  直接调用(非HTTP) │                  │ 经 facade
                                   │                  ▼
  ┌────────────────────────────────┤     ┌──────────────────────────────┐
  │ ① htc_main_app                  │     │ McuService (进程内 facade)   │  McuService.h —— 仅 24 getter + setDatetime
  │   main_app.cpp                  │     │ src/service/mcu/McuService.* │  同步直读，无缓存/无线程
  │   generateDescInfo()/syncWithMCU│     │ McuService::getInstance()    │
  │ ② network lib                   │     └──┬───────────┬──────────┬────┘
  │   MgmtServClient (心跳/注册)    │        │           │          │
  │   RemoteCtrlClient (远程指令)   │        ▼           ▼          ▼
  │ ③ app_workmode lib              │   DeviceSvcT32 SensorSvcT32 SystemSvcT32
  │   WorkMode::getWorkingMode()    │   (device/info)  (device/   (system/
  └────────────────────────────────┘                   sensors)   datetime)
                                                                ▲
                                            CameraParameterRegistry.readMcuValue()
                                            （18 个 STATUS 字段 → /camera/status, /camera/properties/*）
                                                                ▼
                                                     ┌─────────────────────┐
                                                     │   HTTP API (civetweb) │  http_api_v1.cpp
                                                     │   /api/v1/...          │
                                                     └─────────────────────┘
```

**关键事实**：
- MCU 是单例（`MCU::getInstance()`，私有 ctor），全仓只有 **6 个文件** 持有它：`main_app.cpp`、`WorkMode.cpp`、`MgmtServClient.cpp`、`RemoteCtrlClient.cpp`、`McuService.cpp`、`tests/test_mcu_service.cpp`。这是判定「使用/未使用」的边界——**任何不在这 6 个文件（及 3 个 service impl + 1 个 registry）里出现的方法就是未使用**。
- `McuService` 是把 MCU 收窄暴露给 HTTP 层的唯一通道：它只包装了 24 个 getter + 1 个 `setDatetime`。**HTTP 能看到的 MCU 子集 = McuService 这 25 个方法背后的 23 个 MCU 方法**（`getDatetime()`/`getWorkingMode()`/`getGps()` 三个 getter 不经 HTTP 触达）。
- HTTP 与 MCU 之间没有缓存、没有轮询线程（`mcu-service-architecture.md` 描述的 5s 轮询方案最终未落地，改为按需同步读）。每次 HTTP 请求都同步阻塞 I2C。

---

## 3. 寄存器映射（`MCUParams.h` 转录）

`PARAM_PACK(start, bytes)` = `(start<<8)|bytes`。下表为 MCU 通过 I2C 暴露的全部寄存器。

### 3.1 系统核心 `0x0000–0x000F`
| 宏 | 起址 | 字节 | 语义 |
|---|---|---|---|
| `PARAM_MCU_MODE` | 0x00 | 1 | 启动模式：0初始/1单拍/2拍传/3传输/4测试 |
| `PARAM_MCU_FWORK_MARK` | 0x01 | 1 | 首次启动标记 0~1 |
| `PARAM_MCU_EVENT_STATUS` | 0x02 | 1 | 事件类型（见云平台协议） |
| `PARAM_MCU_PType` | 0x03 | 1 | 网络类型 0~10 |
| `PARAM_MCU_VERSION` | 0x04 | 2 | MCU 版本号，如 10002=V10.002 |

### 3.2 基础信息 / 运行时同步 `0x0010–0x00FF`
| 宏 | 起址 | 字节 | 语义 |
|---|---|---|---|
| `PARAM_MCU_BAT1` | 0x10 | 1 | 电池组1电压，÷10（126→12.6V） |
| `PARAM_MCU_BAT2` | 0x11 | 1 | 电池组2电压，÷10 |
| `PARAM_MCU_EPWR` | 0x12 | 1 | 外部电源电压，÷10 |
| `PARAM_MCU_SPWR` | 0x13 | 1 | 太阳能板电压，÷10 |
| `PARAM_MCU_LOCTION_LON/LAT/ELE` | 0x14/0x18/0x1C | 4/4/2 | GPS 经/纬(÷10⁷)/高程(÷10 m) |
| `PARAM_MCU_PID` | 0x1E | 32 | 设备 PID（String） |
| `PARAM_MCU_UWS` | 0x3E | 1 | 唤醒通信设备：0无/1 433/2 LoRa |
| `PARAM_MCU_UPID` | 0x3F | 32 | 通信设备 PID(SSID)（String） |
| `PARAM_MCU_UPWD` | 0x5F | 64 | 通信设备密码（String） |
| `PARAM_MCU_YEAR…SECOND` | 0x9F…0xA5 | — | RTC 时钟（年2B/月日时分秒各1B） |
| `PARAM_MCU_NUFQ` | 0xA6 | 2 | 未上传数量 |
| `PARAM_MCU_TIMEOUT` | 0xA8 | 2 | 关机倒计时（秒），200=重置 |
| `PARAM_MCU_CAM_STATUS` | 0xAA | 1 | 相机状态：0已关/1开机/2连网/3连服务器/4上传中 |
| `PARAM_MCU_AI_ALARM` | 0xAB | 1 | AI 预警码，0无效 |

### 3.3 信号遥测 `0x0100–0x01FF`
| 宏 | 起址 | 字节 | 语义 |
|---|---|---|---|
| `PARAM_MCU_SIG_TYPE` | 0x100 | 12 | 信号制式（String） |
| `PARAM_MCU_SIG_CF` | 0x10C | 2 | 中心频率/频段 |
| `PARAM_MCU_SIG_TP` | 0x10E | 1 | 发射功率 0~50 dBm |
| `PARAM_MCU_SIG_RSSI` | 0x10F | 1 | RSSI −200~40 dBm |
| `PARAM_MCU_SIG_RSRP` | 0x110 | 1 | RSRP −200~40 dBm |
| `PARAM_MCU_SIG_RSRQ` | 0x111 | 1 | RSRQ −200~40 dBm |
| `PARAM_MCU_SIG_RL` | 0x112 | 2 | 路损 0~32767 dBm |
| `PARAM_MCU_SIG_SNR` | 0x114 | 1 | SNR −50~50 |
| `PARAM_MCU_SIG_TD` | 0x115 | 2 | 传输距离（米） |

### 3.4 传感器遥测 `0x0200–0x02FF`
| 宏 | 起址 | 字节 | 语义 |
|---|---|---|---|
| `PARAM_MCU_CDS_DN` | 0x200 | 1 | 日夜模式 0夜/1昼 |
| `PARAM_MCU_CDS_VALUE` | 0x201 | 1 | CDS 电压值 ÷10 |
| `PARAM_MCU_VTS_ALARM/SENS` | 0x202/0x203 | 1/1 | 振动报警 / 灵敏度 |
| `PARAM_MCU_SOR_TEMPS/RHS` | 0x204/0x205 | 1/1 | 温度(℃)/湿度(%)，255=无 |
| `PARAM_MCU_SOR_APS` | 0x206 | 2 | 气压 hPa，0=无 |
| `PARAM_MCU_SOR_CO/CO2` | 0x208/0x20A | 2/2 | CO/CO₂ ppm |
| `PARAM_MCU_SOR_O2` | 0x20C | 1 | O₂ % |
| `PARAM_MCU_SOR_AL` | 0x20D | 2 | 可见光 lx，65535=无 |
| `PARAM_MCU_SOR_UVL` | 0x20F | 1 | 紫外 0~15，255=无 |
| `PARAM_MCU_SOR_NOISE` | 0x210 | 1 | 噪音 dBA |

### 3.5 外扩无线设备 / 探测器 `0x0300–0x03FF`
| 宏 | 起址 | 字节 | 语义 |
|---|---|---|---|
| `PARAM_MCU_ESOR_WS/WID` | 0x300/0x301 | 1/2 | 唤醒外扩设备标记 / 唤醒设备 PID |
| `PARAM_MCU_ESOR_ADD/ID/TYPE` | 0x303/0x305/0x307 | 2/2/1 | 通信码 / 探测器 ID / 类型 |
| `PARAM_MCU_ESOR_BAT` | 0x308 | 1 | 探测器电池电压 ÷10 |
| `PARAM_MCU_ESOR_GPSA/GPSL/GPSH` | 0x309/0x30D/0x311 | 4/4/2 | 坐标 纬/经(÷10⁷)/高(÷10) |
| `PARAM_MCU_ESOR_VALUE` | 0x313 | 4 | 探测器值（单位随类型） |

### 3.6 设置 / 策略 `0x0400–0x04FF`
| 宏 | 起址 | 字节 | 语义 |
|---|---|---|---|
| `PARAM_MCU_CAM_MAXS` | 0x400 | 1 | 每日拍摄次数上限，0不限 |
| `PARAM_MCU_PIR_MODE/SENS/INT/EN` | 0x401–0x406 | — | PIR 模式/灵敏度/触发间隔/开关 |
| `PARAM_MCU_TIMER/TIMER_INT` | 0x405/0x407 | 1/2 | 定时开关 / 循环拍摄间隔(分) |
| `PARAM_MCU_TIMER_1START…5END` | 0x409–0x41B | 各2 | 5 个时段的起止(分) |
| `PARAM_MCU_TIMER_REPEATS` | 0x41D | 1 | 周重复位图 日/六/五/四/三/二/一 |
| `PARAM_MCU_DEVICE_NAME` | 0x41E | 24 | 设备名称（String） |
| `PARAM_MCU_HEARTRATE` | 0x436 | 3 | 状态上报周期(秒) |
| `PARAM_MCU_UP_MODE/NUFQ` | 0x439/0x43A | 1/1 | 上传模式 / 累计阈值 |
| `PARAM_MCU_TDS_CF/TP/BW` | 0x43B/0x43D/0x43E | 2/1/1 | 发射频率/功率/带宽设置 |

> **寄存器 vs 方法**：71 个寄存器参数对应 ~132 个 C++ 方法——多出来的方法是
> 格式化/换算辅助（`convertVersion`、`convertVoltage`）、复合读（`readGps` 一次读 lon+lat+ele、
> `readBatteryVoltage` 选组、`getDatetime/setDatetime` 拼 6 字段）、以及测试入口（`readAllTestData`）。

---

## 4. MCU API 完整清单（132 方法，按类分组，标注使用情况）

图例：✅ = 有调用方；❌ = 无调用方（死代码，§6）；🔗 = 仅经 McuService→HTTP 触达；⚙ = 非 HTTP 路径（app/网络）触达。

### 4.1 版本 / 电源 / 通用
| 方法 | 返回 | 语义 | 状态 | 触达 |
|---|---|---|---|---|
| `readFirmwareVersion()` | string | 相机固件版本（MCU.cpp 内返 `"1.0.0"` 桩） | ✅ | 🔗 device/info, /camera/status；⚙ MgmtServClient |
| `readVersion()` | int | MCU 固件版本号原始值 | ✅ | 🔗 经 convertVersion |
| `convertVersion(int)` | string | 版本号→`"V10.002"` 串 | ✅ | 🔗 device/info, /camera/status；⚙ RemoteCtrl |
| `powerEnoughForFirmwareUpdate()` | bool | 电量是否足够升级 | ❌ | — |
| `waitFor(int seconds)` | bool | 阻塞等待 | ✅ | ⚙ MgmtServClient(RTMP) |
| `IsWifiStationReady()` | bool | WiFi station 就绪 | ✅ | ⚙ main_app, MgmtServClient |
| `readShutdownVoltage()` | int | 关机电压阈值 | ✅ | ⚙ main_app, MgmtServClient |
| `readLowPowerVoltage()` | int | 低电电压阈值 | ✅ | ⚙ main_app, MgmtServClient |
| `convertVoltage(int)` | string | 电压原始值→`"12.6V"` | ✅ | ⚙ main_app, MgmtServClient |

### 4.2 电池
| 方法 | 返回 | 语义 | 状态 | 触达 |
|---|---|---|---|---|
| `readBattery1Voltage()` | int | 电池组1 | ✅ | 🔗 device/sensors, /camera/status；⚙ main_app, Mgmt, Remote |
| `readBattery2Voltage()` | int | 电池组2 | ✅ | 🔗 /camera/status；⚙ main_app, Mgmt |
| `readBatteryVoltage()` | int | 当前组电压 | ✅ | ⚙ main_app, RemoteCtrl |
| `readBatteryLevel()` | int | 电量百分比 | ✅ | 🔗 device/sensors；⚙ main_app, Mgmt, Remote |
| `readBatteryType()` | int | 电池类型 | ✅ | 🔗 device/sensors, /camera/status；⚙ Remote |

### 4.3 信号
| 方法 | 返回 | 语义 | 状态 | 触达 |
|---|---|---|---|---|
| `readSignalType()` | string | 信号制式 | ✅ | 🔗 /camera/status |
| `readSignalCF()` | int | 中心频率/频段 | ✅ | 🔗 /camera/status；⚙ main_app, Mgmt |
| `readSignalRSSI()` | int | RSSI | ✅ | 🔗 /camera/status；⚙ main_app, Mgmt |
| `readSignalRSRP()` | int | RSRP | ✅ | 🔗 /camera/status；⚙ main_app, Mgmt |
| `readSignalRSRQ()` | int | RSRQ | ✅ | 🔗 /camera/status；⚙ main_app, Mgmt |
| `readSignalSNR()` | int | SNR | ✅ | 🔗 /camera/status；⚙ main_app, Mgmt |
| `readSignalTD()` | int | 传输距离 | ✅ | 🔗 /camera/status；⚙ main_app, Mgmt |
| `readSignalTP()` | int | 发射功率 | ✅ | 🔗 /camera/status；⚙ main_app, Mgmt |
| `readSignalRL()` | int | 路损 | ✅ | 🔗 /camera/status |
| `Is4gExist()` | bool | 4G 上网卡在位 | ✅ | ⚙ main_app, MgmtServClient |

### 4.4 远程唤醒 / GPS / RTC
| 方法 | 返回 | 语义 | 状态 | 触达 |
|---|---|---|---|---|
| `writeRemoteWakeup(int)` | bool | 写远程唤醒 | ✅ | ⚙ RemoteCtrlClient |
| `IsRemoteWakeup()` | bool | 读远程唤醒状态 | ❌ | — |
| `useGpsTime()` | bool | 是否用 GPS 授时 | ✅ | ⚙ RemoteCtrlClient |
| `readGps()` | string | GPS（lon,lat,ele 拼串） | ✅ | ⚙ main_app, Mgmt, Remote |
| `writeGps(string)` | bool | 写 GPS | ✅ | ⚙ RemoteCtrlClient |
| `setDatetime(tm*)` | bool | 写 RTC + settimeofday | ✅ | 🔗 system/datetime；⚙ main_app, Remote |
| `getDatetime()` | tm | 读 RTC | ❌ ⚠ | 仅 McuService::getDatetime 转发，但该 facade 方法**无调用方**（device/sensors 的 datetime 字段恒空） |

### 4.5 RM 探测器 / 事件
| 方法 | 返回 | 语义 | 状态 | 触达 |
|---|---|---|---|---|
| `readCds()` | int | 光敏(CDS) | ✅ | 🔗 device/sensors, /camera/status；⚙ Remote |
| `readRMID()` | int | 探测器 ID | ✅ | ⚙ main_app |
| `readRMType()` | int | 探测器类型 | ✅ | ⚙ main_app |
| `readRMValue()` | int | 探测器值 | ✅ | ⚙ main_app |
| `readRMCount()` | int | 探测器数量 | ✅ | ⚙ main_app |
| `readRMSunPowerValue()` | int | 太阳能电压 | ✅ | ⚙ main_app |
| `readEventType()` | int | 事件类型 | ✅ | ⚙ main_app |
| `readEventID()` | int | 事件 ID | ✅ | ⚙ main_app |
| `readEventNum()` | int | 事件序号 | ✅ | ⚙ main_app |

### 4.6 设备标识（PID/UPID/UPWD）
| 方法 | 返回 | 语义 | 状态 | 触达 |
|---|---|---|---|---|
| `readPID()` | string | 设备 PID | ✅ | 🔗 device/info, /camera/status；⚙ main_app |
| `readUPID()` | string | 通信设备 PID | ✅ | ⚙ main_app(syncWithMCU) |
| `readUPWD()` | string | 通信设备密码 | ✅ | ⚙ main_app(syncWithMCU) |
| `writePID(string)` | bool | 写设备 PID | ❌ | — |
| `writeUPID(string)` | bool | 写通信设备 PID | ❌ | — |
| `writeUPWD(string)` | bool | 写通信设备密码 | ❌ | — |

### 4.7 环境
| 方法 | 返回 | 语义 | 状态 | 触达 |
|---|---|---|---|---|
| `readTemperature()` | int | 温度 ℃ | ✅ | 🔗 device/sensors, /camera/status；⚙ main_app, Mgmt, Remote |
| `readHumidity()` | int | 湿度 % | ✅ | 🔗 device/sensors, /camera/status；⚙ main_app, Mgmt, Remote |
| `readAtmosPressure()` | int | 气压 hPa | ✅ | 🔗 device/sensors, /camera/status；⚙ main_app, Mgmt, Remote |
| `readExternalVoltage()` | int | 外部电源 | ✅ | 🔗 device/sensors, /camera/status；⚙ main_app, Mgmt, Remote |

### 4.8 工作模式
| 方法 | 返回 | 语义 | 状态 | 触达 |
|---|---|---|---|---|
| `readWorkingMode()` | int | 工作模式（0单拍/2拍传/3传输/4测试） | ✅ | ⚙ WorkMode.cpp（注意：HTTP `system/workmode` 是 501 桩，**不**调它） |

### 4.9 系统类参数（`0x00` 段读接口）
| 方法 | 返回 | 对应寄存器 | 状态 |
|---|---|---|---|
| `readFworkMark()` | int | FWORK_MARK | ❌ |
| `readEventStatus()` | int | EVENT_STATUS | ❌ |
| `readPType()` | int | PType | ❌ |

### 4.10 基础信息类参数（`0x10`–`0xFF` 段读接口）
| 方法 | 对应寄存器 | 状态 |
|---|---|---|
| `readUWS()` | UWS | ❌ |
| `readCDS_DN()` | CDS_DN（日夜模式） | ❌ |
| `readCDS_Value()` | CDS_VALUE | ❌ |
| `readVTSAlarm()` | VTS_ALARM | ❌ |
| `readVTSSens()` | VTS_SENS | ❌ |
| `readNUFQ()` | NUFQ（未上传数） | ❌ |
| `readTimeout()` | TIMEOUT（关机倒计时） | ❌ |
| `readCamStatus()` | CAM_STATUS | ❌ |
| `readAIAlarm()` | AI_ALARM | ❌ |

### 4.11 传感器类参数 SOR（`0x200` 段读接口）
| 方法 | 对应寄存器 | 状态 |
|---|---|---|
| `readSOR_AL()` | 可见光 lx | ❌ |
| `readSOR_UVL()` | 紫外 | ❌ |
| `readSOR_NOISE()` | 噪音 dBA | ❌ |
| `readSOR_CO()` | CO ppm | ❌ |
| `readSOR_CO2()` | CO₂ ppm | ❌ |
| `readSOR_O2()` | O₂ % | ❌ |

### 4.12 外扩无线 / 探测器 ESOR（`0x300` 段）
| 方法 | 对应寄存器 | 状态 |
|---|---|---|
| `readESOR_WS()` | 唤醒标记 | ❌ |
| `readESOR_WID()` | 唤醒设备 PID | ❌ |
| `readESOR_ADD()` | 通信码 | ❌ |
| `readESOR_ID()` | 探测器 ID | ❌ |
| `readESOR_TYPE()` | 探测器类型 | ❌ |
| `readESOR_BAT()` | 探测器电池 | ❌ |
| `readESOR_GPSA()` | 纬度 | ❌ |
| `readESOR_GPSL()` | 经度 | ❌ |
| `readESOR_GPSH()` | 高度 | ❌ |
| `readESOR_Value()` | 探测器值 | ❌ |
| `writeESOR_WS(int)` | 写唤醒标记 | ❌ |
| `writeESOR_WID(int)` | 写唤醒 PID | ❌ |

### 4.13 设置 / 策略类（`0x400` 段，读 + 写）
**读（25）**：`readCAM_MAXS`、`readPIR_MODE/SENS/INT/EN`、`readTIMER`、`readTIMER_INT`、
`readTIMER_1START…5END`（10）、`readTIMER_REPEATS`、`readDEVICE_NAME`、`readHEARTRATE`、
`readUP_MODE`、`readUP_NUFQ`、`readTDS_CF/TP/BW` —— **全部 ❌**

**写（25）**：`writeCAM_MAXS`、`writePIR_MODE/SENS/INT/EN`、`writeTIMER`、`writeTIMER_INT`、
`writeTIMER_1START…5END`（10）、`writeTIMER_REPEATS`、`writeDEVICE_NAME`、`writeHEARTRATE`、
`writeUP_MODE`、`writeUP_NUFQ`、`writeTDS_CF/TP/BW` —— **全部 ❌**

> 说明：这些 PIR/Timer/TDS/上传策略参数在**应用层**完全没走 MCU——HTTP 侧对应配置
> （`PIR_Mode`、`Timer_*`、`HeartRate` 等）是经 `settings`/`device_config` 持久化读写（PERSISTED），
> 见 §7。MCU 提供的同名寄存器读写接口目前无人调用。

### 4.14 测试
| 方法 | 状态 |
|---|---|
| `readAllTestData()` | ❌（仅单元测试入口，非生产路径） |

---

## 5. HTTP API ↔ MCU 对照（核心）

### 5.1 触达 MCU 的 HTTP 端点（共 4 个 + camera/status）

| HTTP 端点 | handler (http_api_v1.cpp) | service | 经 McuService | 触达的 MCU 方法 |
|---|---|---|---|---|
| `GET /api/v1/device/info` | `api_v1_device_info` :720 | DeviceServiceT32 | getPID / getFirmwareVersion / getMcuFirmwareVersion | `readPID`, `readFirmwareVersion`, `convertVersion`+`readVersion`（model/buildDate 写死） |
| `GET /api/v1/device/sensors` | `api_v1_device_sensors` :734 | SensorServiceT32 | 8 个 getter | `readBattery1Voltage`, `readBatteryType`, `readBatteryLevel`, `readExternalVoltage`, `readCds`, `readTemperature`, `readAtmosPressure`, `readHumidity`（sdcard 写死 0；**datetime 恒空**） |
| `POST /api/v1/system/datetime` | `api_v1_system_datetime` :748 | SystemServiceT32 | setDatetime | `setDatetime`（+ settimeofday） |
| `POST /api/v1/system/workmode` | `api_v1_system_workmode` :777 | SystemServiceT32 | — | **无**（501 桩，`accepted:false`，工作模式由固件/GPIO 决定不可 I2C 写） |
| `GET /api/v1/camera/status`<br>`GET /api/v1/camera/properties/*` | `api_v1_camera_status` :1368 等 | CameraParameterRegistry.readMcuValue :682 | 18 个 getter | 见 §5.3 |

> **没有** `/api/v1/sensor/*`、没有 `/device/battery` `/device/signal` `/device/gps` 等按字段拆分端点——
> 所有电池/信号/环境数据都汇入单一 `/api/v1/device/sensors`。

### 5.2 McuService facade ↔ MCU ↔ HTTP 触达

| McuService getter | → MCU 方法 | HTTP 端点 |
|---|---|---|
| `getPID()` | readPID | device/info, camera/status |
| `getFirmwareVersion()` | readFirmwareVersion | device/info |
| `getMcuFirmwareVersion()` | convertVersion+readVersion | device/info, camera/status |
| `getBattery1Voltage()` | readBattery1Voltage | device/sensors, camera/status |
| `getBattery2Voltage()` | readBattery2Voltage | camera/status |
| `getBatteryType()` | readBatteryType | device/sensors, camera/status |
| `getBatteryLevel()` | readBatteryLevel | device/sensors |
| `getExternalVoltage()` | readExternalVoltage | device/sensors, camera/status |
| `getCds()` | readCds | device/sensors, camera/status |
| `getTemperature()` | readTemperature | device/sensors, camera/status |
| `getHumidity()` | readHumidity | device/sensors, camera/status |
| `getAtmosPressure()` | readAtmosPressure | device/sensors, camera/status |
| `getSignalType()` | readSignalType | camera/status |
| `getSignalCF()` | readSignalCF | camera/status |
| `getSignalTP()` | readSignalTP | camera/status |
| `getSignalRSSI()` | readSignalRSSI | camera/status |
| `getSignalRSRP()` | readSignalRSRP | camera/status |
| `getSignalRSRQ()` | readSignalRSRQ | camera/status |
| `getSignalRL()` | readSignalRL | camera/status |
| `getSignalSNR()` | readSignalSNR | camera/status |
| `getSignalTD()` | readSignalTD | camera/status |
| `setDatetime()` | setDatetime | system/datetime |
| `getWorkingMode()` | readWorkingMode | **无**（仅 WorkMode.cpp 用） |
| `getGps()` | readGps | **无**（仅 app/网络用） |
| `getDatetime()` | getDatetime | **无**（facade 方法零调用，休眠） |

> 结论：McuService 25 个方法中，**22 个经 HTTP 触达**（21 getter + setDatetime），
> 3 个不经 HTTP（`getWorkingMode`/`getGps` 被 app/网络用，`getDatetime` 休眠）。
> HTTP 实际触达的 **去重 MCU 方法 = 23 个**。

### 5.3 `/api/v1/camera/status` 的 18 个 MCU 绑定字段

经 `CameraParameterRegistry::readMcuValue()`（:682）。STATUS 组共 ~44 字段，其中 18 个 `source=real`：

| 字段 | member → McuService | 分组 |
|---|---|---|
| MCU_Version | mcuVersion → getMcuFirmwareVersion | Device |
| Battery_Type | batteryType → getBatteryType | Device |
| Battery1 | battery1Voltage → getBattery1Voltage | Device |
| Battery2 | battery2Voltage → getBattery2Voltage | Device |
| EPower | externalVoltage → getExternalVoltage | Device |
| Signal_Type | signalType → getSignalType | Signal |
| Signal_CF | signalCF → getSignalCF | Signal |
| Signal_TP | signalTP → getSignalTP | Signal |
| Signal_RSSI | signalRSSI → getSignalRSSI | Signal |
| Signal_RSRP | signalRSRP → getSignalRSRP | Signal |
| Signal_RSRQ | signalRSRQ → getSignalRSRQ | Signal |
| Signal_RL | signalRL → getSignalRL | Signal |
| Signal_SNR | signalSNR → getSignalSNR | Signal |
| Signal_TD | signalTD → getSignalTD | Signal |
| Sensor_CDS | cds → getCds | Sensor |
| Sensor_TEMPS | temperature → getTemperature | Sensor |
| Sensor_RHS | humidity → getHumidity | Sensor |
| Sensor_APS | pressure → getAtmosPressure | Sensor |

> 同组其余字段为 placeholder：`Signal_BW`、`Sensor_AL/UVL/NOISE/CO/CO2/O2`（对应 MCU 的 `readSignalBW` 无、
> `readSOR_*` 全未接）、`Location_LON/LAT/ELE`（GPS 占位）、`SPower`（太阳能，`readRMSunPowerValue` 仅 app 用，未入 registry）。

---

## 6. 未使用 MCU API 清单（86 + 1 休眠，全部已 grep 复核）

**判定方法**：`grep -rn '<方法>' src/ tests/ --include='*.cpp' --include='*.h'` 排除 `src/hardware/mcu/`，
确认零命中。MCU 单例只有 6 个持有文件，故该判定是闭环的。

### 6.1 有明确业务语义但未接（9）—— 接入价值最高
| 方法 | 寄存器 | 业务意义 |
|---|---|---|
| `getDatetime()` ⚠休眠 | RTC | device/sensors 已留 datetime 字段却恒空；接上即可补全 |
| `IsRemoteWakeup()` | — | 与已用的 `writeRemoteWakeup` 配对，读当前状态 |
| `powerEnoughForFirmwareUpdate()` | — | OTA 前置电量检查 |
| `writePID/UPID/UPWD()` | PID/UPID/UPWD | 设备标识写入（读侧已用，写侧空） |
| `readFworkMark/EventStatus/PType()` | 0x00 段 | 首启标记/事件状态/网络类型 |

### 6.2 基础信息类读接口未接（9）
`readUWS`、`readCDS_DN`（日夜模式）、`readCDS_Value`、`readVTSAlarm`、`readVTSSens`、
`readNUFQ`（未上传数）、`readTimeout`、`readCamStatus`、`readAIAlarm`

### 6.3 扩展传感器 SOR（6）—— 对应 camera/status 占位字段
`readSOR_AL`、`readSOR_UVL`、`readSOR_NOISE`、`readSOR_CO`、`readSOR_CO2`、`readSOR_O2`

### 6.4 外扩探测器 ESOR（12）—— 整个探测器子系统未接
读 10：`readESOR_WS/WID/ADD/ID/TYPE/BAT/GPSA/GPSL/GPSH/Value`；写 2：`writeESOR_WS/WID`

### 6.5 设置/策略类（50）—— 应用层改走 settings 持久化，MCU 寄存器侧闲置
读 25：`readCAM_MAXS`、`readPIR_MODE/SENS/INT/EN`、`readTIMER`、`readTIMER_INT`、
`readTIMER_1START…5END`、`readTIMER_REPEATS`、`readDEVICE_NAME`、`readHEARTRATE`、
`readUP_MODE`、`readUP_NUFQ`、`readTDS_CF/TP/BW`
写 25：`writeCAM_MAXS`、`writePIR_MODE/SENS/INT/EN`、`writeTIMER`、`writeTIMER_INT`、
`writeTIMER_1START…5END`、`writeTIMER_REPEATS`、`writeDEVICE_NAME`、`writeHEARTRATE`、
`writeUP_MODE`、`writeUP_NUFQ`、`writeTDS_CF/TP/BW`

### 6.6 测试辅助（1）
`readAllTestData()`

> **合计**：6.1(9, 含休眠 getDatetime) + 6.2(9) + 6.3(6) + 6.4(12) + 6.5(50) + 6.6(1) = **87** 未使用（其中 `getDatetime` 为休眠、`readAllTestData` 为测试辅助，纯死 85）。已用 45；合计 132。

---

## 7. 现有文档现状与缺口

| 问题 | 答案 |
|---|---|
| 有没有文档描述 MCU / I2C 通信？ | **有，但分散且偏架构**。`decisions/mcu-service-architecture.md` 讲 service 抽象与方案选型；`playbooks/mcu-http-api-test.md` 列 5 个 MCU 端点与 18 个绑定字段；`doc/api/device-and-system-api.md` 给 4 端点字段契约。 |
| 有没有**寄存器映射**文档？ | **无**。71 个寄存器只存在于 `MCUParams.h` 代码注释里（本文 §3 首次转录）。 |
| 有没有**完整 MCU API 清单**？ | **无**。最全的 `t32-service-implementation-guide.md §7` 只覆盖 ~16 个方法。本文 §4 首次覆盖全部 132 个。 |
| 有没有 **HTTP→MCU 使用对照**？ | **部分**，仅 McuService 触达面。本文 §5 首次给出去重对照（23 个触达 / 45 个总用 / 88 个未用）。 |
| 有没有**未用 API 清单**？ | **无**。本文 §6 首次给出（已 grep 闭环复核）。 |

**缺口**（按优先级）：
1. 寄存器映射文档化（§3 已填）。
2. MCU↔HTTP 使用对照 + 未用清单（§5/§6 已填）。
3. I2C 传输层文档：`IIC.h`、`I2C_BYPASS`/`MCU_EXIST` 的 sim 行为、阻塞/超时特性、返回码约定（`MCU.cpp` I2C 失败返 0）—— 目前只有 `bugs/T32-iso-only-no-mcu-desc-*.md` 把它当 700ms 阻塞源提了一句。
4. 单一 MCU 模块入口索引（本文可充任）。

---

## 8. 建议（非强制，供后续 feature 参考）

- **高价值低成本**：激活 `McuService::getDatetime()` → 填 `device/sensors.datetime`（字段已留，I2C 读已具备）。
- **补 camera/status 占位**：6 个 `readSOR_*`（AL/UVL/NOISE/CO/CO2/O2）接入 registry，把对应 6 个 placeholder 字段转 real。
- **设策略类（§6.5）** 需先决策：MCU 寄存器 vs `settings` 持久化作为单一真相源——目前两者并存且 MCU 侧闲置，是潜在不一致点。
- 死代码（§6 中纯死方法）若确认长期不接，可考虑从 `MCU.h` 移除以缩面；或保留作为 V104 固件能力清单。

---

## 9. 相关文档

- `doc/knowledge/decisions/mcu-service-architecture.md` — McuService 抽象决策（方案 C）
- `doc/api/device-and-system-api.md` — 4 个 MCU 端点字段契约
- `doc/knowledge/playbooks/mcu-http-api-test.md` — MCU 端点实机测试手册
- `doc/knowledge/specs/device-and-storage-surface-behavior.md` — device/storage 面行为（pre-McuService）
- `src/hardware/mcu/MCU.h` / `MCUParams.h` / `MCU.cpp` — API 与寄存器真相源
- `src/service/mcu/McuService.{h,cpp}` — service facade（HTTP 唯一通道）
