# 工作模式（`-wm`）能力清单与 SDK 盘点

> Phase A 产出（T8）。这是 `doc/design/workmode-sdk-architecture.md` 的配套**参考表**。
> 所有 `file:line` 引用均已在 `feature/new-workmode` 工作树核对（`main_app.cpp` 当前 1894 行）。

## 1. `-wm` 子模式清单

分发在 `main()` 的 `is_work_mode_cmd` 分支（`argc==5 && argv[3]=="-rtc"`）内：
解析于 `src/app/main_app.cpp:1163-1164`（`stoi_custom` 读 `working_mode` 与
`is_rtc_work_well`），格式 `htc_main_app -wm <mode> -rtc <0|1>`。每个模式映射到一个 `command`
位图（`main_app.cpp:1167-1191`）；位按顺序在 `:1310-1840` 执行。

启动序列（与模式无关、总是执行）：Settings load `:1217` → DeviceConfig/program-type
`:1219-1220` → SD mount `:1224`/`:1237` → DB init `:1003` → MediaScanner `:1022` →
EasyLogger `:1025-1054` → DayNight `:1059`。

| `workingMode` | 值 | `command` 位图 | 顺序执行步骤（file:line） |
|---|---|---|---|
| `WORKING_MODE_SNAP_ONLY` | 0 | `CMD_SNAP` | 启动序列 → **snap** 分支 `CMD_SNAP && is_rtc_work_well` `:1337` → `processCmdSnap`（`:555`）：搬移 quick-snap 文件、`createDescInfoFile`（`:609`）→ shutdown `:1857` |
| `WORKING_MODE_SNAP_UPLOAD` | 1 | `CMD_SNAP \| CMD_CONN_NET \| CMD_DHCP \| CMD_NTP \| CMD_UPLOAD` | 启动 → snap（`:1337-1358`）→ `CMD_CONN_NET` `Misc::connectWifi`（`:1375`）→ `CMD_DHCP` `Misc::startDHCP`（`:1403`）→ `CMD_NTP` `Misc::ntpSync` + 30s 等待（`:1418-1450`）→ `CMD_AUTH/UPLOAD` MgmtServClient connect+auth（`:1690-1714`）→ 上传循环 `:1716-1840` → shutdown |
| `WORKING_MODE_UPLOAD_ONLY` | 2 | `CMD_CONN_NET \| CMD_DHCP \| CMD_NTP \| CMD_UPLOAD`（+ led blink 60） | 同 SNAP_UPLOAD 减 snap 分支；RGB led `asyncBlink(60)`（`:1172`） |
| `WORKING_MODE_TEST_ONLY` | 3 | `CMD_MOBILE`（+ led blink 30） | **重叠** → 与 `-m`/`--mobile` 完全同路径：`CMD_MOBILE` 块 `:1546-1665`：DayNight 初始化、`connectWifi`/`startDHCP`（`:1574`/`:1578`）、mDNS `MdnsService::start`（`:1601`）、HTTP server（`:1607-1623`）、TcpEventService（`:1625`）、RTSP singleton（`:1631-1657`）、run-loop、teardown `:1653-1657` |
| `WORKING_MODE_UVC` | 4 | `CMD_CONN_NET \| CMD_DHCP \| CMD_RTSP_SERVER` | **重叠** → RTSP 部分与 `-rs`/`--rtsp-server` 完全相同：`CMD_RTSP_SERVER` 块 `:1668-1687`：`RtspServer::getInstance()->start`（`:1679`）、run-loop、`stop()`（`:1687`）。（CONN_NET/DHCP 位在此之前 `:1367-1407` 执行。） |

### 必须显式标注的重叠

- **`TEST_ONLY(3)` ≡ `-m`/`--mobile`。** 二者都设 `command = CMD_MOBILE`（`main_app.cpp:1181`
  vs `:1130`）。同一执行块、同一 `HTC_TEST_MODE=1` env、同一 mDNS+HTTP+TCP+RTSP 栈。抽取
  TEST_ONLY 等于抽取整个 mobile 栈——它们是**一个**能力簇，不是两个。
- **`UVC(4)` 的 RTSP 部分 ≡ `-rs`/`--rtsp-server`。** 二者都到达
  `RtspServer::getInstance()->start()`（`:1679` vs `-rs` 分发于 `:1145`）。UVC 额外先做
  CONN_NET+DHCP（`-rs` 单独不做）。RTSP-server 能力被 UVC 与独立 `-rs` user-mode 命令共享。
