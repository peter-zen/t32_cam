# 属性通道配置加载与生效机制分析

> 分析日期: 2026-05-05
> 更新日期: 2026-05-05 (P0+P1 实现完成)
> 背景: 属性通道(CameraParameterRegistry + CameraPropertyService)已实现属性的 set/get/persist，但需要梳理 set 保存为 JSON/INI 后，配置如何在下一次启动时被加载并产生实际效果。

---

## 1. 架构概览

```
┌──────────────────────────────────────────────────────────────────────┐
│                         属性通道 (Property Channel)                    │
│                                                                       │
│  HTTP API ──→ CameraServiceT32::setProperty()                        │
│                  └── CameraPropertyService::setRegistryPropertyValue()│
│                       ├── writeRegistryValue() → Settings 内存字段     │
│                       │   └── persistSettings() → saveToJsonFile()   │
│                       └── writeRegistryValue() → DeviceConfig 内存    │
│                           └── persistDeviceConfig() → flush()        │
├──────────────────────────────────────────────────────────────────────┤
│                         启动加载 (Startup Load)                        │
│                                                                       │
│  media_app / main_app                                                │
│    → EnvManager::parsePrimaryEnv()  // 设定 CONFIG_FILE, SETTING_FILE │
│    → DeviceConfig::getInstance()    // 构造时 parse(ini) 到内存       │
│    → Settings::loadFromJsonFile()   // JSON 各字段加载到 Settings 内存 │
├──────────────────────────────────────────────────────────────────────┤
│                         运行时生效 (Runtime Apply)                     │
│                                                                       │
│  拍照/录影: takePhoto()/startRecord() 直接读 Settings 当前值           │
│  网络相关:  main_app.cpp 连接时 config->get()                         │
│  功能开关:  isParameterEnabled() 检查 INI Functions 节                 │
│  MCU 相关:  缺少启动时主动下发到 MCU 的环节                             │
└──────────────────────────────────────────────────────────────────────┘
```

## 2. 存储体系

属性根据 `ParameterStorageKind` 分为两类存储:

| Storage Kind | 存储介质 | 写入口 | 读入口 |
|-------------|---------|-------|-------|
| `SETTINGS` | JSON 文件 (setting.json) | `CameraPropertyService::writeRegistryValue()` → `Settings` 字段 | `CameraPropertyService::readRegistryValue()` → `Settings` 字段 |
| `DEVICE_CONFIG` | INI 文件 (config.sim.ini) | `CameraPropertyService::writeRegistryValue()` → `DeviceConfig::set()` | `CameraPropertyService::readRegistryValue()` → `DeviceConfig::get()` |
| `PLACEHOLDER` | 无 | 返回 "not implemented" 错误 | 返回默认值 |
| `COMPUTED` | 无 | 只读 | 计算生成 (如固件版本) |

## 3. 启动加载链路 (已验证完整)

### 3.1 pre-app (media_app.cpp)

```cpp
// media_app.cpp:207-226
EnvManager::getInstance()->parsePrimaryEnv(ENV_FILE_PATHNAME);
auto config = DeviceConfig::getInstance();  // 构造时 parse(ini)
// ...
setting_file_path = EnvManager::getInstance()->getEnv("SETTING_FILE_PATH", "");
Settings::getInstance()->loadFromJsonFile(setting_file_path);  // 加载 JSON
```

### 3.2 主应用 (main_app.cpp)

```cpp
// main_app.cpp:1042-1045
std::string setting_file_path = EnvManager::getInstance()->getEnv("SETTING_FILE_PATH", "");
if (!setting_file_path.empty()) {
    Settings::getInstance()->loadFromJsonFile(setting_file_path);
}
```

### 3.3 模拟模式路径 (main_app.cpp SIMULATION_MODE)

```
CONFIG_FILE    = projectRoot + "/res/config.sim.ini"
SETTING_FILE   = projectRoot + "/res/setting.json"
```

## 4. 各属性生效状态逐项分析

