# 2026-06-24 — wm Slice 2（capture lane + m0/m1）实现 + 真机验证（部分）

> wm 第二片：SnapTask（photo）+ CaptureLane（cameraMode 路由）+ WmScheduler m0/m1。**这是 IMP-crash 风险点**。
> spec §5；Slice 1 见 [`2026-06-24-wm-slice1-m2.md`](2026-06-24-wm-slice1-m2.md)。

## 产物

- `src/app/workmode/snap_task.{h,cpp}` — photo 捕获 lane（RecordTask 的兄弟）。**同步** `ImageSnap::snap`（~1-2s，调用线程，避免 worker 线程碰 IMP）→ saveThumbnail → createDescInfoFile → enqueue。每次 trigger 新建 ImageSnap（destruct 释放 channel，避免跨拍碰撞）。
- `src/app/workmode/capture_lane.{h,cpp}` — 按 `cameraMode`（0=photo/1=photo+record 顺序/2=record）路由。cm==1 先拍照（同步等完）再录影，避免并发 CH2 争用。
- `wm_scheduler.{h,cpp}` — 加 capture lane + trigger，实现 m0/m1。`HTC_WM_CAMERA_MODE` env 覆盖（devtest 免改 setting.json）。
- `wm_app.cpp` — m0/m1 构造 CaptureLane + SimPirTrigger。

## 真机验证（T32）

### ✅ 已验：m0 PHOTO（cameraMode=0）单次
md5 `.so` c7044940→（首轮）。首轮 `wm -m 0 HTC_WM_CAMERA_MODE=0`：
```
op=boot mode=0 → op=time(source=system) → op=start mode=0
→ (SimPir 5s) CaptureLane: trigger cameraMode=0
→ SnapTask: snap start 2560x1440 burst=1 first=...20260624_092443_1.jpg
→ SnapTask: thumbnail saved (5043 bytes)
→ SnapTask: desc enqueued: .../upload/20260624_092443.json
→ op=shutdown reason=idle-grace → _exit(0)
```
**结论**：SnapTask 路径 HW GREEN——IMP 懒启（ImageSnap→sharedVideo→IMP_System_Init 一次）无 crash/defog、文件+缩略图DB+desc 齐全、teardown-safety（cleanupHook stopAutoSwitch）跑通。

### ✅ 已验：m1 CAPTURE+UPLOAD（cameraMode=0，one-shot）
冷启单 IMP run，app.log 时序：
```
09:03:24.373  SnapTask: snap start           (SimPir 5s)
09:03:29.452  SnapTask: desc enqueued        (capture 完成)
09:03:29.754  UploadWorker: auth failed      (upload 尝试，auth-fail=无后端，预期)
09:03:37.952  op=shutdown idle-grace mode=1  ← +8.5s（双 lane 空，idleGraceMs 8000）✓
09:03:39.068  writebackMcuTime
```
**结论**：m1 调度器 GREEN——capture→desc→UploadWorker drain→**双 lane idle-grace**→shutdown→MCU 回写；「首个 capture gate upload」成立（upload 仅在 capture 后拿到 desc）。文件+desc 落盘，wm 干净退出无 wedge。

### ✅ 已验：m0 RECORD（cameraMode=2，one-shot）
冷启单 IMP run，app.log 时序：
```
09:10:42.392  RecordTask: record start       (SimPir 5s)
09:10:45.935  record started duration=30s
09:11:29.128  RecordTask: thumbnail saved     (record 完成，6940 bytes)
09:11:46.444  op=shutdown idle-grace mode=0   ← record-done + ~8s grace ✓
```
产物：`20260116_091042.mp4` + 缩略图(6940B) + desc `20260116_091138.json`(1228B，onComplete 时写入)。
**结论**：RecordTask 在 wm 里 GREEN——录影+缩略图+desc+IMP+teardown 无 crash。30s 录影期间 SimPir 连发被 RecordTask CAS 拒（"previous record in progress, skip"）是**正常**并发拒绝，非 bug；one-shot 生效（仅 1 个 mp4，record 完成后 mask → idle-grace → shutdown）。

### Slice 2 捕获矩阵小结（test_wm.py，1-boot-1-case，2026-06-24）
| case | 结果 |
|------|------|
| m2-upload（IMP-free） | ✅ PASSED |
| m0-photo（cm==0） | ✅ PASSED |
| m0-record（cm==2） | ✅ PASSED |
| m1-photo（cm==0） | ✅ PASSED |
| m1-record（cm==2） | ✅ PASSED |
| m0-both / m1-both（cm==1） | 🔴 xfail（WEDGE，见下） |

