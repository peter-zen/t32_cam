# 2026-06-28 — wm task slot/task 调度重构：T32 真机验证 + 收尾打磨

> wm slot/task 调度架构（b0d6791：SlotOutputPort=signal、新私有 UploadTask、UploadWorker 不动）的真机验证轮。
> 本轮在 T32 上把 `-m 1`（cm==1 拍+录）端到端跑通并打磨：capture-upload-forget（上传后删 desc+媒体）、
> desc 与媒体同名、idle-grace 2s、diag 降级。spec 真相源：[`wm-task-slot-scheduler.md`](../doc/knowledge/specs/wm-task-slot-scheduler.md) + [`wm-app-spec.md`](../doc/knowledge/specs/wm-app-spec.md)。

## 背景（b0d6791 已落）

slot 调度本就正确（type1 Done→slot_trigger→type2），但旧 type2 adapter `UploadWorkerTask` 是被动观察者——真实上传由 `UploadWorker::enqueue()` 在 type1 期间（Snap/Record 调）提前触发，slot2 从未掌管真实上传。重构收敛：

- **新 wm 私有 `UploadTask`**（`upload_task.{h,cpp}`）：own 线程，扫 SD（`F_UploadedTag==0`）→ lazy connect/auth → 上传；不共用 legacy `UploadWorker`。
- **`SlotOutputPort` = signal（非 data）**：capture 产新活只推 wake token；upload 扫 SD 取真实 desc。
- **UploadTask 终止**：线程从不自 Done；scheduler `uploadSlot.poll(allowComplete=captureSlot.isEmpty())` 在 slot1 空 + drained + 无 token 时标 Done。
- 新文件 `slot_output_port.h`（header-only signal：push/parkIfNoWork/isSettled，idle+token 同 mutex 杜绝 TOCTOU）、`tests/test_slot_output_port.cpp` + `test_wm_task_scheduler.cpp`。

## 本轮打磨（本次提交）

| 文件 | 改动 |
|---|---|
| `wm_app.cpp` | idle-grace 默认 30000→**2000**：上传完即关机（grill 2026-06-28：wm 不做 PIR 合并窗口，后续 PIR 走完整冷启）。`HTC_WM_IDLE_GRACE_MS` 可覆盖。 |
| `upload_task.cpp` | **capture-upload-forget**：整体上传成功（desc 自身+全部媒体）→ 删 desc + 删已传媒体（去 `FileManage` 门，wm 始终删；legacy `UploadWorker`/`WorkModeRunner` 不动）。部分成功保留 desc，按 `file_inf.F_UploadedTag` 重传（m2 resume 语义不变）。 |
| `upload_task.cpp` | `HTC_UPLOAD_DIAG` **降级为异常才打**：默认静默，仅 `slow idle scan`(dt>1s) / `token-spin`(≥3 次空醒) / `runLoop exit`。 |
| `snap_task.cpp` / `record_task.cpp` | **desc 文件名 = 媒体 basename**（仅换 `.json`）：拍照 `<ts>_1.json`↔`<ts>_1.jpg`、录影 `<ts>.json`↔`<ts>.mp4`。修掉录影 desc 用「结束时刻」命名、与 mp4「开始时刻」对不上的错位。`fileStem()` 从媒体路径推导。 |
| `wm-app-spec.md` | §5.2 加 desc 命名契约 + capture-upload-forget 说明；§9 idle-grace 默认 2000。 |

> **部署标记看 `.so`**：snap/record/upload_task 都编进 `app_workmode` 这个 .so target，wm 可执行仅 thin main（md5 不变属预期）。本轮 `libapp_workmode.so` md5 = **93b7343800dc1779bd7aa4bc68bc63c7**。

## 真机验证（T32，`-m 1` cm==1 拍+录，连续 2 轮干净）

命令：`HTC_WM_CAMERA_MODE=1 HTC_NO_MCU=1 HTC_LOG_DEBUG=1 HTC_TEST_NO_POWEROFF=1 HTC_WM_ONE_SHOT=1 HTC_UPLOAD_DIAG=1 LD_LIBRARY_PATH=/mnt/huntcam/lib:$LD_LIBRARY_PATH /mnt/huntcam/bin/wm -m 1`

