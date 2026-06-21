# 2026-06-21 — record teardown 稳定化（kill-switch 退役 + 进程内 channel 释放）

devtest Phase-1 挖出 record 关机 panic + 进程内重复失败，两轮修复 + 真机验证。

## task-8：关机 teardown panic 修复（kill-switch 退役）✅ 已验证

**根因**（devtest 确定性复现 + analyst 定位）：关机 cleanupHook 的 `controlISP(DAY)` 在 sensor/ISP 仍 enabled 时调 `IMP_ISP_Tuning_SetISPRunningMode` → 触发 ISP 帧中断 defog 刷新（`defog_count_weight35abc`）解引用已释放 rmem buffer → `Kernel panic: Fatal exception in interrupt` → 设备 warm 重启。epc `defog_count_weight35abc` ← `tisp_day_or_night_par_refresh`。

**修复**（4 处，SDK 契约 `imp_isp.h:74-102` 支撑）：
- `workmode_app.cpp` cleanupHook + `main_app.cpp` cleanupHook + `ProcessLifecycle.cpp` auto_release：**删 `controlISP(DAY)`**（关机语义下多余）+ `stopAutoSwitch()`，保留 `controlIRLed/Cut`（纯 GPIO）。
- `record_task.cpp` `stop()`：删 `HTC_SKIP_TEARDOWN_ON_SHUTDOWN` 分支 → 无条件 `releaseVideoResources`。

→ `HTC_SKIP_ISP_DAYNIGHT_ON_SHUTDOWN` + `HTC_SKIP_TEARDOWN_ON_SHUTDOWN` **两 kill-switch 全退役**（代码不再读）。record_smoke GREEN（70s，`[HAL] exit: teardown complete`，无 panic）。

## task-9：进程内重复录影（channel 释放）✅ 实现 + 单段无回归

**根因**：`RecordTask::recorder_` 跨 trigger 复用，两 trigger 间不释放 `video_recorder_` → CH0 encoder channel 不 Destroy → record#2 `IMP_Encoder_CreateChn(0) failed`。放大器：现有 `releaseVideoResources → ~VideoRecorder::uninitVideo → video_->exit()` 跑 `IMP_System_Exit` 全 teardown（违反"进程内不 System_Exit"）。

**修复**（3 处）：
- `VideoRecorder.cpp`：新增进程级单例 `sharedVideo()`（C++11 函数局部 static，首次 createVideo+init 一次）；`initVideo` 复用它；`uninitVideo` 改 `stream_.reset()`（channel 级 ~IngenicVideoStream: DestroyChn）+ **移除 `video_->exit()`**。
- `record_task.cpp` `trigger()`：开头 `recorder_->releaseVideoResources()`（主线程，channel 级，避 self-join）。

→ 进程内永不 `IMP_System_Exit`（static 持有 ref）→ record#2 `CreateChn(0)` 因 channel 已释放而成功。record_smoke GREEN（73s，单段无回归）。HTTP 路径（CameraServiceT32）同受益。

**进程内重复验证待 harness**：devtest 无 upload 后端 → EventLoop 录完#1 即 shutdown（upload-idle），#2 不发生；进程内重复只在生产有真后端时发生。需 EventLoop 测试钩子（如 `HTC_TEST_RECORD_COUNT=N`）才能在 devtest 验证。

## 关键硬件限制：跨进程 IMP wedge（无解）

**IMP 驱动不支持一 boot 内 2+ 个 IMP-using 进程**——第 2 个进程的 `IMP_System_Init` wedge 内核（需手动断电）。无论进程1 是否 `IMP_System_Exit`：
- task-8（exit）：进程A exit → 进程B init OK → 进程C wedge。
- task-9（不 exit）：进程A leak → 进程B init wedge（更早）。

→ **devtest 必须 1-wm-per-boot**（冷启间隔，devtest ADR 冷启纪律正是为此）。`test_wm_repeat.py`（同 boot 2x）已标 skip（测的是硬件做不到的事）。

## 文件改动

| 文件 | 改动 |
|---|---|
| `src/app/workmode_app.cpp` | cleanupHook：删 controlISP + stopAutoSwitch |
| `src/app/main_app.cpp` | cleanupHook：删 controlISP + stopAutoSwitch |
| `src/app/app_lifecycle/ProcessLifecycle.cpp` | auto_release：删 controlISP + stopAutoSwitch |
| `src/app/workmode/record_task.cpp` | stop() 删 HTC_SKIP_TEARDOWN 分支；trigger() 加 releaseVideoResources |
| `src/media/video/VideoRecorder.cpp` | sharedVideo() 单例 + uninitVideo channel 级（移除 video_->exit） |
| `tests/host/*` | record_smoke 去死 env；wm_repeat 标 skip（硬件限制） |

双平台编译 ✓（build + build_sim）。不碰 `src/hal/**`。

## 待办

- [ ] 进程内重复验证 harness（EventLoop `HTC_TEST_RECORD_COUNT=N` 测试钩子 + 冷启验证）。
- [ ] 跨进程 IMP wedge 限制写进 ADR（`devtest-automation-loop.md` §2.3 强化）。
- [ ] broker 日志碎片泄漏（40 列折行 marker）— 放宽 `MARKER_LINE_RE`。

## Authority

- analyst 全文：`artifacts/2026-06-21-record-teardown-crash-analyst-full.md` + `repeated-record-channel-leak-analyst-full.md`
- 验证：record_smoke GREEN（serial.log 有 record stats observed_fps + thumbnail + desc + `[HAL] exit: teardown complete`）
- 相关：`doc/knowledge/bugs/T32-imp-residue-workmode-record-2026-06-21.md`、[[project_t32_wm_um_stabilization_plan]] 决策 7
