# devtest：新 build-PC 准备 + build 机 IP 迁移指南

> 把 devtest 自动闭环（broker + devctl + 主机 pytest）搬到一个**新 build PC**（如明天回公司换台机器），或**改 build 机 IP**（如 company `192.168.0.210`→`192.168.0.206`）时该做什么。
> 架构见 [`../decisions/devtest-automation-loop.md`](../decisions/devtest-automation-loop.md)；NFS 拓扑见 `../../../.claude/CLAUDE.md` "T32 deployment via NFS"。

## A. 新 build-PC 首次准备（一次性）

前提模型：**Claude 运行 + 编译 + NFS server 是同一台 PC**（设备从这台挂 NFS 跑你刚编的二进制）。下面 5 项是循环自己管不了的，得在这台 PC 上手动备齐。

| # | 项 | 怎么做 / 怎么验 |
|---|----|----------------|
| 1 | **repo 检出**（含 `tools/devctl/`、`tests/host/`） | `git clone`/pull 到本机；`ls tools/devctl/devctl` 在 |
| 2 | **Python 3 + pyserial**（broker 依赖）；可选 pytest（host 套件） | `sudo apt install -y python3-pytest python3-serial`（或 `pip install --user pyserial pytest`）；`python3 -c "import serial"` 无报错 |
| 3 | **`/dev/ttyUSB0` 可用**：USB-UART 插上、WSL2 透传、`dialout` 组 | WSL2：Windows 上 `usbipd winattach` 把 FTDI 挂进 WSL；`sudo usermod -aG dialout $USER` 后重登；`ls -l /dev/ttyUSB0`、`id \| grep dialout` |
| 4 | **本机 NFS export**：`/etc/exports` 暴露 repo 的 `build/` | 在 build PC：`echo "<repo>/build *(rw,sync,no_subtree_check,no_root_squash,insecure)" \| sudo tee -a /etc/exports && sudo exportfs -a`（insecure+no_root_squash 是嵌入式 NFS 客户端常用） |
| 5 | **WiFi 密码** 进环境变量（不进 git） | `export HTC_WIFI_PWD='<pwd>'`（写进 `~/.bashrc` 省得每次敲） |

验完毕一条命令自测（设备已上电、串口已插）：
```sh
python3 tools/devctl/broker.py --self-test     # broker 逻辑
python3 tools/devctl/broker.py &               # 起常驻 broker
HTC_WIFI_PWD=... tools/devctl/devctl bringup   # 自动判环境 + wait-boot + 挂SD/连wifi/挂NFS + 验证
```
`bringup` 会自动：`hostname -I` 命中网段 → 选 home/company → 等设备 shell 就绪 → 挂 SD → `htc_net_app` 连对应 SSID → **用本机 IP + 本 repo `build/` 路径** noac 挂 NFS → verify。

## B. build 机 IP 迁移清单（例：company `192.168.0.210` → `192.168.0.206`）

**关键：`devctl bringup` 用本机 IP 自动检测，换 IP 不用改循环代码。** 只有下面几处"硬编码 IP"的手动路径产物要同步（保持文档/手动脚本与新 IP 一致）：

| # | 位置 | 要不要改 | 动作 |
|---|------|----------|------|
| 1 | `tools/devctl/devctl` (`ENVS` company `nfs_host` 回退值) | 改（仅为回退默认值） | `192.168.0.210`→`.206`（**已改**） |
| 2 | `devctl bringup` 实际挂载 | **不用改** | 自动用本机 IP ✓ |
| 3 | `script/mount_nfs.sh`（company，人手敲用） | 改 | `NFS_HOST=192.168.0.210`→`.206` |
| 4 | 设备 SD 卡 `/mnt/sdcard/mount_nfs.sh` 副本 | 改 | NFS 中转重推（见下）或手改 |
| 5 | `.claude/CLAUDE.md` 双环境表 | 改 | company NFS server `.210`→`.206`（**已改**） |
| 6 | `doc/knowledge/decisions/devtest-automation-loop.md` §2.1 | **不用改** | 已泛化（不再写死 IP）（**已改**） |
| 7 | build 机 `/etc/exports` | 一般不用改 | 导出按**路径+客户端白名单**，不依赖 server 自身 IP；确认仍 export repo `build/`、`exportfs -a` |
| 8 | （建议）build 机静态 IP | 配 | 给 build 机固定 `.206`，免得 DHCP 漂移（bringup 自动检测能扛漂移，但静态更省心） |

**同步 SD 卡副本**（设备上，经 broker，规避串口行缓冲长度限制）：
```sh
mkdir -p build/.devtest_sync && cp script/mount_nfs.sh build/.devtest_sync/
tools/devctl/devctl run 'cp /mnt/huntcam/.devtest_sync/mount_nfs.sh /mnt/sdcard/mount_nfs.sh && echo SYNCED'
rm -rf build/.devtest_sync
```

> home（`192.168.31.x`/`192.168.31.200`）同理：换 home build 机 IP 时，改 `script/mount_nfs_home.sh` + 设备 SD 卡副本 + CLAUDE.md 表 + devctl `ENVS` home 回退值；bringup 本身不用动。详见 §B.2。

## B.2 build 机 IP 迁移清单（home `192.168.31.x`）

与 §B 同构。下面是当前 home 现状（`192.168.31.200`，home SSID `no_mesh_02_2.4G`）对应的 8 个同步点；如果 home build 机 IP 漂移（DHCP 换号、或换 build PC），按此表逐项改：

