# wm task slot scheduler

> 用途：定义 `wm` 多 task 场景的调度内核。该机制必须能在 PC simulation 中用 mock task 独立验证，再接入 T32 real task。
> 当前实现：`src/app/workmode/wm_task_scheduler.{h,cpp}`；仿真测试：`tests/test_wm_task_scheduler.cpp`。

## 1. Model

调度器由固定 task slot 组成。每个 slot 同一时间最多放一个 task。

| Slot | Task type | 当前用途 |
|------|-----------|----------|
| slot 1 | `TaskType::Capture` | 拍照、录影、拍照+录影，作为一个原子 capture task |
| slot 2 | `TaskType::Upload` | 上传 SD card 中的 desc/media |

`Shutdown` 是 special terminal flow，不放入普通 slot。进入 shutdown 后所有 slot lock，外部 trigger 不再接受。

每个 task 通过 `WmTask` 接口接入（纯虚接口，`timeoutAgeMs` 提供默认实现）：
- `type()` 返回 task 类型。
- `state()` 返回 `Ready` / `Running` / `Stopping` / `Done` / `Failed`。
- `start()` 非阻塞开始执行；成功返回只表示 task 已接受启动。
- `poll()` 推进或观察状态。
- `stop()` **阻塞契约**：返回时 task 必须已经停止并完成自己负责的 cleanup；调用方可在 `stop()` 返回后安全清理 task 关联资源。
- `timeoutAgeMs()` 可覆盖超时年龄；真实 upload 用自 `start()` 起算的年龄（含 connect grace，见 §3.4）。

`Stopping` 不是 terminal state。slot 只有在 task 进入 `Done` 或 `Failed` 后才移除。

### 1.1 SlotOutputPort：slot 间的唤醒信号通道

调度器（`WmScheduler` runtime wrapper）持有一个 `SlotOutputPort` 作为 slot 1 → slot 2 的**唤醒信号**通道（单生产者-单消费者，`mutex`+`condition_variable`+计数）：

- port **不携带数据**，只携带「capture 产了新活」的 wake token。真实 desc 路径由 upload task 自己扫 SD 获得（见下）。
- slot 1 task 收尾进入 `Done` 后，**由 wrapper**（在 `events.captureCompleted` 时）向 port push 一个 wake token（capture task 自身不感知 port、不直连任何 upload 对象）。
- upload task（type 2）在 SD 扫空后 `wait()` 在 port 上；被 wake 唤醒后重扫 SD。
- port push 发生在 `op=task_state type=1 ... to=Done` 之后；其唯一作用是让空闲等待中的 upload task 重扫，避免轮询目录。

**为什么是 signal 而非 data 通道**：upload 的真实数据源是 SD 上 `F_UploadedTag==0` 的 desc 文件（持久、跨 boot resume）。capture 产的新 desc 也落在同一目录，upload 扫描自然发现；故 port 若再携带路径就与 SD 扫描重复。port 退化为「有新活、唤醒重扫」的信号，单一真相源在 SD。`SlotOutputPort<T>` 仍是可复用的通用抽象——本 capture→upload pair 用 signal 形态；未来非文件系统 task pair 可用 data 形态（payload 真在内存、无 SD 可扫时）。

### 1.2 SlotBlockedReason：slot 阻塞可观察性

每个 slot 暴露只读 `blockedReason(type)`：

| 值 | 触发条件 |
|---|---|
| `None` | slot 正常运行（upload 正在上传 / capture 正在拍录）或空且无新触发 |
| `WaitingUpstream` | **type=2 slot 在 `Running`、upload 内部空闲（parked，最近一次扫描无 `F_UploadedTag==0`、无在途），但 slot 1 非空 或 port 有未消费 wake token**（capture 刚产活还没被 upload 扫到）→ upload 被占住等上游落定，不能 Done |
| `Idle` | slot 空且无新触发、未进入 shutdown |
| `Cleanup` | slot 正在 `stop()` 阻塞等待 task 清理完成 |
| `Locked` | shutdown 已触发，slot lock 拒绝新触发 |

