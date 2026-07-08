# um / wm 应用身份与文件根目录体系调研

> 调研主题：t32_cam 项目里 `um` 和 `wm` 两个应用程序的进程身份、职责，以及文件根目录 / 挂载点 / DCIM 路径体系。
> 广度：medium。只读调研，所有结论带 `file:line` 引用。

---

## 1. um / wm 的进程身份与职责

### 1.1 binary 清单（src/app/CMakeLists.txt:65-73）

| binary | 源文件 | 语义 |
|--------|--------|------|
| `htc_main_app` | `main_app.cpp` | 全功能多命令入口（`-wm`/`-s`/`-m`/`-rs`/...），legacy |
| `htc_media_app` | `media_app.cpp` | 启动器：读工作模式引脚 → quick_snap → **spawn `htc_main_app -wm`** |
| `htc_daemon_app` | `daemon_app.cpp` | 守护进程 |
| `htc_net_app` | `net_app.cpp` | WiFi / USB-dongle 冷启动联网工具 |
| `htc_workmode_app` | `workmode_app.cpp` | `htc_main_app -wm` 的等价 thin shell（重组产物，注释明说 `UNSPAWNED in C3`） |
| **`wm`** | `wm_app.cpp` | **新独立 binary**，一次性任务程序（录影/拍照/上传） |
| **`um`** | `um_app.cpp` | **新独立 binary**，长驻交互服务程序（RTSP/HTTP/mDNS） |
| `snap_test` / `time_test` | 测试 harness | 非生产 |

### 1.2 wm 的职责（src/app/wm_app.cpp:1-15 头注释 + :122-340 main）

- **身份**：一次性任务程序（one-shot worker）。3 模式由 `-m <0|1|2>` 选（wm_app.cpp:197-214）：
  - `m0` CaptureOnly — 仅拍照/录影（离线，netif 选 `CMD_SNAP`）
  - `m1` CaptureUpload — 拍照 + 上传（需网络）
  - `m2` UploadOnly — 仅上传（扫 SD 上的 desc JSON）
- **架构**：`ProcessLifecycle` 三阶段启动 → `WmScheduler.run()`（wm_app.cpp:305-307）。
  内部 `UploadTask` 自扫 SD upload 目录取 desc 上传；capture lane 经 `SlotOutputPort` 唤醒上传。
- **关机**：idle-grace（默认 2s，wm_app.cpp:286）或上传 timeout → `requestShutdown` → `Misc::poweroff`（wm_app.cpp:335）。
- **注释明说**丢弃 crash 不收敛的 `htc_workmode_app`（wm_app.cpp:3-4）。
- IMP 懒初始化（`sharedVideo` 单例），进程内永不 `IMP_System_Exit`（wm_app.cpp:10）。

### 1.3 um 的职责（src/app/um_app.cpp:1-17 头注释 + :104-383 main）

- **身份**：长驻交互服务程序（server-lifecycle）。与 wm（一次性任务）对应。
- **架构** = server-lifecycle：bring-up → idle-wait loop → teardown（**非** wm 的 task-scheduler，um_app.cpp:2-4）。
- **bring-up**（um_app.cpp:227-332，照搬 WorkModeRunner CMD_MOBILE :580-700）：
  1. day/night one-shot
  2. 端口 + WiFi + interface + IP
  3. mDNS（`--no-mdns` 跳过）
  4. HTTP server（`--no-http` 跳过）
  5. TcpEvent（非致命）
  6. RTSP server（`--no-rtsp` 跳过）— `start()` 懒初始化整个 IMP 栈
- **关机** = idle-timeout 自关机：无 RTSP 客户端 + 无 HTTP 请求持续 T（默认 5min，um_app.cpp:176-179）→ `requestShutdown`。
  RTSP play / HTTP request 续命（um_app.cpp:316-347）。
- CLI 无模式，只 `--no-*` / `--force-day` 开关（um_app.cpp:80-100）。

### 1.4 binary 间关系（谁 fork 谁、谁包含谁）

