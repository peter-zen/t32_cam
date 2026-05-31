# htc_main_app 模式行为决策规格

> 本文档记录 `htc_main_app` 启动后如何根据传入的工作模式 (`-wm`) 和 `Settings::cameraMode` 决定执行行为（拍照、录像、并发、网络服务等）。  
> 以 `src/app/main_app.cpp` 当前代码为准，逐步完善。

---

## 1. 程序定位

| 项目 | 值 |
|------|-----|
| 可执行文件 | `bin/htc_main_app` |
| 源码 | `src/app/main_app.cpp` |
| 调用方 | `htc_media_app` 通过 `system("htc_main_app -wm <mode> -rtc <status>")` 拉起 |
| 职责 | 接收工作模式，编排具体命令执行路径（拍照/录像/网络服务/上传等） |

---

## 2. 命令行参数与模式映射

### 2.1 入口参数解析

```cpp
// main_app.cpp:1182-1185
working_mode = (enum workingMode)stoi_custom(argv[2]);
is_rtc_work_well = (bool)stoi_custom(argv[4]);
```

参数格式：
```bash
htc_main_app -wm <0-4> -rtc <0|1>
```

| 参数 | 值 | 含义 |
|------|-----|------|
| `-wm` | `0~4` | 工作模式枚举值 |
| `-rtc` | `0` / `1` | RTC 是否正常（影响时间戳和 NTP） |

### 2.2 工作模式 → Command 位掩码映射

`main_app.cpp` 将 `working_mode` 映射为 `command` 整数位掩码：

| `working_mode` | 枚举名 | `command` 位掩码 |
|---------------|--------|-----------------|
| `0` | `WORKING_MODE_SNAP_ONLY` | `CMD_SNAP` |
| `1` | `WORKING_MODE_SNAP_UPLOAD` | `CMD_SNAP \| CMD_CONN_NET \| CMD_DHCP \| CMD_NTP \| CMD_UPLOAD` |
| `2` | `WORKING_MODE_UPLOAD_ONLY` | `CMD_CONN_NET \| CMD_DHCP \| CMD_NTP \| CMD_UPLOAD` |
| `3` | `WORKING_MODE_TEST_ONLY` | `CMD_MOBILE` |
| `4` | `WORKING_MODE_UVC` | `CMD_CONN_NET \| CMD_DHCP \| CMD_RTSP_SERVER` |

Command 位定义（`main_app.cpp:747-760`）：

| 位定义 | 值 | 含义 |
|--------|-----|------|
| `CMD_CONN_NET` | `1 << 0` | 连接网络（WiFi/USB Dongle） |
| `CMD_DHCP` | `1 << 1` | 启动 DHCP |
| `CMD_SNAP` | `1 << 2` | 执行拍照/录像相关逻辑 |
| `CMD_AUDIO_RECORD` | `1 << 3` | 独立音频录制测试 |
| `CMD_VIDEO_RECORD` | `1 << 4` | 独立视频录制测试（命令行直接触发） |
| `CMD_AUTH` | `1 << 5` | 认证 |
| `CMD_HEARTBEAT` | `1 << 6` | 心跳 |
| `CMD_UPLOAD` | `1 << 7` | 上传照片 |
| `CMD_MOBILE` | `1 << 8` | 手机配对/测试模式 |
| `CMD_RTSP_SERVER` | `1 << 9` | RTSP 流服务 |
| `CMD_NTP` | `1 << 10` | NTP 时间同步 |

### 2.3 `-wm` 与 `-m` 的区别

`-wm` 和 `-m` 都能进入 `CMD_MOBILE`（TEST 模式），但用途和子参数支持不同：

| | `-wm` (work-mode) | `-m` (mobile) |
|---|---|---|
| **用途** | 产品正式启动入口 | 手动调试用入口 |
| **调用方** | `htc_media_app` 自动传入 | 开发者/脚本手动执行 |
| **参数格式** | `-wm <0-4> -rtc <0\|1>` | `-m [--no-rtsp] [--no-audio] [--force-day] [--record-stream1]` |
| **行为** | 根据 `working_mode` 枚举映射到一组 `command` 位掩码 | 直接设置 `command = CMD_MOBILE` |
| **子参数** | 不支持 | 支持 4 个调试子参数 |

**`-wm 3`** 示例（产品自动启动 TEST 模式）：
```bash
htc_main_app -wm 3 -rtc 1
```
- 由 `htc_media_app` 在检测到 `WORKING_MODE_TEST_ONLY` 时自动调用
- 进入 `CMD_MOBILE` 分支，启动 HTTP/RTSP/mDNS/TCP Event 服务
- 不支持额外子参数

