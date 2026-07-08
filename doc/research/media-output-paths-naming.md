# 调研：照片 / 视频 / 缩略图 — 目录与命名规则

- **Topic**: `media-output-paths-naming`
- **广度**: medium
- **日期**: 2026-07-01
- **范围**: t32_cam 项目三类产出文件（JPEG 照片、MP4 视频、缩略图）的落盘目录、文件名构成、目录来源、um/wm 一致性与冲突分析。
- **方法**: 只读源码调研（Read/Grep），覆盖 5 个 app 入口 + 2 套 wm 实现。

---

## TL;DR（关键发现）

项目有 **3 个独立可执行 app**，每个 app 走**不同的目录方案**，互不一致：

| App | 入口源 | 照片目录 | 视频目录 | 路径来源 |
|-----|--------|----------|----------|----------|
| **`um`**（user/mobile，HTTP+RTSP 常驻） | `src/app/um_app.cpp` | `/mnt/sdcard/DCIM/` | `/mnt/sdcard/DCIM/` | 硬编码（`CameraServiceT32`） |
| **`wm`**（work mode，新 PIR 触发，`wm_app.cpp`） | `src/app/wm_app.cpp` | `/mnt/huntcam/media/` | `/mnt/huntcam/media/` | `wm_paths.h::wmMediaPath()` |
| **`htc_main_app -wm N`**（legacy wm 级联） | `src/app/main_app.cpp` → `WorkModeRunner.cpp` | `/mnt/sdcard/media/<ts>/` | `/mnt/sdcard/media/` | `app.h::MEDIA_TARGET_PATH` |

- **缩略图**：三类 app 都**不落盘成文件**，而是把 JPEG 二进制 blob 写进 SQLite（`media_thumb.db` 的 `thumbnails` 表）。代码里出现的 `<dir>/thumb/<basename>.jpg` 路径只是注释/死字段，从不被写。
- **连续/分段录影**：第 2、3 段与第 1 段**命名规则完全相同**（都是 `<timestamp>.mp4`），靠时间戳天然区分；commit ba3516d 修的是"第 2 段不入库"（编码器首帧非 IDR → muxer 拒收 → 静默 abort），不是命名问题。

---

## Findings（逐条 + file:line 证据）

### Q1. 照片（JPEG）保存目录与命名

存在 **3 套并存**的照片命名路径：

#### A. `um` / HTTP API 路径（`CameraServiceT32::takePhoto`）

- 目录：**`/mnt/sdcard/DCIM/`**（硬编码绝对路径，非从配置取）
- 命名：**`IMG_<YYYYMMDD_HHMMSS>.jpg`**（注意小写 `.jpg`）
- 证据：`src/service/camera/impl/CameraServiceT32.cpp:190-195`

```cpp
// CameraServiceT32.cpp:191-195
auto now = std::time(nullptr);
auto tm = *std::localtime(&now);
std::ostringstream oss;
oss << "/mnt/sdcard/DCIM/IMG_" << std::put_time(&tm, "%Y%m%d_%H%M%S") << ".jpg";
std::string filename = oss.str();
```

- `um_app` 在启动时 `createDirectory("/mnt/sdcard/DCIM")`：`src/app/um_app.cpp:130,135`
- `cfg.mediaRoot = "/mnt/sdcard/DCIM"`：`src/app/um_app.cpp:130`

**前缀**：`IMG_`；**时间戳**：本地时区 `%Y%m%d_%H%M%S`（秒级）；**序号**：**无**（同秒内连拍会撞名 —— 见 Q5）；**扩展名**：`.jpg`（小写）。

#### B. `wm` 新任务路径（`SnapTask::trigger`）

- 目录：**`/mnt/huntcam/media/`**（NFS 共享路径，由 `wm_paths.h::wmMediaPath()` 给出）
- 命名：**`<YYYYMMDD_HHMMSS>_<idx>.jpg`**（burst 连拍带 1-based 序号）
- 证据：`src/app/workmode/snap_task.cpp:102-114`

```cpp
// snap_task.cpp:102-113
const std::string ts = formatNow();              // "%Y%m%d_%H%M%S"  (snap_task.cpp:31-36)
const std::string mediaPath = wmMediaPath();     // "/mnt/huntcam/media/"
...
std::vector<std::string> files;
for (int i = 0; i < burst; ++i) {
    files.push_back(mediaPath + ts + "_" + to_string_custom(i + 1) + ".jpg");
}
```