- **`htc_media_app` spawn `htc_main_app`**：`media_app.cpp:243-245`
  ```cpp
  // call htc_main_app
  std::string command = "htc_main_app -wm " + ... ;
  startApp(command);
  ```
  即 `htc_media_app` 是启动器，通过 shell 命令拉起 `htc_main_app -wm <mode>`。
- **`htc_workmode_app`** 是 `htc_main_app -wm` 的 thin 等价 shell（workmode_app.cpp:1-9），**未被 spawn**（注释 `UNSPAWNED in C3: media_app still spawns htc_main_app -wm until C4/T17 repoints it`）。
- **`wm` / `um` 是全新独立 binary**，**不被** `htc_media_app` spawn。它们各自独立 boot（独立 `ProcessLifecycle` 三阶段 + 自跑时间链），与 `htc_main_app` 是平级替代关系，不是父子。
  - 证据：wm_app.cpp:122 `int main(...)` 自带完整 main；um_app.cpp:104 `int main(...)` 自带完整 main；CMakeLists.txt:70-71 各自 `add_executable`。
- **fork 关系总结**：只有 `htc_media_app → htc_main_app` 一条 spawn 边（shell `startApp`）。`wm`/`um`/`htc_workmode_app` 都是被设计成直接 boot 的独立入口，目前不互相 spawn。

---

## 2. 文件根目录 / 挂载点 的确定方式

### 2.1 SD 卡挂载点 `/mnt/sdcard`

- **硬编码** 在 `app.h:20`：
  ```cpp
  // src/app/app.h:20
  #define SD_CARD_PATH   "/mnt/sdcard/"
  ```
- **实际 mount** 发生在 `ProcessLifecycle::commonStartupPostDispatch` S11 步（HW 路径）：
  - `ProcessLifecycle.cpp:475`：`Misc::mountSDCard(SD_CARD_PATH);`
  - `Misc::mountSDCard` 实现（`Misc.cpp:631-663`）：HW 路径 `mount /dev/mmcblk0p1 <target>`；SIM 路径只 `createDirectory`（不真 mount）。
- **disk 信息查询** 也硬编码 `/mnt/sdcard`：
  - `StorageServiceT32.cpp:12`：`statvfs("/mnt/sdcard", &stat)`
  - `CameraRecorder.cpp:112,160`：`statvfs("/mnt/sdcard", ...)`
  - `Common.h:83`：`#define DISK_PATHNAME "/mnt/sdcard/"`（被 `RemoteCtrlClient.cpp:836`、`MgmtServClient.cpp:843`、`Manifest.cpp:137` 用）

### 2.2 NFS 路径 `/mnt/huntcam`

- **NFS 服务端**由 build host 导出 repo 的 `build/`，T32 设备 mount 到 `/mnt/huntcam`（见 `.claude/CLAUDE.md` 里的部署章节）。代码里 **不负责 mount NFS**，只在路径常量里 **引用** `/mnt/huntcam`：
  - `wm_paths.h:8-11`：wm 的 DB / media / upload 根都在 `/mnt/huntcam/...`
  - `wm_app.cpp:152`：`cfg.logRoot = "/mnt/huntcam/logs"`
  - `um_app.cpp:131`：`cfg.logRoot = "/mnt/huntcam/logs"`
  - `wm_app.cpp:108`：`const std::string huntcamConfig = "/mnt/huntcam/config.ini"`（wm 的 PID-based config 切换 fallback）
- 即 wm / um 把**日志和 media 产物落在 NFS 共享区** `/mnt/huntcam/`（方便 devtest 从 host 直接读），而 legacy `htc_main_app`/`htc_workmode_app` 落在 `/mnt/sdcard/`。

### 2.3 配置文件路径（env-driven）

- **env.ini**（HW：`/config/htc/env.ini`，由 `app.h:21` `ENV_FILE_PATHNAME` 定义；SIM：`./res/env.ini`，app.h:15）由 `EnvManager::parsePrimaryEnv` 解析（EnvManager.cpp:36-72），只读 `[ENV]` section。
- `res/env.ini:1-6` 实际内容：
  ```ini
  [ENV]
  CONFIG_FILE=/config/htc/config.ini
  BROADCAST_FILELIST_PATHNAME=/mnt/sdcard/media/audio/AUDIO_PLAY_LIST.txt
  BROADCAST_FILE_PATH=/mnt/sdcard/media/audio/
  ISP_FILE_PATH=/mnt/sdcard/media/audio/
  SETTING_FILE_PATH=/config/htc/setting.json
  ```