### 4.1 Camera_Setting (拍照录影属性) — 状态：✅ 完整

| 属性 | 存储 | 生效代码路径 |
|------|------|------------|
| `CAM_Mode` | Settings.cameraMode | `takePhoto()` L110, `startRecord()` L304 即时读取 |
| `CAM_ImageSize` | Settings.stillSize | `takePhoto()` L119 即时读取 |
| `CAM_Shooting_P` | Settings.burstNumber | `takePhoto()` 使用 `burstNumber` |
| `Video_Size` | Settings.videoSize | `getCurrentVideoMode()` → `applyConfiguredVideoParams()` |
| `Video_Encoded` | Settings.videoCodec | `applyConfiguredVideoParams()` L72 |
| `Video_Bitrate_Type` | Settings.videoRcMode | `applyConfiguredVideoParams()` L76 |
| `Video_Length` | Settings.videoLength_h/l | `getVideoRecordLength()` → `startRecord()` L337 |
| `CAM_ImageQuality` | SETTINGS (stillQuality) | ✅ P0 已补全，`takePhoto()` 读取 + JPEG 1-3→1-100 映射 |
| `CAM_Shooting_INT` | SETTINGS (shootingInterval) | ✅ P1 已补全，`startBurstPhoto()` 读取 (100ms 单位) |
| `CAM_Ffixed_Shutter` | PLACEHOLDER | ⏳ 等待 ISP 驱动接口 |
| `CAM_Min_Shutter` | PLACEHOLDER | ⏳ 等待 ISP 驱动接口 |
| `Video_Bitrate_Value` | PLACEHOLDER | ⏳ 暂保持 (bitrate 通过 resolution bucket 计算) |

**结论**: 拍照录影核心属性全部通过运行时即时读取生效。P0/P1 补全了 ImageQuality 和 Shooting_INT。剩余等待 ISP 驱动。

### 4.2 Audio_Setting (音频属性) — 状态：✅ 基本完整

| 属性 | 存储 | 生效代码路径 |
|------|------|------------|
| `Audio_SPK_Volume` | DeviceConfig [DEVICE] SPKVOL | ⚠️ 需确认音频播放器是否读取 |
| `Audio_Record_Volume` | Settings.audioRecordVolume | `startRecord()` L327 即时读取 |
| `Audio_Record_Gain` | Settings.audioRecordGain | `startRecord()` L328 即时读取 |

### 4.3 PIR_Setting (PIR 红外属性) — 状态：⚠️ 缺少 MCU 下发

| 属性 | 存储 | 当前写入 | 生效缺口 |
|------|------|---------|---------|
| `PIR_Mode` | Settings.pirEn | `writeRegistryValue()` 写 Settings 内存 | **启动时未下发到 MCU** |
| `PIR_Sensitivity` | Settings.ckPirSensitivity | 同上 | **启动时未下发到 MCU** (`MCU.cpp` PARAM_PIR_SENSITIVITY) |
| `PIR_Interval` | PLACEHOLDER | ❌ 不支持 | — |
| `PIR_MaxShooting` | Settings.shootingLimits | 写入 Settings 内存 | **使用方不明确** |

**老代码路径** (`RemoteCtrlClient.cpp`): 这些属性是通过旧 API 直接写入 Settings 后再同步下发 MCU 的。新属性通道写入后，Settings 有值但 MCU 没收到。启动时也缺少"读取 Settings → 下发 MCU"的初始化逻辑。

### 4.4 Timer_Setting (定时属性) — 状态：⚠️ 缺少调度器

