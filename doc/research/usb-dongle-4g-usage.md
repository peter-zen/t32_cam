# UsbDongle 4G 联网类与 quickSnap → wm 接入调研

> 调研主题：(1) `network/` 下 UsbDongle 类**当前如何被使用**——API、4G 机制、生命周期、使用者;
> (2) 在 quickSnap → wm(`wm -m 2/3`,1-IMP-per-boot,w fork+execv 禁 system_call)流程下,
> 若 wm 需要在 upload 之前用 UsbDongle 启动 4G 连网,应怎么接入、在哪初始化、有哪些约束。
>
> 广度:medium。只读调研,所有结论带 `file:line` 证据。
> 关联 memory: quicksnap-boot-entry-spec.md(1-IMP-per-boot,w fork+execv)、
> no-fork-shell-policy.md(T32 禁 fork-shell,MemFree 凤 MB,system/popen 会 OOM)。

---

## 0. TL;DR(结论速览)

| 问题 | 结论 |
|------|------|
| 类的确切名字 | **`network::UsbDongle`**(`src/network/UsbDongle.{h,cpp}`,活跃);另有遗留 `network::Usb4gDongle`(`Usb4gDongle.{h,cpp}`,仅 EC20/EC200,无 `start()`,不再推荐) |
| 4G 连网机制 | **AT 命令 + SerialPort**(QMI/ECM 模式),**不用 pppd**。`start()` = AT+QICSGP(设 APN) + AT+QIACT=1(激活 PDP context)。激活后 dongle 在 USB 网卡模式(`usb0`)拿到 IP,再 `udhcpc` 取地址 |
| 是否 fork-shell | **混合**。AT 命令路径(SerialPort = 纯 POSIX,零 fork)**OOM-safe**;但 **`loadDriver()`/`load_one()`/`have()` 内部用 `Misc::syscall`/`Misc::popencall`**(`depmod`/`modprobe`/`insmod`/`find`),走 `system_call_daemon` = **fork-exec shell**,**受 no-fork-shell OOM 约束** |
| 当前谁在用 | **三处**:`WorkModeRunner.cpp:441`(legacy `-wm` cascade,m2/m3 不走)、`net_app.cpp:421`(htc_net_app 冷启动联网工具,SD 卡常驻)、`RemoteCtrlClient.cpp:450`(仅读 SIM 号) |
| wm(-m 2/3)用了吗 | **没有**。wm 不跑 cascade(`wm_app.cpp:249` 注释),`commonStartupPostDispatch` 对 PTYPE_USB_DONGLE 只设 netif 名(`ProcessLifecycle.cpp:491-492`),**不执行 loadDriver/open/start**。**wm 当前完全无 4G 连网能力** |
| start() 漏调风险 | legacy `-wm` 和 htc_net_app 默认路径都**不调 `start()`**(net_app.cpp:442 `--usb-bringup` gate),已知风险 **T7-usb-no-start**(`net_app_logic.cpp:135-142`):不激活 context → usb0 无 carrier |

---

## A. UsbDongle 类定位与理解

### A.1 两个类(新旧并存)

| 类 | 文件 | 支持模块 | 关键 API 差异 | 状态 |
|----|------|---------|--------------|------|
| `network::UsbDongle` | `src/network/UsbDongle.{h,cpp}` | EC20/EC200A/EG800K/RG255AA(`UsbDongle.h:9`) | 有 `start()`/`stop()`/`loadDriver()`/`preconfig()`/`getIP()`/`ping()`/`getSignal()` | **活跃**(2026-07-08 改动) |
| `network::Usb4gDongle` | `src/network/Usb4gDongle.{h,cpp}` | EC20/EC200(`Usb4gDongle.h:9`) | **无** `start()`/`loadDriver()`,只有 open/close/getSimNumber + 一组私有 set* | 遗留,无 start 能力 |

> 下文统一指 `UsbDongle`(新版)。

