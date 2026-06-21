# 2026-06-21 — devtest Phase-1（Level 2）实现 + 部分真机验证

## What

把 `/devtest` 从单场景(wm-repeat)扩成 Phase-1：通用判决 + 多场景 pytest 矩阵 + JSON report + Level-2 诊断/修复流。

## Done（代码完成）

- **通用判决** `tests/host/scenario_verdict.py`：`VerdictSpec` + `judge()`（rc/timeout/锚点/metric/E行过滤共享），`wm_verdict` 改为复用 helper、保留 record 专属措辞。新 `test_scenario_verdict.py`（8 case）。
- **修了 Phase-0 假锚点**：`wm_verdict` 原找 `record started: file=`，源码实际是 `record start:`（record_task.cpp:76 / WorkModeRunner.cpp:229）。fps 锚点(`record stats/summary observed_fps`)本就真(VideoRecorder.cpp:686/909)，保留。`test_verdict` fixture 同步。18/18 单测绿。
- **新场景**：`test_net_smoke`(wlan0 IPv4 + `htc_net_app` REUSE 干净退出)、`test_record_smoke`(单发 -wm0)、`test_wm_modes_matrix`(@parametrize mode{0,1,2}×rtc{0,1}，纯 crash/hang 探测，不造锚点)。
- **JSON report**：conftest `pytest_sessionfinish` → `logs/devtest_report.json`(per-test outcome+duration+counts+iteration_cap=3)。
- **SKILL 扩展**：场景表 + Level-2 五步闭环(capture→diagnose→propose→approve→apply+rerun) + 3 迭代上限 + 已知 crash 签名 + 冷启动/wedge/busybox guardrail。

## 真机验证（部分）

- **net_smoke：HW ✓**（wlan0 IP `192.168.31.133` + NFS 上的 `htc_net_app` 干净加载 REUSE 退出）。新场景 infra 在非-record 功能上端到端跑通。
- **record_smoke：HW ✗ → 挖出新 crash + 设备 wedge**。`-wm 0` 在 `initVideo`（紧跟 GC4653 `VTS corrected to 0x0690`）触发**内核 `Unhandled kernel unaligned access`**（`do_ade`/`ExcCode 04`/`BadVA 00032bdf`/PID 0 中断上下文），设备硬挂（ctrl-c 不救）。

## Findings（喂稳定化 / 待处理）

1. **新 crash 签名**：`initVideo` 后内核未对齐访问。uptime `[3444s]`≈57min=非冷启、IMP 脏，吻合 memory 记的 `tisp_awb_init`“首跑脏状态、冷启动清掉=瞬态”。区别于 `IMP_Encoder_CreateChn`（连续录影第2段、确定性、冷启不清）。**待冷启后重跑 record_smoke** 验证单发是否过 + 抓真实 record 成功日志确认 fps 锚点。
2. **设备 wedge = 阻塞**：硬挂需**手动断电**（串口软重启救不回；网络继电器是 Phase-3）。所有 record-heavy HW 验证待用户冷启。
3. **broker 日志碎片泄漏**（cosmetic，未修）：注入命令 40 列折行劈断 marker，`_tee` 逐行过滤漏 `echo __S_`/`d9_%d__` 碎片，污染 `serial.log`/`devctl log`。不影响 `run()`。建议放宽 `MARKER_LINE_RE`。

## Not done（待冷启 / 后续）

- record_smoke 干净 IMP 单发验证；wm-modes-matrix 逐 case（IMP 残留→一 boot 一 case）。
- 长驻模式 3(mobile)/4(rtsp) 的 probe+ctrl-c teardown 场景未写。
- broker 碎片过滤修复。

## Authority

- 架构 ADR：`doc/knowledge/decisions/devtest-automation-loop.md`
- Phase-1 todo：`doc/knowledge/todo.md` “DevTest 自动化闭环 §Phase 1”
- 测试：`tests/host/{scenario_verdict,wm_verdict,test_*}.py`
- skill：`.claude/skills/devtest/SKILL.md`