### ✅ 端到端时序（第 2 轮，16:48–16:49）
```
16:48:11.291  capture_run_once begin (cameraMode=1)
16:48:11.393  SnapTask: snap start          → 12.135 jpg saved (103544B)
16:48:13.137  SnapTask: desc written 20260628_164811_1.json   ← 与 jpg 同名
16:48:13.246  RecordTask: record start 20260628_164813.mp4
16:48:48.914  record done (duration=35622ms, observed_fps 25.8 — 已知漂移)
16:48:50.678  RecordTask: desc written 20260628_164813.json   ← 用开始时刻，与 mp4 同名
16:48:51.177  capture cleanup_end
16:48:51.208  type=1 Running→Done
16:48:51.253  type=2 Ready→Running          ← +45ms，slot 真串行
16:48:51.587  UploadTask connected+authed    ← 仅在 type=2 之后 connect（修掉 SnapTask::enqueue 提前唤醒）
16:48:52.151  photo desc json err=0 → 52.627 jpg err=0
16:48:53.452  video desc json err=0 → 59.378 mp4 err=0 (15940255B, 4272kb/s)
16:48:59.676  desc removed ×2                ← capture-upload-forget
16:48:59.878  type=2 Done                    ← 末文件 59.554→Done 324ms
16:49:01.913  shutdown idle-grace            ← +2.04s（grace=2s）
16:49:02.582  Power off
```
boot→poweroff ~51.5s。

### 结论
- **slot 真串行**：type=1 含 video desc 写 + cleanup 全完成（51.208）才 type=2 Running（51.253，+45ms）；真上传完全受 type=2 控制——彻底消除旧 `SnapTask::enqueue` 提前唤醒 `UploadWorker` 的设计偏离。
- **拍+录顺序 + 上传内容**：先拍后录，4 文件全 err=0。
- **capture-upload-forget**：磁盘核对 `media/`、`media/upload/` 均**空**。
- **desc 同名**：拍照 `164811_1.json`↔`164811_1.jpg`、录影 `164813.json`↔`164813.mp4`，精确 basename 匹配。
- **idle-grace 2s**：Done→shutdown 精准 2.04s。
- **diag 降级**：全日志仅 1 行 `[udiag] runLoop exit`，无刷屏、无异常告警。
- **cm==1 wedge 未复现**（2 轮）：MemAvail ~30MB / VmSwap=0 / fopen <100ms，对照旧 wedge 签名（MemAvail<10MB、VmSwap 飙升、fopen 57s 卡）。支持「延迟上传移出 capture 窗口 → 去掉 dirty-page/swap-thrash 触发」的 06-26 RAM+swap 理论。

### scan-gap RCA（已结案）
曾观察到「上传完→type=2 Done 的 ~9.5s 静默空档」。`HTC_UPLOAD_DIAG` 打点坐实：`scan-end dt` 量的是**整轮 pass（listFilenames + 每个待传 desc 的上传 I/O）**，不是纯扫盘。纯空目录扫盘 ~60ms（正常）；7999ms 的大 dt 是 mp4 上传本身（净传 3.9s）。**根因：上传 I/O，非扫盘慢**。delete-on-upload 后上传完目录立即清空，rescan `n=0`，无累积 desc 拖慢——末文件传完→Done 仅 ~324ms。

## 待办（未做）
- **m0（CaptureOnly）/ m2（UploadOnly resume）真机验证**：目前只验 m1。m0 无上传、m2 是跨 boot 补传。
- **cm==1 多轮**（再跑 2-3 轮）确认 wedge 真不复现（2 轮干净，尚不构成铁证）→ 翻 `tests/host/test_wm.py` 的 xfail。
- legacy `-wm 0` 受 `RecordTask` desc 改名波及（desc 名变更；服务端按 `file_inf` 内容关联，低风险）—— 视情况回归。

## 关联
- spec：[`wm-task-slot-scheduler.md`](../doc/knowledge/specs/wm-task-slot-scheduler.md) · [`wm-app-spec.md`](../doc/knowledge/specs/wm-app-spec.md)
- 前序：[`2026-06-24-wm-slice1-m2.md`](2026-06-24-wm-slice1-m2.md) · [`2026-06-24-wm-slice2-capture.md`](2026-06-24-wm-slice2-capture.md) · [`2026-06-25-wm-cm1-reproducer-investigation.md`](2026-06-25-wm-cm1-reproducer-investigation.md)
