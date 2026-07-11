# htc_net_app 架构定位与 quickSnap 同步链可行性调研

> **ERRATUM (T27, 2026-07-11)**：`htc_net_app` 二进制已重命名为 `net`（CMake target / binary 名），
> 源码文件 `net_app.cpp` 不变。本文其余 `htc_net_app` 保留历史名，均指今 `net`。

> 调研主题：(1) `htc_net_app` 的当前职责、退出语义、在产品 boot 流里的位置 ——
> 回答用户核心疑问"把 4G dongle 准备网络放进 htc_net_app 是否架构合理"；
> (2) quickSnap spawn network app 后"等 4G 准备好再 spawn wm"的同步链是否可行 ——
> 重点论证同步等待期间 quickSnap 已释放 IMP/内存，不会撞 T32 MemFree。
>
> 广度：medium。只读调研，所有结论带 `file:line` 证据。
> 关联前轮：[`usb-dongle-4g-usage.md`](usb-dongle-4g-usage.md)（UsbDongle API + loadDriver OOM 风险已查实）。
> 关联 memory：quicksnap-boot-entry-spec.md（1-IMP-per-boot，wm fork+execv 禁 system_call）、
> no-fork-shell-policy.md（T32 禁 fork-shell 会 OOM）、devtest-device-quirks.md。

---

## 0. TL;DR（结论速览）

| 问题 | 结论 |
|------|------|
| htc_net_app 当前职责 | **统一上行连接工具**（WiFi / Ethernet / USB dongle 三合一），"connect once per invocation, no daemon"（`net_app.cpp:13`）。是 T7 重构产物，**设计定位就是"系统启动时准备网络连通性"**，WiFi/4G/ETH 都是本职 |
| 它是 boot 阶段网络 app 吗 | **是**（设计意图），但**当前代码只在开发/测试手工 bring-up 场景被调用**（devctl 冷启动连 WiFi + mount NFS）。产品正常 boot 流里**没有证据显示 htc_net_app 被自动拉起**——repo 内无 inittab/rcS/service，产品 boot 脚本不在代码库 |
| 退出语义 | **非常驻、一次跑完即退出**（`net_app.cpp:13`）。退出码契约稳定：`0` 成功 / `2` 驱动失败 / `3` 连接失败 / `4` DHCP 失败 / `5` MCU 回写 gated / `6` 参数错误（`net_app_logic.h:26-33`）。**退出码可直接当"网络就绪信号"** |
| 4G 接入架构合理吗 | **合理且推荐**。htc_net_app 已经是"准备网络连通性"的专职 app（管 WiFi+ETH+USB 三套上行），4G 是本职扩展而非职责膨胀。它已 link `network`+`system_call`（`CMakeLists.txt:512-536`），4G 链 `runUsb` 已写好（`net_app.cpp:403-465`），`--usb-bringup` gate 已调 `start()` |
| 同步链可行吗 | **可行**。quickSnap 的 Step 9（IMP 释放，`quick_snap.cpp:374-380`）在 Step 11（spawn，`:392-441`）**之前**——即 spawn 下游时 quickSnap 已 `IMP_System_Exit`。若改成同步 waitpid 等 net_app，**wait 期间 quickSnap 不持有 IMP**，MemFree OK |
| quickSnap 当前能 wait 吗 | **不能直接复用**。当前 `spawn()` 是 fire-and-forget（`quick_snap.cpp:130-132` 父进程不 wait）。同步等待需改 `spawn` 或新增 `spawnAndWait`（waitpid + WEXITSTATUS 读退出码）——属方案建议，走 `/feature` |

---

## A. htc_net_app 当前功能与职责

### A.1 源文件与 main 入口

