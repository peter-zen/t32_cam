# 2026-07-01 — storage-layout S1：StoragePaths 抽取（行为不变）

按 [`doc/knowledge/decisions/storage-layout.md`](../doc/knowledge/decisions/storage-layout.md) §6 S1 + plan `bubbly-pondering-ritchie.md` 执行。

## 做了什么

- 新增 `src/storage/StoragePaths.{h,cpp}`（`namespace storage`，纯布局计算器：`root/mediaRoot/dataDb/mediaDb/thumbDb/uploadDir`），加入 `src/storage/CMakeLists.txt`。
- `um_app` / `wm_app` / `main_app` / `workmode_app` 四个入口的 `StartupConfig.dbPath/mediaRoot` 手写拼接 → `storage::StoragePaths`：
  - SIM：`sp(simRootPath, "DCIM")`
  - HW um/main/workmode：`sp("/mnt/sdcard", "DCIM")`
  - HW wm：`sp("/mnt/huntcam", "media")`
  - HW `dbPath` 保留 `EnvManager::getEnv("DB_PATH", sp.dataDb())` 的 env 覆盖语义。
- `ProcessLifecycle` 消费侧、`DatabaseManager::init`、`setenv(SIM_SD_ROOT)` 均**未动**（S1 只改"谁填 cfg"，不改"谁读 cfg"）。

## 范围边界（故意外推到 S2）

- `CameraServiceT32.cpp` 6 处硬编码（`:194/:395/:430/:542/:546`）—— 牵连 `http_api_v1.cpp:243-247` 用 db path 反推 mediaRoot，**留 S2**。
- `wm_paths.h` 的 inline 函数（被 record_task/snap_task/wm_scheduler 直接调）—— **留 S2**。
- `WorkModeRunner.cpp` 老宏、`DISK_PATHNAME`、log/config/netif 路径 —— 不动。

## 验证

- **双平台编译全绿**：`cmake --build build_sim` + `./script/build_t32@200.sh`，4 个 app（um/wm/htc_main_app/htc_workmode_app）均 link 成功。
- **sim 运行**：`build_sim/bin/um --no-http --no-rtsp`（`HTC_UM_IDLE_TIMEOUT_MS=1000`）→ `rc=0`；`sim_sdcard_runtime/` 下 `DCIM/`、`data/db/`、`logs/` 正确创建（删后重建），证明 `sp.mediaRoot()/sp.dataDb()` 与原拼接逐字一致。

## 发现 / 遗留

- **wm SIM 下 `cfg.mediaRoot` = `/DCIM`，但 `wm_paths.h::wmMediaPath()` 给 task 的是 `/media`** —— 已存在的不一致（wm 的 cfg 走 /DCIM 喂 MediaScanner、task 走 /media 写文件）。S1 保持现状，**留 S2**（命名/目录统一）一并修正。
- T32 build 的 `normalizePath unused` / `SnapImgSize unused` / `parseIniFile unused` warning 均为**预先存在**（SIM-only static 函数在 HW build 下 unused 等），非本次引入。

## 下一步（S2）

统一命名 `makeMediaName(TYPE, ts, seq)` + um 加 burst 序号修同秒撞名；届时把 `wm_paths.h` 统一到 `StoragePaths`、修正 wm SIM `/DCIM`↔`/media` 不一致、并把 `CameraServiceT32` 硬编码改为注入（同步 `http_api_v1` 反推逻辑）。