### A.2 公开 API(`UsbDongle.h:10-29`)

```cpp
class UsbDongle {  // 单例(call_once)
public:
    static std::shared_ptr<UsbDongle> getInstance();        // :12
    void setModel(const UsbDongleModel &model);             // :13
    bool getSimNumber(std::string &sim_number);             // :14  AT+CNUM
    bool getIP(std::string &ip);                            // :15  AT+CGPADDR=1
    bool ping(const std::string &ip);                       // :16  AT+QPING
    bool getSignal(int &signal);                            // :17  AT+CSQ
    int  getScanMode();                                     // :18  AT+QCFG="nwscanmode"
    bool getAct(int &act);                                  // :19  AT+QNWINFO (LTE/NR5G...)
    bool sleep();                                           // :20  AT+QSCLK=1
    bool powerOff();                                        // :21  AT+QPOWD=0
    bool start();                                           // :22  ★激活 4G 数据上下文
    bool stop();                                            // :23  AT+QIDEACT=1 (去激活)
    bool preconfig();                                       // :24  USBnet=3 + scanmode + wakeup
    bool setSimPin(const std::string &pin);                 // :25
    bool open(const std::string &device_name="");           // :26  串口打开 + AT 端口探测
    bool close();                                           // :27
    bool loadDriver();                                      // :28  ★加载 usbnet/qmi_wwan/option 等 .ko
    ~UsbDongle();
};
```

- **单例**:`getInstance()` = `std::call_once`(`UsbDongle.cpp:187-193`),线程安全构造。但**实例本身的 AT 会话非线程安全**(单 `SerialPort io` 成员,无请求串行化——`UsbDongle.h:71`)→ **多线程并发调 AT 命令会撕裂串口**。
- **无回调/信号**:纯同步阻塞 API,每个 AT 命令内 `io.write` → `io.read(timeout 5000ms)` 轮询重试(典型 10 次)。

### A.3 4G 连网机制(关键)

**不是 pppd 拨号,是 AT 命令 + QMI/ECM USB 网卡模式**。证据链:

1. **`preconfig()`(`UsbDongle.cpp:1079-1091`)** 设 `AT+QCFG="usbnet",3`(`setNetType(3)`,`:368`/`:1084`)。
   - EC20/EC200x 文档:usbnet=3 = **ECM 模式**(dongle 枚举成 USB 以太网卡 `usb0`,host 直接 DHCP 取 IP);usbnet=1 = QMI 模式。
   - 对 EC200A/EG800K 额外 `AT+QNETDEVCTL=3,1`(`:1086`),对 EC20 无此步。
2. **`start()`(`UsbDongle.cpp:1057-1072`)**:
   ```
   clear → setEcho(0) → setErrMsgFmt(1) → querySimReady → [若 PIN required: setPinInternal]
         → getApn(AT+CIMI 解析 IMSI → 移动 cmnet/联通 uninet/电信 ctnet)   :949-988
         → setContextProfile(AT+QICSGP=1,1,"<apn>",...)                      :990-1010
         → activateContextProfile(AT+QIACT=1)  ★激活 PDP context            :1012-1034
   ```
   - `AT+QIACT=1` 让 dongle 在 USB 网卡上拿到 carrier/IP。
3. **激活后取 IP**:`getIP()` = `AT+CGPADDR=1`(`:657-688`),读 context 1 的 IP。但 host 侧 `usb0` 接口的地址通常由 **`udhcpc -i usb0`** 取(`net_app.cpp:453`、`Misc.cpp:497`)。
4. **依赖的设备节点**:AT 端口 = `/dev/ttyUSB0..6`,`probe()`(`:199-235`)逐个 open → 发 `AT\r\n` → 收 `OK` 则锁定。USB 网卡 = `usb0`(`app.h:30 USB_DONGLE_IFNAME`)。

**结论**:4G 数据通路 = dongle 内部 PPP-less(ECM),host 侧表现为 `usb0` 以太网卡 + `udhcpc`。**不依赖 pppd/qmicli/NetworkManager**。AT 控制面 = 串口读写,**零 fork**。