| 文件 | 角色 |
|------|------|
| `src/app/net_app.cpp`（511 行） | main 入口（`:469`）+ CLI 解析（`:138-198`）+ 三套 uplink 执行（runWifi `:204` / runEth `:362` / runUsb `:403`）+ 退出码 |
| `src/app/net_app_logic.{h,cpp}` | 纯逻辑层（无 syscall）：WiFi Decision、`isNetworkUp`、`ptypeToNetType`、`usbNeedsStartDefault`（T7 风险锁定）|
| `src/app/wifi_reconnect.{h,cpp}` | WiFi SSID 优雅切换（不重启 wpa_supplicant）|

文件头注释（`net_app.cpp:1-33`）明确定位：

> `htc_net_app — unified uplink connection tool (WiFi / Ethernet / USB dongle).`
> `This binary is a "connect once per invocation" foreground tool (no daemon).`

即：**htc_net_app 是"连接一次就退出"的前台工具，非常驻、无 daemon 循环**。三套上行共享一个 binary，通过 `--type {wifi|eth|usb}` 分派。

### A.2 CLI 参数（全部 flag）

`parseArgs`（`net_app.cpp:138-198`）+ `printUsage`（`:96-135`）：

| flag | 默认 | 作用 |
|------|------|------|
| `--type <wifi\|eth\|usb>` | `wifi` | 上行类型分派（`:157-164`）。**不读 ini**（`net_app.cpp:17-19` 注释明说，避开 `INI_KEY_UPWD="PWD"` 命名陷阱）|
| `--ssid <SSID>` | 空（读 MCU） | WiFi 目标 SSID（`:172-176`）|
| `--pwd <PASSWORD>` | 空（读 MCU） | WiFi 密码（`:177-178`）|
| `--if <name>` | `wlan0` | WLAN 接口名（`:167-169`）|
| `--write-mcu` | false | 连接成功后把 SSID/pwd 持久化到 MCU（`:165-166`，严格 gate）|
| `--usb-bringup` | **false** | USB 分支额外调 `UsbDongle::start()`（激活 4G 数据上下文，`:179-180`）。默认 OFF = 镜像 main_app 只 loadDriver→open→preconfig（**T7-usb-no-start**）|
| `--usb-model <EC20\|EC200A\|EG800K\|RG255AA>` | `EC20` | dongle 型号，仅 `--usb-bringup` 时生效（`:181-185`）|
| `--no-dhcp` | false | 跳过 DHCP（`:155-156`）|
| `-v` / `--verbose` | false | 详细日志（`:153-154`）|
| `-h` / `--help` | - | 帮助（`:151-152`）|

> **注**：CLAUDE.md 冷启动 bring-up 用的 `htc_net_app --ssid ... --pwd ...`（无 `--type`）走默认 wifi 分支。

### A.3 WiFi 部分（`--ssid`/`--pwd` 连 WiFi）

`runWifi`（`net_app.cpp:204-355`），行为等价于前身 `htc_wifi_app`：

```
1. 解析凭据：CLI --ssid/--pwd 优先；否则从 MCU 读 UPID/UPWD（readMcuStrWithRetry 3 次）  :214-223
2. decide() 决策（net_app_logic.cpp:8-26）：                                                   :244
     ABORT（无凭据）→ exit 6
     FRESH_CONNECT（未连）→ Misc::connectWifi(ssid,pwd)                                        :259
     RECONNECT（连了但 SSID 不同）→ wifi_reconnect::reconnectSSID（不重启 supplicant）           :272
     REUSE（已连且 SSID 匹配）→ 跳过                                                            :279
3. 连接后探针：currentSSID() 校验已关联到目标 SSID                                              :285-294
4. DHCP：Misc::startDHCP(ifname)（除非 --no-dhcp）                                              :297-302
5. MCU 回写（仅 --write-mcu 且 gate 通过）：writeUPID/UPWD + readback 校验                       :305-351
```

- `Misc::connectWifi` 内部走 `wpa_supplicant` / speedy / wpa_conn（fork-shell，见前轮 D.4）。
- 跑完 `return EXIT_OK`(0) 退出。

