# 2026-07-11 — wm 上传失败 SD 落卡 + 下次启动续传（T22 落地）

**spec**：`doc/knowledge/specs/wm-upload-sd-fallback.md`（grill 定型：Q1-Q6 + ordering 修正）
**flow**：feature（planner → implementer → tester → reviewer，全 C1 通过，C2 audit OK，state T22=done）
**执行方式**：4 节点全 PM 内联（subagent 撞 provider minimax 429 token-plan quota，同 T17-R4 先例）。

## 落地内容（7 EDIT + 2 NEW test，双平台编译绿 + sim 单测绿）
- `wm_app.cpp:336` 超时默认 60s→120s；`wm_app.cpp:365-396` workDirs 重排（`-d` 恒 [0]，tmpfs∪SD backlog 仅 backlog 排序，sdFallbackEnabled gate）。
- `wm_scheduler.{h,cpp}` 落卡钩子 `persistStrandedTmpDir(ShutdownReason)`：run() 的 stopAll() 后调用，门控 UploadTimeout + HTC_WM_SD_FALLBACK!="0"，retry mountSDCard，递归 move tmpfs→SD，碰撞 _2/_3，ENOSPC 清半成品。
- `wm_sweep.{h,cpp}` `sdFallbackEnabled()` + `isTimestampDir` 放宽 `^\d{8}_\d{6}(_\d+)?$`。
- `Misc.{h,cpp}` `moveDirectoryRecursive`（rename→EXDEV fallback 递归 copy+delete，native open/read/write copyFileNative，禁 fork-exec）。
- `tests/test_misc_move_directory.cpp`（4 case PASS）+ `tests/test_wm_sweep_stranded.cpp`（3 case PASS）。
- uClibc 适配：碰撞后缀 int→string 用 bare `snprintf`+`<stdio.h>`（无 `std::to_string`/`std::snprintf`）。

## 关键审查结论（reviewer APPROVE）
- **时序最关键风险已排除**：SIGTERM 处理器只置 `g_pending_signal`+write(pipe)，不 `_exit`（ProcessLifecycle.cpp:138-151）→ persist 在主线程 run() 出循环后可靠执行（stopAll join 后文件静默）。
- 调度内核 / UploadTask / ProcessLifecycle S11 / m1 / m3 / hal 全未改。

## 非阻塞观察（risk 入 state）
- RV1（low）：EXDEV copy+delete 分支 sim 未触发（PC 无 tmpfs/vfat 边界，走 rename 快路径，单测验行为等价；真机必触发）。
- RV2（medium）：AC2 sim 端到端（full wm boot + mock 上行）未做——PC 不可行；wiring 由 code inspection + 编译 + 纯逻辑单测保证。
- RV3（low）：persist 前缀过滤对裸 base `/tmp/media/` 也匹配，但数据流上 workDirs_ 只含 `<ts>` 子目录，裸 base 永不出现。

## 合入前置（真机 §8 AC3，留用户 / devctl loop）
1. 断网 → `wm -m 2 -d /tmp/media/<ts>/` → 120s 超时 → `[wm] persist: ... moved -> /mnt/sdcard/media/<ts>/` → poweroff。
2. 复网重启 → 本次 `-d` 传完后续传 SD 滞留 → 目录删 → idle-grace 关机。
3. 碰撞 → `<ts>_2`；`HTC_WM_SD_FALLBACK=0` → 不落卡。
4. md5：设备 `bin/wm` == 主机 `build/bin/wm`。

## 回滚
- env kill-switch：`HTC_WM_SD_FALLBACK=0`（运行时，禁 persist+resume）。
- 逐触点独立回滚（见 artifacts/T22-implementer-full.md「回滚点」）。
