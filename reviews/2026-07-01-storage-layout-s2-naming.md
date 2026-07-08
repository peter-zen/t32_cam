# 2026-07-01 — storage-layout S2：媒体命名统一 + 修 um 同秒撞名

按 [`doc/knowledge/decisions/storage-layout.md`](../doc/knowledge/decisions/storage-layout.md) §6 S2 + plan `bubbly-pondering-ritchie.md` 执行。

## 做了什么

- **`StoragePaths` 加 `enum MediaKind` + `static makeMediaName(MediaKind, time_t, seq)`**（`strftime %Y%m%d_%H%M%S` + `snprintf %s_%s_%03d.%s`），消除 3 份独立 ts 拷贝（um `put_time` / wm `formatNow`×2）。
- **um 撞名修复**（核心）：`CameraServiceT32` 加成员 `last_photo_sec_` / `photo_seq_in_sec_`，`takePhoto` 在 `op_mutex_` 锁内（`:164`）用同秒计数器 + `makeMediaName` —— **不改 `takePhoto` 虚函数签名**（`ICameraService.h:62`）与 6 个调用点。覆盖 `startBurstPhoto`(100ms) / HTTP 快连拍 / `startTimerPhoto`(<1s) 所有同秒撞名场景。`startRecord` 用 `makeMediaName(Video, now, 0)`。
- **`CameraServiceSim`** `takePhoto`/`startRecord` 对齐 `makeMediaName`（seq=0）。
- **wm** `snap_task.cpp`/`record_task.cpp` 改用 `makeMediaName`，删两份 anonymous `formatNow()`。
- **wm SIM 目录修正**：`wm_app.cpp` SIM 分支 `sp(simRootPath,"DCIM")` → `"media"`，与 `wm_paths.h::wmMediaPath()`/HW 一致，消除 S1 遗留的 MediaScanner `/DCIM` vs task `/media` 不一致。

## 发现

- **uClibc `<cstdio>` 不把 `snprintf` 放入 `std::`**（T32 编译报 `'snprintf' is not a member of 'std'`，sim 的 glibc 通过）→ 改全局 `snprintf` + `<stdio.h>`。后续 T32 代码用 `snprintf` 注意走全局。
- 调研确认：所有下游（db key / thumb key / desc stem / `F_FileName`）都引用上游同一变量，改名后自动跟随 —— S2 只改 7 个命名生成点，无下游同步改动。

## 验证

- **双平台编译全绿**：`cmake --build build_sim` + `./script/build_t32@200.sh`，4 app 均 link 成功。
- **wm 命名实拍**（sim）：`wm -m 0` → `SnapTask: snap start ... first=.../sim_sdcard_runtime/media/IMG_20260701_181952_001.jpg`（前缀 + 3 位填充 + 小写 ext 全对）。
- **wm SIM 目录**：`find sim_sdcard_runtime -maxdepth 2` 确认 wm 侧建 `media/` + `media/upload/`（非 `DCIM/`），um 侧建 `DCIM/`。
- **um 不回归**：`um --no-http --no-rtsp`（1s idle）→ `rc=0`。

## 遗留

- **um burst 撞名修复未实跑**：需 HTTP 触发 `startBurstPhoto`（sim 下要起 HTTP server + curl），本轮靠代码审查（计数器逻辑 + `op_mutex_` 串行 + `makeMediaName` 输出已由 wm 验证）。留真机 devtest 或 sim 单测补强 `_000`/`_001` 不覆盖。
- **legacy `WorkModeRunner.cpp` 命名不改**（用户确认，退役中）。
- `snap_task`/`record_task` 删 `formatNow` 后 `<iomanip>`/`<sstream>`/`StringConvert.h` include 变 unused，保留未删（无害，避免误删）。

## 下一步 S3

`CameraServiceT32`/`Sim` 的**路径**注入（`/mnt/sdcard/DCIM` 硬编码 → `StoragePaths`）+ `http_api_v1.cpp:243-247` db-path 反推 mediaRoot + `wm_paths.h` 统一到 `StoragePaths`；db schema（加 `status`/`source` 列 + 相对路径 + WAL）。