| # | 位置 | 当前值 / 状态 | 动作 |
|---|------|---------------|------|
| 1 | `tools/devctl/devctl` (`ENVS` home `nfs_host` 回退值) | `192.168.31.200` | 改（IP 漂移时） |
| 2 | `devctl bringup` 实际挂载 | 自动用本机 IP | **不用改** ✓ |
| 3 | `script/mount_nfs_home.sh`（home，人手敲用） | `NFS_HOST=192.168.31.200` | 改（IP 漂移时） |
| 4 | 设备 SD 卡 `/mnt/sdcard/mount_nfs_home.sh` 副本 | 同上 | 改（IP 漂移时），SD 卡中转重推命令见 §B 同步 SD 卡副本块 |
| 5 | `.claude/CLAUDE.md` 双环境表 | home NFS server `.200` | 改（IP 漂移时） |
| 6 | `script/build_t32@200.sh` 顶部 `TOOLCHAIN_DIR` / `CMAKE_BIN` | 跟公司 @206 同形 | **host 切换时改**：换一台 home build PC 时（不是 IP 漂移，是机器换了），按新机的 toolchain/cmake 路径改这两个 knob；文件名 `build_t32@<新host>.sh`，并更新 CLAUDE.md §Build 引导行 |
| 7 | build 机 `/etc/exports` | — | 同 §B #7：按路径+客户端白名单 export `build/`；`exportfs -a` |
| 8 | （建议）build 机静态 IP | — | 同 §B #8：固定 `.200` 省心 |

**home 与公司的两个固定差异**（写脚本/文档时记住）：

- **SSID**：home `no_mesh_02_2.4G` / company `no_mesh_01_2.4G`（CLAUDE.md §T32 deployment 双环境表）。
- **NFS 挂载脚本**：`mount_nfs_home.sh` vs `mount_nfs.sh`（两脚本路径不同，SD 卡副本各一份，不能混用）。

> 经验法则：先按 §B（company）8 项跑一遍流程验证环境对，再回到 §B.2 把 home 同样的 8 项对照；两边条目数量一致但**文件不同**，最容易踩坑的就是第 4 项 SD 卡副本被混改。

## C. 日常流程（每次冷启后）

```sh
python3 tools/devctl/broker.py &          # 1. 起 broker（独占 /dev/ttyUSB0）
HTC_WIFI_PWD=... tools/devctl/devctl bringup   # 2. 自动判环境 + 唤醒设备（内置 wait-boot）
/devtest <scenario>                       # 3. build→部署→跑→判决（见 SKILL.md）
```

- 冷启后 `bringup` 内部已先 `wait_boot`（等 shell 就绪）；卡死/超时它会明确报"power-cycle"。
- **串口单占有**：broker 跑着时，别在串口工具里手敲（会冲突，bring-up 必须经 `devctl run`/`bringup`）。

## D. 排错

| 现象 | 原因 | 处理 |
|------|------|------|
| `bringup` 各步显示 `?` / verify FAIL | 设备 shell 没就绪 或 卡死 | `tools/devctl/devctl wait-boot`；不行 → 断电重启（串口只能软复位，kernel wedge 救不回） |
| NFS 步 `NFS_RC` 非 0 / 挂不上 | build 机没 export / 设备到不了 build 机 | build 机 `cat /etc/exports`、`sudo exportfs -a`；设备 `ping <build机IP>` |
| 环境判错（home/company） | `hostname -I` 没命中预期网段 | `devctl bringup --env home\|company` 覆盖 |
| 判对了环境但 SSID/密码不对 | 该环境 WiFi 变了 | `--ssid <X>` 覆盖；密码改 `$HTC_WIFI_PWD` |
| 串口 open 失败 | 没透传 / 不在 dialout / 被占 | 见 A.3；确认没别的进程开着 `/dev/ttyUSB0` |
| broker 在跑但 `devctl status` 显示 `last_activity: 0.0` / serial.log 不推进（**常见于重启 T32 后**） | usbipd 的 attach 会话掉了（FTDI 经 `vhci_hcd` 从 Windows 透传，设备重启/USB 抖动后 WSL 侧只剩悬空 `/dev/ttyUSB0`，broker 打开但收 0 字节） | **Windows 侧**：`usbipd list` 看 FTDI 是否还 Attached 到 WSL，否则 `usbipd attach --wsl --busid <X>`；必要时拔插 USB-UART dongle。**WSL 侧**：`tools/devctl/devctl broker stop && tools/devctl/devctl broker start` 重开端口（broker 会**自动选新的 `/dev/ttyUSB*`**——usbipd 重连后常从 `ttyUSB0` 变 `ttyUSB1`，重编号自动兜住）。判据：`cat /sys/bus/usb-serial/devices/ttyUSB*/../idVendor` 能读到 `0403` 才算真连上 |

## 关联

- 架构 ADR：[`../decisions/devtest-automation-loop.md`](../decisions/devtest-automation-loop.md)
- 真机验证 + broker 关键坑（CRLF/START-END 标记）：[`../../../reviews/2026-06-21-devtest-phase0-hw-validation.md`](../../../reviews/2026-06-21-devtest-phase0-hw-validation.md)
- skill 流程：`../../../.claude/skills/devtest/SKILL.md`
- WSL NFS 拓扑（home 视角）：[`../wsl-nfs-for-t32.md`](../wsl-nfs-for-t32.md)