- `DB_PATH` **未在 env.ini 里定义**，代码用 `getEnv("DB_PATH", "<默认>")` 带默认值读取：
  - main_app / workmode_app / um：默认 `/mnt/sdcard/data/db`（main_app.cpp:176、workmode_app.cpp:94、um_app.cpp:129）
  - wm：默认 `app_workmode::wmDbPath()` = `/mnt/huntcam/data/db`（wm_app.cpp:150、wm_paths.h:8,26-32）
- `CONFIG_FILE` / `SETTING_FILE_PATH` 是 env-driven；wm 启动时有 **PID-based config 切换** 逻辑（wm_app.cpp:96-117 `selectWmHardwareConfig`）：若当前 CONFIG_FILE 的 PID 是 test PID 且 `/mnt/huntcam/config.ini` 的 PID 非 test，则切到后者。
- SIM 路径（`ProcessLifecycle.cpp:275-282`）：`SIM_SD_ROOT` env → `CONFIG_FILE`/`SETTING_FILE_PATH`/`BROADCAST_*`/`ISP_FILE_PATH` 默认值。

### 2.4 DCIM 根目录

- **`/mnt/sdcard/DCIM`**：main_app / workmode_app / um 硬编码（main_app.cpp:177、workmode_app.cpp:95、um_app.cpp:130）。
- **`/mnt/huntcam/media`**：wm 用 `wmMediaRoot()`（wm_paths.h:9,34-40），**不是** DCIM 命名。
- CameraService（HTTP snap/record 的实际执行者，um 通过 HTTP 触发）**硬编码** `/mnt/sdcard/DCIM/IMG_*.jpg` / `VID_*.mp4`（CameraServiceT32.cpp:194, 430）。
- HTTP server 从 DB 路径反推 media root：`http_api_v1.cpp:243-247, 365` 用 `get_media_root_from_database_path` = `<sd_root>/DCIM`。

---

## 3. 路径常量 / 宏 / 函数 全量清单（原样）

### 3.1 `src/app/workmode/wm_paths.h`（wm 专用，整个文件）

```cpp
// wm_paths.h:6-50
namespace app_workmode {

static const char kWmDbPath[] = "/mnt/huntcam/data/db";        // :8
static const char kWmMediaRoot[] = "/mnt/huntcam/media";       // :9
static const char kWmMediaPath[] = "/mnt/huntcam/media/";      // :10
static const char kWmUploadPath[] = "/mnt/huntcam/media/upload/"; // :11

inline std::string trimTrailingSlash(const std::string& path);  // :13-18
inline std::string wmSimRoot();        // :20-24  env SIM_SD_ROOT | "./sim_sdcard_runtime"
inline std::string wmDbPath();         // :26-32  sim: wmSimRoot()+"/data/db" | HW: kWmDbPath
inline std::string wmMediaRoot();      // :34-40  sim: wmSimRoot()+"/media"   | HW: kWmMediaRoot
inline std::string wmMediaPath();      // :42-44  wmMediaRoot()+"/"
inline std::string wmUploadPath();     // :46-48  wmMediaRoot()+"/upload/"

}  // namespace app_workmode
```

### 3.2 `src/app/app.h`（全 app 共享，整个文件）