| 属性 | 存储 | 生效缺口 |
|------|------|---------|
| `Timer_Enable` | Settings.timerEn | ✅ P1 `initScheduler()` 已消费 |
| `Timer_1Start/1End` | Settings.timer1s/e_h/m | ✅ P1 `isInTimeWindow()` 已消费 |
| `Timer_2Start/2End` | Settings.timer2s/e_h/m | ✅ P1 `isInTimeWindow()` 已消费 |
| `Timer_3Start/3End` | Settings.timer3s/e_h/m | ✅ P1 `isInTimeWindow()` 已消费 |
| `Timer_4Start` ~ `Timer_5End` | PLACEHOLDER | ⏳ 由 INI `Timer_Range_MAX` 控制，当前默认 3 |
| `Timer_Interval_Time` | SETTINGS (timerLapse) | ✅ P1 已补全，格式 "MM:SS" |
| `Timer_Repeats` | Settings.weekRepeats | ✅ P1 `isInTimeWindow()` 已消费 |
| `Timer_PIR_Enable` | PLACEHOLDER | ⏳ 依赖 MCU PIR 功能 |

**说明**: P1 实现了后台调度器 `initScheduler()`，在 CameraServiceT32 构造时自动启动。每 30 秒检查时间窗口，窗口内按 `Timer_Interval_Time` 间隔自动拍照。

### 4.5 Network_Setting (网络属性) — 状态：✅ 完整

| 属性 | 存储 | 生效代码路径 |
|------|------|------------|
| `CSSID` / `CPWR` | DeviceConfig [DEVICE] CSSID/CPWD | `main_app.cpp:1387-1388` WiFi 连接时读取 |
| `UPID` / `UPWR` | DeviceConfig [SYS] UPID/UPWD | `main_app.cpp:1167-1168` 上级 WiFi 连接 |
| `DHCP_ON` 等 | PLACEHOLDER | ❌ 无存储 |

### 4.6 Server_Setting (服务器属性) — 状态：✅ 完整

| 属性 | 存储 | 生效代码路径 |
|------|------|------------|
| `M_Server` | DeviceConfig [SERVER] MS_IP | `main_app.cpp:1503` 管理服务连接 |
| `NTP_Server` | DeviceConfig [SERVER] NTP_IP | `main_app.cpp:1208` NTP 同步 |
| `NTP_Timezone` | DeviceConfig [NTP] TIMEZONE | `main_app.cpp:1128` 时区设置 |
| `BS_Server` | DeviceConfig [SERVER] FS_IP | 备用服务器连接 |
| `AI_Server` | PLACEHOLDER | ❌ 无存储 |

### 4.7 System_Setting (系统属性) — 状态：部分完整

| 属性 | 存储 | 生效缺口 |
|------|------|---------|
| `Device_Name` | Settings.devName | ✅ 描述文件生成时读取 |
| `Stamp` | Settings.stampEn | ⚠️ 拍照时读取应用，但需确认叠加逻辑是否完整 |
| `Stamp_List` | PLACEHOLDER | ❌ 无存储 |
| `Cycle` (自动覆盖) | Settings.autoCover | ✅ `startRecord()` 空间检查时读取 |
| `HeartRate` (心跳间隔) | Settings.heartRate_0~3 | ⚠️ 写入 Settings 后，**`MgmtServClient` 是否读取此值控制心跳频率?** |
| `Remote_Wakeup` | Settings.remote_wakeup | ⚠️ 旧代码 `RemoteCtrlClient.cpp:659` 写入时同步调 `MCU::writeRemoteWakeup()`，但属性通道 set 后**没有调用 MCU 下发** |
| `Upload_*` 系列 | PLACEHOLDER | ❌ 无存储 |
| `Data_Suicide` | PLACEHOLDER | ❌ 无存储 |
| `VTS_Sensitivity` | PLACEHOLDER | ❌ 无存储 |
| `GPS_*` | PLACEHOLDER | ❌ 无存储 |

### 4.8 AI_Setting — 状态：❌ 全为 Placeholder

全部 7 个 AI 属性均为 `PLACEHOLDER` 存储，set 时返回错误。

### 4.9 Factory 属性 (INI Functions 节) — 状态：✅ 作为 dependency 开关生效

这些属性存 INI `[Functions]` 节，由 `isParameterEnabled()` 动态读取，控制其他属性的启用/禁用:

```
Photo_DS_EN, Video_DS_EN, Data_ASD_EN, Data_AUD_EN, AI_Alarm_SET,
BServer_EN, RWakeup_SET, Smart_DCVR_EN, Video_ATR_EN, DHCP_EN,
Stamp_EN, VTS_Alarm_EN, Audio_SPK_EN, Timer_Range_MAX, Upload_EN
```

通过 `DeviceConfig::get()` 即时读取，无需额外 apply。

## 5. 核心问题汇总

### 5.1 ✅ P0 已解决: CAM_ImageQuality binding + JPEG 质量映射

- `CAM_ImageQuality`: placeholderBinding → settingsBinding("stillQuality") ✅
- JPEG 质量映射: `getStillQualityForJpeg()` 1→60, 2→80, 3→95 ✅
- `takePhoto()`: `quality=0` 时自动从 Settings 读取 mapped 值 ✅

### 5.2 ✅ P1 已解决: Timer 调度器 + Burst Photo + Shooting_INT + Timer_Interval_Time

- `CAM_Shooting_INT`: placeholderBinding → settingsBinding("shootingInterval") ✅
- `startBurstPhoto()`: 线程驱动连拍实现 ✅
- `Timer_Interval_Time`: placeholderBinding → settingsBinding("timerLapse") ✅
- `initScheduler()`: 后台调度器，每 30s 检查时间窗口，自动触发拍照 ✅
- `isInTimeWindow()`: 多时间段 + weekRepeats 检查 ✅

### 5.3 MCU 下发缺失 (等待 MCU 同事)

以下属性在通过属性通道 set 后，只更新了 Settings 内存/JSON 文件，但**没有调用 MCU 接口下发**到硬件。

| 属性 | MCU 接口 | 影响 |
|------|---------|------|
| `PIR_Mode` (pirEn) | 需确认 MCU PIR enable 写入接口 | PIR 开关不生效 |
| `PIR_Sensitivity` (ckPirSensitivity) | `MCU::PARAM_PIR_SENSITIVITY` 相关接口 | PIR 灵敏度不生效 |
| `Remote_Wakeup` (remote_wakeup) | `MCU::writeRemoteWakeup()` | 远程唤醒不生效 |

### 5.4 ISP 驱动未就绪属性

| 属性 | 当前存储 | 阻塞项 |
|------|---------|--------|
| `CAM_Ffixed_Shutter` | PLACEHOLDER | ISP 快门控制接口 |
| `CAM_Min_Shutter` | PLACEHOLDER | ISP 快门控制接口 |
| metering/wb/iso/ev/edge/stablizer/pvflickermode | 无 Registry 定义 | ISP 驱动接口 |

### 5.5 其余 Placeholder 属性

- `Video_Bitrate_Value`: 保持 PLACEHOLDER (bucket 方式已满足需求)
- `PIR_Interval`: 依赖 MCU PIR 功能
- `Timer_4Start`~`Timer_5End`: 由 INI `Timer_Range_MAX` 控制，当前默认 3
- `Timer_PIR_Enable`: 依赖 MCU PIR 功能
- `DHCP_ON`/`LOCAL_IP`/`NETMASK`/`GATEWAY`/`DNS1`/`DNS2`: DHCP 自动获取，无需 writable
- `Upload_*` 系列: 上传模块通过 INI Policy 节控制
- `Data_Suicide`/`VTS_Sensitivity`/`GPS_*`: 依赖 MCU 或需独立评估
- `Stamp_List`: 依赖 stamp overlay 模块
- 全部 `AI_Setting` 属性: AI 模块未就绪

### 5.6 心跳间隔未消费 (P2)

`heartRate` 存入 Settings，但需确认 `MgmtServClient` 心跳发送逻辑是否读取此值。

---

## 6. 排除 MCU 相关后的属性对齐方案

> MCU 功能由其他同事并行开发中，以下分析排除 MCU 相关属性（PIR_Setting / Remote_Wakeup / VTS_Sensitivity / Audio_SPK_Volume）。