> 2026-06-28 修订（实现期发现）：port 虽是 signal（不带数据），其**未消费 token 计数仍参与 Done 判定**——不是冗余。场景：upload 扫空后 parked，此时 capture2 产 desc2 并 push token；若 Done 只看 `drained`，会在 upload 还没重扫到 desc2 时误判 Done，把 desc2 丢到下次 boot（m1「拍完即传」破）。故 Done 必须同时满足 slot1 空 **且** upload parked **且** port 无未消费 token，三者同在 port mutex 下原子判定（线程消费 token 与置 busy 同锁，杜绝 TOCTOU）。

`blockedReason` 与 `[wm] op=slot_blocked` 日志 5 个值一一对应。

## 2. Slot Rules

1. 不同 type 的 task 必须进入不同 slot。
2. slot 非空时，同 type 新 task 被拒绝；外部 capture trigger 被忽略。
3. task 进入 `Done` 或 `Failed` 后，调度器从对应 slot 移除它。
4. 初始 task 进入 slot 后处于 ready/running 语义，由调度器启动。
5. `-m 0` / `-m 1` 启动时先放入并启动 slot 1。
6. `-m 2` 启动时直接放入并启动 slot 2。
7. `-m 1` 中，slot 1 首个 task 完成并移除后，触发 slot 2。
8. slot 2 运行时允许新的 slot 1 task；前提是 slot 1 为空。
9. slot 2 是否结束，必须检查 slot 1 是否为空。slot 1 非空时，upload task 即使暂时无工作也不能完成退出。
10. 所有 slot 为空并持续 `HTC_WM_IDLE_GRACE_MS` 后，调度器 lock 全部 slot 并进入 shutdown。
11. type 1 的 `Done` 必须表示 capture 操作、后处理、**desc 落盘**（写 desc 文件 `F_UploadedTag=0`）、资源释放都完成；不能只表示录影主循环停止。capture task 不再 enqueue 任何 upload 对象。
12. **port 衔接（signal）**：slot 1 收尾进入 `Done` 后，wrapper 向 `SlotOutputPort` push 一个 wake token（不携带路径）；upload task 被 wake 后自扫 SD 取真实 desc。port push 必须发生在 `op=task_state type=1 ... to=Done` 之后。slot 2 已在运行时，push 的作用是唤醒空闲等待中的 upload 重扫；slot 2 为空时由 scheduler 触发新建 upload task（其 `start()` 起即扫 SD）。
13. **阻塞可见性**：slot 2 处于 `Running`、upload 内部空闲（parked）但 slot 1 非空 或 port 有未消费 token 时，`blockedReason(Upload)` 必须返回 `WaitingUpstream`；仅当 slot 1 空、upload parked（扫描无 `F_UploadedTag==0`、无在途）、且 port 无未消费 token 时才能进入 `Done`（三者原子判定，见 §1.2 修订注）。

## 3. Mode Mapping

| Mode | Bootstrap | Follow-up |
|------|-----------|-----------|
| `-m 0` CAPTURE_ONLY | slot 1 capture | 无 slot 2；全空 idle-grace 后 shutdown |
| `-m 1` CAPTURE+UPLOAD | slot 1 capture | slot 1 完成后触发 slot 2；slot 2 运行中可接受新 capture |
| `-m 2` UPLOAD_ONLY | slot 2 upload | drain 完成后 idle-grace shutdown |

`HTC_WM_ONE_SHOT=1` 时，首个 capture task 启动后不再接受新的 slot 1 trigger。

## 4. PC Simulation Contract

调度器核心不得依赖 HAL、IMP、network、DB、`ProcessLifecycle` 或真实文件系统。PC simulation 单测使用 mock task 覆盖：
- m0 capture-only shutdown。
- m2 upload-only immediate start。
- m1 capture 完成触发 upload。
- capture 进入 `Stopping`/cleanup 时 upload 不能启动，直到 capture 进入 `Done`。
- slot 1 busy 时 trigger 被忽略。
- upload 运行中 slot 1 为空时可接受新 capture。
- slot 1 非空时 upload 不结束。
- upload timeout 进入 shutdown。
- shutdown lock 后 trigger 被拒绝。

验证命令：

```bash
cmake -S . -B build_sim -DBUILD_FOR_SIMULATION=ON
cmake --build build_sim --target test_wm_task_scheduler -j$(nproc)
./build_sim/bin/test_wm_task_scheduler
```