### A.4 4G/USB 部分（`--type usb`，`--usb-bringup` gate）

`runUsb`（`net_app.cpp:403-465`），**唯一完整 4G→DHCP 链**（前轮已详查，这里只补退出语义）：

```
1. setNetworkInterfaceName("usb0")                                                             :412
2. UsbDongle::getInstance()                                                                    :421
3. loadDriver()         ← 失败 exit 2（depmod/modprobe/insmod，fork-shell，OOM 风险）          :423-426
4. open()               ← 失败 exit 3（AT 端口探测 /dev/ttyUSB*，OOM-safe）                    :428-431
5. [--usb-bringup] setModel(model)                                                            :433-435
6. preconfig()          ← 失败 exit 3（usbnet=3 + scanmode，OOM-safe）                         :437-440
7. [--usb-bringup] start()  ← 失败 exit 3（AT+QIACT=1 激活 PDP，OOM-safe）                     :442-450  ★T7 gate
8. [--no --no-dhcp] startDHCP("usb0")  ← 失败 exit 4（udhcpc，fork-shell）                    :452-457
9. isNetworkUp(IP + gateway) 判定 → 成功 exit 0 / 无 IP exit 3                                 :459-464
```

**T7-usb-no-start 现状（未修）**：`net_app_logic.cpp:135-142` `usbNeedsStartDefault()` 仍返回 `false`，注释明说"main_app baseline 默认不调 start()，--usb-bringup 是显式 opt-in"。即：**不带 `--usb-bringup` 时 4G 数据上下文不激活，usb0 无 carrier**。`--usb-bringup` 是必须显式传的 flag。

跑通后状态：`usb0` 接口拿到 IP + 默认网关（`isNetworkUp` = IP 非空 && gateway 非空，`net_app_logic.cpp:121-126`）。路由由 udhcpc 注入默认路由。

### A.5 退出语义（关键：能否当"网络就绪信号"）

**非常驻、一次跑完即退出**（`net_app.cpp:13` 文件头）。退出码契约（`net_app_logic.h:26-33`、`net_app.cpp:30-32`）：

| exit code | 常量 | 含义 |
|-----------|------|------|
| 0 | `EXIT_OK` | 上行就绪（IP + gateway 均非空）|
| 2 | `EXIT_DRIVER_FAIL` | 驱动加载失败（USB loadDriver / WiFi driver）|
| 3 | `EXIT_CONNECT_FAIL` | 连接失败（WiFi connect/reconnect、USB open/preconfig/start、最终无 IP）|
| 4 | `EXIT_DHCP_FAIL` | DHCP 失败 |
| 5 | `EXIT_WRITE_GATED` | 连上了但 MCU 回写被 gate（仅 WiFi `--write-mcu`）|
| 6 | `EXIT_ARG_ERROR` | 参数错误 / 无凭据 |

**退出码可直接当"网络就绪信号"**：
- `exit 0` = 网络就绪（IP+gateway 到位）
- `exit != 0` = 网络未就绪（细分原因在退出码里）

> **这是同步链的关键前提**：quickSnap 可以 waitpid 读 WEXITSTATUS，`0` 继续 spawn wm，非 0 走失败分支。

### A.6 链接依赖（CMakeLists）

`htc_net_app` 链接（`src/app/CMakeLists.txt:511-536`）：
- `network`（含 UsbDongle）+ `system_call`（wifi_reconnect 的 popencall/syscall、loadDriver 走它）
- `common_misc`（connectWifi/startDHCP/getIPAddress）+ `mcu`（UPID/UPWD）
- 无 HAL / media / imp —— **纯网络工具，不碰 camera/ISP**

对比 `quickSnap`（`CMakeLists.txt:624-650`）：**不 link network、不 link system_call**（quickSnap 用 fork+execv，禁 system_call）。这印证了 quickSnap 不能自己跑 4G —— 必须 spawn 一个带 network 能力的下游。

---

## B. htc_net_app 在产品 boot 流里的位置（架构核心）