### 6.1 非 MCU 属性分类总览

```
Registry Property (非MCU)
├── ✅ 已完整闭环 ──── CAM_Mode, CAM_ImageSize, CAM_Shooting_P, Video_Size,
│                      Video_Encoded, Video_Bitrate_Type, Video_Length,
│                      Audio_Record_Volume, Audio_Record_Gain, Stamp, Cycle,
│                      Device_Name, CSSID/CPWD, UPID/UPWR, M_Server, NTP_Server,
│                      NTP_Timezone, BS_Server
│
├── ✅ P0 已补全 ──── CAM_ImageQuality (stillQuality + JPEG quality mapping)
│
├── ✅ P1 已补全 ──── CAM_Shooting_INT (shootingInterval + startBurstPhoto),
│                      Timer_Interval_Time (timerLapse),
│                      Timer_Enable + 时间窗口 + Week_Repeats (initScheduler)
│
├── ⏳ 等待 ISP 驱动 ── CAM_Ffixed_Shutter, CAM_Min_Shutter
│
├── ⏳ 等待 ISP 驱动 ── metering, wb, iso, ev, edge, stablizer, pvflickermode
│                      (Settings 字段已存在，但无 Registry 定义和消费代码)
│
├── ⏳ 等待 MCU 同事 ── PIR_Mode, PIR_Sensitivity, Remote_Wakeup, Audio_SPK_Volume,
│                      VTS_Sensitivity, GPS_Enable, GPS_Value
│
├── ❓ 待评估 ──── HeartRate (MgmtServClient 消费确认),
│                 Stamp_List (stamp overlay 模块),
│                 stillDriverMode, videoQuality, videoSeamless (历史遗留字段)
│
└── ❌ 保持 PLACEHOLDER ── DHCP_ON/LOCAL_IP/NETMASK/GATEWAY/DNS1/DNS2 (自动获取)
                           Timer_PIR_Enable (依赖 MCU)
                           Data_Suicide (安全功能，需独立评估)
                           Upload_* 系列 (通过 INI Policy 节控制)
                           AI_Server + AI_Setting 全部 (AI 模块未就绪)
                           Timer_4Start~5End (Timer_Range_MAX 控制)
                           Video_Bitrate_Value (bucket 方式已满足需求)
```

### 6.2 第一类: ✅ P0 已完成 — CAM_ImageQuality (stillQuality)

**`CAM_ImageQuality` (stillQuality)** — 已完成。

改动内容:
1. `CameraParameterRegistry.cpp`: `placeholderBinding()` → `settingsBinding("stillQuality")`
2. `CameraPropertyService.cpp`: readRegistryValue/writeRegistryValue 添加 stillQuality 分支
3. `CameraPropertyService.cpp`: 新增 `getStillQualityForJpeg()` — 1→60, 2→80, 3→95
4. `CameraServiceT32::takePhoto()`: `quality=0` 时调用 `getStillQualityForJpeg()` 获取映射值

### 6.3 第二类: P1 已完成项

#### ✅ `CAM_Shooting_INT` (连拍间隔) + `startBurstPhoto()` — 已完成

1. `Settings.h`: 添加 `uint8_t shootingInterval = 1;` (单位: 100ms)
2. `Settings.cpp`: saveToJsonFile/loadFromJsonFile 序列化
3. `CameraParameterRegistry.cpp`: `placeholderBinding()` → `settingsBinding("shootingInterval")`
4. `CameraPropertyService.cpp`: read (值×100 输出 ms), write (校验 100-2000 step 100, ÷100)
5. `CameraServiceT32::startBurstPhoto()`: 线程驱动连拍，API interval 优先，interval=0 fallback shootingInterval

#### ✅ `Timer_Interval_Time` — 已完成

1. `CameraParameterRegistry.cpp`: `placeholderBinding()` → `settingsBinding("timerLapse")`
2. `CameraPropertyService.cpp`: read 格式化 "MM:SS", write 解析 "MM:SS" → timerLapse_m/timerLapse_s
3. 复用已有 Settings 字段 timerLapse_m, timerLapse_s (原已序列化)

