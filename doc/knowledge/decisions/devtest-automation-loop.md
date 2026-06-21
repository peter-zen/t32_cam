# 基于 Claude Code 的 T32 开发-测试-修复自动闭环（devtest-automation-loop）

## 1. 决策主题

把当前全人工的 T32 开发测试链路：

```
改码 → 编译 → NFS 部署 → 串口工具手敲命令跑 app → 人眼读串口日志判成败 → 存 logs/ → 对照代码分析
```

替换为一条**可自动化、可审计、带安全护栏**的闭环，由 Claude Code CLI 在 WSL 主机上驱动。本 ADR 记录架构决策与分阶段路线；**本文不含代码改动**，落地由后续 Phase 推进（Phase-0 拆解见 [`../todo.md`](../todo.md)）。

## 2. 决策背景

### 2.1 现状与痛点

- 部署：编译产物经 NFS，设备挂 `192.168.0.210` 的 `build/` 到 `/mnt/huntcam`（见 [`script/mount_nfs.sh`](../../../script/mount_nfs.sh)）。⚠️ 该脚本用 `mount -o nolock`，**缺 `noac`**——这是"重编后设备仍跑旧码"的已知坑源。
- 执行与判定：**人在串口工具手敲命令**，**人眼读串口日志**判成败。
- 分析：必要时把串口输出存进 `logs/`（如 `logs/debug.log`、`logs/fps.log`），再对照代码。

### 2.2 仓库已有的半成品（应复用，非另造）

- `script/` 下 ~25 个测试脚本，其中 [`regress_net_real.sh`](../../../script/regress_net_real.sh) 已有 `ok()/fail()/known()` helper + `[PASS]/[FAIL]/[KNOWN]` + `RESULT: PASS=N FAIL=N` + 退出码的**确定性判决约定**，但脚本注释明写 *"Run MANUALLY on T32 ... NOT in CI (the PC cannot reach the MIPS target)"*——这正是本闭环要消除的痛点。
- `tests/` 下 C++ 单测（PC 侧逻辑层）。
- `build_sim` PC 仿真已能跑通 workmode→EventLoop→RecordTask→initVideo→record stats 全链路（`logs/debug.log` 即一次 sim 跑，可见 `SimPirTrigger`）。
- orchestrator 多 agent 流水线（`flows/` + [`orchestration-state.yaml`](../../../orchestration-state.yaml) + `/bug` `/feature` `/refactor`，CLI 在 `~/.local/bin/orchestrator`）。

### 2.3 硬约束（已核实，决定架构）

| 约束 | 事实 | 来源 |
|------|------|------|
| HW 正常退出即断电 | `main_app` 末尾 `Misc::poweroff(); while(1);` | `src/app/main_app.cpp:384-385` |
| poweroff = 切板电 | `Misc::poweroff()` = `syscall("poweroff")`；`Misc::reboot()` = `syscall("reboot")`（软重启，能复活；poweroff 不能） | `src/common/misc/Misc.cpp:664-680` |
| 串口无法给断电板重新上电 | UART 只能软复位（Ctrl-C / `reboot`） | 物理 |
| 重复跑不冷启必挂 | IMP 资源残留（encoder/ISP/OSD），第 2-3 次 configure() 卡死；`poweroff+while(1)` 跳过 C++ 析构 | bug 报告 T2/T3 |
| warm reboot 不彻底清 IMP | 软 poweroff/warm reboot 后 IMP 内核态脏状态跨重启残留 | bug 报告 T3 |
| 进程级看门狗 | 卡死 180s 被 SIGKILL（rc=137），shell 存活 | 多份 bug 报告 |
| 日志可机析 | EasyLogger 格式 `I\|W\|E/<tag> [ts] msg` | `logs/debug.log` |

## 3. 决策内容（8 个子决策，经 grill 逐个咬合定型）