- `formatNow()` 定义：`src/app/workmode/snap_task.cpp:31-36`（`std::put_time(&tmv, "%Y%m%d_%H%M%S")`）
- `wmMediaPath()` 定义：`src/app/workmode/wm_paths.h:42-44`（`wmMediaRoot() + "/"`，非 SIM 时 = `kWmMediaPath` = `"/mnt/huntcam/media/"`，`wm_paths.h:9-11`）

**前缀**：**无**（纯时间戳起头）；**序号**：`_<i+1>`（1-based，burst）；**扩展名**：`.jpg`（小写）。

#### C. legacy `htc_main_app -wm` 路径（`processCmdSnap`，经 `media_app.cpp::quick_snap` 先写 `/tmp`）

- 初始写到 `/tmp/quick_snap/<dir>/`（`media_app.cpp:96-127`），再由 `WorkModeRunner::processCmdSnap` 移动到最终目录。
- 最终目录：`/mnt/sdcard/media/<YYYYMMDD_HHMMSS>/`（RTC 正常时）；`/mnt/sdcard/media/<ts>/`（RTC 失败时，文件名前缀加时间戳）。
- 命名：`<YYYYMMDD_HHMMSS>_<idx>.JPG`（**大写 `.JPG`**）
- 证据：
  - `quick_snap`：`src/app/media_app.cpp:102-127`

```cpp
// media_app.cpp:102-127
std::string timeStr = getCurrentTimeFormatted();   // "%Y%m%d_%H%M%S" (WorkModeRunner.cpp:71-79)
...
if (is_rtc_work_well) {
    fileNames.push_back(dirPath + "/" + timeStr + "_" + to_string_custom(i + 1) + ".JPG");
} else {
    fileNames.push_back(dirPath + "/" + to_string_custom(i + 1) + ".JPG");
}
```

  - 移动到 `MEDIA_TARGET_PATH + dir`：`src/app/workmode/WorkModeRunner.cpp:109-117`
  - `MEDIA_TARGET_PATH = SD_CARD_PATH "media/"`：`src/app/app.h:26`（`SD_CARD_PATH = "/mnt/sdcard/"`，`app.h:20`）

**注意**：`getCurrentTimeFormatted()` 调用了 `Misc::getDateTime()` 但丢弃返回值（`WorkModeRunner.cpp:78`），仅副作用；实际用到的是 `put_time` 的结果。

---

### Q2. 视频（MP4）保存目录与命名 + 连续分段

同样 **3 套并存**：

#### A. `um` / HTTP API 路径（`CameraServiceT32::startRecord`）

- 目录：**`/mnt/sdcard/DCIM/`**
- 命名：**`VID_<YYYYMMDD_HHMMSS>.mp4`**
- 证据：`src/service/camera/impl/CameraServiceT32.cpp:426-431`

```cpp
// CameraServiceT32.cpp:427-431
auto now = std::time(nullptr);
auto tm = *std::localtime(&now);
std::ostringstream oss;
oss << "/mnt/sdcard/DCIM/VID_" << std::put_time(&tm, "%Y%m%d_%H%M%S") << ".mp4";
std::string filename = oss.str();
```

- HTTP API `/api/v1/camera/record` 入口：`src/service/http_server/http_api_v1.cpp:1000`（`startRecord(channel, duration, audio, "record_id_placeholder")`）

**前缀**：`VID_`；**序号**：无；**扩展名**：`.mp4`。**只产 H.264/H.265 + AAC 封装的 mp4**（`CameraRecorder::buildVideoParams`，`CameraRecorder.cpp:73-77`）。无 `.ts` 路径。

#### B. `wm` 新任务路径（`RecordTask::trigger`）

- 目录：**`/mnt/huntcam/media/`**（`wmMediaPath()`）
- 命名：**`<YYYYMMDD_HHMMSS>.mp4`**（无前缀、无序号）
- 证据：`src/app/workmode/record_task.cpp:68-77`

```cpp
// record_task.cpp:68-77
const std::string mediaPath = wmMediaPath();
const std::string uploadPath = wmUploadPath();
if (!Misc::createDirectory(mediaPath) || !Misc::createDirectory(uploadPath)) { ... }
std::string record_path = mediaPath + formatNow() + ".mp4";
```

#### C. legacy `htc_main_app -wm` 路径（`WorkModeRunner::processCmdVideoRecord`）