### A.4 fork-shell 风险点(重点核查)

| 方法 | 机制 | fork-shell? | 证据 |
|------|------|------------|------|
| AT 命令(`start`/`preconfig`/`getIP`/...) | `SerialPort`(POSIX open/read/write/select + termios) | **否**(OOM-safe) | `SerialPort.cpp:19-114`,全 `::open`/`::read`/`::write`/`select`,无 fork/exec/system |
| `probe()`(AT 端口探测) | SerialPort 逐个 open | **否** | `UsbDongle.cpp:199-235` |
| `loaded(module)` | `Misc::moduleLoaded` 读 `/proc/modules` | **否**(注释明说 OOM-safe) | `Misc.cpp:289-300`,`Misc.h:53` 注释 "read /proc/modules — no fork (OOM-safe)" |
| `have(cmd)` | `Misc::syscall("command -v ...")` | **是** | `UsbDongle.cpp:19-22` |
| `load_one(module)` | `Misc::syscall("modprobe ...")` + `Misc::popencall("find ...")` + `Misc::syscall("insmod ...")` | **是**(3 处) | `UsbDongle.cpp:30-86`(`:35` modprobe / `:52` find / `:72` insmod) |
| `loadDriver()` | `Misc::syscall("depmod -a")` + 循环 `load_one` | **是** | `UsbDongle.cpp:775-804`(`:779` depmod) |

- `Misc::syscall` → `system_call(cmd, timeout)`(`Misc.cpp:570-588`),`Misc::popencall` → `popen_call(cmd, out, ...)`(`Misc.cpp:590-608`)。
- 真机实现见 `sdk/include/systemcall/system_call.h:8-18`:**"跨进程的 system 调用",经 `system_call_daemon` 守护进程执行**——本质仍是 fork-exec `/bin/sh -c <cmd>`,只是隔离了句柄异常。**fork 一个 sh + 复制页表在 T32 MemFree 凤 MB 下即 OOM**(memory no-fork-shell-policy.md:2026-07-08 `wm -m 1` 因 `system("mount|grep")` 被 OOM kill 实证)。
- 仿真实现 `src/platform/sdk_stub/system_call_stub.c:21-30` 直接 `system(cmd)` / `popen(cmd)`。

> **风险定性**:`UsbDongle` 的 AT 控制面(`open`/`preconfig`/`start`/`stop`/查询)是 OOM-safe 的;**唯一的 fork-shell 集中在 `loadDriver()`**(加载内核模块)。若 wm 要用 4G 且 dongle 驱动已就绪,**只调 `open`+`preconfig`+`start` 是安全的**;若需 `loadDriver`,则有 OOM 风险。

### A.5 生命周期/资源

- **单例长驻**:`getInstance()` 进程级单例,析构 `close()`(`:182-185`)关串口。无显式 `stop()` 必须调用——但 `stop()`(`:1074-1077` = `AT+QIDEACT=1`)会断 PDP context。
- **无 ready 信号/事件**:所有 API 同步阻塞。判断"4G 就绪"只能轮询 `getIP()` 或 `Misc::isWifiConnected`-等价的 `getIPAddress("usb0")` + `getGatewayAddress("usb0")`(`net_app_logic.cpp:121-126` 的 `isNetworkUp`)。

---

## B. 当前使用点(谁在用、怎么用)

### B.1 使用点清单(grep `dongle` / `UsbDongle`)

