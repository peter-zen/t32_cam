# t32_cam 知识治理待办

## Now
- [x] 建立 `doc/knowledge/` 基础骨架
- [x] 建立 `README.md / overview.md / working-set.md`
- [x] 建立首份历史文档映射评审记录
- [x] 完成 RTSP 主题首批迁移样例
- [x] 完成 HTTP API 主题 spec / decision / refs / playbook 样例
- [x] 完成 mDNS 主题 spec / decision / refs / playbook 样例
- [x] 完成 TCP Event 主题 spec / decision / refs / playbook 样例
- [x] 完成 WorkMode 主题 spec / decision / refs / playbook 样例
- [x] 完成 Device / Storage 主题 spec / decision / refs / playbook 样例
- [x] 完成 event_port 暴露链路主题 spec / decision / refs / playbook 样例
- [ ] 选择下一条高价值主题继续迁移或进入真机校准阶段

## Next
- [ ] 梳理 `doc/analysis/` 中哪些应沉淀为 `bugs/` 或 `reviews/`
- [ ] 梳理 `doc/design/` / `doc/solution/` 中哪些应沉淀为 `specs/` 或 `decisions/`
- [ ] 梳理 `doc/reference/` 中哪些应沉淀为 `refs/` 或 `playbooks/`
- [ ] 为当前活跃开发主题补充一份任务级 working note
- [ ] C4 repoint 前执行 `-wm 3/4` 从 workmode 迁往 usermode（见 `doc/knowledge/decisions/workmode-usermode-process-split.md §7`，本次只落决策未动代码）

## Later
- [ ] 对过时历史文档建立 redirect / superseded 说明
- [ ] 将高复用调试流程沉淀为 `playbooks/`
- [ ] 把跨项目稳定流程再提炼成 Hermes skill

## Blocked
- [ ] 历史文档是否要大规模重命名/搬迁，需结合团队协作成本再决定

---

## DevTest 自动化闭环（架构见 [`decisions/devtest-automation-loop.md`](decisions/devtest-automation-loop.md)）

> 目标：把"串口手敲→人眼读日志→存 log→对照代码"全人工链路，自动化成 WSL 上 Claude 驱动的可审计闭环。Phase-0 为 tracer bullet，在活跃的 wm/um 稳定化问题上打通全链路。

### Phase 0 — MVP / tracer bullet（= Level 1：build/部署/跑/抓/呈现 + 单用例确定性判决）

依赖顺序：P0-1 与 P0-2/P0-4 可并行；P0-3 依赖 P0-2；P0-6 依赖 P0-1/P0-3/P0-4；P0-7 依赖 P0-6。

- [x] **P0-1 `HTC_TEST_NO_POWEROFF` 标志**：`src/app/main_app.cpp:384` 与 `workmode_app` 末尾，`Misc::poweroff()` 前加 `if (getenv("HTC_TEST_NO_POWEROFF")) _exit(0);`；env 守护、不影响产线；双平台编译过。
  - done：HW 上 `HTC_TEST_NO_POWEROFF=1` 跑完 app 后**回 shell、不断电**。
- [x] **P0-2 串口 broker**：`tools/devctl/broker.py`，独占 `/dev/ttyUSB0`（`115200 8N1 raw`），常驻 tee→`logs/serial.log`（过滤 sentinel 行），sentinel 界定 + 退出码捕获。
  - done：后台常驻，持续落盘串口字节；能抓到自发 reboot 的启动日志。
- [x] **P0-3 devctl CLI（run/log/status）**：`devctl run '<cmd>' [--timeout N]` 注入 `<cmd>; echo __DEVCTL_DONE_$?__` 界定返回 stdout+rc；`devctl log tail [-n N]`；`devctl status`。
  - done：`devctl run 'uname -a'` 可靠返回输出与 rc=0；超时能判 hung。
- [x] **P0-4 NFS `noac` 修复**：`script/mount_nfs.sh` 改 `-o noac,nolock,vers=3`。
  - done：重编后设备立即见新二进制（host md5 == 设备 md5，无需 remount）。
- [x] **P0-5 md5 部署校验脚本**：固化 `.claude/CLAUDE.md` "Verifying the build" 三步（host md5 == 设备 `/mnt/huntcam/bin` md5 == `/proc/$PID/maps` 实际加载）。
  - done：一条命令给出三方 md5 一致/不一致判定。
- [x] **P0-6 tracer bullet 测试**：`tests/host/conftest.py`（device fixture）+ `test_wm_repeat.py`，经 devctl 在**同一 boot 内**连跑两次 `htc_workmode_app -wm 0 -rtc 1`，断言：两轮都有 `record started`+`record stats ... observed_fps`、无 `E/` 错误行、**第 2 轮 configure() 不挂（超时内 exit 0 而非 rc=137）**。
  - done：`pytest tests/host/test_wm_repeat.py` 给出确定性 PASS/FAIL + JSON report；该用例直接验证 wm/um 稳定化的"单进程可重复"。
- [x] **P0-7 `/devtest` skill 骨架**：`.claude/skills/devtest/SKILL.md`，串起 build→deploy→devctl→pytest→判决→诊断呈现（Phase-0 仅 Level 1：呈现给人，**不自动改码**）。
  - done：`/devtest wm-repeat` 一键跑通并产出判决 + 日志摘要。

### Phase 1 — Level 2（自动判 pass/fail + 提议修复，人批准）

> 状态：**代码完成，HW 验证待设备冷启**（record_smoke 跑出 initVideo 内核 unaligned access → 设备 wedge，需手动断电；见 reviews/2026-06-21-devtest-phase1.md）。

- [x] pytest 全套：`scenario_verdict.judge`(通用) + `test_net_smoke`(wlan0 IP + net_app REUSE，HW✓) + `test_record_smoke`(单发 -wm0) + `test_wm_modes_matrix`(@parametrize mode{0,1,2}×rtc{0,1}，crash/hang 探测)。修了 Phase-0 的假锚点（`record started: file=`→真实 `record start:`；fps 锚点本就真，VideoRecorder.cpp:686/909）。
- [x] Claude 诊断 + 修复提议流：`/devtest` SKILL 落 Level-2 五步闭环(capture→diagnose→propose→approve→apply+rerun) + 3 迭代上限 + 已知 crash 签名表。
- [x] JSON report 汇总 + 迭代/重试上限：conftest `pytest_sessionfinish` 写 `logs/devtest_report.json`(per-test outcome+duration+counts+iteration_cap=3)。
- [ ] HW 验证余项（待冷启）：record_smoke 干净 IMP 单发、wm-modes-matrix 逐 case（IMP 残留→一 boot 一 case）。长驻模式 3/4 的 probe+ctrl-c teardown 待写。
- [ ] broker 日志碎片泄漏（40 列折行劈断 marker，`_tee` 逐行过滤漏碎片）——放宽 `MARKER_LINE_RE` 捕 `__S_|__E_|%d__`。

### Phase 2 — orchestrator 接线
- [ ] `flows/*.yaml` 的 tester/analyst/reviewer 节点改为调 `/devtest`；`/bug` `/feature` 自动带真机验证

### Phase 3 — Level 3（无人值守自治）
- [ ] 网络继电器 wedge 救援 + 冷复位清 IMP；`devctl power-cycle` 落地
- [ ] 恢复已知良好态（主机回退 `build/` + 设备冷启动校验 md5）
- [ ] 内环无人跑通（带预算 + 熔断）