```cpp
// app.h:9-34
#define QUICK_SNAP_DIR   "/tmp/quick_snap/"                       // :9
#define QUICK_SNAP_INFO_FILE   QUICK_SNAP_DIR"info.json"          // :10

#ifdef BUILD_FOR_SIMULATION
#define SD_CARD_PATH   "./sim_sdcard_runtime/"                    // :14
#define ENV_FILE_PATHNAME "./res/env.ini"                         // :15
#define CONFIG_FILE_PATHNAME "./res/config.sim.ini"               // :16
#define NETIF_NAME "eth0"                                         // :17
#else
#define SD_CARD_PATH   "/mnt/sdcard/"                             // :20
#define ENV_FILE_PATHNAME "/config/htc/env.ini"                   // :21
#define CONFIG_FILE_PATHNAME "/config/htc/config.ini"             // :22
#define NETIF_NAME "wlan0"                                        // :23
#endif

#define MEDIA_TARGET_PATH   SD_CARD_PATH"media/"                  // :26
#define MEDIA_UPLOAD_PATH   SD_CARD_PATH"media/upload/"           // :27
#if ALL_MEDIA_FILE_IN_ONE_FOLDER
#define MEDIA_STORE_FOLDER_PATH   MEDIA_TARGET_PATH"file/"        // :29
#endif
#define WIFI_IFNAME "wlan0"                                       // :31
#define ETH_IFNAME "eth0"                                         // :32
#define USB_DONGLE_IFNAME "usb0"                                  // :33
#define UPDATE_CONFIG_FILE_PATHNAME SD_CARD_PATH"update_config.ini" // :34
```

### 3.3 `src/common/Common.h` 里路径相关（非全部，只列路径项）

```cpp
// Common.h:56
#define INI_CFG_FILENAME "B:/UDF/OPTIONS.CFG"

// Common.h:83
#define DISK_PATHNAME "/mnt/sdcard/"
```
（Common.h 其余是 INI section/key 名、pin 定义、enum，非路径。）

### 3.4 CameraServiceT32 里硬编码的路径（um HTTP 触发 snap/record 的落点）

```cpp
// CameraServiceT32.cpp:194
oss << "/mnt/sdcard/DCIM/IMG_" << ... << ".jpg";
// CameraServiceT32.cpp:395
const std::string base_dir = "/mnt/sdcard/.preview/";
// CameraServiceT32.cpp:430
oss << "/mnt/sdcard/DCIM/VID_" << ... << ".mp4";
// CameraServiceT32.cpp:542
return "/mnt/sdcard/data/db/media_file.db";
// CameraServiceT32.cpp:546
return "/mnt/sdcard/data/db/media_thumb.db";
```

### 3.5 `StartupConfig` 路径字段（ProcessLifecycle.h:19-36）

```cpp
struct StartupConfig {
    std::string simRootPath;          // sim: <root>, HW: empty
    std::string projectRootPath;      // sim: <root>, HW: empty
    std::string dbPath;               // simRootPath+"/data/db" | "/mnt/sdcard/data/db"
    std::string mediaRoot;            // simRootPath+"/DCIM"     | "/mnt/sdcard/DCIM"
    std::string logRoot;              // simRootPath+"/logs"     | "/mnt/sdcard/logs"
    std::string logFile;              // logRoot + "/app.log"
    bool isSimulation = false;
    // skip flags ...
};
```
注：字段注释（:25-27）写的是 legacy 默认值；wm 实际传入的是 `/mnt/huntcam/...`（见 wm_app.cpp:148-153），与注释不符（注释只反映 main_app/workmode_app/um 路径）。

---

## 4. um 与 wm 是否共享目录路径体系？

**不共享。两套独立体系，且都没统一的 PathManager / StorageService。**

### 4.1 wm 用 `wm_paths.h`（app_workmode 命名空间）

- DB `/mnt/huntcam/data/db`、media `/mnt/huntcam/media/`、upload `/mnt/huntcam/media/upload/`、log `/mnt/huntcam/logs`
- 消费者：record_task.cpp:68-69/137、snap_task.cpp:103-104、wm_scheduler.cpp:208、upload_task（uploadDir 注入）、wm_app.cpp:150-152,187
- SIM 下全部回到 `wmSimRoot()`（env `SIM_SD_ROOT` | `./sim_sdcard_runtime`）下的 `media/`、`data/db`（wm_paths.h:26-48）。

### 4.2 um 沿用 legacy `/mnt/sdcard` 体系（main_app 同款）