| 文件:行 | 使用者 | 做什么 | 在哪个进程 |
|---------|--------|--------|-----------|
| `src/app/workmode/WorkModeRunner.cpp:440-455` | legacy `-wm` cascade `CMD_CONN_NET` | `loadDriver → open → preconfig`(**不 start**) | htc_main_app `-wm`(旧)、**wm 不走此路径** |
| `src/app/net_app.cpp:421-457` | `runUsb`(`htc_net_app usb` 子命令) | `loadDriver → open → [setModel] → preconfig → [--usb-bringup 时] start → startDHCP(usb0)` | **htc_net_app**(SD 卡常驻冷启动联网工具) |
| `src/network/RemoteCtrlClient.cpp:443-450` | 远程控制客户端 | `getSimNumber`(读 SIM 号上报),**非联网** | main_app remote ctrl |

### B.2 现有"启动 4G → 等待 → upload"调用链

**唯一的完整 4G→upload 链在 legacy htc_main_app `-wm` cascade**(`WorkModeRunner.cpp`),顺序由 `workModeToCommand`(`:865-902`)的 command bitmap 决定:

```
WORKING_MODE_SNAP_UPLOAD → CMD_SNAP | CMD_CONN_NET | CMD_DHCP | CMD_NTP | CMD_UPLOAD (:887)

runCommands 顺序(:360-863):
  CMD_SNAP      → processCmdSnap / processCmdVideoRecord  (:398-425)
  CMD_CONN_NET  → if PTYPE_USB_DONGLE:                    (:440-455)
                    loadDriver → open → preconfig          (★ 无 start,见 B.3 风险)
                  if PTYPE_WIFI: connectWifi              (:428-439)
  CMD_DHCP      → Misc::startDHCP()                        (:463-468) ← fork-shell(udhcpc)
  CMD_NTP       → Misc::ntpSyncAndWait                     (:470-488) ← fork-shell(busybox ntpd)
  CMD_UPLOAD    → MgmtServClient connect/auth → StorageServClient.uploadFile (:738-860)
```

> **这条链 wm -m 2/3 不跑**(见 C.2)。wm 只在 `commonStartupPostDispatch` 设 netif 名(`ProcessLifecycle.cpp:491`)。

### B.3 T7-usb-no-start 风险(已知)

`net_app_logic.cpp:135-142` `usbNeedsStartDefault()` 注释:

> main_app current behaviour: only `loadDriver -> open -> preconfig`. It **never calls `UsbDongle::start()`**, so the 4G data context is never activated (**risk T7-usb-no-start**). `--usb-bringup` flips this at the execution layer; this function locks the "main_app baseline" default = false.

即:legacy `-wm` 路径(`WorkModeRunner.cpp:440-455`)**不调 `start()`** → `usb0` 无 carrier → upload 连不上服务器。`htc_net_app` 用 `--usb-bringup` flag 显式打开(`net_app.cpp:442-450`)。**任何 wm 接入方案都必须显式调 `start()`**,否则重蹈 T7-usb-no-start。

### B.4 program_type 的来源

`ProcessLifecycle.cpp:459`:`program_type = config->get(INI_SECTION_BOOT, INI_KEY_PTYPE, PTYPE_NO_NET)`
- 宏:`Common.h:20-21` → `INI_SECTION_BOOT="BOOT"` / `INI_KEY_PTYPE="PType"`。
- 值:`Common.h:77-80` → `PTYPE_NO_NET=0 / PTYPE_WIFI=1 / PTYPE_USB_DONGLE=4 / PTYPE_ETHERNET=8`。
- 当前默认:`res/config.ini:49 PType=1`(WiFi 设备)。4G 场景需改 `PType=4`。

---

## C. quickSnap → wm 当前流程

### C.1 quickSnap(boot 首程序,`src/app/quick_snap.cpp`)

按 spec `quicksnap-app-spec.md` §7(12 步),quickSnap **不碰网络、不碰 4G**:

