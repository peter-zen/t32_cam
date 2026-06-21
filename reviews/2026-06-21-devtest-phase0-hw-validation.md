# 2026-06-21 · devtest 自动闭环 Phase-0 真机验证

**主题**：把 `doc/knowledge/decisions/devtest-automation-loop.md` 定型的 devtest 闭环在真机（T32 / WSL 串口）上端到端跑通，并顺带校正双环境脚本 + 加 `devctl bringup`。

## 结论

**Phase-0 真机验证通过**——串口 broker / devctl / verify_deploy / wait-boot / 常驻抓包 / 确定性判决 全部在真硬件上 proven。Tracer bullet 还自动挖出两个真 bug（见下）。

## 已验证（真机）

| 环节 | 结果 |
|------|------|
| 串口在 WSL 可用 | FTDI → `/dev/ttyUSB0`，用户在 `dialout` 组，pyserial 3.5；活的双向 shell `[root@Zeratul:huntcam]#` |
| broker 真串口路径 | `devctl run` 干净返回 + 真退出码（uname rc0 / `false` rc1 / `(exit 7)` rc7 / 长命令无回显泄漏） |
| `verify_deploy.sh` | host md5 == 设备 md5（NFS 即同一文件） |
| `wait-boot` + 常驻抓包 | 物理断电重启全程抓到 U-Boot/kernel boot 日志；shell 回来自动检测 |
| `HTC_TEST_NO_POWEROFF` | 两轮 run 后 board 都没断电（`_exit(0)` 回 shell） |
| 确定性判决 | run1 FAIL（真 encoder 错）/ run2 PASS（良性 upload 错容忍） |

## 真机调试抓到并修了两个 broker bug（没设备发现不了）

1. **CRLF（关键）**：T32 串口输出 `\r\n`，标记正则锚定 `__S_token__\n`，中间 `\r` 让匹配整个失败、全 timeout。修复：broker reader 读入后剥 `\r`（CRLF→LF 归一化）。
2. **回显/折行**：原方案 `stty -echo` + 剥首行回显，但**这台设备 `stty -echo` 不可靠**（命令仍被回显，~40 列折行把 token 劈成两半）。换成 **START/END 双标记**夹真实输出（`echo __S_token__; <cmd>; printf '__E_token_%d__' $?`，正则只认 `\n` 后真 start + `\d+` 真 end），回显/折行都不再影响。

附带教训（已进 `/devtest` skill 护栏）：`devctl run` 绝不能用裸 `exit N`（杀设备登录 shell → sentinel 不返回 → timeout + shell 重启）；测非零 rc 用 `(exit N)`/`false`。

## Tracer bullet 挖出的两个真 bug

详见 [`../doc/knowledge/bugs/T32-imp-residue-workmode-record-2026-06-21.md`](../doc/knowledge/bugs/T32-imp-residue-workmode-record-2026-06-21.md)：
- **#1 `tisp_awb_init` 内核 oops**（首跑、脏状态）→ 冷启动清掉 → 瞬态残留。
- **#2 `IMP_Encoder_CreateChn(0) failed`**（连续录影第 2 段）→ 冷启动后仍复现 → 确定性 encoder teardown 缺陷（喂给 wm/um 稳定化）。

## 顺带完成的环境校正

- **双环境脚本**（`script/mount_nfs.sh` company / `mount_nfs_home.sh` home）：旧路径 `t32_cam` 对齐成当前路径，两份都加 `noac`，只差 `NFS_HOST`。SD 卡上的副本已同步（NFS 中转推送）。
- **`devctl bringup`**：按 build 主机网段自动判 home/company（`192.168.31.x`/`192.168.0.x`）→ 挂 SD → 连对应 SSID → 内联 `noac` 挂 NFS → 验证。幂等、自包含、密码走 `$HTC_WIFI_PWD`。
- 文档：`.claude/CLAUDE.md` NFS 段重写为双环境表 + bring-up 序列 + `devctl bringup` 指针。

## 产物清单

- 新增：`tools/devctl/{broker.py,devctl,devctl_client.py,verify_deploy.sh}`、`tests/host/{conftest,wm_verdict,test_wm_repeat,test_verdict}.py`、`.claude/skills/devtest/SKILL.md`
- 改动：`src/app/main_app.cpp` + `workmode_app.cpp`（`HTC_TEST_NO_POWEROFF`）、`script/mount_nfs{,_home}.sh`、`.claude/CLAUDE.md`、`doc/knowledge/{decisions/devtest-automation-loop.md,bugs/...,todo.md,working-set.md}`

## 未验证 / 下一步

- `devctl bringup` 的"从干净冷启"分支（本次跑的是幂等路径；实际 mount/net_app/NFS 命令即用户原 bring-up 序列，已单独 proven）。下次冷启动一条 `devctl bringup` 验全。
- pytest 未装在这台 WSL（无 pip）；host 套件用了内置 `--junit-xml`，verdict 逻辑已用裸 python 单测验证。开发盒装 `python3-pytest` 后即可 `pytest tests/host/`。
- Phase-1：pytest 矩阵 + Claude 诊断/提议修复流（Level 2）。
