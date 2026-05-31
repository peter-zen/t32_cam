# htc_media_app 启动流程与模式判断

> 本文档聚焦 `src/app/media_app.cpp` 的启动流程，记录上电后第一个用户程序（`htc_media_app`）如何检测工作模式、执行快速拍照、并拉起 `htc_main_app`。

---

## 1. 程序定位

| 项目 | 值 |
|------|-----|
| 可执行文件 | `bin/htc_media_app` |
| 源码 | `src/app/media_app.cpp` |
| 构建目标 | `htc_media_app`（`src/app/CMakeLists.txt:52`） |
| 启动顺序 | **上电后第一个执行的用户应用**，负责模式检测和前置拍照，最终拉起 `htc_main_app` |

---

## 2. 启动流程总览

```
上电
  │
  ▼
htc_media_app 入口 (main)
  │
  ├── 1. 记录启动时间戳
  │
  ├── 2. 设置 Power Hold GPIO（保持供电）
  │
  ├── 3. 加载环境配置 (ENV_FILE_PATHNAME)
  │     ├── 设置时区
  │     └── 加载用户设置 (settings.json)
  │         └── 若 force_upload == 1：强制设为 UPLOAD_ONLY 模式，写回配置，跳过拍照
  │
  ├── 4. 检测 RTC 是否正常
  │
  ├── 5. 【核心】获取工作模式 (WorkMode::getWorkingMode())
  │     ├── 非 MCU 场景：读 GPIO 模式脚 PC(9) / PC(8)
  │     └── MCU 场景：调用 MCU::readWorkingMode()
  │
  ├── 6. 【核心】模式判断与执行
  │     ├── SNAP_ONLY / SNAP_UPLOAD：执行 quick_snap()
  │     │     └── 若 cameraMode == 2 (video only)：跳过拍照
  │     ├── 其他模式：跳过拍照
  │     └── UVC_ENABLE 宏开启时：强制设为 UVC 模式
  │
  ├── 7. 启动守护进程 htc_daemon_app（若 DAEMON_ENABLE）
  │
  └── 8. 拉起 htc_main_app，传入工作模式参数
        └── 命令格式：htc_main_app -wm <mode> -rtc <rtc_status>
```

---

## 3. 模式检测机制

### 3.1 检测入口

```cpp
// src/app/media_app.cpp:235
working_mode = WorkMode::getWorkingMode();
```

### 3.2 非 MCU 场景（当前真机使用）

`WorkMode::getWorkingMode()` 读取两根 GPIO 引脚：

| GPIO | 引脚定义 |
|------|---------|
| `mode_pin_0` | `PC(9)`（`WORKING_MODE_CHECK_PIN_0`）|
| `mode_pin_1` | `PC(8)`（`WORKING_MODE_CHECK_PIN_1`）|

电平组合映射：

| `PC(9)` | `PC(8)` | 工作模式 | 枚举值 |
|---------|---------|---------|--------|
| LOW | LOW | **仅拍照** | `WORKING_MODE_SNAP_ONLY = 0` |
| LOW | HIGH | **拍照+上传** | `WORKING_MODE_SNAP_UPLOAD = 1` |
| HIGH | LOW | **仅上传** | `WORKING_MODE_UPLOAD_ONLY = 2` |
| HIGH | HIGH | **测试/手机配对** | `WORKING_MODE_TEST_ONLY = 3` |

### 3.3 MCU 场景

`MCU::readWorkingMode()` 返回值映射：

| MCU 返回值 | 工作模式 |
|-----------|---------|
| `-1` | `WORKING_MODE_SNAP_UPLOAD` |
| `0` | `WORKING_MODE_SNAP_ONLY` |
| `1` | `WORKING_MODE_SNAP_UPLOAD` |
| `2` | `WORKING_MODE_UPLOAD_ONLY` |
| `3` | `WORKING_MODE_TEST_ONLY` |
| 其他 | `WORKING_MODE_MAX`（非法）|

### 3.4 缓存行为

`WorkMode` 内部有缓存：
- `already_get_mode`：第一次读取后设为 `true`
- `working_mode`：缓存结果

因此同一次上电周期内重复调用 `getWorkingMode()` 会返回缓存值，不会重新读取 GPIO。

---

## 4. 模式判断与 quick_snap 执行

### 4.1 判断逻辑

```cpp
// src/app/media_app.cpp:237-247
if (working_mode == WORKING_MODE_SNAP_ONLY || working_mode == WORKING_MODE_SNAP_UPLOAD) {
    uint8_t camMode = Settings::getInstance()->cameraMode;
    if (camMode == 2) {
        // video only 模式跳过拍照
        Logger::log(LogLevel::INFO, "Work Mode: cameraMode=%d (video only), skip quick_snap", camMode);
    } else {
        if (quick_snap(rtc_work_well) < 0) {
            Logger::log(LogLevel::ERROR, "Failed to quick snap");
            working_mode = WORKING_MODE_MAX;  // 拍照失败，模式设为非法
        }
    }
}
```