```
1. 记录启动时间戳
2. POWER_HOLD_PIN HIGH                    (:270-278)
3. 读 GPIO → mode                          (:295-300)
4. 读 quicksnap.json(5 字段)              (:302-308)
5. set timezone                            (:311-314)
6. syncSystemTime(RTC 优先 + MCU 兜底)     (:317)
7. force_upload 判定 + mode router         (:321-355)
8. [willSnap] HalProvider::start + doSnap(ImageSnap ≤8M) → /tmp/media/ + info.json  (:358-370)
9. [impInitialized] resetSharedVideo(IMP_System_Exit)    (:373-380)
10. [forceConsumed] 写回 force_upload=0     (:383-390)
11. fork+execv 下游(IMP 释放后,lean 时机)   (:392-441):
      SNAP_ONLY    → 不 spawn,return
      SNAP_UPLOAD  → wm -m 2
      UPLOAD_ONLY  → wm -m 3
      TEST_ONLY    → um
12. exit
```

- **fork+execv 无 shell**(`spawn()`,`:111-133`):`fork()` → 子关 FD 3..max → `execv(path, argv)`。**禁 system/system_call/popen**(spec §6,no-fork-shell-policy)。
- **不传 RTC 结果**给下游;wm/um 各自跑时间链(spec §4、§13)。

### C.2 wm(-m 2/3,`src/app/wm_app.cpp` + `wm_scheduler.cpp`)

**关键结论:wm 当前完全没有 4G/WiFi 连网代码**。证据:

1. `wm_app.cpp:249-258`:wm 把 `CMD_CONN_NET | CMD_DHCP | CMD_NTP` 塞进 `command` 变量,**仅为 `commonStartupPostDispatch` 的 S11 netif 名选择**(注释:"wm 不跑 cascade")。
2. `wm_app.cpp:261`:`commonStartupPostDispatch(cfg, command)` → `ProcessLifecycle.cpp:487-496` S11:
   - `PTYPE_USB_DONGLE` → **只** `Misc::setNetworkInterfaceName("usb0")`(`:491-492`)。**不 loadDriver、不 open、不 start、不 startDHCP**。
   - `PTYPE_WIFI` → 只 `setNetworkInterfaceName("wlan0")`(`:487-488`)。**不 connectWifi**。
3. 连网动作(loadDriver/open/start/connectWifi/startDHCP)只在 `WorkModeRunner.cpp:428-468`(cascade)里,**wm 不进 cascade**。
4. `wm_scheduler.cpp`:`runHeartbeat`(`:287-308`)直接 `MgmtServClient.connect(3000)` + `authenticate()` + `sendHeartbeat()`;`run`(`:192-285`)里 `UploadTask` 的 `ensureConnected`(`upload_task.cpp:138-166`)直接 `mgmt->connect(3000)`。**两者都假设底层网络已通,无任何 4G/WiFi 就绪检查或启动**。

→ **断层**:quickSnap 不联网,w commonStartupPostDispatch 只设 netif 名,**wm 实际启动后没有任何代码把 4G/WiFi 链路拉起来**。upload/heartbeat 会直接 `connect()` 失败。

> 注:当前 `res/config.ini PType=1`(WiFi),且 devtest 环境(devtest-automation-loop)由外部(devctl / htc_net_app)先把 WiFi 联通再跑 wm,所以 wm 在 devtest 下"看起来能用"。但**产品现场(4G 设备 PType=4)wm 自己不会联网**——这就是要调研接入的根因。

### C.3 upload 当前的网络假设

`StorageServClient`(`src/network/StorageServClient.cpp`):纯 TCP 上传(socket_fd + `sendWithTimeout`),**对底层网络零假设**——它只管往已 connect 的 socket 写。`MgmtServClient.connect()` 也只是 TCP connect。两者都依赖**有人先把 4G/WiFi 链路拉通**。

### C.4 wm -m 2/3 不 init IMP / 1-IMP-per-boot 兼容性

- spec §4:wm `-m 2/3` **不 init IMP**(upload/heartbeat 用网络栈,不碰 camera)→ 每 boot 最多 1 个 IMP 进程(quickSnap snap 那个)。
- UsbDongle 是纯网络类,**完全不碰 IMP/ISP/sensor** → 与 1-IMP-per-boot **零冲突**。接入 UsbDongle 不影响 IMP 边界。