| # | 决策点 | 定型 | 关键理由 |
|---|--------|------|----------|
| 1 | 设备通道 | **串口**（`/dev/ttyUSB0` 已在 WSL 可见） | 现在唯一可用；宕机/重启只有串口稳；NFS 证明网络通但 sshd 暂不具备 |
| 2 | 串口拥有者 | **常驻 Python broker 独占**，tee→`logs/serial.log`，**sentinel 界定 + 真实退出码** | 串口只能单进程独占；要抓宕机日志须 24/7 读口；sentinel 比"匹配 `[root@Zeratul:...]#`"稳（hostname/cwd/重刷都不影响），且首次拿到 shell 真退出码 |
| 3 | 自治级别 | **Level 2**：自动判 pass/fail + 提议修复，**人批准才改码** | 真实硬件上越自治越危险；一个幻觉 pass 会楔死设备 |
| 4 | 测试架构 | **主机侧 pytest**，设备只跑 app | Claude 住主机，同进程读写测试代码与日志；判决须确定性 |
| 5 | 主机框架 | **pytest + devctl Python client** | 比自研 yaml 框架**更少代码、更多能力**（fixture/parametrize/JSON report）；判决是可审计代码 |
| 6 | Sim 角色 | **非闸门**、机会性信息 | sim 覆盖不全（真实 sensor/wifi/GPIO/audio.ko 测不了）；但 **Tier-0 双平台编译仍强制**（项目 guardrail，近乎免费） |
| 7 | 上电/复位 | **`HTC_TEST_NO_POWEROFF` 标志**（app `_exit(0)` 回 shell，同 boot 重跑）；**网络继电器留后续**（wedge 救援 + 冷复位清 IMP） | 标志最省且与 wm/um 稳定化"单进程可重复"同向；继电器是唯一覆盖 kernel wedge + 冷断电清零 IMP 的手段 |
| 8 | 集成 | **独立 `/devtest` skill 先行**，验证后接 orchestrator 节点 | 传输层全新未验证，先证明再集成，不拿赖以工作的 flows 冒险 |

## 4. 端到端循环（Level 2 形态）

1. Claude 改源码。
2. **Tier-0 编译闸门**：`cmake --build build` + `cmake --build build_sim` 必须都编过（硬闸门）。
3.（机会性）跑 sim pytest / `tests/` 单测作**信息**，不 block。
4. **部署校验**（固化 [`../../../.claude/CLAUDE.md`](../../../.claude/CLAUDE.md) "Verifying the build" 三步）：NFS 已即时可见（`mount_nfs.sh` 须补 `noac`）；`md5sum build/bin/<app>` == 设备 `/mnt/huntcam/bin/<app>` == `/proc/$PID/maps` 实际加载的 `.so`/二进制。
5. `/devtest` 跑 pytest 套件：每用例经 devctl client → `devctl run 'HTC_TEST_NO_POWEROFF=1 LD_LIBRARY_PATH=/mnt/huntcam/lib:$LD_LIBRARY_PATH /mnt/huntcam/bin/htc_workmode_app -wm 0 -rtc 1'` → broker 注入（带 sentinel）+ tee `serial.log` + 返回 stdout 与退出码 → pytest 断言日志模式 + 退出码。
6. **判决**：pytest 确定性 pass/fail，输出 JSON report。
7. **fail**：Claude 读 `serial.log` + 源码诊断 → 提议修复 diff → **人 approve** → apply → 回 1（带迭代/重试上限）。
8. **wedge**（超时无 shell）：现阶段**停下报警**（人工上电兜底）；Phase-3 起网络继电器冷启 + 校验已知良好 md5 + 续跑。

## 5. 组件与代码落点

| 组件 | 落点 | 说明 |
|------|------|------|
| 串口 broker（守护） | `tools/devctl/broker.py` | 独占 `/dev/ttyUSB0`，`115200 8N1 raw`，tee→`logs/serial.log`（过滤 sentinel 行），sentinel 界定 + 退出码捕获 |
| devctl CLI + Python client | `tools/devctl/devctl` + `devctl_client.py` | 见 §6 接口 |
| 主机测试套件 | `tests/host/` | `conftest.py`（device fixture）+ `test_wm_repeat.py`（tracer bullet）+ … |
| 不断电测试标志 | `src/app/main_app.cpp:384` 与 `workmode_app` 末尾 | `if (getenv("HTC_TEST_NO_POWEROFF")) _exit(0);` 置于 `Misc::poweroff()` 前；env 守护，**不影响产线** |
| `/devtest` skill | `.claude/skills/devtest/SKILL.md` | 串起 build→deploy→devctl→pytest→判决→诊断呈现 |
| NFS noac 修复 | `script/mount_nfs.sh` | `-o noac,nolock,vers=3` |
| orchestrator 接线（后续） | `flows/*.yaml` | tester/analyst/reviewer 节点改为调 `/devtest` |