### 6.4 等待 ISP 驱动的属性 (P2)

#### `CAM_Ffixed_Shutter` / `CAM_Min_Shutter` (快门速度)

- **用途**: ISP 快门速度控制 (固定快门 0~5000, 最低快门 10/15/25/50/100/200)
- **依赖开关**: `Photo_DS_EN` (存 INI [Functions]，已生效)
- **阻塞项**: ISP 驱动接口未就绪
- **结论**: 等 ISP 接口就绪后补全

#### ISP 参数 (metering/wb/iso/ev/edge/stablizer/pvflickermode)

- Settings 字段已存在，但无 Registry 定义，无消费代码
- **阻塞项**: ISP 驱动接口未就绪

#### `Video_Bitrate_Value`

- **推荐**: 保持 PLACEHOLDER。bucket 方式已满足需求。

#### `Stamp_List` (水印内容选择)

- **阻塞项**: stamp overlay 底层模块未就绪

### 6.5 ✅ 已存储+已完成消费逻辑 (原第三类)

#### 6.5.1 ✅ Timer 定时调度 — P1 已完成

`CameraServiceT32::initScheduler()` 在构造时启动后台线程:
- 每 30 秒检查 `isInTimeWindow()`
- 检查 3 个时间窗口 (timer1s/e, timer2s/e, timer3s/e)
- 检查 weekRepeats 星期 bitmap
- 窗口内按 `Timer_Interval_Time` 间隔自动拍照

#### 6.5.2 HeartRate (心跳间隔) — 待评估

**存储状态**: ✅ Settings 序列化 + PropertyService 读写
**缺口**: `MgmtServClient` 心跳发送时是否读取此值？需要查看确认。
**改动**: 如需，在 `MgmtServClient` 心跳定时器中改为读取 Settings.heartRate。

### 6.6 不建议改动（保持 PLACEHOLDER 或转为只读状态）

| 属性 | 理由 |
|------|------|
| DHCP_ON/LOCAL_IP/NETMASK/GATEWAY/DNS1/DNS2 | 这些值由 DHCP 自动获取，手动写入无意义 |
| GPS_Enable/GPS_Value | 定位状态，应为只读 STATUS 而非 writable PROPERTY |
| Timer_PIR_Enable | 依赖 MCU 的 PIR 功能 |
| Data_Suicide | 安全敏感功能，需独立设计评审 |
| Upload_Protocol/Order/Mode/NUFQ/Delete | 上传模块目前通过 INI Policy 节控制，确认后将属性绑定改为 DEVICE_CONFIG |
| AI_Server + AI_Setting 全部 | AI 模块未就绪 |
| Timer_2Start~5End | 由 INI `Timer_Range_MAX` 控制是否启用，目前 Timer_Range_MAX 默认 3 所以 2/3 段可用但 4/5 段被禁用 |

### 6.7 Settings 结构体中存在但无消费者代码的字段（历史遗留）

以下字段在 `Settings.h` 中被定义、在 `Settings.cpp` 中被序列化，但 grep 全仓后**没有任何业务代码读取**。这些字段不在 CameraParameterRegistry 中，属性通道无法操作它们。如需通过属性通道暴露，需要新增 ParameterDefinition。