### B.1 开发/测试场景（devctl bring-up）

`tools/devctl/devctl:150-170` 的冷启动 bring-up 流程：

```
wait_boot → mount SD → htc_net_app --ssid --pwd (WiFi) → mount NFS → verify htc_workmode_app
```

- devctl 只调 htc_net_app 的 **WiFi 分支**（`:156`，无 `--type`，默认 wifi），目的是**连 WiFi 后 mount NFS**（让设备能跑 NFS-shared 的 build/bin）。
- devctl **不调** `--type usb` / `--usb-bringup`。
- 这是**开发测试手工 bring-up**，不是产品 boot 流。

### B.2 产品正常 boot 流（是否有 htc_net_app？）

**关键结论：repo 内无证据显示产品 boot 流自动拉起 htc_net_app。**

搜索范围与结果：

| 搜索 | 范围 | 结果 |
|------|------|------|
| inittab / rcS / rc.local / *.service | 全 repo（排除 build/build_sim）| **无**（`find` 未发现任何 init 脚本文件）|
| `htc_net_app` 引用 | script/ + tools/ | 只在 `mount_nfs*.sh`（注释文档）、`regress_net_real.sh`（手动回归脚本）、`devctl`（bring-up）|
| `quickSnap` 引用 | script/ + tools/ + 任何 init | **无**（quickSnap 是 boot 首程序，但**它的启动者不在代码库里**——应是 bootloader/内核 init 或 SD 卡启动脚本，属于设备固件层，不在 app repo）|
| `htc_media_app` 引用 | 同上 | 同上（quickSnap 的前身，已重构）|

**推断**（标注为推断，非证实）：
- 产品的 boot 启动脚本（拉起 quickSnap）**不在 app repo**，应在设备固件/SD 卡 rootfs 层（如 `/etc/init.d/rcS` 或 initramfs）。
- **当前产品 boot 流大概率是 `quickSnap → wm/um`**（quickSnap spec §0、§7 定型的链），中间**没有 htc_net_app**。
- 这意味着：**wm 启动时，网络链路（4G/WiFi）没有被任何前置进程拉起来**——这正是前轮调研发现的"断层"（wm 直接 connect() 假设底层已联网，`usb-dongle-4g-usage.md` §C.2）。

### B.3 htc_net_app 的设计意图 vs 实际使用

| 维度 | 设计意图（代码注释/命名） | 实际使用 |
|------|-------------------------|---------|
| 定位 | "unified uplink connection tool"（`net_app.cpp:1`）| 开发 bring-up 工具（devctl 只用 WiFi 分支）|
| 三套上行 | WiFi + ETH + USB（`--type`）| 只 WiFi 被用；ETH/USB 有回归脚本（`regress_net_real.sh`）但无 boot 流编排 |
| 在 boot 流的角色 | T7 planner 产物（文件头 `:3-11` 引 "T7 planner full"）—— 显然**设计为系统启动时准备网络** | **未编入产品 boot 链**（或编排脚本不在 repo）|
| 4G 能力 | `runUsb` + `--usb-bringup` 已完整实现（含 `start()`）| **未被任何 boot 流调用** |

> **架构判读**：htc_net_app 是 T7 重构里**专门为"系统启动时准备网络连通性"而生的 app**（三套上行合一 + 稳定退出码契约 + 纯逻辑层可单测）。它的设计职责**本来就包括 4G**。只是当前产品 boot 流还没把它编进去。

---

## C. 架构合理性评估（回答用户核心疑问）

### C.1 用户疑问

> 用户倾向"把 4G dongle 准备网络这件事放进 htc_net_app"，但要先确认 htc_net_app 的当前职责和架构定位是否本来就该干这个，而不是职责错位。

### C.2 判断依据

**htc_net_app 已经是"系统启动时准备网络连通性"的专职 app**，证据链：