- DB `getEnv("DB_PATH","/mnt/sdcard/data/db")`、media `/mnt/sdcard/DCIM`（um_app.cpp:129-130）
- 唯一例外：**log 落 `/mnt/huntcam/logs`**（um_app.cpp:131，注释 `NFS-shared，devtest 可读（同 wm）`）—— 仅日志走 NFS，media/DB 仍在 SD。
- HTTP snap/record 的实际落点（CameraServiceT32）**硬编码** `/mnt/sdcard/DCIM/IMG_*.jpg` / `VID_*.mp4`（CameraServiceT32.cpp:194,430），与 um 的 `cfg.mediaRoot` 一致但**不经 cfg**，是直接字面量。
- DB 路径 `getMediaDatabasePath()` / `getThumbnailDatabasePath()` 也硬编码 `/mnt/sdcard/data/db/media_file.db` / `media_thumb.db`（CameraServiceT32.cpp:542,546）。

### 4.3 有无统一 PathManager / StorageService？

- **无统一 PathManager**。路径来源分散在：
  - `app.h` 宏（`SD_CARD_PATH` / `MEDIA_TARGET_PATH` / `MEDIA_UPLOAD_PATH`）—— legacy main_app/workmode_app + WorkModeRunner cascade 用
  - `wm_paths.h` inline 函数 —— wm 专用
  - `Common.h` 宏（`DISK_PATHNAME`）—— disk 查询用
  - `StartupConfig` 字段 —— 各 app main 里手工拼，再传给 `ProcessLifecycle`
  - `CameraServiceT32` 里硬编码字面量 —— HTTP 触发的 snap/record
- **`src/service/storage/`** 不是 PathManager：
  - `IStorageService.h:8-13` 只声明 `getStorageInfo()` + `formatStorage()`（容量查询 + 格式化），**不提供路径**。
  - `StorageServiceT32.cpp:12` 自己也硬编码 `/mnt/sdcard` 查 statvfs。
- 结论：**路径体系是分散硬编码 + env.ini 部分参数化，无集中管理层**。wm/um 各自独立，legacy 与新 binary 之间也不统一。

---

## 5. SD 卡整体目录树（从代码常量推断）

### 5.1 `/mnt/sdcard/`（SD 卡本体，legacy + um + CameraService 落点）

```
/mnt/sdcard/                          # SD_CARD_PATH (app.h:20), mount /dev/mmcblk0p1 (Misc.cpp:654)
├── DCIM/                             # mediaRoot (main_app/um hardcode; CameraServiceT32:194,430)
│   ├── IMG_YYYYMMDD_HHMMSS.jpg       # HTTP snap 产物 (CameraServiceT32.cpp:194)
│   ├── VID_YYYYMMDD_HHMMSS.mp4       # HTTP record 产物 (CameraServiceT32.cpp:430)
│   ├── harness_photo.jpg             # singleton_harness 测试 (singleton_harness.cpp:87)
│   ├── harness.mp4                   # singleton_harness 测试 (singleton_harness.cpp:178)
│   └── thumb/                        # 录影缩略图（CameraRecorder.cpp:41-51 computeThumbnailPath 规则：
│       └── <stem>.jpg                #   <video dir>/thumb/<basename>.jpg，即 DCIM/thumb/）
├── data/
│   └── db/                           # dbPath 默认 (main_app:176, um:129, CameraServiceT32:542,546)
│       ├── media_file.db             # 媒体元数据库 (CameraServiceT32.cpp:542)
│       ├── media_thumb.db            # 缩略图 BLOB 库 (CameraServiceT32.cpp:546)
│       └── thumb_pending/            # MediaScanner pending-thumbnail 队列 (ProcessLifecycle.cpp:308-312)
├── logs/
│   └── app.log                       # logRoot/logFile (main_app:178-179, workmode_app:96-97)
│   └── hal_trace.log                 # HAL trace (IngenicVideo.cpp:60)
├── media/                            # MEDIA_TARGET_PATH (app.h:26) — legacy upload lane
│   ├── upload/                       # MEDIA_UPLOAD_PATH (app.h:27) — desc JSON + 上传
│   │   └── <ts>.json                 # WorkModeRunner.cpp:275,348,745-747
│   ├── file/                         # MEDIA_STORE_FOLDER_PATH (app.h:29, ALL_MEDIA_FILE_IN_ONE_FOLDER)
│   └── audio/                        # BROADCAST_FILE_PATH (env.ini:4) — 语音播报音频
│       ├── AUDIO_PLAY_LIST.txt       # BROADCAST_FILELIST_PATHNAME (env.ini:3)
│       └── <isp_files>               # ISP_FILE_PATH 也指到此 (env.ini:5)
├── .preview/                         # CameraServiceT32.cpp:395 — HTTP 预览临时图
├── .snap_tmp/                        # ImageSnap.cpp:480 — snap 临时目录
├── update_config.ini                 # UPDATE_CONFIG_FILE_PATHNAME (app.h:34) — OTA 配置触发
├── config.ini                        # (非 SD；/config/htc/config.ini env.ini:2，但 wm 的 PID 切换会读 /mnt/huntcam/config.ini)
└── setting.json                      # (非 SD；/config/htc/setting.json env.ini:6)
```