- **`SNAP_ONLY(0)` 共享 `processCmdSnap`，也被 `-s`/`--snap` 使用**——但注意 `-wm 0`/`-wm 1` 走
  `is_rtc_work_well` 守卫的 snap 分支（`:1337`），而 `-s` 走
  `CMD_SNAP && !is_rtc_work_well`（`:1456`）。两个 snap 分支对应两种 RTC 结果。

### 重要纠偏（修正一个种子假设）

`processCmdSnap`（`:555`）在 `-wm` 路径上**不**调 `ImageSnap::snap`。它**搬移**由
`htc_media_app -qs` 产出的预存 quick-snap 文件（`QUICK_SNAP_INFO_FILE`）。`processCmdSnap` 里
唯一的 `ImageSnap::snap` 调用在 `if (file_names.empty())` 的「only for test」块。同理
`processCmdVideoRecord`（`:626`）与 `processCmdConcurrentSnapRecord`（`:725`）直接用
`CameraRecorder`/`VideoRecorder`，但 `-wm` 路径**不**布线 DB 行或缩略图——manifest 来自
`generateDescInfo`，而非 `storage`。所以能力 3（拍照+缩略图）和 7（DB）今天与 work mode
**松耦合**；能力 8（JSON manifest）才是紧耦合的那个。

## 2. 逐能力 SDK 盘点（8 项）

格式：实现 → 库(target) → 当前公开 API → 调用方今天手工布线的东西 → **缺口**（要让 API 干净
必须搬出 `main_app.cpp` 的东西）。

### (1) Network / WiFi / DHCP connect

- **实现：** `src/common/misc/Misc.cpp`（connect+DHCP 在此；driver-reuse 逻辑
  `Misc::isWifiDriverLoaded/isWifiConnected`）。
- **库：** `common_misc`，**SHARED**（`src/common/misc/CMakeLists.txt:20`）。已抽到独立 app：
  `htc_wifi_app`。
- **公开 API：** `src/common/misc/Misc.h:31-32`——`static bool connectWifi(const std::string
  &ssid, const std::string &password)`；`static bool startDHCP(const std::string
  &ifname="")`。
- **调用方今天手工布线：** SSID/pwd 从 `DeviceConfig`（`INI_KEY_CSSID/CPWD`）查 `:1370-1373`
  及再次 `:1570-1573`；空检查 `:1568`；按 `program_type` 选接口名 `:1242-1249`；run-loop 无
  （fire-and-forget）。
- **缺口：** SSID/pwd/iface 解析 + `BUILD_FOR_SIMULATION` bypass 应移进一个小 `WifiConnector`
  helper（或 `app_workmode`），让 `-wm 1/2/3` 与 `-wm 4` 共享一个 connect 步骤，而不是两段
  复制（`:1367-1407` vs `:1568-1582`）。

### (2) NTP sync

- **实现：** `Misc::ntpSync` in `common_misc`。
- **库：** `common_misc` SHARED。
- **公开 API：** `Misc.h:36`——`static bool ntpSync(const std::string& ntp_server)`。
- **调用方今天手工布线：** server ip:port 从 `DeviceConfig`（`INI_KEY_NTP_IP/PORT`）拼装
  `:1410-1414`；**30s `tm_year > YEAR_MIN` 等待循环完全在 `main_app.cpp:1418-1450`**；RTC
  回写 `if (is_rtc_work_well) RTC::setTime`（`:1447` 之后）。
- **缺口：** 等到同步的循环 + RTC 回写是 app 内联，且与 RTC 流程概念重复。移进
  `common_misc`（如 `Misc::ntpSyncAndWait(server, timeout, rtc=nullptr)`），让 `-wm 1/2` 与
  `-n`/`-qs` 共享。

### (3) 拍照 + 可选缩略图

- **实现：** `src/media/snap/ImageSnap.cpp`。
- **库：** `media_snap`，**SHARED**（`src/media/snap/CMakeLists.txt:21`）；
  `PUBLIC hal_video ... storage common_utils_jpeg`。
- **公开 API：** `src/media/snap/ImageSnap.h:42-49`——`bool snap(const std::string&)`
  （+ vector/async 重载）；缩略图 via `getThumbnailData()`/`hasThumbnail()`，
  `THUMB_STREAM_ID 2`（`:16`）。
- **调用方今天手工布线：** `-wm` 路径**不**拍照——只搬移文件（`processCmdSnap :555`）。
  `ImageSnap` 仅用于测试 fallback（`:614-619`）。真正的 quick-snap 在 `htc_media_app`（`-qs`）。