1. **命名 + 文件头**：`net_app.cpp:1` "unified uplink connection tool" —— 设计意图明确是"网络上行准备"。
2. **三套上行已合一**：`--type {wifi|eth|usb}`（`net_app.cpp:99-105`），WiFi/ETH/USB **都是本职**，不是新塞的。
3. **T7 planner 产物**：`net_app.cpp:3-11` 引用 T7 planner full，重构目标是把分散的联网逻辑（旧 htc_wifi_app + main_app 的 PTYPE 分支）统一到一个 app。
4. **稳定退出码契约**：`net_app_logic.h:26-33` 定义了 6 个退出码，`net_app.cpp:30-32` 注释 "stable, shared across uplinks" —— **为被编排（脚本/上游进程 waitpid）而设计**。
5. **纯逻辑层可单测**：`net_app_logic.h:8-14` 注释 "unit-testable on PC simulation" —— 工程化程度高，不是临时工具。
6. **4G 链已写好**：`runUsb`（`net_app.cpp:403-465`）完整实现 loadDriver→open→preconfig→[start]→DHCP，`--usb-bringup` gate 已正确调 `start()`（规避 T7-usb-no-start）。
7. **依赖已就位**：link `network`（UsbDongle）+ `system_call`（loadDriver 走它）（`CMakeLists.txt:512-536`）。

### C.3 结论：架构合理，推荐复用 htc_net_app

**4G 是 htc_net_app 的本职扩展，不是职责膨胀。** 理由：

- htc_net_app 的设计定位是"统一上行连接工具"，4G（USB dongle）与 WiFi/Ethernet 并列，是三套上行之一。
- 它的退出码契约、纯逻辑层、非常驻语义，**完全契合"被 boot 流程编排、跑完即退、退出码当就绪信号"的角色**。
- 把 4G 塞进 quickSnap（违反 quickSnap 不 link network/system_call 的约束）、或塞进 wm（wm 当前无 4G 能力 + loadDriver OOM 风险），都比放进 htc_net_app 更不合理。

**推荐方案：复用 htc_net_app**（不新建独立 network app）。具体编排：

```
产品 boot 流（建议）:
  quickSnap (snap + IMP 释放)
    → spawn htc_net_app --type usb --usb-bringup --usb-model <M>  (4G)
    → waitpid 等 exit code
    → exit 0 → spawn wm -m 2
    → exit != 0 → （失败语义见 D.3）
```

WiFi 场景对称：`htc_net_app --type wifi --ssid <S> --pwd <P>`（或读 MCU）。

### C.4 不推荐"新建独立 network app"的理由

- htc_net_app **已经是**那个独立 network app（T7 已做完重构）。新建会重复造轮子。
- htc_net_app 的退出码契约 / 纯逻辑层 / 三套上行已就绪，新建要重新设计一遍。
- 唯一要做的"改动"是**把它编进 boot 流**（quickSpawn spawn 它 + waitpid），不是改它的职责。

---

## D. 同步链可行性（quickSnap → htc_net_app(4G) → 等 OK → wm）

### D.1 当前 quickSnap spawn 是 fire-and-forget

`quick_snap.cpp:111-133` 的 `spawn()`：

```cpp
static int spawn(const char* path, char* const argv[])
{
    pid_t pid = fork();
    ...
    if (pid == 0) { ...子进程... execv(path, argv); }
    // Parent: do not wait — child becomes orphan and init adopts it.
    Logger::log(LogLevel::INFO, "spawned child pid=%d for %s", pid, path);
    return 0;
}
```

`:130-132` 注释明说 "Parent: do not wait"。当前 spawn 后立即 return，quickSnap 不 waitpid。

**同步等待需要改造**（方案建议，走 `/feature`）：
- 新增 `spawnAndWait(path, argv)`：fork → 子 execv → 父 `waitpid(pid, &status, 0)` → 返回 `WEXITSTATUS(status)`。
- 或复用 `spawn` 但加一个 `bool wait` 参数。
- 注意：waitpid 期间 quickSnap 阻塞在 waitpid syscall（几乎不占 CPU），子进程（net_app）跑 4G 拨号。