### 4.2 quick_snap 流程

`quick_snap(bool is_rtc_work_well)` 执行步骤：

1. **创建目录** — `QUICK_SNAP_DIR`（默认 `/mnt/sdcard/DCIM/`）
2. **生成子目录名**
   - RTC 正常：`YYYYMMDD_HHMMSS/`
   - RTC 异常：`pic/`
3. **确定连拍数量** — 读取 `Settings::burstNumber`
4. **生成文件名** — 根据 burstNumber 生成 `name_1.JPG` ~ `name_N.JPG`
5. **确定分辨率** — 读取 `Settings::stillSize`，默认 4M（2560x1440）
6. **执行拍照** — 调用 `ImageSnap::snap(fileNames)`
7. **写 info.json** — 记录照片文件名和目录信息到 `QUICK_SNAP_DIR/info.json`

### 4.3 特殊覆盖逻辑

#### force_upload 强制上传模式

```cpp
// src/app/media_app.cpp:218-223
if (Settings::getInstance()->force_upload == 1) {
    working_mode = WORKING_MODE_UPLOAD_ONLY;
    Settings::getInstance()->force_upload = 0;
    Settings::getInstance()->saveToJsonFile(setting_file_path);
    goto main_exit;  // 跳过所有拍照逻辑
}
```

用途：用户通过设置强制进入上传模式，本次上电不拍照只传已有照片。

#### UVC 宏覆盖

```cpp
// src/app/media_app.cpp:248-250
#if UVC_ENABLE
    working_mode = WORKING_MODE_UVC;
#endif
```

若编译时开启 `UVC_ENABLE`，强制覆盖为 UVC 模式。

---

## 5. 拉起 htc_main_app

### 5.1 最终命令格式

```cpp
// src/app/media_app.cpp:257
std::string command = "htc_main_app -wm " + to_string_custom((int)working_mode)
                      + " -rtc " + to_string_custom(rtc_work_well);
startApp(command);
```

参数说明：

| 参数 | 值 | 含义 |
|------|-----|------|
| `-wm` | 0~4 | 工作模式 |
| `-rtc` | 0/1 | RTC 是否正常 |

### 5.2 同时启动的辅助进程

```cpp
// src/app/media_app.cpp:253-255
#if DAEMON_ENABLE
    startApp("htc_daemon_app &");
#endif
```

若 `DAEMON_ENABLE` 开启，在拉起 `htc_main_app` 前会先启动 `htc_daemon_app`。

---

## 6. 与 htc_main_app 的模式映射关系

`htc_media_app` 只负责**检测模式 + 前置拍照**，真正的模式执行在 `htc_main_app` 中：

| `htc_media_app` 检测到的模式 | `htc_main_app` 接收的参数 | `htc_main_app` 执行的命令组合 |
|---------------------------|------------------------|---------------------------|
| `SNAP_ONLY` (0) | `-wm 0` | `CMD_SNAP`（拍照后关机）|
| `SNAP_UPLOAD` (1) | `-wm 1` | `CMD_SNAP \| CMD_CONN_NET \| CMD_DHCP \| CMD_NTP \| CMD_UPLOAD` |
| `UPLOAD_ONLY` (2) | `-wm 2` | `CMD_CONN_NET \| CMD_DHCP \| CMD_NTP \| CMD_UPLOAD` |
| `TEST_ONLY` (3) | `-wm 3` | `CMD_MOBILE`（HTTP/RTSP/mDNS 服务）|
| `UVC` (4) | `-wm 4` | `CMD_CONN_NET \| CMD_DHCP \| CMD_RTSP_SERVER` |

---

## 7. 关键代码文件

| 文件 | 职责 |
|------|------|
| `src/app/media_app.cpp` | 启动入口、模式判断、quick_snap、拉起 main_app |
| `src/app/workmode/WorkMode.cpp` | GPIO/MCU 模式检测实现 |
| `src/app/workmode/WorkMode.h` | 工作模式枚举定义 |
| `src/common/Common.h` | `WORKING_MODE_CHECK_PIN_0/1` 引脚定义 |
| `src/app/main_app.cpp` | 主程序入口，接收 `-wm` 参数执行对应模式 |

---

## 8. 调试与验证

### 查看当前模式检测日志

T32 设备串口日志中搜索：
```
getWorkingMode    // 模式检测结果
quick_snap        // 快速拍照执行
htc_main_app -wm  // 拉起主程序的命令
```

### 手动测试模式检测

在 T32 shell 中：
```bash
# 直接运行 media_app 看模式检测和拍照行为
htc_media_app

# 或手动指定工作模式运行 main_app
htc_main_app -wm 0 -rtc 1   # SNAP_ONLY
htc_main_app -wm 3 -rtc 1   # TEST_ONLY (CMD_MOBILE)
```

### 查看拍照输出

```bash
cat /mnt/sdcard/DCIM/info.json   # 查看 quick_snap 生成的照片列表
ls /mnt/sdcard/DCIM/             # 查看照片目录
```