---

## D. wm 接入 UsbDongle 4G 方案(文档建议,非代码)

> 纪律:本节是设计建议,不是代码改动。实现请走 `/feature`。

### D.1 接入位置与初始化顺序

**位置**:`wm_app.cpp` main 里,`commonStartupPostDispatch` 之后(已拿到 program_type + 设好 netif 名)、`WmScheduler.run()` 之前。具体在 `:302-315`(upload lane 配置)与 `:317`(调度器旋钮)之间,新增一段"uplink bring-up"。

```
wm_app.cpp main 现状:
  commonStartupPostDispatch(cfg, command)        :261   ← S11 设 netif 名(usb0)
  setCleanupHook(...)                            :267
  acquireTimeChain(ntpServer, ntpSynced)         :297   ← 时间链
  upload lane 配置(ms_ip/ms_port)               :302-315
  [新增] uplink bring-up(此处)                 ← PTYPE_USB_DONGLE 时拉 4G
  调度器旋钮                                      :317-323
  capture lane(仅 m0/m1)                        :325-348
  m2 ingest                                      :351-353
  WmScheduler.run()                              :356-358
```

**门控**:只在 `program_type == PTYPE_USB_DONGLE && wm_mode ∈ {UploadOnly, Heartbeat, CaptureUpload}` 时执行(m0 CaptureOnly 离线,不需要)。

**顺序(镜像 `net_app.cpp:403-464` 的 `runUsb`)**:
```
1. UsbDongle::getInstance()
2. [if 驱动未加载] loadDriver()        ← ★ fork-shell 风险,见 D.4
3. open()                              ← AT 端口探测(/dev/ttyUSB*),OOM-safe
4. setModel(<从 config 或默认 EC20>)
5. preconfig()                          ← usbnet=3 + scanmode + wakeup,OOM-safe
6. start()                              ← ★ 必须调(否则 T7-usb-no-start),OOM-safe
7. Misc::startDHCP("usb0")              ← fork-shell(udhcpc),见 D.4
8. [轮询] 等待 isNetworkUp(IP+gateway)  ← OOM-safe
```

### D.2 等待"4G 就绪"再做 upload

UsbDongle **无 ready 信号**,需轮询。两个层次的就绪:

1. **dongle 侧 PDP context 激活**:`start()` 返回 true 即 `AT+QIACT=1` 收到 OK(`UsbDongle.cpp:1012-1034`)。但 OK ≠ carrier 已上。
2. **host 侧 usb0 拿到 IP**:`Misc::getIPAddress("usb0")` 非空 **且** `Misc::getGatewayAddress("usb0")` 非空(`net_app_logic.cpp:121-126` `isNetworkUp`,纯 `/proc` + getifaddrs,OOM-safe)。

**推荐**:在 D.1 步骤 7 之后,轮询 `isNetworkUp` 带超时(如 30s,`net_app.cpp:459` 即此判定)。未就绪则 log + 让 scheduler 进入(UploadTask 的 `ensureConnected` 会 connect 失败 → park 等重试,最终 upload-timeout 60s 关机)——即**4G 未就绪不硬 abort,交给 UploadTask 的 connect-grace/timeout 机制兜底**(`upload_task.cpp:34-41` `connectGraceMs` 默认 30s)。

### D.3 与 wm -m 2/3 "不 init IMP" / 1-IMP-per-boot 的兼容性

- **完全兼容**。UsbDongle 不碰 IMP/ISP/sensor。wm -m 2/3 仍是 0 IMP/boot(quicksnap spec §4)。
- **注意 IMP 释放时序**:quickSnap 在 fork 下游前已 `resetSharedVideo()`(IMP_System_Exit)。wm 进程是 fork+execv 出来的新进程,**不继承 quickSnap 的 IMP 状态**(execv 后地址空间全新)→ 无 IMP residue 问题。

### D.4 fork-shell 约束(no-fork-shell-policy,重点)