### D.2 关键论证：wait 期间 quickSnap 的 IMP/内存状态（为什么不会 mem 问题）

**这是"同步链可行"的核心论据。** quickSnap 的 Step 顺序（`quick_snap.cpp` main，spec §7）：

| Step | 代码行 | 做什么 | IMP 状态 |
|------|--------|--------|----------|
| 8 (willSnap) | `:358-370` | HalProvider::start + doSnap（ImageSnap 构造 → IMP init）| IMP 已 init |
| 9 | `:372-380` | `if(impInitialized) HalProvider::resetSharedVideo()` → `~IngenicVideo` → **`IMP_System_Exit`** | **IMP 已释放** |
| 10 | `:382-390` | force_upload 写回 0 | - |
| 11 | `:392-441` | spawn 下游（`switch effectiveMode`）| IMP 已释放 |

**证据**：Step 9（`:374` `if (impInitialized)`）在 Step 11（`:392` `// Step 11: spawn downstream`）**之前**。`resetSharedVideo()`（`:379`）触发 `IMP_System_Exit`（spec §4.1 第 3 步，`quicksnap-app-spec.md:112`）。

**结论**：spawn 下游时（Step 11），quickSnap **已经 `IMP_System_Exit`**。若把 spawn 改成 spawnAndWait（同步等 net_app），**wait 期间 quickSnap 不持有 IMP** —— IMP 的连续内存池（encode buf / DPB / channel buf）已释放，MemFree 回到接近 boot 初态。

对比 T32 MemFree 约束（no-fork-shell-policy.md）：MemFree 凤 MB 下 fork-shell 会 OOM。但同步链里：
- quickSnap 在 waitpid（不 fork 新进程，只阻塞）。
- net_app 是 quickSnap 的子进程（fork 在 spawnAndWait 里发生，fork 时 quickSnap 已 lean —— 这正是 spec §6 说的"fork 时 quickSnap 已 lean，加上上电首进程 MemFree 最大 → OOM 风险最低的 fork 时机"）。
- net_app 跑 4G 时，它的 fork-shell（loadDriver/udhcpc）在 net_app 自己的进程上下文 —— net_app 不碰 IMP、不碰 media，内存预算比 wm 更宽（net_app 只 link network/common_misc/mcu，无 HAL/media/imp，`CMakeLists.txt:512-536`）。

> **内存论证结论**：同步 wait 期间 MemFree OK。理由：(1) quickSnap 已 IMP_System_Exit（Step 9 < Step 11）；(2) net_app 无 HAL/media/imp 依赖，是所有 app 里最 lean 的之一；(3) waitpid 不占额外内存。**唯一需实测的是 net_app 内 loadDriver 的 fork-shell MemFree**（前轮 open question，建议产品方案把 dongle 驱动做成内核内置规避此风险）。

### D.3 失败语义建议（4G 拨号失败时）

htc_net_app `exit != 0` 时，quickSnap 怎么办？两种策略：

| 策略 | 行为 | 适用场景 |
|------|------|---------|
| **A. 继续 spawn wm（best-effort）** | 4G 失败也 spawn wm -m 2，wm 的 UploadTask `ensureConnected` 会 connect 失败 → park 等重试 → upload-timeout 60s 关机（`wm_app.cpp:322`）| 产品默认推荐：snap 已成功（照片在 /tmp），wm 能重试上传（如果有补传机制），或至少 snap 落盘不丢 |
| **B. 中止（不 spawn wm）** | 4G 失败直接关机，不跑 wm | 省电优先（避免 wm 跑 60s timeout 才关机）；但丢失这次 snap 上传机会 |