- **缺口：** 对 `-wm` 抽取无阻塞。设计文档应注明：work mode 今天**不拥有**拍照（它在
  `media_app`）；把拍照搬进 workmode app 会改变既有 `media_app → main_app` 切分。Phase C 建议
  **拍照留在 `media_app`**，workmode app 只编排 post-capture（move + manifest + upload）。

### (4) 录影 + 可选缩略图

- **实现：** `src/media/video/VideoRecorder.cpp` + `src/service/camera/CameraRecorder.cpp`。
- **库：** `media_recorder` **SHARED**（`src/media/video/CMakeLists.txt:18`）+ `camera_service`
  STATIC（`src/service/camera/CMakeLists.txt:6`）。
- **公开 API：** `CameraRecorder`（`src/service/camera/CameraRecorder.h`）——async `record()` 带
  `RecordOptions{audio,autoCover}`；`processCmdVideoRecord :626` 内联构建 `RecordOptions`。
- **调用方今天手工布线：** `processCmdVideoRecord`（`:626`）构造 `CameraRecorder`，设
  `opts.audio/autoCover`、env 驱动 `HTC_RECORD_TMPFS` 路径（`:640-652`）与
  `HTC_RECORD_BITRATE_KBPS`（`:670`）；`processCmdConcurrentSnapRecord`（`:725`）内联构建
  `VideoParams/AudioParams`（`:735-756`）；`generateDescInfo` 在之后调（`:716`）。
- **缺口：** record-options 组装 + 诊断 env 钩子是 app 内联，且与 `media_app` 的 recorder 用法
  重复。把一个 `WorkModeRecorder` config builder 移进 `app_workmode`（不要进能力库——env 诊断
  是策略）。

### (5) mDNS discovery

- **实现：** `src/service/discovery/MdnsService.cpp`。
- **库：** `discovery_service`，**STATIC**（`src/service/discovery/CMakeLists.txt:6`）。
- **公开 API：** `src/service/discovery/MdnsService.h:27-32`——singleton `getInstance()`、
  `bool start(const MdnsServiceParams&)`、`void stop()`。**八项中耦合最低**。
- **调用方今天手工布线：** `buildMdnsParams(config, iface, ip, http_port, rtsp_port)`
  （`main_app.cpp:239`）；enable-check `isMdnsEnabled(config)`（`:1599`）；端口查找
  `getConfiguredPort(...)` 对 `INI_KEY_MDNS_CTRL_PORT/MDNS_RTSP_PORT`。
- **缺口：** `buildMdnsParams` + 端口解析是 app 内联。移进 `discovery_service`（或
  `app_workmode`），让 `CMD_MOBILE`（`:1601`）与任何未来 mobile-like 模式共享。缺口小——这个
  能力已经干净。

### (6) RTSP server

- **实现：** `src/media/rtsp/RtspServer.cpp`（+ `MediaSession`）。
- **库：** `media_rtsp`，**SHARED**（`src/media/rtsp/CMakeLists.txt:33`）。
- **公开 API：** `src/media/rtsp/RtspServer.h:25-43`——singleton `getInstance()`、
  `static registerOnsessionClosedCallback`、`bool start()/stop()`，以及**进程生命期
  `void shutdown()`**（`:43`）——它释放 HAL。
- **调用方今天手工布线：** 端口从 `DeviceConfig`（`:1629/1674`）；`rtsp_singleton_used = true`
  守卫（`:1630/1670`）使 `:1857` 的 shutdown 路径只在实际用过 singleton 时触碰它；
  `registerOnsessionClosedCallback` lambda 在 `:1631` 与 `:1675` 重复；run-loop
  `while(!already_in_exit_flow)`；teardown 顺序在 `main_exit`（`:1857`）。
- **缺口：** `rtsp_singleton_used` 守卫 + 重复的 start/run/stop 序列（mobile `:1631-1657`
  vs rtsp-only `:1675-1687`）是核心复制。Phase B 应抽一个 `startRtspUntilSignal(port)`
  helper。**约束：** `shutdown()` 是进程终止（HAL teardown）——它必须留在 app 的 exit path，
  不能移进长生命期库调用。Caps 3/4/6 对 HAL 透明（PIC 拥有的 `src/hal/**`）。

### (7) SQLite DB 文件信息