## 6. devctl 接口契约

| 子命令 | 行为 |
|--------|------|
| `devctl status` | broker 存活？端口打开？最近提示符时间戳？ |
| `devctl run '<cmd>' [--timeout N]` | 注入 `<cmd>; echo __DEVCTL_DONE_$?__`，读到标记界定返回 stdout + 退出码；超时判 hung |
| `devctl ctrl-c` / `devctl send '<raw>'` | 注入控制字符（停长跑 app，如 record 30s） |
| `devctl log tail [-n N]` | 读 `logs/serial.log` 尾部（诊断用） |
| `devctl reboot` / `devctl wait-boot` | 软重启 / 轮询 sentinel 直到 shell 回来 |
| `devctl power-cycle` | Phase-0 为 stub（报"未配继电器"）；Phase-3 驱动网络继电器 |

## 7. 安全护栏（默认带上）

- 每个 `devctl` 操作带超时；sentinel 未到即判 hung。
- 迭代/重试上限（Level 2 天然由人批准节流，但测试执行重试也封顶）。
- **破坏性命令白名单**：永不 `mkfs`/`dd`/`flash`/`rm -rf /`；devctl 拒绝或走 confirm 层。
- 每轮 SD 清理 + 余量检查（防录影写满）。
- 单 loop 锁（broker 天然串行化；同一时刻只跑一个 loop）。
- wedge 检测：boot banner / 提示符超时即停，不无限重试。

## 8. 备选方案与弃用理由

- **设备通道**：网络 SSH/telnet——弃（现在不具备；宕机不可用）。保留为后续提升项。
- **串口拥有者**：labgrid 框架——弃（单设备 overkill，学习曲线）；每次开/关串口——弃（命令间无人读口，宕机日志丢，且反复开/关活动设备易丢字节）。
- **主机框架**：自研声明式 yaml——弃（要自建 runner/断言引擎，更多代码更少能力）；Claude 当裁判读日志判 pass/fail——弃（判决不可重现/不可审计，不满足 Level 2 信任前提；Claude 仅做**诊断**）。
- **上电/复位**：rtcwake RTC 唤醒——备选（免硬件，待 5 分钟实测 T32 是否支持 RTC-alarm-wake；但 warm 状态未必清 IMP，kernel wedge 救不回）；智能插座——采纳为 Phase-3 兜底骨干。
- **集成**：直接把 device-test 焊进 orchestrator 的 analyst/tester/reviewer 节点 prompt——弃（传输层未验证，出问题会连累日常依赖的 flows）。

## 9. 分阶段路线

| Phase | 目标 | Done 标准 |
|-------|------|-----------|
| **0（MVP / tracer bullet）** | 在"wm 重复跑卡死"这个真 bug 上一趟打通全链路 | broker + `devctl run/log/status` + `HTC_TEST_NO_POWEROFF` + `test_wm_repeat.py` + `noac` + `/devtest` 骨架跑通，产出确定性 PASS/FAIL + 日志摘要（= Level 1 能力：build/部署/跑/抓/呈现 + 单用例确定性判决） |
| **1（Level 2）** | 全套 pytest（fixture + `@parametrize` 覆盖 `{mode,rtc}` + record/net/wifi smoke）+ JSON report + Claude 诊断/提议修复流 | `/devtest <场景>` 对多场景给出确定性判决 + 修复提议，人批准后 apply |
| **2（orchestrator 接线）** | `/devtest` 接入 analyst/tester/reviewer 节点 | `/bug` `/feature` 自动带真机验证 |
| **3（Level 3）** | 网络继电器 wedge 救援 + 冷复位；解锁无人值守自治修复循环 | 带预算 + 熔断的内环无人跑通 |