**推荐 A（继续 spawn wm）**，理由：
- snap 的照片在 `/tmp/media/`（quickSnap Step 8 产出），wm -m 2 直读（`wm_app.cpp:351-353` ingestQuickSnapManifest）。4G 失败时 wm 至少能尝试上传（万一 4G 是临时问题，wm 的 connect-grace 30s / upload-timeout 60s 内可能恢复）。
- 与 quickSnap 现有"doSnap failed 也继续 spawn"（`quick_snap.cpp:364-369` 注释 "Continue to spawn downstream anyway (best effort)"）的容错哲学一致。
- 失败原因 log 出来（net_app 退出码 + stderr），便于诊断。

**可选优化**：quickSnap 把 net_app 的 exit code 传给 wm（作为环境变量或 argv），让 wm 据此决定 idle-grace（4G 失败时缩短 grace 早关机）。但这增加耦合，建议一期不做。

### D.4 同步链的完整改造点（方案建议，非代码）

> 纪律：本节是设计建议，不是代码改动。实现请走 `/feature`。

```
quick_snap.cpp Step 11 改造（建议）:

现状:
  switch (effectiveMode) {
    SNAP_UPLOAD: spawn(wmPath, wmArgv); return 0;       // fire-and-forget
    ...
  }

建议（伪码）:
  // Step 10.5 (新增): 若需要上行，先 spawn net_app 并等结果
  bool needsUplink = (effectiveMode == SNAP_UPLOAD || effectiveMode == UPLOAD_ONLY);
  if (needsUplink) {
    // 根据 PType 选上行类型（需 quickSnap 读 config.ini [BOOT] PType，
    //   或 quicksnap.json 加 uplinkType 字段 —— 方案选择见 open_questions）
    int rc = spawnAndWait(netAppPath, netAppArgv);  // 阻塞等 net_app 退出
    log("net_app exit=%d", rc);
    // 失败也继续（best-effort，见 D.3），或按策略中止
  }
  // Step 11: spawn wm（fire-and-forget 保持不变）
  switch (effectiveMode) { ... }
```

**改造涉及的决策点**（留给 `/feature`）：
1. **PType 来源**：quickSnap 当前只读 quicksnap.json（5 字段，无 PType）。同步链需要知道上行类型（wifi/usb/eth）—— 是加进 quicksnap.json，还是让 quickSnap 读 config.ini `[BOOT] PType`？（spec §2 quickSnap 只读 quicksnap.json，读 config.ini 会破 γ 架构）。或：**quickSnap 不选类型，固定 spawn net_app 让它自己读 ini**（但 net_app 当前不读 ini，`net_app.cpp:17-19`）。
2. **net_app argv 怎么传**：quickSnap 需知道 `--type usb --usb-bringup --usb-model <M>` 还是 `--type wifi --ssid <S> --pwd <P>`。这要求 quickSnap 有上行配置源。
3. **spawnAndWait 的实现**：fork + waitpid（或用 vfork 更省内存，但 vfork 下父进程阻塞直到子 execv/_exit，不能 waitpid —— 需斟酌）。

> 这三个决策点超出本调研范围，建议 `/feature` 时一并 grill。

### D.5 net_app 退出码能否直接当 "4G OK/FAIL" 信号？

**能。** `net_app_logic.h:26-33` 的退出码契约稳定（文件头 `:30-32` 注释 "stable, shared across uplinks"）。quickSnap waitpid 后 `WEXITSTATUS(status)`：
- `0` = 4G 就绪（usb0 有 IP+gateway）
- `2/3/4` = 驱动/连接/DHCP 失败（细分原因）
- `6` = 参数错误（quickSnap 传错 argv）

**无需让 net_app 写状态文件或改退出码** —— 现有契约够用。

---

## E. 待确认点（open questions）