**5 PASSED · 2 xfail**。wm 最大风险（IMP 捕获 crash）已排除：所有单捕获 + upload 路径 teardown-safety 全 GREEN。cm==1 combo 是唯一已知问题（deferred）。

### 🔴 发现：cm==1（photo+record combo）WEDGE 设备
`wm -m 0 HTC_WM_CAMERA_MODE=1`（一个 trigger 内：SnapTask 拍照 → RecordTask 录影）跑后**设备内核 hang**（shell 无响应，需硬断电）。单捕获路径（cm==0 photo、cm==2 record）各自 GREEN；问题只在**组合**——同进程内 ImageSnap 拍完再 CameraRecorder 录，IMP 通道管理冲突（疑似 ~ImageSnap 释放与 CameraRecorder 重开 CH0/CH1 之间未完全串行化）。

- **spec 冲突**：spec §5.1 含 cm==1（photo+record）。impl 当前 wedge。
- **处理（2026-06-24）**：test_wm.py 把 m0-both/m1-both 标 `xfail`（不跑、记为已知问题），不阻塞矩阵其余 case。cm==1 修复 deferred——需 IMP 通道管理专项调查（可能要 ImageSnap/CameraRecorder 共享通道释放协议，或 cm==1 改语义：photo 与 record 分两个 trigger 而非同 trigger 内 combo）。
- **按治理**：cm==1 改语义/暂下架需用户拍板（未擅改 spec）。

### ⚠️ 发现并修了 idle-grace bug（已复验 GREEN）
首轮 shutdown 在拍照后 0.4s 触发（应 8s）：**sync SnapTask::isBusy() 恒 false**，调度器在同步拍照期间看不到 busy → idleStart 从 boot 起算（拍照~10s 期间已超 8s）→ 拍完即关。fix：每次 capture 后 `idleStart=0`（捕获活动重置 grace）。

**复验（2026-06-24，冷启后单 IMP run，app.log 时序）**：
```
08:56:39.474  SnapTask: snap start       (SimPir 5s 触发)
08:56:44.135  SnapTask: desc enqueued    (capture 完成 ~4.7s)
08:56:52.604  op=shutdown idle-grace     ← +8.47s（=idleGraceMs 8000）✓
08:56:54.112  writebackMcuTime
```
shutdown 距 capture 完成 +8.47s（修前 0.4s）→ **fix 确认**。文件 139KB jpg + desc 落盘，wm 干净退出（shell 响应、无 wedge）。

### ❌ 设备 wedge（操作失误，非 wm bug）
**我在同一 boot 内连跑了 2 次 IMP-using `wm -m 0`**（首轮 + 复验），违反 **1-wm-per-boot** 硬约束——第 2 个 `IMP_System_Init` wedge 内核（memory 已记：「需手动断电」）。设备 shell 现无响应，**需手动硬断电重启**（broker 仍 alive，是 host 侧）。

## 待办（需冷启逐 case 验，1-boot-1-case 纪律）

- **idle-grace fix 复验**（首轮证明 bug 存在、fix 正确，但要在 fresh boot 上再跑一次 m0 photo 确认 8s grace）。
- **m0 RECORD（cameraMode=2）**：复用 RecordTask（test_record_smoke 已 HW-green）；在 wm 里验一次。
- **m0 cm==1（photo+record 顺序）**。
- **m1（capture+upload）**：capture 产 desc → UploadWorker drain → idle-grace/timeout 关机。
- 全 matrix（3 模式 × cameraMode 0/1/2）→ 1-boot-1-case，写 `tests/host/test_wm.py`。

## 踩坑（已沉淀）

- **1-wm-per-boot 是硬约束**：IMP-using wm（m0/m1）每个 case 必须冷启间隔。我连跑 2 次 → wedge。devtest loop 的 1-boot-1-case 纪律就是为此。
- **thin-loader md5 陷阱**（再确认）：wm 可执行 md5 不变，`capture_lane.cpp` 在 `libapp_workmode.so`——验 `.so` md5（host `build/lib/libapp_workmode.so` == 设备 `/mnt/huntcam/lib/`）。
- **CMake file(GLOB) 不自动重扫**：新增 snap_task.cpp/capture_lane.cpp 后必须 reconfigure（`cmake -B`）才进 lib。
- **busybox 无 sed/jq/head**：改 setting.json 的 cameraMode 难——故加 `HTC_WM_CAMERA_MODE` env 覆盖（devtest hook）。

## 下一步

1. **用户硬断电重启设备**（恢复 shell）。
2. 冷启后：1-boot-1-case 验 m0 record / cm==1 / m1 + idle-grace fix 复验。
3. 写 `tests/host/test_wm.py`（mirror test_snap_smoke，3 模式 × cameraMode，1-boot-1-case）。