- 目录：**`/mnt/sdcard/media/`**（`MEDIA_TARGET_PATH`，`app.h:26`）
- 命名：**`<YYYYMMDD_HHMMSS>.mp4`**（无前缀、无序号）
- 证据：`src/app/workmode/WorkModeRunner.cpp:179-188`

```cpp
// WorkModeRunner.cpp:185-187
} else {
    record_path = std::string(MEDIA_TARGET_PATH) + getCurrentTimeFormatted() + ".mp4";
}
```

- 诊断 env `HTC_RECORD_TMPFS=1` 可把文件改写到 `/tmp/<ts>.mp4`（绕过 SD 卡，FPS 调试用）：`WorkModeRunner.cpp:181-184`

#### 连续 / 分段录影的第 2、3 段命名（commit ba3516d）

**命名上：第 2、3 段与第 1 段规则完全一致**，都按"本次录影开始时刻"生成新时间戳，无 `_seg2` / `_part2` 之类的后缀。例如连续两段：
- 第 1 段：`20260701_130000.mp4`
- 第 2 段：`20260701_130010.mp4`（间隔 10s，时间戳自然不同）

**ba3516d 修的不是命名，是入库**：编码器通道重建后首发帧是无 SPS/PPS 的非 IDR slice，mp4 muxer（`need_sps`）拒收 → `VideoRecorder` 走静默 abort（`return false` 在 `addMedia` 之前）→ 文件不入 DB → 历史回放看不到第 2 段及以后。第 1 段（冷编码器）首帧是带 SPS/PPS 的 IDR 所以正常。修复（`src/media/video/VideoRecorder.cpp`）：

1. stream start 后 `requestIDR()` 强制编码器首发 IDR（`VideoRecorder.cpp:372-376`，ba3516d 新增）：

```cpp
// VideoRecorder.cpp:372-376 (ba3516d)
// 连续录影重建编码器通道后，编码器可能先吐非 IDR slice（无 SPS/PPS），mp4 muxer 要求
// 首帧为带 SPS/PPS 的 IDR。此处强制首发 IDR；record loop 还会丢掉首批非 IDR 帧兜底。
if (!stream_->requestIDR()) {
    Logger::log(LogLevel::WARNING, "record: requestIDR failed ...");
}
```

2. record loop 丢掉开头非 IDR 帧直到拿到第一个 keyframe，上限 120 帧（~4s）防死循环（`VideoRecorder.cpp:665-709`）：

```cpp
// VideoRecorder.cpp:665-670 + 694-708 (ba3516d)
bool gotFirstKeyframe = false;
int drainSkip = 0;
const int kMaxDrainSkip = 120;
...
if (!gotFirstKeyframe) {
    if (!frame.key) {
        drainSkip++;
        stream_->releaseFrame(frame);
        if (drainSkip >= kMaxDrainSkip) { ... return false; }
        continue;
    }
    gotFirstKeyframe = true;
}
```

- 排查证据链：`reviews/2026-07-01-record-missing-investigation.md`
- `wm` 的 `RecordTask` 每段都会 `releaseVideoResources()`（`record_task.cpp:66`）重建编码器通道，故同样命中该 bug，同一处修复覆盖。

---

### Q3. 缩略图保存目录与命名

#### 关键结论：**缩略图不入文件系统，进 SQLite DB**

三个 app 的缩略图都通过 `MetadataDao::saveThumbnail(filePath, jpegBytes)` 把 JPEG 二进制作为 **BLOB** 写入 `media_thumb.db` 的 `thumbnails(file_path, data)` 表（`INSERT OR REPLACE`）。**键是媒体文件的完整路径**，不存在独立的缩略图文件名/目录。

- DB 路径：`/mnt/sdcard/data/db/media_thumb.db`（`CameraServiceT32::getThumbnailDatabasePath`，`CameraServiceT32.cpp:545-547`）
- `saveThumbnail` 实现：`src/storage/MetadataDao.cpp:93-114`

```cpp
// MetadataDao.cpp:93-114
bool MetadataDao::saveThumbnail(const std::string& filePath, const std::vector<uint8_t>& data) {
    sqlite3* db = DatabaseManager::getInstance().getThumbDb();
    ...
    const char* sql = "INSERT OR REPLACE INTO thumbnails (file_path, data) VALUES (?, ?);";
    ...
    sqlite3_bind_text(stmt, 1, filePath.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_blob(stmt, 2, data.data(), data.size(), SQLITE_STATIC);
    ...
}
```