T32 MemFree 凤 MB,`system()`/`popen()`/`Misc::syscall(命令串)`/`system_call(命令串)` 都走 fork-exec shell,会 OOM(2026-07-08 `wm -m 1` 实证)。UsbDongle 接入需审视:

| 步骤 | 是否 fork-shell | 建议 |
|------|----------------|------|
| `loadDriver()` | **是**(depmod/modprobe/find/insmod,`UsbDongle.cpp:775-804`) | ⚠️ **高风险**。建议:(a) 产品现场把 dongle 驱动做成**内核内置**(menuconfig `=y` 而非 `=m`),wm 永不 loadDriver;或 (b) 由 **htc_net_app**(SD 卡常驻,专责联网,OOM 预算更宽)在 wm 之前加载;或 (c) 改 `loadDriver` 用 `finit_module(2)`/`init_module(2)` syscall 直接加载(无 shell,memory no-fork-shell-policy 建议),需 `/feature` 改 `UsbDongle.cpp`。**wm 内直接调 loadDriver 是最差选项**。 |
| `open/preconfig/start/stop` + 查询 | 否(SerialPort POSIX) | ✅ 安全,wm 直接调 |
| `Misc::startDHCP("usb0")` | **是**(udhcpc,`Misc.cpp:497`) | ⚠️ udhcpc 是 fork-exec。替代:(a) ECM 模式下 dongle 可能已通过 DHCP/Router-Adv 给 usb0 分配,先查 `getIPAddress("usb0")` 非空则跳过;或 (b) 用 ioctl `SIOCSIFADDR` 手动设静态 IP(若 dongle 文档给出网段);或 (c) 接受 udhcpc fork 风险但**只在 lean 模式(m2/m3 无 DB/无 scanner,MemFree 更宽)调一次**。需实测 MemFree。 |
| `Misc::ntpSyncAndWait` | **是**(busybox ntpd,`Misc.cpp:511`) | 已在 wm 时间链 `acquireTimeChain` 里(`wm_app.cpp:297`),不在本次 4G 接入范围 |

> **最关键约束**:wm 若调 `loadDriver()`,几乎必 OOM。方案优先级:**驱动内置 > htc_net_app 预加载 > finit_module 改造 > wm 直接 loadDriver(最差)**。

### D.5 生命周期 / 资源

- **4G 是否常驻**:`UsbDongle` 单例常驻 wm 进程生命周期。wm 是 one-shot(upload 完 idle-grace 2s 关机,`wm_app.cpp:320`),关机时 `~UsbDongle` → `close()` 关串口即可,**无需 `stop()`(去激活 context)**——下次 boot 重新走 loadDriver→open→start。
- **与 WiFi 并存**:PTYPE 是单选(config `[BOOT] PType` 一项),wm 一次 boot 只走一种 uplink。`commonStartupPostDispatch` 已按 PTYPE 设好 netif 名,wm bring-up 按同一 PTYPE 走对应分支即可,不会 WiFi/4G 冲突。
- **RemoteCtrlClient 的 SIM 号读取**(`RemoteCtrlClient.cpp:450`):若 wm 复用 RemoteCtrlClient 且与 bring-up 共用同一 UsbDongle 单例,需注意**串口非线程安全**(A.2)——SIM 号读取与 start() 若并发会撕裂。建议 bring-up 完成后再启动 RemoteCtrlClient,或加串口互斥(`UsbDongle` 当前无私有 `io` 互斥,需 `/feature` 加)。

### D.6 PTYPE_WIFI 的对称问题(顺带)

wm 对 PTYPE_WIFI 同样**不 connectWifi**(C.2)。若产品有 WiFi 现场版(非 devtest 外部联网),wm 也需对称接入 `Misc::connectWifi`——但它也是 fork-shell(`Misc.cpp:455/477`,insmod + speedy/wpa_conn)。同一 OOM 约束适用。**本次调研只 4G,但 WiFi 接入是同类问题**。