| 字段 | 用途猜测 | 建议 |
|------|---------|------|
| `stillDriverMode` | 拍照驱动模式 | 确认后加入 Registry |
| `stillStamp` | 拍照水印 | 已有 stampEn，此字段冗余 |
| `videoQuality` | 视频质量 | 确认后加入 Registry |
| `videoSeamless` | 无缝录影 | 确认后加入 Registry |
| `videoStamp` | 视频水印 | 已有 stampEn，此字段冗余 |
| `metering` | 测光模式 | ISP 参数，等 ISP 接口就绪 |
| `wb` | 白平衡 | 同上 |
| `iso` | ISO 感光度 | 同上 |
| `ev` | 曝光补偿 | 同上 |
| `edge` | 边缘增强 | 同上 |
| `stablizer` | 防抖 | 同上 |
| `pvflickermode` | 工频闪烁 | 同上 |
| `viddist` | 视频畸变校正 | 同上 |
| `vidrsc` | 视频分辨率缩放 | 同上 |
| `isWLed` | 白光灯 | 硬件控制 |
| `continuous_record` | 连续录影 | 确认后加入 Registry |
| `showDevNameEn` | 显示设备名 | 确认后加入 Registry |
| `pwdEn` / `devPwd` | 密码相关 | 确认后加入 Registry |
| `onTime_0/1` | 开机时间 | MCU 相关 |
| `lowVol_l/h` / `endVol_l/h` | 低电/关机电压 | MCU 相关 |
| `uploadCnt` | 上传计数 | 运行时状态 |
| `gpsLatitude/Longitude/Altitude*` | GPS 坐标 | MCU 相关 |
| `comm_code` / `euid` / `setting_mark` | 通信码/设备 ID/标记 | 用途不明确 |
| `enable_firmware_update` | 固件升级开关 | 确认后加入 Registry |

**结论**: ISP 参数（metering/wb/iso/ev 等）需要等 ISP 驱动接口就绪后才能有意义地暴露为属性。其余字段需逐项确认业务需求。

---

## 7. 推荐执行优先级

### ✅ P0: 已完成 (2026-05-05)

| 序号 | 改动 | 改动量 |
|------|------|--------|
| 1 | `CAM_ImageQuality` binding (stillQuality) | 1 registry 行 + PropertyService 读写 |
| 2 | JPEG 质量映射 `getStillQualityForJpeg()` (1→60, 2→80, 3→95) | CameraPropertyService.cpp |

### ✅ P1: 已完成 (2026-05-05)

| 序号 | 改动 | 改动量 |
|------|------|--------|
| 3 | `CAM_Shooting_INT` 存储 + `Settings::shootingInterval` | Settings.h/cpp + Registry + PropertyService |
| 4 | `startBurstPhoto()` 线程驱动实现 | CameraServiceT32.h/cpp |
| 5 | `Timer_Interval_Time` 存储 (timerLapse) | Registry + PropertyService |
| 6 | `initScheduler()` 后台定时调度器 | CameraServiceT32.h/cpp |

### P2: 等待 ISP 驱动接口就绪

| 序号 | 改动 | 阻塞项 |
|------|------|--------|
| 7 | `CAM_Ffixed_Shutter`/`CAM_Min_Shutter` 存储 + ISP 调用 | ISP 驱动接口 |
| 8 | ISP 参数 (metering/wb/iso/ev/edge/stablizer/pvflickermode) 暴露为属性 | ISP 驱动接口 |
| 9 | `Stamp_List` 存储 + stamp overlay 实现 | stamp 叠加模块 |
| 10 | HeartRate 消费确认 (MgmtServClient 心跳) | 调研 |

### P3: 等待 MCU 驱动接口就绪 (MCU 同事进行中)

| 序号 | 改动 |
|------|------|
| 11 | PIR_Mode / PIR_Sensitivity MCU 下发 |
| 12 | Remote_Wakeup MCU 下发 |
| 13 | Audio_SPK_Volume 消费代码确认 |
| 14 | GPS / VTS 等 MCU 相关属性 |

### 暂不处理

- DHCP 网络参数 → 自动获取，无需 writable
- AI 全部 → AI 模块未就绪
- Upload 系列 → 通过 INI Policy 节控制
- Timer_4Start~5End → Timer_Range_MAX 控制启用范围
- Video_Bitrate_Value → bucket 方式已满足需求
- Timer_PIR_Enable → 依赖 MCU
- Data_Suicide → 安全敏感，需独立评估
- 历史遗留 Settings 字段 → 按需确认后逐个加入 Registry
