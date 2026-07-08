# 2026-07-01 — storage-layout S3：路径注入（StoragePaths 成唯一来源）

按 [`doc/knowledge/decisions/storage-layout.md`](../doc/knowledge/decisions/storage-layout.md) + plan `bubbly-pondering-ritchie.md`（S3）执行。收敛 S1/S2 后仍绕过 StoragePaths 的 4 条硬编码链。

## 做了什么

1. **`StoragePaths` 加 `previewDir()`**（`<root>/.preview`，capturePreviewFrame 用）。
2. **CameraService 构造注入**：
   - `ICameraService` 加虚函数 `getMediaRoot()`。
   - `CameraServiceFactory::create(sp)` + `getInstance(sp=nullptr)`（Meyer's：首次 um_app prewarm 带注入，http_api 无参拿缓存；`create` 内 `!sp` abort 防御）。
   - `CameraServiceT32` 构造存 `storage_` 成员，5 处硬编码（takePhoto `:196`/startRecord `:430`/capturePreviewFrame `:397`/getMediaDatabasePath `:542`/getThumbnailDatabasePath `:546`）改读 `storage_->*()` + 加 `getMediaRoot()`。
   - `CameraServiceSim` 同样注入，**删** `detectSimSdRoot`/`simSdRoot`/`simMediaDir`/`simDbDir` 四 helper（约 34 行）改读 `storage_`。T32/Sim 改后结构对称。
3. **`um_app`**：`storage::StoragePaths sp(...)` → `auto storagePaths = make_shared<...>`，`getInstance(storagePaths)->prewarm()`（prewarm 在 HTTP/RTSP 启动前，保证首次注入）。
4. **`http_api_v1`**：删 `get_media_root_from_database_path()`（db-path 三层反推），`:288` 路径遍历防护 + `:364` SIM-only 反推都改 `camera_service->getMediaRoot()`。
5. **`wm_paths.h` 全局委托**（方案 a）：删 `kWm*` 常量 + `wmSimRoot`/`trimTrailingSlash`，加 `wmStorage()`/`setStorage(sp)`，`wmMediaPath`/`wmUploadPath`/`wmDbPath`/`wmMediaRoot` 委托 `wmStorage()`。`wm_app` `setStorage(make_shared<...>)`。RecordTask/SnapTask/WmScheduler 调用点零改。

## 发现

- **`wm_paths.h` 的 inline 函数调 `->mediaRoot()`/`dataDb()` 需 `StoragePaths` 完整定义**，光 forward decl 不够 → 必须 `#include "StoragePaths.h"`。而 `CameraServiceT32.h/Sim.h` 的 `shared_ptr<StoragePaths>` 成员 + 方法声明可 forward decl（实现在 .cpp，.cpp 已 include 完整）。
- `CameraServiceFactory.cpp` 的 `#include "StoragePaths.h"`（无前缀）在 service_camera 库解析（`-I src/storage` 经 storage PUBLIC 传递）。
- `SIM_SD_ROOT` 的 `setenv`（`ProcessLifecycle.cpp:275`）**保留**——`IngenicVideo.cpp:55` HAL trace 仍读（hal 层不归 StoragePaths）。S3 后 SIM_SD_ROOT 消费者从 4 减到 2（app main + IngenicVideo）。

## 验证

- **双平台编译全绿**：`cmake --build build_sim` + `./script/build_t32@200.sh`。
- **wm_paths.h 委托**（sim 实拍）：`wm -m 0` → `first=.../sim_sdcard_runtime/media/IMG_20260701_190141_001.jpg`（路径 `media/` + 命名 `IMG_<ts>_001.jpg` 与 S2 完全一致）+ 建 `media/upload/`。
- **um 不回归**：`um --no-http --no-rtsp`（1s idle）→ `rc=0` + `op=boot` 正常。

## 遗留

- **CameraService 注入运行验证留真机**：um idle（`--no-http --no-rtsp`）不触发 prewarm（在 `!noHttp && !noRtsp` 块内）；`getInstance(sp)->prewarm()` + `takePhoto` 用 `storage_->mediaRoot()` 的实跑留真机 devtest。当前靠双平台编译 + 代码审查（构造注入逻辑明确）。
- `CameraServiceSim.cpp` 删 `detectSimSdRoot` 后 `normalizePath`/`parentPath`/`directoryExists`/`directoryWritable` 变 unused（anonymous static，warning 不阻塞，保留未删）。
- 至此 S1-S3 完成，`StoragePaths` 已是媒体/db 路径唯一来源（CameraServiceT32/Sim + wm + http_api 全走它）。

## 下一步 S4

db schema：加 `status`/`source` 列（现有 `migrateMediaSchema` `:197-225` 探列+ALTER+DEFAULT 模式，bump `user_version=3`）+ path 相对化 + upload policy（DELETE/KEEP + ready/done/failed + `onUploadSuccess`）。