#### 三个 app 的调用点

- **um 拍照**：`CameraServiceT32::takePhoto` → `dao.saveThumbnail(filename, image_snap_->getThumbnailData())`（`CameraServiceT32.cpp:210-216`）
- **um 录影**：`CameraServiceT32::startRecord` 的 onComplete → `dao.saveThumbnail(r.filePath, ...)`（`CameraServiceT32.cpp:463-471`）
- **wm 录影**（新）：`RecordTask::onCompleteRecord` → `dao.saveThumbnail(record_path, ...)`（`record_task.cpp:113-121`）
- **wm 录影**（legacy）：`WorkModeRunner::processCmdVideoRecord` → `dao.saveThumbnail(record_path, ...)`（`WorkModeRunner.cpp:240-252`）
- **wm 拍照**（新）：`SnapTask::trigger` → `dao.saveThumbnail(files[0], ...)`（`snap_task.cpp:132-140`）

#### 缩略图尺寸 / 格式

- 格式：**JPEG**（硬件编码器 IVDC 直出）
- 尺寸：**宽 320px，高度按宽高比等比缩放并对齐到 2**（照片和录影共用 CH2 stream）
- 证据：
  - 照片缩略图配置：`src/media/snap/ImageSnap.cpp:175-187`（`tcfg.width = 320; tcfg.height = (h*320 + w/2)/w; tcfg.height = (tcfg.height+1) & ~1;`，quality=60，FIXQP）
  - 录影缩略图：`VideoRecorder` 的 CH2 stream（同样 320 宽，见 `CameraRecorder.cpp` 注释 `320x180`，`CameraRecorder.cpp:43`）

#### 关于代码里的 `thumb/` 目录（陷阱）

`CameraRecorder::computeThumbnailPath` 会算出一个 `<video dir>/thumb/<basename>.jpg` 路径并填进 `RecordResult::thumbnailPath` 字段（`CameraRecorder.cpp:41-52, 217, 252-253`）。**但全代码库没有任何一处把这个字段对应的文件写到磁盘** —— `grep thumbnailPath` 仅命中注释/字段填充，无 `fopen(thumbnailPath, "w")`。该字段实际是**死字段**（`CameraRecorder.h:105` 注释 `<video dir>/thumb/<basename>.jpg` 仅描述意图）。真正落盘走 `saveThumbnail` → DB BLOB。

#### 例外：snap_test 与 thumb_pending 离线同步

- `snap_test`（诊断二进制）：把缩略图写到 `<first_jpeg>.thumb.jpg`（`src/app/snap_test.cpp:143-151`），即**与媒体同目录、媒体全名 + `.thumb.jpg` 后缀**。这是测试可验证产物，非生产路径。
- 离线同步机制（`MediaScanner::scanPendingThumbnails`）：扫 `<data_root>/thumb_pending/` 下形如 `<media_full_name>.thumb.jpg` / `.thumb.jpeg` 的文件（`MediaScanner.cpp:108-124`），读出二进制 → `saveThumbnail` 入 DB → 删文件（`MediaScanner.cpp:281-298`）。`thumb_pending` 目录位置：`http_api_v1.cpp:369-377`，默认 `<data_root>/thumb_pending`，可由 env `THUMB_PENDING_DIR` 覆盖。这是给"无 DB 写入权限的采集进程"留下的离线入库通道。

---

### Q4. 目录路径来源 + DCIM 子目录结构

#### 路径来源（三套来源并存）

| 来源 | 定义位置 | 用在哪 |
|------|----------|--------|
| **硬编码** `/mnt/sdcard/DCIM/` | `CameraServiceT32.cpp:194, 430` | um / HTTP API（照片 `IMG_*` + 视频 `VID_*`） |
| **`app.h` 宏** `MEDIA_TARGET_PATH` 等 | `src/app/app.h:26-29`（`SD_CARD_PATH "media/"`） | legacy `htc_main_app -wm`（经 `WorkModeRunner`） |
| **`wm_paths.h`** `wmMediaPath()` 等 | `src/app/workmode/wm_paths.h:8-48` | 新 `wm` app（经 `RecordTask`/`SnapTask`） |