**`-m`** 示例（手动调试 TEST 模式）：
```bash
htc_main_app -m                          # 标准 TEST 模式
htc_main_app -m --no-rtsp                # 禁用 RTSP
htc_main_app -m --no-audio               # 禁用音频
htc_main_app -m --force-day              # 强制日间模式（设置 HTC_FORCE_RECORD_DAY_MODE=1）
htc_main_app -m --record-stream1         # 录制 stream1（设置 HTC_RECORD_STREAM_ID=1）
htc_main_app -m --no-rtsp --no-audio     # 组合使用
```

**关键结论**：
- 产品上电自动启动用 `-wm`
- 手动调试 TEST 模式用 `-m`，因为它支持 `--no-rtsp`、`--force-day` 等调试开关

---

## 3. CMD_SNAP 分支：cameraMode 决定拍照/录像行为

**这是核心决策逻辑。** `CMD_SNAP` 不是"只拍一张照片"，而是根据 `Settings::cameraMode` 决定执行路径。

### 3.1 代码位置

RTC 正常时（`main_app.cpp:1360-1387`）：
```cpp
if (command & CMD_SNAP && is_rtc_work_well) {
    uint8_t camMode = Settings::getInstance()->cameraMode;
    // ... 根据 camMode 分发
}
```

RTC 异常时（`main_app.cpp:1479-1506`）：逻辑完全一致，仅时间戳可能不准确。

### 3.2 cameraMode 行为矩阵

| `cameraMode` | 行为 | 调用的函数 | 说明 |
|-------------|------|-----------|------|
| `0` | **仅拍照** | `processCmdSnap()` | 纯拍照模式 |
| `1` | **拍照 + 录像** | `processCmdSnap()` → `processCmdVideoRecord()` | 先拍照，再录像 |
| `2` | **仅录像** | `processCmdVideoRecord()` | 跳过拍照，直接录像 |
| `3` | **并发拍照录像** | `processCmdSnap()` → `processCmdConcurrentSnapRecord()` | CH2 硬件并发 8M JPEG + H265 录像 |
| 其他 | 不支持 | 仅日志警告 | — |

### 3.3 各函数职责（待细化）

#### `processCmdSnap(is_rtc_work_well)`
- 待补充：具体拍照流程、输出路径、分辨率设置

#### `processCmdVideoRecord(is_rtc_work_well)`
- 待补充：录像参数、时长控制、存储路径

#### `processCmdConcurrentSnapRecord(is_rtc_work_well)`
- 待补充：CH2 硬件 scaler 并发拍照录像的具体实现
- 已知：使用 Group 2 JPEG 流 + CH0 H265 录像流

---

## 4. 日夜模式（Day/Night）控制

### 4.1 判定依据：CDS 光敏传感器

`DayNightSwitch::getDayNightState()` 读取 GPIO `PA(10)`（CDS 光敏传感器）：

| CDS 引脚状态 | 判定结果 |
|-------------|---------|
| `HIGH` | **NIGHT（夜间/光线暗）** |
| `LOW` | **DAY（白天/光线亮）** |

### 4.2 三个控制面

判定后，`DayNightSwitch` 同步控制三个硬件：

| 控制函数 | DAY 行为 | NIGHT 行为 | 作用 |
|---------|---------|-----------|------|
| `controlISP()` | ISP 彩色模式 | ISP 黑白/红外增强模式 | 图像处理（全局） |
| `controlIRCut()` | IRCut 切到白天位置（过滤红外） | IRCut 切到夜晚位置（允许红外通过） | 滤光片 |
| `controlIRLed()` | 关闭红外 LED | 开启红外 LED 补光 | 补光灯 |

> **关键事实**：sensor 只有一路 streaming，`controlISP()` 是**全局设置**，不可能出现"录影彩色、预览黑白"的情况。

### 4.3 WLED 白光灯强制覆盖

```cpp
auto wled = DeviceConfig::getInstance()->get(INI_SECTION_BOOT, INI_KEY_WLED, 0);
if (state == DayNightState::NIGHT && wled == 1) {
    state = DayNightState::DAY;  // 强制按白天处理
}
```

若 INI 配置 `wled=1`（使用白光灯而非红外灯），即使 CDS 检测到暗环境，也**强制保持 DAY 模式**。

### 4.4 CMD_MOBILE 分支的日夜模式初始化

`-m` 和 `-wm 3` 最终都进入 `CMD_MOBILE` 分支。日夜模式初始化统一放在 `CMD_MOBILE` 分支入口处（`main_app.cpp`）：