## 10. 关联文档

- wm/um 稳定化（本闭环 Phase-0 的 tracer bullet 直接服务它）：[`workmode-usermode-process-split.md`](workmode-usermode-process-split.md)
- NFS 拓扑与 `noac`：[`../wsl-nfs-for-t32.md`](../wsl-nfs-for-t32.md)、项目 `.claude/CLAUDE.md` "T32 deployment via NFS"
- 不对称 snap/record 设计（record 在 main_app，与本闭环录影回归相关）：[`asymmetric-snap-vs-record-design.md`](asymmetric-snap-vs-record-design.md)
- 已有真机回归脚本约定：[`../../../script/regress_net_real.sh`](../../../script/regress_net_real.sh)
- 重复启动卡死根因（IMP 残留）：`doc/knowledge/bugs/`（T2/T3）、[`../playbooks/t32-memory-profile-and-debug.md`](../playbooks/t32-memory-profile-and-debug.md)

## 11. 决策结论

**采纳本架构**。串口通道 + 常驻 broker/devctl + 主机 pytest 确定性判决 + Level-2 人批准 + `/devtest` 独立先行，是把现有"串口手敲+人眼读日志"链路自动化的最小可信路径；test-no-poweroff 标志 + 后续网络继电器解决"HW 退出即断电、串口无法复活"这一硬约束。

Phase-0 作为 tracer bullet，在活跃的 wm/um 稳定化问题上验证全链路；验证通过后再扩到 Phase-1/2/3。任何偏离本架构的改动（如改通道、改框架、改自治级别）须先更新本 ADR。

## 12. Phase-0 真机验证（2026-06-21，已完成）

**结论：Phase-0 在真机（T32 + WSL 串口）端到端 proven。** 详见 [`../../../reviews/2026-06-21-devtest-phase0-hw-validation.md`](../../../reviews/2026-06-21-devtest-phase0-hw-validation.md)。

真机调试对 broker 的两处修正（self-test 用 FakeTransport 没覆盖，只有真串口才暴露）：
- **CRLF 归一化**：T32 串口输出 `\r\n`，marker 正则锚定 `__S_<token>__\n` 被 `\r` 破坏 → reader 读入后剥 `\r`。
- **START/END 双标记取代单 sentinel + `stty -echo`**：`stty -echo` 在本设备不可靠（命令仍回显 + ~40 列折行劈断 token）。改为 `echo __S_token__; <cmd>; printf '__E_token_%d__' $?`，正则只认 `\n` 后的真 start + `\d+` 的真 end，回显/折行免疫。

判决逻辑（`tests/host/wm_verdict.py`）真机校准：
- `HTC_TEST_NO_POWEROFF` **不靠日志行判定**（`_exit(0)` 不刷 EasyLogger 缓冲，那行丢失）——改由"sentinel 正常返回 = board 未断电"蕴含。
- 容忍良性 `UploadWorker: auth failed`（devtest 环境无 mgmt 后端）；对 `IMP_Encoder_CreateChn` 这类真错判 FAIL。
- 判 ANSI 色码后再匹配 `E/` 锚点；fps 取 `record summary` 终值。

Tracer bullet 同时挖出两个 IMP 残留 bug（见 [`../bugs/T32-imp-residue-workmode-record-2026-06-21.md`](../bugs/T32-imp-residue-workmode-record-2026-06-21.md)），其中 `IMP_Encoder_CreateChn` 连续录影失败为确定性 encoder teardown 缺陷，已移交 wm/um 稳定化。

**新增能力**：`devctl bringup`（按 build 主机网段自动判 home/company → 挂 SD → 连 wifi → 内联 noac 挂 NFS → 验证；幂等、密码走 `$HTC_WIFI_PWD`）；双环境脚本 `mount_nfs.sh`/`mount_nfs_home.sh` 校正（noac + 当前路径，见 `.claude/CLAUDE.md` NFS 段）。
