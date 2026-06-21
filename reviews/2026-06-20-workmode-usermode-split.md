# 2026-06-20 — WorkMode / UserMode 进程拆分决策

## Decision

从 `htc_main_app` 拆出 `htc_workmode_app` 后，按**运行语义**（一次性任务 vs 长驻
服务）重新划分 `-wm` 归属：

- `htc_workmode_app` 只留 `-wm 0/1/2`（SNAP_ONLY / SNAP_UPLOAD / UPLOAD_ONLY）
- 新建 `htc_usermode_app` 接管 `-wm 3`（CMD_MOBILE）/ `-wm 4`（CMD_RTSP_SERVER）

## Why

`-m`（`main_app.cpp:219`）与 `-wm 3` 同走 `CMD_MOBILE`，`-rs`（`main_app.cpp:235`）
与 `-wm 4` 同走 `CMD_RTSP_SERVER` —— 对称重叠。判定标准是 `runCommands` 内是否有
`while(keepRunning())` 长驻循环，**不是**“是否与 `-m` 重叠”。不止 `-wm 3`，`-wm 4`
是对称情况。详见 ADR。

## Authority

- 决策全文：`doc/knowledge/decisions/workmode-usermode-process-split.md`
- spec 已标注目标态：`doc/knowledge/specs/workmode-selection-and-switching.md §14`
- 与现有 ADR `decisions/workmode-vs-cmd-mobile-layering.md` 正交，不修改其结论

## Not yet（本次只写决策，不动代码）

- `htc_usermode_app` 尚未创建；`workmode_app` 仍 C3 UNSPAWNED。
- `workModeToCommand` 中 `TEST_ONLY` / `UVC` 两 case 仍在 workmode 侧。
- 迁移兼容点（`-m` 子参数 `--no-rtsp`/`--no-audio`/`--force-day`/`--record-stream1`；
  `-wm 3` 的 `RGB asyncBlink(30)` 取舍；CMake link 路线 A/B）见 ADR §7。

## Timing

在 **C4 / T17 repoint**（`media_app` 改 spawn `workmode_app`）之前完成归属拆分。
当前是定义归属成本最低的窗口。