```cpp
if (command & CMD_MOBILE) {
    const char* forceDay = std::getenv("HTC_FORCE_RECORD_DAY_MODE");
    if (forceDay && strcmp(forceDay, "1") == 0) {
        daynight_switch->controlISP(DayNightState::DAY);
        daynight_switch->controlIRCut(DayNightState::DAY);
        daynight_switch->controlIRLed(DayNightState::DAY);
    } else {
        auto daynight_state = daynight_switch->getDayNightState();
        daynight_switch->controlISP(daynight_state);
        daynight_switch->controlIRCut(daynight_state);
        daynight_switch->controlIRLed(daynight_state);
    }
}
```

- `-m --force-day`：环境变量在参数解析时设置，CMD_MOBILE 分支中读取并强制 DAY
- `-wm 3`：由 `htc_media_app` 传入，不经过 `-m` 参数解析，但同样进入 CMD_MOBILE 分支

### 4.5 `--force-day` 覆盖链

`CMD_MOBILE` 分支内存在多层 `daynight_switch()` 调用，需确保 forceDay 不被后续模块覆盖：

```
CMD_MOBILE 分支入口：初始化 ISP/IRCut/IRLed（已加 forceDay 检查）
        │
        ▼
启动 RTSP Server → RtspServer::start() → daynight_switch(true)
        │
        ▼
RtspServer::daynight_switch()：也读取 HTC_FORCE_RECORD_DAY_MODE（已修复）
```

| 模块 | 是否读取 `HTC_FORCE_RECORD_DAY_MODE` |
|------|-------------------------------------|
| `main_app.cpp` CMD_MOBILE 分支入口 | ✅ 读取 |
| `VideoRecorder::daynight_switch()` | ✅ 读取 |
| `RtspServer::daynight_switch()` | ✅ **已修复** |
| `ImageSnap::daynight_switch()` | ❌ 未读取（拍照场景需关注） |

### 4.6 `--force-day` 使用方式

**使用场景**：环境光线暗（CDS 检测为 NIGHT）时，希望 preview / 录像保持彩色（DAY 模式）。

**命令**：
```bash
# TEST 模式（手动调试）
htc_main_app -m --force-day

# 也支持组合其他子参数
htc_main_app -m --force-day --no-rtsp
```

**效果验证**：

| 条件 | Preview 图像 | 确认方式 |
|------|-------------|---------|
| 不加 `--force-day`（环境暗） | **黑白**（NIGHT 模式） | CDS 检测为 NIGHT，ISP 切为黑白 |
| 加 `--force-day`（环境暗） | **彩色**（DAY 模式） | forceDay 强制覆盖 CDS，ISP 保持彩色 |

**日志确认**：启动日志中应出现以下两行：
```
CMD_MOBILE: force DAY mode from --force-day
RtspServer daynight_switch: force DAY mode
```

**注意**：
- `--force-day` 只在 `CMD_MOBILE`（TEST 模式）和 `VideoRecorder`/`RtspServer` 中生效
- `ImageSnap` 当前未读取 `HTC_FORCE_RECORD_DAY_MODE`，拍照场景暂不支持 forceDay
- 需要确保 binary 已 rebuild（包含 `main_app.cpp` 和 `RtspServer.cpp` 的修改）

### 4.7 自动切换线程

`DayNightSwitch::startAutoSwitch()` 启动后台线程，每 5 秒读取 CDS 并自动切换：

```cpp
while (running) {
    getDayNightState();
    controlIRCut(dayNightState);
    controlIRLed(dayNightState);
    controlISP(dayNightState);
    sleep(5);
}
```

当前 `main_app.cpp` 的 TEST 模式**未启动该线程**，preview 期间不会自动切换日夜模式。

---

## 5. 非 SNAP 分支行为

### 5.1 TEST 模式（`CMD_MOBILE`）

`main_app.cpp:1569-1671`

```cpp
if (command & CMD_MOBILE) {
    setenv("HTC_TEST_MODE", "1", 1);    // 设置测试环境变量
    // 1. 连接 WiFi（配置中的 CSSID/CPWD）
    // 2. 启动 DHCP
    // 3. 启动 mDNS 服务
    // 4. 启动 HTTP Server
    // 5. 启动 TCP Event Service
    // 6. 若 mobile_rtsp_enabled，启动 RTSP Server
    // 7. 进入 while 循环等待退出信号
}
```

**注意**：TEST 模式**不进入 CMD_SNAP 分支**，因此不会执行拍照/录像逻辑。

### 4.2 UVC 模式（`CMD_RTSP_SERVER`）

`main_app.cpp:1673+`