`SD_CARD_PATH` 定义：`src/app/app.h:20`（`"/mnt/sdcard/"`，真机）/ `app.h:14`（`"./sim_sdcard_runtime/"`，SIM）。
`wm_paths.h` 常量：`kWmMediaRoot="/mnt/huntcam/media"`（`wm_paths.h:9`）/ `kWmMediaPath="/mnt/huntcam/media/"`（`wm_paths.h:10`）。SIM 下走 `wmSimRoot()`（env `SIM_SD_ROOT` 或 `./sim_sdcard_runtime`）。

**`StorageService` 不决定媒体目录**：`StorageServiceT32` 只报容量（`StorageServiceT32.cpp:9-19`，`statvfs("/mnt/sdcard")`），不提供路径。格式化是 TODO（`StorageServiceT32.cpp:22-27`）。

#### DCIM 子目录结构：**扁平，无子目录**

- **`/mnt/sdcard/DCIM/` 下直接放 `IMG_*.jpg` / `VID_*.mp4`**，**不按日期 / 类型 / 会话分子目录**（`CameraServiceT32.cpp:194, 430` 直接拼到 `DCIM/`）。
- 仅 MediaScanner 从 `DCIM` 递归扫（`http_api_v1.cpp:365`，`MediaScanner.cpp:185-227`）——扫描器支持子目录，但生产路径不创建子目录。
- `wm` app 的 `/mnt/huntcam/media/` 同样扁平（`record_task.cpp:77`、`snap_task.cpp:113`）。
- 唯一分目录的是 **legacy `htc_main_app -wm` 拍照**：`/mnt/sdcard/media/<YYYYMMDD_HHMMSS>/`（按"会话时间戳"分一级目录，`WorkModeRunner.cpp:113-117`）；视频不分目录（`WorkModeRunner.cpp:186`）。

#### 数据库 / 日志目录

- 媒体 DB：`/mnt/sdcard/data/db/media_file.db`（`CameraServiceT32.cpp:542`）
- 缩略图 DB：`/mnt/sdcard/data/db/media_thumb.db`（`CameraServiceT32.cpp:546`）
- desc JSON（上传清单）：`/mnt/sdcard/media/upload/`（`MEDIA_UPLOAD_PATH`，`app.h:27`）= legacy；`/mnt/huntcam/media/upload/`（`wmUploadPath()`，`wm_paths.h:47`）= 新 wm

---

### Q5. um 与 wm 一致性 + 冲突分析

#### 命名规则不一致（三套差异汇总）

| 维度 | um (`CameraServiceT32`) | wm 新 (`RecordTask`/`SnapTask`) | wm legacy (`WorkModeRunner`) |
|------|------------------------|--------------------------------|------------------------------|
| 照片前缀 | `IMG_` | 无 | 无（但文件名后缀大写 `.JPG`） |
| 视频前缀 | `VID_` | 无 | 无 |
| 照片扩展名 | `.jpg` | `.jpg` | `.JPG` |
| 视频扩展名 | `.mp4` | `.mp4` | `.mp4` |
| 照片序号 | 无 | `_<i+1>` | `_<i+1>` |
| 根目录 | `/mnt/sdcard/DCIM/` | `/mnt/huntcam/media/` | `/mnt/sdcard/media/` |

#### 冲突分析

**结论：um 与 wm 之间无现实冲突**（不同物理介质/不同目录），但 **um 自身有同秒撞名风险**。

1. **um vs wm 新**：根目录完全不同（`/mnt/sdcard/DCIM/` vs `/mnt/huntcam/media/`），且 `um` 和 `wm` 是两个**独立二进制**（`src/app/CMakeLists.txt:70-71`：`wm`/`um` 分别由 `wm_app.cpp`/`um_app.cpp` 构建），**不会同机同时运行**（设备按工作模式冷启进入其一）。无冲突。

2. **um vs wm legacy**：都在 `/mnt/sdcard/` 下，但 `DCIM/` vs `media/` 不同子树。无冲突。

3. **wm 新 vs wm legacy**：`/mnt/huntcam/media/` vs `/mnt/sdcard/media/`，且 `wm`（新）取代的是 `htc_main_app -wm`（legacy），二者互斥部署。无冲突。