### 5.2 `/mnt/huntcam/`（NFS 共享区，wm 专用 + wm/um 日志）

```
/mnt/huntcam/                         # NFS mount of build-host's build/ (CLAUDE.md)
├── bin/                              # htc_main_app / wm / um 等 binary (部署产物)
├── lib/                              # *.so (部署产物)
├── data/
│   └── db/                           # kWmDbPath (wm_paths.h:8) — wm 专用 DB 根
├── media/                            # kWmMediaRoot (wm_paths.h:9) — wm 专用 media 根
│   ├── <ts>.mp4                      # wm RecordTask 产物 (record_task.cpp:77, wmMediaPath())
│   ├── <ts>_1.jpg                    # wm SnapTask 产物 (snap_task.cpp:113, wmMediaPath())
│   └── upload/                       # kWmUploadPath (wm_paths.h:11) — wm desc JSON
│       └── <stem>.json               # record_task.cpp:137, snap_task.cpp:145
├── logs/
│   └── app.log                       # wm + um 日志 (wm_app.cpp:152-153, um_app.cpp:131-132)
└── config.ini                        # wm PID-based config 切换的 fallback (wm_app.cpp:108)
```

### 5.3 SIM 模式（`./sim_sdcard_runtime/` 或 `$SIM_SD_ROOT`）

```
<simRoot>/                            # wmSimRoot() wm_paths.h:20-24 / simSdRoot() CameraServiceSim.cpp:80-90
├── DCIM/                             # sim mediaRoot (main_app/wm/um: cfg.mediaRoot = simRoot+"/DCIM")
├── data/db/                          # sim dbPath
├── logs/app.log                      # sim logRoot
└── media/                            # wm sim 路径 wmMediaRoot() = simRoot+"/media" (wm_paths.h:36)
    └── upload/                       # wmUploadPath() (wm_paths.h:47)
```
注：SIM 下 wm 的 media 根是 `<simRoot>/media`，而 main_app/um 的 media 根是 `<simRoot>/DCIM` —— **即使在 SIM 下两者也不一致**。

实际验证（本仓库 `sim_sdcard_runtime/` 已存在）：
```
sim_sdcard_runtime/
├── DCIM/
├── data/db/
│   ├── media_file.db
│   └── media_thumb.db
└── logs/
```

---

## 6. 关键差异对照表

