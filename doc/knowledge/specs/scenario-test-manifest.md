# 场景测试清单（scenario-test-manifest）

> 用途：把 Phase-1「单功能稳定化 → Phase-2 wm/um 组合」所需的全部场景测试程序，落成一份可勾选的工作清单。
> 供换环境（同代码）实现未完成功能时直接照推。由 2026-06-22 会话整理，依据
> [`phase1-module-stabilization-plan.md`](phase1-module-stabilization-plan.md) §6 交付物 + 现有测试代码事实。
>
> **真相源**：本文为索引；每个场景的契约/锚点/HW 状态，以代码 + 最新 commit 说明 + 最新 review 为准。
> HW 状态认**最新**提交说明 / 最新 review，不要认更早的中间快照（phase1.md 是冷启前快照，
> 7a5a1a9 冷启后 record 全绿）。

## 0. 结构：两层 × 11 模块 × wm/um 组合

- **L1 sim**（`tests/*.cpp`，sdk_stub）：功能正确性；crash 类永远跑不出。
- **L2 真机**（`tests/host/*.py`，经 devctl 串口）：crash 类唯一仪器层（决策 2）。
- **wm（一次性）** = {snap, record, upload, ntp}；**um（长驻）** = {rtsp, http控制+回放, mdns}；
  共享库 = {mcu, thumbnail/DB, manifest}；前置 = network（`htc_net_app`）。
- **硬约束**：
  - **1-wm-per-boot** —— IMP 驱动不支持一 boot 内 ≥2 个 IMP 进程，第 2 个 `IMP_System_Init`
    wedge 内核（需手动断电）。无论进程 1 是否 `IMP_System_Exit`，跨进程重复无解
    （`test_wm_repeat.py` 已永久 skip，测的是硬件做不到的事）。
  - 关机 teardown 两 kill-switch（`HTC_SKIP_ISP_DAYNIGHT_ON_SHUTDOWN` /
    `HTC_SKIP_TEARDOWN_ON_SHUTDOWN`）已于 2026-06-21 **退役**，代码不再读（task-8）。
  - 进程内永不 `IMP_System_Exit`（`sharedVideo()` 单例 + channel 级释放，task-9）。

## 1. 判决 helper API（新场景照此接）

通用判决 `tests/host/scenario_verdict.py`：

```python
from scenario_verdict import judge, VerdictSpec
v = judge(output, rc, timed_out, VerdictSpec(
    name="场景名",
    required_anchors=(r"必须出现的正则",),   # 全部命中才过
    metric_re=r"...=(\d+(?:\.\d+)?)",        # 可选，提取数值指标
    benign_errors=(r"容忍的 E/ 行正则",),     # devtest 无后端噪声
    rc_ok=(0,),                              # 接受的退出码（137/timeout 永不过）
    run_no=1))
# 返回 {"ok": bool, "reasons": [str], "metric": float|None}
```

record 专属 facade `tests/host/wm_verdict.py`：
- `check_run(output, rc, timed_out, run_no)` —— 单发；断 `record start:` + `observed_fps` +
  `thumbnail saved` + 仅 benign E/。
- `check_repeat_run(output, rc, timed_out, expected_count, run_no)` —— 进程内多段；断
  ≥N 个 `record start:` 且 **无** `IMP_Encoder_CreateChn`。

`device` fixture（`conftest.py`，session 级）：broker/设备不可达自动 skip；`pytest_sessionfinish`
写 `logs/devtest_report.json`（per-test outcome + duration + counts + iteration_cap=3）。

devctl 现有子命令：`run` / `log` / `status` / `send`（原始字节）/ **`ctrl-c`** / `reboot` /
`wait-boot` / `bringup` / `broker`。→ 长驻 um 的 probe+ctrl-c teardown **无需新加命令**
（`send`+`ctrl-c` 已具备）。

## 2. 模式语义（实现前必读，别照 ADR 表猜）

`-wm N` 的实际语义以 **`src/app/workmode/WorkModeRunner.cpp:865 workModeToCommand`**
的 switch + `workmode_app` 的 EventLoop 分发为准。switch case 见：
`SNAP_ONLY`(:869) / `UPLOAD_ONLY`(:872) / `TEST_ONLY`(:878) / `SNAP_UPLOAD`(:886) / `UVC`(:889)。

> ⚠️ [`decisions/workmode-usermode-process-split.md`](../decisions/workmode-usermode-process-split.md) §3
> 的 `wm0=SNAP_ONLY …` 表是**目标态/旧映射**，与当前代码不一致：**`-wm 0` 现实测 = PIR EventLoop
> 录影循环**（commit 06a130a 引入），不是 snap-only。两者不要混。

## 3. 已有的场景测试

### 3.1 L2 真机（`tests/host/`）