1. **产品 boot 流是否真有 htc_net_app**（本调研最大未证实点）。repo 内无 inittab/rcS/service，产品启动脚本不在 app repo。**需确认设备固件层（SD 卡 rootfs / initramfs / bootloader）的 boot 序列**：是否 `quickSnap → wm/um` 直连，还是中间有 net_app？这决定同步链是"新增 net_app 到 boot 流"还是"改 quickSnap spawn 方式"。
2. **4G 拨号实测耗时**：`start()` = AT+QIACT=1 激活 PDP，`UsbDongle.cpp:1018` 重试 10 次 × 5s = 最长 50s。加上 loadDriver/open/preconfig/DHCP，net_app 总耗时 5–60s。同步等待会让 quickSnap boot 时间延长这么多 —— 是否可接受？（产品相机 MCU 周期唤醒，boot 时间影响拍摄响应）。需 HW 实测 start() 耗时分布。
3. **PType / 上行类型来源**（D.4 决策点 1）：quickSnap 选 net_app argv 需知道上行类型。是加进 quicksnap.json、读 config.ini、还是让 net_app 自己读 ini（需改 net_app 当前"不读 ini"的设计）？
4. **dongle 驱动加载方式**：net_app 的 loadDriver 是 fork-shell（depmod/modprobe/insmod），T32 MemFree 下有 OOM 风险（前轮 D.4）。产品方案建议把 dongle 驱动做成内核内置（`=y`），net_app 永不 loadDriver —— 需固件层配合。
5. **net_app 的 no-`--type` 默认行为与 regress 脚本不一致**：`net_app.cpp:485-488` 无 `--type` 时默认 wifi（只 log 不 exit），但 `regress_net_real.sh:97-100` 期望 `no --type → exit 6`。这是 net_app 自身的小 bug（regress 脚本预期与实现不符），不影响 4G 接入，但建议 `/bug` 修。
6. **spawnAndWait 用 fork 还是 vfork**：fork 复制页表（T32 省内存下有风险，但 quickSnap spawn 时已 lean + IMP 释放）；vfork 不复制页表但父进程阻塞到子 execv/_exit，不能 waitpid（需 signal/pipe 读退出码）。需 `/feature` 时定方案。

---

## F. 关联文档与证据索引

- 前轮调研：[`usb-dongle-4g-usage.md`](usb-dongle-4g-usage.md)（UsbDongle API + loadDriver OOM + wm 无 4G 能力三重确认）
- spec：`doc/knowledge/specs/quicksnap-app-spec.md`（§4 IMP 1-per-boot、§6 fork+execv、§7 Step 1-12 顺序）
- memory：`quicksnap-boot-entry-spec.md`（1-IMP-per-boot）、`no-fork-shell-policy.md`（fork-shell OOM 实证）、`devtest-device-quirks.md`
- 关键代码：
  - `src/app/net_app.cpp:1-33`（文件头定位：unified uplink tool）、`:96-198`（CLI + parseArgs）、`:204-355`（runWifi）、`:362-384`（runEth）、`:403-465`（runUsb 完整 4G→DHCP 链）、`:469-510`（main + 分派）
  - `src/app/net_app_logic.h:8-33`（纯逻辑层 + 退出码契约）、`net_app_logic.cpp:121-142`（isNetworkUp / usbNeedsStartDefault T7 锁定）
  - `src/app/CMakeLists.txt:511-536`（htc_net_app link network + system_call）、`:624-650`（quickSnap 不 link network/system_call）
  - `src/app/quick_snap.cpp:111-133`（spawn fire-and-forget）、`:372-380`（Step 9 IMP 释放）、`:392-441`（Step 11 spawn 下游）
  - `src/app/app_lifecycle/ProcessLifecycle.cpp:490-496`（PTYPE_USB_DONGLE 只设 netif 名，不联网）
  - `src/app/workmode/upload_task.cpp:138-166`（ensureConnected 直接 connect，假设底层已联网）
  - `tools/devctl/devctl:150-170`（bring-up 只调 WiFi 分支，不编入产品 boot）
  - `script/regress_net_real.sh`（net_app ETH/USB 手动回归，含 no-`--type` exit 6 预期 vs 实现不符）
