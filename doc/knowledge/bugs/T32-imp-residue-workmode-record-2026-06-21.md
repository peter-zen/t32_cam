# T32 IMP/编码器残留 · workmode 连续录影（tisp_awb_init oops + IMP_Encoder_CreateChn 失败）

**日期**：2026-06-21
**作者**：zengping（+ Claude 协作）
**状态**：已定位，待修（喂给 wm/um 稳定化 phase1-module-stabilization）
**目的**：记录 devtest tracer bullet 真机挖出的两个 IMP 资源残留表现，区分"瞬态脏状态"与"确定性 teardown 缺陷"

## TL;DR

> 经 devtest 自动闭环（broker + devctl + 主机 pytest 判决）在真机连跑 `htc_workmode_app -wm 0 -rtc 1`，挖出**两个同属 IMP 资源残留类**的问题，严重度不同：
> - **#1 `tisp_awb_init` 内核 oops**（首跑、脏状态下）→ **冷启动清掉** → 瞬态残留，**非确定性驱动 bug**。
> - **#2 `IMP_Encoder_CreateChn(0) failed`**（同一次 run 内连续录影）→ 冷启动后**仍复现** → 确定性 **encoder teardown 缺陷**（编码器通道在第 1 段录影后未释放）。

| # | 表现 | 冷启动后是否复现 | 性质 | 判决 |
|---|------|------------------|------|------|
| 1 | `tisp_awb_init` oops, BadVA 00006000（首跑） | ❌ 清掉 | 瞬态 ISP/传感器脏残留 | timeout（app 卡死） |
| 2 | `IMP_Encoder_CreateChn(0) failed`（连续录影第 2 段） | ✅ 复现 | 确定性 encoder 通道未释放 | run FAIL（非良性 E/） |

## Finding 1 — `tisp_awb_init` 内核 oops（瞬态，冷启动清掉）

- **触发**：开机后第一次跑 `-wm 0`（设备此前累积了未清理的 ISP/传感器状态）。
- **现象**：`RecordTask: record start` → 视频/ISP 初始化时内核空指针解引用：

```
[ 8324.229857] CPU 0 Unable to handle kernel paging request at virtual address 00006000
epc : 80218d58 tisp_awb_init+0x138/0x444
ra  : 80218d34 tisp_awb_init+0x114/0x444
BadVA : 00006000
Call Trace:
 tisp_awb_init+0x138/0x444 → lib_tisp_init → ispcore_core_ops_init
 → subdev_sensor_ops_set_input（sensor input setup）
```

- **后果**：app 进程卡在不可中断 D 态，shell 阻塞在 `wait()`，串口 Ctrl-C 无效 → 需物理断电重启（devtest wedge 场景，正是 Phase-3 网络继电器的理由）。
- **关键判定**：**冷启动（物理断电再上电）后重跑，不再 oops** → 是开机累积的 ISP/传感器脏残留，**非确定性驱动 bug**。印证 T2/T3 的 IMP 残留类问题，这次表现为 kernel oops。

## Finding 2 — `IMP_Encoder_CreateChn(0) failed`（确定性，冷启动后仍复现）

- **触发**：同一次 `-wm 0` run 内，PIR 多次触发 → 第 1 段录影完成后，第 2 段录影创建编码器通道。
- **现象**（冷启动干净状态下的 run #1）：

```
E/LEGACY [HAL] configure: IMP_Encoder_CreateChn(0) failed
E/LEGACY initialize: stream configure failed
E/LEGACY VideoRecorder is not initialized
E/CamRec  record: VideoRecorder::record() start failed for ...mp4
E/LEGACY RecordTask: record start failed (rejected)
```

- **后果**：第 2 段录影被拒（app 优雅处理，不崩），EventLoop 随后正常关机、`exit 0`。
- **关键判定**：冷启动后**仍复现** → 第 1 段录影的编码器通道未释放，第 2 段 `CreateChn` 失败。**确定性 encoder teardown 缺陷**——正是 wm/um 稳定化（phase1-module-stabilization）的目标。
- **附带信号**：run #2 的 `observed_fps`（25.77）低于 run #1（27.92），且 run #2 有 `max_delta_ms=3285`（3.3s 停顿）——二次 ISP/encoder 重初始化更慢，但能恢复完成。

## 复现 / 数据源

| 项 | 值 |
|----|----|
| 设备 | T32 MIPS（`Linux Zeratul 4.4.94-Archon ... mips`） |
| 二进制 | `htc_workmode_app -wm 0 -rtc 1`（NFS `/mnt/huntcam/bin/`，md5 `794cb0ab...`） |
| env | `HTC_TEST_NO_POWEROFF=1`（app `_exit(0)` 回 shell，同 boot 重跑）、`HTC_SIM_PIR_INTERVAL_MS=3000` |
| 采集 | 经 devtest broker 抓串口；原始日志 `logs/tracer_run1_cold.txt`、`logs/tracer_run2_cold.txt`、`logs/serial.log` |
| 判决 | `tests/host/wm_verdict.py`（run1 FAIL / run2 PASS；容忍良性的 `UploadWorker: auth failed`） |

复现命令（devtest 闭环）：
```
HTC_WIFI_PWD=... tools/devctl/devctl bringup
tools/devctl/devctl run 'HTC_TEST_NO_POWEROFF=1 HTC_SIM_PIR_INTERVAL_MS=3000 \
  LD_LIBRARY_PATH=/mnt/huntcam/lib:$LD_LIBRARY_PATH \
  /mnt/huntcam/bin/htc_workmode_app -wm 0 -rtc 1' --timeout 150
```

## 修复方向（待 wm/um 稳定化承接）

- **#2（确定性）**：`RecordTask`/`VideoRecorder` 第 1 段录影结束后必须释放 encoder channel（`IMP_Encoder_DestroyChn` + 相关 group/bind 反操作，顺序遵循 `imp_system.h` 先 Disable FS 再 UnBind）。与 T3 的 `IngenicVideo::exit()` 销毁顺序缺陷同源。
- **#1（瞬态）**：根因是脏状态未被冷启动以外的手段清除。若 #2 的干净 teardown 落地，同 boot 重跑不再累积脏状态，#1 的触发概率应大幅下降；仍需关注首次 ISP 初始化在脏状态下的鲁棒性。

## 关联

- 稳定化计划：[`../specs/phase1-module-stabilization-plan.md`](../specs/phase1-module-stabilization-plan.md)、memory `project_t32_wm_um_stabilization_plan`
- 同类 IMP 残留根因（T2/T3）：`doc/knowledge/bugs/T32-*-2026-06-1*`、[`../playbooks/t32-memory-profile-and-debug.md`](../playbooks/t32-memory-profile-and-debug.md)
- 挖出本 bug 的闭环：[`../decisions/devtest-automation-loop.md`](../decisions/devtest-automation-loop.md)、[`../../../reviews/2026-06-21-devtest-phase0-hw-validation.md`](../../../reviews/2026-06-21-devtest-phase0-hw-validation.md)