| 文件 | 模块 | 契约 | 驱动 | HW 状态 |
|------|------|------|------|--------|
| `test_record_smoke.py` | 2 record | loop-faithful | `-wm 0` 单段 | ✅ GREEN（含 thumbnail 锚点） |
| `test_record_repeat.py` | 2 record | loop-faithful | `-wm 0` `HTC_TEST_RECORD_COUNT=3` 进程内 3 段 | ✅ GREEN（3×900帧~28fps，零 CreateChn） |
| `test_net_smoke.py` | 11 network | single-shot | wlan0 IP + `htc_net_app --no-dhcp` REUSE | ✅ GREEN |
| `test_wm_modes_matrix.py` | wm 一次性组合 | crash/hang 探测 | `mode{0,1,2}×rtc{0,1}`，仅 rc/timeout | 🟡 代码就绪，**HW 未逐 case**（1 boot 1 case） |
| `test_wm_repeat.py` | 跨进程 | — | 同 boot 2× `-wm 0` | ⏭️ **永久 skip**（IMP 跨进程 wedge=硬件限制） |

### 3.2 L1 sim（`tests/*.cpp`，sdk_stub）

`test_database`（模块6 thumbnail/DB）、`test_mdns_model_normalization` + `test_mdns_txt_record`
（模块8）、`test_minimp4_fragmented_mux`（模块9 fMP4 mux）、`test_mcu_service`（模块10）、
`test_net_app_logic`（模块11）、`test_tcp_event_service`、`test_camera_properties` +
`test_camera_service`（模块7，**部分**）。

### 3.3 L2 二进制

`snap_test`（`src/app/snap_test.cpp`）—— **48M 可行性工具，非生产 `quick_snap` 路径，不计入覆盖**。

---

## 4. 待实现（按优先级）

### 🔴 P0 — Phase-1 核心交付（crash-prone）

- [x] **B1. 生产 snap 路径场景** — `tests/host/test_snap_smoke.py`（✅ snap_test 已可独立跑通）。
  先做 snap feature 统一（F1 可选缩略图 / F2 >8M→strip / F3 >8M 连拍 temp-buffer），再
  **root-cause 修复两个让 snap_test 在 fresh boot 上跑不起来的 bug**（受控实验确认，见
  [`reviews/2026-06-22-snap-unification.md`](../../reviews/2026-06-22-snap-unification.md)）：
  #1 fresh-boot 时钟 1970(<FAT 纪元)→ fopen kernel oops（fix: snap_test 启动 `ensurePlausibleClock`）；
  #2 framesource CH0 配成高于 sensor 的分辨率 → corrupt → polling segfault（fix: 目标 > sensor
  一律走 sensor 原生 + 软件缩放，不再只限 >8M）。验证：fresh boot 不手动设时钟、不 priming，
  `snap 3840 2160`(软件缩放 813KB) 与 `snap 2560 1440`(HW 原生 71KB) 均独立 GREEN。
  **2026-06-23**：全 5-case 矩阵 1-boot-1-case 跑完(4m/8m/16m single + 8m-burst 全 GREEN)；8m-burst
  发现 flaky OOM(5.5MB 全帧 nv12 向量 vs 32MB 预算,余量仅 ~1.7MB)→ 修 `snapLargeFromFile`(按 strip 从
  临时文件 fseek+fread,不载全帧;余量→~7MB,快 40%)。见
  [`reviews/2026-06-23-snap-burst-oom-fix.md`](../../reviews/2026-06-23-snap-burst-oom-fix.md)。
  ≤sensor 连拍(snap_internal 循环)仍待复测。）
  - 模块 1（loop-faithful，CH0 主 + CH2 缩略图）。
  - 生产 snap 源码：`src/app/workmode/WorkModeRunner.cpp:94 processCmdSnap`（→ `ImageSnap`
    :157-158）/ `:287 processCmdConcurrentSnapRecord`（并发拍录）；boot launcher 侧
    `src/app/media_app.cpp:94 quick_snap`。
  - ⚠️ **实现前先确认**：触发生产 snap 的 CLI/env 入口（`CMD_SNAP` 位图对应哪条 `-wm`/命令；
    `snap_test` 不是它）。在 serial.log 抓到真实成功行再写锚点正则。
  - 断言锚点：`thumbnail saved`（CH2 concurrentSnap）+ snap 成功行 + rc 0；benign 同 record
    （`UploadWorker: auth failed|no storage client`）。

- [x] **B2. upload 独立断言** — `tests/host/test_upload_smoke.py`（✅ HW GREEN 2026-06-22）
  - 模块 4（single-shot）。worker 已抽：`src/app/workmode/upload_worker.cpp`
    （`start`/`enqueue`/`isIdle`/`flush`；auth failed log :45）。
  - ⚠️ **硬约束**：devtest **无 mgmt/storage 后端** → 上传必失败。本场景只能断「upload worker
    启动 + 干净失败 + 无 crash/无 hang」，**不能断上传成功**。benign_errors 必含
    `UploadWorker: auth failed|no storage client`。真上传需 mock 后端（留 Phase-2）。
  - 形态参考 `-wm 2`（UPLOAD_ONLY）单发，或直接驱动 worker。