```cpp
if (command & CMD_RTSP_SERVER) {
    // 启动 RTSP Server
    // 进入流服务循环
}
```

**注意**：UVC 模式也不进入 CMD_SNAP 分支。

### 4.3 联网上传分支

当 `command` 包含 `CMD_CONN_NET / CMD_DHCP / CMD_NTP / CMD_UPLOAD` 时：
- 先连接网络（WiFi 或 USB Dongle）
- 启动 DHCP 获取 IP
- NTP 同步时间
- 执行上传逻辑（`CMD_UPLOAD`）

---

## 5. 关键配置影响

### 5.1 Settings::cameraMode

来源：`settings.json` 中的 `cameraMode` 字段。

决定 `CMD_SNAP` 分支内的具体行为（见 3.2 矩阵）。

### 5.2 Settings::burstNumber

决定 `processCmdSnap()` 连拍张数。

### 5.3 Settings::stillSize

决定 `processCmdSnap()` 拍照分辨率（默认 4M = 2560x1440）。

### 5.4 program_type

来自 `DeviceConfig`（INI 文件 `boot` 节的 `ptype`）：

| `program_type` | 网络接口 |
|---------------|---------|
| `PTYPE_WIFI` | WiFi（INI 中的 `upid`/`upwd`） |
| `PTYPE_ETHERNET` | 有线网口 |
| `PTYPE_USB_DONGLE` | USB 4G  dongle |

### 5.5 mobile_rtsp_enabled

决定 TEST 模式下是否启动 RTSP Server。

---

## 6. 执行顺序图

```
htc_main_app 入口
    │
    ├── 解析 -wm / -rtc 参数
    │   └── working_mode → command 位掩码
    │
    ├── 挂载 SD 卡
    │   └── 若有 factory JSON，导入配置并可能重启
    │
    ├── 根据 program_type 设置网络接口
    │
    ├── 【RTC 处理】
    │   └── CMD_GET_RTC / CMD_SET_RTC（若命令行指定）
    │
    ├── 【核心分支 A：CMD_SNAP】
    │   └── 根据 cameraMode 执行：
    │       ├── camMode=0: processCmdSnap (仅拍照)
    │       ├── camMode=1: processCmdSnap + processCmdVideoRecord (拍照+录像)
    │       ├── camMode=2: processCmdVideoRecord (仅录像)
    │       └── camMode=3: processCmdSnap + processCmdConcurrentSnapRecord (并发)
    │
    ├── 【核心分支 B：联网】
    │   ├── CMD_CONN_NET → 连接 WiFi/USB Dongle
    │   ├── CMD_DHCP → 获取 IP
    │   ├── CMD_NTP → 时间同步
    │   └── CMD_UPLOAD → 上传照片
    │
    ├── 【核心分支 C：TEST 模式】
    │   └── CMD_MOBILE → HTTP/RTSP/mDNS/TCP Event 服务
    │
    ├── 【核心分支 D：UVC 模式】
    │   └── CMD_RTSP_SERVER → RTSP 流服务
    │
    └── 清理退出
```

---

## 7. 待补充项

以下细节尚未记录，后续根据实际问题逐步补充：

- [ ] `processCmdSnap()` 内部完整流程（分辨率设置、文件命名、存储路径、OSD 叠加）
- [ ] `processCmdVideoRecord()` 内部完整流程（编码参数、分段策略、存储路径）
- [ ] `processCmdConcurrentSnapRecord()` 内部完整流程（CH2 Group 配置、JPEG 质量、与 VideoRecorder 的协调）
- [ ] `CMD_UPLOAD` 上传流程细节（HTTP 上传、重试策略、上传完成后行为）
- [ ] 各模式下的电源管理行为（拍完是否关机、上传完是否关机）
- [ ] `HTC_TEST_MODE` 环境变量对产品代码的影响范围
- [ ] `main_app.cpp` 中的 `main_exit` 清理路径

---

## 8. 关键代码入口

| 文件 | 职责 |
|------|------|
| `src/app/main_app.cpp:1180-1225` | 工作模式 → command 映射 |
| `src/app/main_app.cpp:1360-1387` | CMD_SNAP 分支（RTC 正常） |
| `src/app/main_app.cpp:1479-1506` | CMD_SNAP 分支（RTC 异常） |
| `src/app/main_app.cpp:1569-1671` | CMD_MOBILE 分支 |
| `src/app/main_app.cpp:1673+` | CMD_RTSP_SERVER 分支 |
| `src/service/camera/impl/CameraServiceT32.cpp` | cameraMode 相关设置 |
| `src/media/video/VideoRecorder.cpp` | 录像/并发录像实现 |