4. **um 内部同秒撞名（真实风险）**：`IMG_<YYYYMMDD_HHMMSS>.jpg` / `VID_<YYYYMMDD_HHMMSS>.mp4` 时间戳只到秒，且**无序号兜底**。
   - HTTP 连拍 `startBurstPhoto`（`CameraServiceT32.cpp:232-285`）实际是循环调 `takePhoto`，每张都用"当下时间戳"——若 interval < 1s（可由 Settings 控制），**两张会生成同文件名**，后写覆盖先写。
   - 定时拍 `startTimerPhoto` 同理（`CameraServiceT32.cpp:294-359`）。
   - 相比之下 wm 的 `SnapTask` 用 `_<idx>` 序号（`snap_task.cpp:113`），burst 内无撞名；但**两次独立 trigger** 仍靠时间戳区分，若同秒触发仍会撞。
   - 照片 + 视频前缀不同（`IMG_` vs `VID_`），即使同秒也不同名，无跨类型冲突。

5. **同名并发写**：`CameraServiceT32` 用 `op_mutex_` 串行化 takePhoto/startRecord（`CameraServiceT32.cpp:164, 420`），同进程内不会真并发开两个同文件名 fp；但 um 的 HTTP 路径与 wm 的 PIR 触发路径既然不同进程不同机时，也无并发写。

---

## Open questions（不确定 / 需进一步确认）

1. **`RecordResult::thumbnailPath` 死字段是否计划复活？** 当前 `CameraRecorder::computeThumbnailPath` 算出 `<dir>/thumb/<stem>.jpg` 但无任何消费者写文件。若未来要把缩略图落盘（而非 DB BLOB），需同步改 3 个 app 的 onComplete。建议确认产品意图（DB BLOB vs 文件）。

2. **`/mnt/huntcam/media`（wm 新）与 `/mnt/sdcard/DCIM`（um）是否最终要统一？** 当前 wm app 产物在 NFS 共享区（`/mnt/huntcam` = 构建主机 `build/`），um 产物在设备 SD 卡。若 um 历史回放要看到 wm 拍的素材，二者 DB / 目录必须打通；当前看似两套独立存储。需确认产品形态（wm 是否为"采集+上传后即弃"，um 是否为"本地浏览主入口"）。

3. **`um` 同秒连拍覆盖是否为已知 bug？** `takePhoto` 时间戳到秒、无序号，interval<1s 的 burst 会覆盖。`Settings::shootingInterval` 单位是 `*100`（`CameraServiceT32.cpp:241`），最小 100（=10s? 待确认单位），若最小粒度远大于 1s 则不触发。需读 Settings 定义确认 `shootingInterval` / `burstNumber` 的单位与最小值。

4. **`MediaScanner` 的 `mediaRootDir`（`http_api_v1.cpp:365` 写死 `DCIM`）只覆盖 um 产物。** wm 新产出的 `/mnt/huntcam/media/*.mp4` 不在该扫描根下，是否由 wm app 自己入库（`snap_task.cpp` 的 `ImageSnap::snap_internal` 会 `addMedia`，`ImageSnap.cpp:387-402`）？wm 的 `RecordTask` onComplete 是否也写 media_file.db？需确认 wm 是否有独立的 DBM init（`wm_paths.h` 的 `kWmDbPath="/mnt/huntcam/data/db"` 与 um 的 `/mnt/sdcard/data/db` 是两套库）。

---

## Conclusion / 建议

1. **命名规则碎片化**：同一项目 3 套照片命名 + 3 套视频命名，前缀/扩展名大小写/序号都不一致。若要做跨 app 的素材管理或迁移，建议抽一个 `MediaNaming` 工具类统一（前缀 + 时间戳 + 序号 + 扩展名），三个入口都调它。当前不统一是历史分层（HTTP/wm-legacy/wm-new 各自演进）的结果。

2. **缩略图走 DB BLOB 是合理的**（避免 fs 上碎小文件 + 事务一致），但 `computeThumbnailPath` 死字段应删除或落实，避免误导。

3. **连续录影分段命名无问题**（时间戳天然区分），ba3516d 已修第 2 段不入库的根因（编码器首帧非 IDR）。无需在命名层改动。

4. **um 同秒撞名**是唯一现实风险点，建议给 `takePhoto` 加 burst 序号后缀（对齐 `SnapTask` 的 `_<idx>`），或时间戳提到毫秒。

5. **DCIM 扁平结构**当前无分层（不按日期/类型），素材多了后 fs 级 `readdir` 性能会下降；MediaScanner 已支持递归扫描，未来若要分层（如 `DCIM/YYYYMM/`），扫描器侧无需改，只需改 `takePhoto`/`startRecord` 的路径构造。