| 维度 | `wm` | `um` | `htc_main_app` / `htc_workmode_app` |
|------|------|------|-------------------------------------|
| 进程模型 | one-shot worker | 长驻 server | one-shot (-wm) / 多命令 |
| 调度内核 | `WmScheduler` (task-slot) | `UmIdleCore` (idle-timeout) | cascade (`runWorkMode`/`runCommands`) |
| media 根 (HW) | `/mnt/huntcam/media` (wm_paths.h:9) | `/mnt/sdcard/DCIM` (um_app.cpp:130) | `/mnt/sdcard/DCIM` (main_app.cpp:177) |
| DB 根 (HW) | `/mnt/huntcam/data/db` (wm_paths.h:8) | `/mnt/sdcard/data/db` (um_app.cpp:129) | `/mnt/sdcard/data/db` (main_app.cpp:176) |
| upload/desc | `/mnt/huntcam/media/upload/` (wm_paths.h:11) | 无（经 HTTP → CameraServiceT32 直写 DCIM） | `/mnt/sdcard/media/upload/` (app.h:27) |
| log 根 (HW) | `/mnt/huntcam/logs` (wm_app.cpp:152) | `/mnt/huntcam/logs` (um_app.cpp:131) | `/mnt/sdcard/logs` (main_app.cpp:178) |
| 路径来源 | `wm_paths.h` inline | `StartupConfig` + CameraServiceT32 硬编码 | `StartupConfig` + app.h 宏 |
| SD mount | 不自己 mount（NFS 区） | `ProcessLifecycle` S11 mount `/mnt/sdcard` (ProcessLifecycle.cpp:475) | 同 um |
| media 根 (SIM) | `<simRoot>/media` (wm_paths.h:36) | `<simRoot>/DCIM` (um_app.cpp:123) | `<simRoot>/DCIM` (main_app.cpp:168) |

---

## Open questions

1. **`/mnt/huntcam` 在生产设备上是否存在？** wm/um 把日志和（wm 的）media/DB 写到 `/mnt/huntcam`，这是 NFS devtest 共享区。生产固件里若无 NFS mount，这些路径是否可写？（`wm_app.cpp:131` 注释只说 `NFS-shared，devtest 可读`，未说明生产语义。）—— 倾向于「devtest 专用，生产可能落回 SD」，但需确认。
2. **`/config/htc/` 是什么分区？** `env.ini:2,6` 把 `CONFIG_FILE` / `SETTING_FILE_PATH` 指到 `/config/htc/`，既非 `/mnt/sdcard` 也非 `/mnt/huntcam`。是设备 rootfs 上的只读配置分区还是另一处 mount？未在本次调研的代码里找到 mount 证据。
3. **wm 的 DB 与 um 的 DB 是否需要互通？** wm 把媒体元数据写 `/mnt/huntcam/data/db`，um（HTTP 回放）读 `/mnt/sdcard/data/db`。若 wm 拍的产物要在 um 的 HTTP 回放里可见，两套 DB/media 根不一致会导致回放看不到 wm 的产物。这是设计意图（两套完全隔离）还是遗留 bug？—— 代码里未见桥接逻辑。
4. **`ALL_MEDIA_FILE_IN_ONE_FOLDER` 宏是否定义过？** `app.h:28-30` 条件编译 `MEDIA_STORE_FOLDER_PATH`，但 grep 未找到该宏的 `-D` 定义，疑似遗留死代码（WorkModeRunner.cpp:113,121 仍引用）。

---

## Conclusion

- `um` = 长驻交互服务（RTSP/HTTP/mDNS，idle-timeout 自关机）；`wm` = 一次性任务（`-m 0/1/2` 拍/传，idle-grace 关机）。二者是 **全新独立 binary**，不被 `htc_media_app` spawn，与 legacy `htc_main_app`/`htc_workmode_app` 是平级替代。
- **两套目录路径体系互不共享**：wm 全走 `/mnt/huntcam/`（NFS，wm_paths.h 集中定义）；um 沿用 legacy `/mnt/sdcard/`（main_app 同款，仅日志改 NFS）。
- **无统一 PathManager / StorageService**：路径分散在 `app.h` 宏、`wm_paths.h`、`Common.h`、`StartupConfig` 字段、`CameraServiceT32` 硬编码字面量五处。`IStorageService` 只管容量/格式化，不管路径。
- SD 卡目录树以 `/mnt/sdcard/DCIM`（媒体）+ `/mnt/sdcard/data/db`（元数据/缩略图）+ `/mnt/sdcard/media/upload`（legacy desc）为三大根；wm 在 NFS 区有平行的 `/mnt/huntcam/media` + `/mnt/huntcam/data/db`。
- 建议（非本调研范围）：若后续要让 wm 产物在 um HTTP 回放里可见，需统一 media/DB 根或加桥接；`StartupConfig` 注释（ProcessLifecycle.h:25-27）也需补 wm 的 `/mnt/huntcam` 实际值。