---

## E. 待确认点(open questions)

1. **`loadDriver()` 在 wm 进程里是否真会 OOM**——取决于 wm m2/m3 lean 模式下的实际 MemFree。lean 跳过 DB/scanner/factory/update(`wm_app.cpp:199-211`),但 dongle 驱动模块(9 个 .ko,`UsbDongle.cpp:783-793`)加载后内核内存占用未实测。需 HW 实测 `/proc/meminfo` MemFree 前后差。
2. **ECM 模式(usbnet=3)下 `usb0` 是否需要 host 侧 udhcpc**——还是 dongle 自带 DHCP server 直接给?若不需 udhcpc,可省一个 fork-shell 点。需查具体 dongle 型号(EC20/EC200A/EG800K/RG255AA)的 Quectel 文档,或 HW 实测 `ifconfig usb0`(start() 后是否自动有 IP)。
3. **wm 现场版的完整 boot 流**——quickSnap → wm 之间是否还有 htc_net_app?(devtest 里 devctl 会跑 htc_net_app,但产品冷启动脚本未知)。若产品 boot 序列本就有 htc_net_app 先联网,wm 可能根本不需要自己接 4G(只需等 netif up)。**这是决定 D.1 方案是否必要的前提**——建议先确认产品 boot 流程。
4. **`start()` 的真实耗时**——AT+QIACT=1 激活 PDP 可能耗时数秒到数十秒(取决于网络注册),`UsbDongle.cpp:1018` 重试 10 次 × 5s = 最长 50s。是否超过 wm upload-timeout(60s,`wm_app.cpp:322`)或 connect-grace(30s)?需 HW 实测 start() 耗时分布。
5. **UsbDongle 单例串口非线程安全**(A.2)——若 wm 里 bring-up 线程与 UploadTask/heartbeat 并发访问,需加锁。当前 `UsbDongle` 无内部 `io` 互斥。需 `/feature` 加或调用方串行化。
6. **`Usb4gDongle`(旧类)是否还有引用**——`RemoteCtrlClient.cpp` 等是否混用新旧?本次未深查旧类引用(聚焦新类 UsbDongle)。建议后续清理时一并查。

---

## F. 关联文档与证据索引

- spec:`doc/knowledge/specs/quicksnap-app-spec.md`(§4 IMP、§6 fork+execv、§10 wm 外部依赖)
- memory:`quicksnap-boot-entry-spec.md`(1-IMP-per-boot)、`no-fork-shell-policy.md`(fork-shell OOM 实证)、`devtest-device-quirks.md`(wm mount fork OOM)
- 关键代码:
  - `src/network/UsbDongle.{h,cpp}`(类本体)
  - `src/common/utils/serial/SerialPort.cpp`(OOM-safe 串口)
  - `src/common/misc/Misc.cpp:289-300,486-505,570-608`(moduleLoaded/startDHCP/syscall/popencall)
  - `src/app/workmode/WorkModeRunner.cpp:428-468`(legacy CMD_CONN_NET cascade,wm 不走)
  - `src/app/net_app.cpp:403-464`(runUsb,唯一完整 4G→DHCP 链,带 --usb-bringup)
  - `src/app/net_app_logic.cpp:121-142`(isNetworkUp / usbNeedsStartDefault T7 风险注释)
  - `src/app/app_lifecycle/ProcessLifecycle.cpp:459,487-496`(program_type 读取 + S11 netif 选择)
  - `src/app/wm_app.cpp:249-261`(wm command 仅用于 netif,w 不跑 cascade)
  - `src/app/workmode/wm_scheduler.cpp:287-308`(runHeartbeat,无 4G)、`upload_task.cpp:138-166`(ensureConnected,无 4G)
  - `sdk/include/systemcall/system_call.h:8-18`(system_call 经 daemon = fork-exec)
  - `res/config.ini:49`(PType=1 = WiFi)、`src/common/Common.h:77-80`(PTYPE 值)