### 🟠 P1 — 长驻 um（probe + ctrl-c teardown，devctl `send`/`ctrl-c` 已具备）

- [x] **B3. wm3 mobile 长驻** — `tests/host/test_um_mobile_probe.py`（✅ HW GREEN 2026-06-22）
  - 模块 8+7+5（mdns+http+rtsp，usermode）。`wm3 = TEST_ONLY`（workModeToCommand :878）。
  - 形态：后台起 `-wm 3` → sleep + 探测（mDNS 广播 / HTTP 端口 / RTSP 端口 reachable）
    → `devctl ctrl-c` → 断**干净 teardown**（rc 非 137、无 panic、无 hang）。
  - 后台 spawn idiom 待定（如 `devctl run 'nohup <app> & echo $!'` 拿 PID，或 `send` 注入）。

- [x] **B4. wm4 rtsp-server 长驻** — `tests/host/test_um_rtsp_probe.py`（✅ HW GREEN 2026-06-22）
  - 模块 5（rtsp，CH1）。`wm4 = UVC`（workModeToCommand :889）。
  - 形态同 B3，探 RTSP 流；顺带验 audio on/off 子参数（`--no-audio`→`HTC_NO_AUDIO`）。

### 🟡 P2 — L1 sim 补全（低风险，可穿插）

- [x] **B5. ntp 单测** — `tests/test_ntp.cpp`（✅ sim GREEN 2026-06-22）
  - 模块 3。验 `app_lifecycle::syncSystemTime` + `RTC::setTime` 逻辑（sim 层 stub RTC）。
- [x] **B6. http 控制 + 回放补全** — `tests/test_http_api.cpp`（✅ sim GREEN 2026-06-22）
  - 模块 7。现有 `test_camera_properties`/`test_camera_service` 只覆盖属性；**控制端点 +
    渐进式回放（playback_token）未单测**（`test_minimp4_fragmented_mux` 只验 mux 不验端点）。

### 🟢 P3 — WM 组合矩阵功能锚点（crash 探测之上加功能正确性）

- [~] **B7. wm_modes_matrix 功能锚点** — 升级 `test_wm_modes_matrix.py`（wm2-rtc1
  ✅ HW GREEN；per-mode verdict 已落地。wm0 record 由 `test_record_smoke` 证明 GREEN，
  wm1 锚点已抓取；wm0/wm1 的矩阵 pytest-green 待 1-boot-1-case 冷启逐项跑。）
  - 现仅 crash/hang 探测。每个 one-shot 模式（0/1/2）抓真机成功日志后，补 mode-specific 成功锚点
    （而非现在的 `benign_errors=(r".*",)` 全放行）。

---

## 5. 建议实现顺序

1. **B1 snap** + **B2 upload** —— record 已绿，补齐后 wm 一次性组合的 4 功能（snap/record/upload/ntp）就齐 3 个。
2. **B3/B4 长驻 um** —— 先敲定后台 spawn + ctrl-c teardown idiom，再写 probe。
3. **B5/B6 sim 补全** —— 低风险，穿插做。
4. **B7** —— WM 矩阵从 crash 探测升级到功能正确性。

HW 验证纪律：**每个 record/snap/wm 跑都 1-boot-1-case**（冷启间隔）；先 `verify_deploy.sh`
确认 host md5 == 设备 md5 再跑。

## 6. 踩坑提醒

- **HW 状态认最新提交说明 / 最新 review**，不认更早中间快照。
- **模式语义以 `workModeToCommand`(:865) 代码为准**，ADR §3 表是目标态/旧映射。
- **新锚点先在 serial.log 抓真实格式再写正则**（Phase-0 假锚点教训：`record started: file=`
  实际是 `record start:`；fps 锚点 `observed_fps` 真，VideoRecorder.cpp:686/909）。
- 长驻进程 teardown 是 wm/um 稳定化的真正考验（关机并发竞态曾导致 defog ISR panic）；
  B3/B4 的 ctrl-c 干净退出是该判据的真机验证入口。

## 7. 关联文档

- [`phase1-module-stabilization-plan.md`](phase1-module-stabilization-plan.md) — 11 模块 + 交付物（本文的来源）
- [`../decisions/workmode-usermode-process-split.md`](../decisions/workmode-usermode-process-split.md) — wm/um 拆分 ADR
- [`../decisions/devtest-automation-loop.md`](../decisions/devtest-automation-loop.md) — devtest 闭环架构
- [`../../reviews/2026-06-21-record-teardown-stabilization.md`](../../reviews/2026-06-21-record-teardown-stabilization.md) — task-8/9 修复 + record HW 绿
- [`../../reviews/2026-06-21-devtest-phase1.md`](../../reviews/2026-06-21-devtest-phase1.md) — Phase-1 矩阵（冷启前快照，HW 状态已被 7a5a1a9 更新）