- **实现：** `src/storage/{DatabaseManager,MetadataDao,MediaScanner}.cpp`。
- **库：** `storage`，**STATIC**（核实：`build/src/storage/libstorage.a`；
  `src/storage/CMakeLists.txt:1` 无 SHARED 关键字、项目无 `BUILD_SHARED_LIBS`；
  `POSITION_INDEPENDENT_CODE ON`）。
- **公开 API：** `DatabaseManager::getInstance().init(db_path)`；`MetadataDao`；
  `MediaScanner::getInstance().startScan(opts)`（`src/storage/MediaScanner.h:25-32`）。
- **调用方今天手工布线：** DB init（`main_app.cpp:1003`）；`MediaScannerOptions` 构建 +
  `startScan`（`:1022`）；`-wm` 每模式路径上无任何写 DB 行（manifest 是 JSON，不是 DB）。
- **缺口：** 对 `-wm` 无。最干净的能力。Phase B/C 应让 DB 由「先启动的 app」拥有（当前
  `main_app`）；未来拆分可能需要两个 app 共享同一 DB 文件路径。

### (8) JSON manifest 生成 + server 上传

- **实现（分裂）：**
  - **Transport**（干净）：`src/network/{MgmtServClient,StorageServClient,Client}.cpp`，库
    `network` **SHARED**（`src/network/CMakeLists.txt:3`）。API：
    `MgmtServClient::connect/authenticate/newStorageServClient`、
    `StorageServClient::uploadFile/bindUploadCallback/isUploadFinished`（`src/network/*.h`）。
    `network` 是**依赖汇聚点**——它链 `setting env disk mcu common_time_rtc
    common_time_timezone power jsoncpp md5 ... common_misc common_utils_serial`
    （`src/network/CMakeLists.txt:37`）。
  - **Content**（脏）：`generateDescInfo`（`main_app.cpp:268`）与 `createDescInfoFile`
    （`:453`）是 `main_app.cpp` 内 `static`，熔合在 `Settings::getInstance()`、
    `MCU::getInstance()`、`DeviceConfig::getInstance()`、`Disk`、`CRC`、`Timezone`、
    `Misc::getIPAddress/getFilepath/getFilename` 上。
- **调用方今天手工布线：** 整个上传编排——`MgmtServClient` connect+auth
  （`:1690-1714`）、8s desc-file 等待循环（`:1751-1768`）、per-file 上传循环 +
  `F_UploadedTag` 回写（`:1769-1840`）、`FILE_MANAGE_DELETE` 策略（`:1817`）——是一个约
  150 行的 app 内联块。
- **缺口（最大）：** `generateDescInfo`/`createDescInfoFile` 必须变成一个**新能力库**
  （暂名 `manifest`，或折进 `storage`/`network`）。在它们搬走之前，任何 workmode app 都仍要
  拖入三个 singleton + `Disk`/`CRC`。**这是最高价值的 Phase B 抽取目标。** 上传循环编排
  （auth → desc-file → per-file → tag 回写）应成为 `network` 或 `app_workmode` 里的
  `UploadSession`。

## 3. 最大缺口（callout）

`generateDescInfo`（`src/app/main_app.cpp:268`）与 `createDescInfoFile`（`:453`）是八项能力中
**唯一没有独立库、且熔在 singleton 上**的能力。它是 `-wm 0/1/2` 路径的必经点（snap 与 record
之后都调它产 manifest），却以 `static` 函数形式住在 `main_app.cpp` 里，直接读
`Settings`/`MCU`/`DeviceConfig` 三个进程全局。只要它不动，任何 workmode app 都无法摆脱这三个
singleton。**这是最高价值的 Phase B 抽取目标。** 详见架构文档 §4 的 B1 步。

## 4. 依赖汇聚点注记（`network`）

`network`（`src/network/CMakeLists.txt`）是整个能力层的依赖汇聚点——`target_link_libraries`
（`:37`）拉入 `setting env disk mcu common_time_rtc common_time_timezone power jsoncpp md5
pthread common_utils_base64 common_misc common_utils_serial`。设计含义：

- 任何新编排库一旦需要上传，链 `network` 就传递性拉进大半个 config/硬件 helper 树——编排层必须
  保持薄（见架构文档 §5 约束 4）。
- `network` 永远停在能力层，不允许被 HAL 或 app 层直接绕过。
- B1 抽取的 `manifest` 库若需要 `Disk`/`CRC`，应直接依赖它们，而不是通过 `network` 传递引入，
  以避免在 `manifest` 上堆积无关依赖。