T32 real task 只能作为 slot scheduler 的适配器：`CaptureLane` 适配为 type 1 task（`CaptureTask`），新 wm 私有 `UploadTask` 适配为 type 2 task（**不复用、不修改** legacy 共享的 `UploadWorker`——后者仍服务于 `htc_workmode_app`）。`UploadTask` 自扫 SD 取 desc（真实文件系统访问只在此 adapter 层，不在 core）；lazy connect/auth + per-desc 上传逻辑移植自 `UploadWorker`。后续调度规则变更必须先更新本文件和 PC simulation 测试。

## 5. Runtime Log Contract

真机和 PC runtime 都必须输出结构化 wm 日志，便于确认 type 1/type 2 没有交叉：

- `[wm] op=slot_put type=<1|2> task_id=<id> state=Ready`
- `[wm] op=task_state type=<1|2> task_id=<id> from=<state> to=<state> reason=<reason>`
- `[wm] op=slot_remove type=<1|2> task_id=<id> final_state=<Done|Failed>`
- `[wm] op=slot_port_push type=1 port=upload count=1` —— wrapper 在 `events.captureCompleted` 时 push 的 wake token（signal；count 为唤醒计数，非 desc 数）
- `[wm] op=slot_trigger from=1 to=2 reason=capture_done`
- `[wm] op=trigger_accepted type=1 ...` / `[wm] op=trigger_ignored type=1 ...`
- `[wm] op=slot_blocked type=<1|2> reason=<None|WaitingUpstream|Idle|Cleanup|Locked>` —— slot 阻塞状态（与 `blockedReason(type)` 5 值一一对应）
- `[wm] op=slot_lock type=<1|2> ...`

`-m 1` 的关键验收序列必须是（首拍）：

```text
op=task_state type=1 ... to=Done
op=slot_remove type=1 ... final_state=Done
op=slot_trigger from=1 to=2 reason=capture_done
op=slot_put type=2 ...
op=task_state type=2 ... to=Running
op=slot_port_push type=1 port=upload count=1   # wake token（后续拍唤醒已运行的 upload 用）
```

若出现 `type=2 ... to=Running` 早于 `type=1 ... to=Done`，说明 type 1 cleanup 尚未完成就启动了 upload，违反本设计。
若 `blockedReason(Upload) == WaitingUpstream` 时 upload 内部非空闲或 slot 1 为空，或反之，说明阻塞状态机与 slot/upload 实际状态不一致，违反规则 13（signal 化后此判定与 port 状态无关）。

`CaptureLane::runOnceBlocking()` 还会输出：

```text
op=capture_run_once phase=begin
op=capture_run_once phase=trigger_result ok=<0|1>
op=capture_run_once phase=cleanup_begin interrupted=<0|1>
op=capture_run_once phase=cleanup_end result=<done|interrupted>
```

这些日志用于确认 type 1 的 `Done` 已覆盖 capture、desc 落盘、以及资源 cleanup（upload 不在此列——由 type 2 `UploadTask` 扫 SD 自取）。

## 6. Runtime Simulation Smoke

除纯调度单测外，PC runtime 可用真实 `wm` 适配层做 smoke。simulation 下 wm media/upload 路径映射到 `SIM_SD_ROOT`（默认 `sim_sdcard_runtime`），T32 仍使用 `/mnt/huntcam`。

默认 `res/setting.json` 的 `stillSize=2` 会走 sim 未实现的 large-image path；runtime smoke 可用临时 setting 把 `stillSize` 降到 4M：

```bash
cp res/setting.json /tmp/wm_setting_4m.json
perl -0pi -e 's/"stillSize"\s*:\s*2/"stillSize" : 1/' /tmp/wm_setting_4m.json
SETTING_FILE_PATH=/tmp/wm_setting_4m.json \
HTC_TEST_NO_POWEROFF=1 \
HTC_WM_IDLE_GRACE_MS=100 \
HTC_UPLOAD_TIMEOUT_MS=3000 \
HTC_WM_ONE_SHOT=1 \
./build_sim/bin/wm -m 1
```

没有 mgmt/storage server 时，upload 会失败或超时并保留 desc，这是 devtest 预期；该 smoke 的目标是验证调度和日志顺序，不验证上传成功。
