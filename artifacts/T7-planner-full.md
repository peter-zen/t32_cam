# T7 Planner — Full Plan (htc_wifi_app → htc_net_app)

> 配套 report card: `artifacts/T7-planner-report.md`。本文件只规划，不写实现代码。批准后由 implementer 执行。
> 所有事实均亲自读码坐实，行号引用当前仓库（branch `merge_develop_simu`）。

---

## 0. 目标 / 非目标

**目标**
- 把独立 `htc_wifi_app` 扩展为统一网络管理应用 `htc_net_app`，保留 WiFi 能力（行为等价），新增 Ethernet + USB dongle(4G) 两种上行。
- 使 `htc_net_app` 能 cover `htc_main_app` 当前所有"建立网络上行"逻辑，作为独立可用工具。
- 双平台编译；PC 可单测纯逻辑层；真机回归脚本（留用户执行）。

**非目标（本阶段坚决不做）**
- 不改 `src/app/main_app.cpp`（零改动，git diff 验证）。
- 不改 `src/hal/**`（PIC-owned）。
- 不改 `src/hardware/mcu/MCU.cpp`。
- main_app 切换到 net_app 留下一阶段任务。
- 不补 main_app 既有 USB 缺陷行为（缺 `start()`）——照搬现状，缺陷标风险，`--usb-bringup` 开关由用户拍板是否启用。

---

## 1. 调研结论（亲自读码坐实）

### 1.1 PType 常量（Common.h:77-80）
```
PTYPE_NO_NET      0
PTYPE_WIFI        1
PTYPE_USB_DONGLE  4   ← input 写"待定"，坐实为 4
PTYPE_ETHERNET    8
```
INI 来源：`INI_SECTION_BOOT="BOOT"` / `INI_KEY_PTYPE="PType"`（Common.h:20-21）。
main_app 取值：`config->get(INI_SECTION_BOOT, INI_KEY_PTYPE, PTYPE_NO_NET)`（main_app.cpp:1220），`program_type` 即上行类型。

### 1.2 Ethernet 序列（坐实）
- main_app.cpp:1244-1245 对 `PTYPE_ETHERNET` 仅 `Misc::setNetworkInterfaceName(ETH_IFNAME)`。
- `ETH_IFNAME="eth0"`（app.h:32）。
- CMD_CONN_NET 块（main_app.cpp:1367-1399）只处理 `PTYPE_WIFI` 与 `PTYPE_USB_DONGLE`，`PTYPE_ETHERNET` 落 `else` → `goto main_exit`（"not support"，1396-1397）。即 **ETH 根本没有 connect 步骤**。
- DHCP（CMD_DHCP，1402-1407）对所有 PType 统一 `Misc::startDHCP()`（用 `netifname`，ETH 下即 eth0）。
- 全仓 grep `ifconfig` / `ip link set`：仅 `src/platform/tool/wpa_conn.cpp:140,245-250` 有 `ifconfig`，且都是 wlan0 专用（supplicant 启停链路），与 eth 无关。**ETH 无 ifconfig up / 静态 IP。**
- 结论：**Eth 序列 = `setNetworkInterfaceName("eth0")` + `startDHCP("eth0")`，照搬。**

### 1.3 USB dongle 序列（坐实 + 完整性结论 = 不完整，高风险）
- main_app.cpp:1379-1394（CMD_CONN_NET / PTYPE_USB_DONGLE）：
  - `usb_dongle->loadDriver()`（UsbDongle.cpp:778：depmod + insmod usbnet/cdc_ether/cdc_ncm/cdc_mbim/rndis_host/qmi_wwan/usbserial/usb_wwan/option）
  - `usb_dongle->open()`（UsbDongle.cpp:240：probe /dev/ttyUSB0..6 找 AT 口）
  - `usb_dongle->preconfig()`（UsbDongle.cpp:1082：setEcho/setErrMsgFmt/setNetType(3)/setDevCtrl(3,1) 仅 EC200A|EG800K/setNetScanMode(3,1)/setWakeupConfig）
  - **到此为止。没有 `start()`，没有 `activateContextProfile()`，没有 `setModel()`，没有 `setSimPin()`。**
- `UsbDongle::start()`（UsbDongle.cpp:1060）才是真正激活 4G 数据连接：
  `clear → setEcho(0) → setErrMsgFmt(1) → querySimReady → (SIM PIN 时 setPinInternal) → getApn → setContextProfile(apn) → activateContextProfile (AT+QIACT=1)`
- `activateContextProfile()`（1015）发 `AT+QIACT=1` —— 这是 PDP context 激活，4G 拿到 IP 的关键。
- 全仓 grep 证实：**`->start()` / `activateContextProfile` / `setSimPin` / `setModel` 在 src/tests 中除 UsbDongle 自身定义外零调用。**
- `USB_DONGLE_IFNAME="usb0"`（app.h:33）。
- DHCP（CMD_DHCP）对 USB 也只是 `startDHCP()`（usb0 上跑 udhcpc）——但 context 未激活时 usb0 无 carrier，udhcpc 超时失败。
- 结论：**main_app 的 USB 序列缺 `start()`，照搬后同样拿不到 IP。这是 main_app 既有缺陷，非本任务引入。** 默认忠实搬运（不偷偷补），`--usb-bringup` 开关（默认关）留给用户显式补 `start()`。

### 1.4 Usb4gDongle 已废弃
- `src/network/Usb4gDongle.{h,cpp}`：全仓 grep 仅自引用（自身 .h/.cpp 内），**无任何外部 #include 或调用**。
- `network` 共享库（src/network/CMakeLists.txt:3-11）只编 `UsbDongle.cpp`，不含 `Usb4gDongle.cpp`。
- 真正在用的是 `UsbDongle`。**net_app 只用 UsbDongle，不碰 Usb4gDongle。**

### 1.5 WiFi 现有能力（保留不变）
- `src/app/wifi_app.cpp`（main，355 行）：CLI 解析 → 凭据解析（CLI 优先，否则 MCU readUPID/readUPWD 带 retry）→ `decide()` → FRESH_CONNECT(connectWifi)/RECONNECT(wifi_reconnect::reconnectSSID)/REUSE/ABORT → post-probe L2 校验 → DHCP → MCU write-back 严格门控 → exit code。
- `wifi_app_logic.{h,cpp}`：纯函数 `decide/decisionExitCode/mayWriteBack/normalizeSsid`，`namespace wifi_app_logic`，无 syscall。
- `wifi_reconnect.{h,cpp}`：`currentSSID` / `reconnectSSID`（经 wpa_cli ctrl_iface `/tmp/wpa_supplicant` 优雅切网，不 kill supplicant，T5 修复保持）。sim 下 `currentSSID()` 返回 ""、`reconnectSSID()` 返回 false，不 segfault。
- exit code 契约（wifi_app_logic.h:13-19）：0 success / 2 driver load fail / 3 connect fail / 4 DHCP fail / 5 connected OK but MCU write-back gated / 6 arg or credential error。
- **WiFi 行为必须等价搬运：决策/reconnect/DHCP/MCU-writeback/exit code 全不变。** 只改 namespace、include、日志里的 app 名。

### 1.6 底层 API（三种上行复用，Misc.cpp）
- `Misc::connectWifi(ssid,pwd)` @ Misc.cpp:425（isWifiConnected 短路 → isWifiDriverLoaded→insmod 8189fs.ko→recheck → wpa_conn wlan0 …）。**WiFi 专用。**
- `Misc::startDHCP(ifname="")` @ Misc.cpp:473（`udhcpc -i <if> -t 10`；已有 IP 则跳过）。**三种上行都用。**
- `Misc::isWifiConnected(ifname="wlan0")` @ Misc.cpp:300（`!getIPAddress(ifname).empty() && !getGatewayAddress(ifname).empty()`，纯 POSIX getifaddrs + /proc/net/route）。**与上行类型无关，Eth/USB 也复用作"isNetworkUp"。**
- `Misc::setNetworkInterfaceName/getNetworkInterfaceName` @ Misc.cpp:415-423（设/读全局 `netifname`）。
- `Misc::getIPAddress/getGatewayAddress`（isWifiConnected 的两子条件，wifi_app.cpp 已用于 write-gate 调试）。

### 1.7 构建联动（已读 CMakeLists）
- `htc_wifi_app` target 定义在 `src/app/CMakeLists.txt`：
  - 第 5 行 `WIFI_APP_SOURCES` = `wifi_app.cpp`；第 6-9 行 `WIFI_APP_HELPER_SOURCES` = `wifi_app_logic.cpp` + `wifi_reconnect.cpp`。
  - 第 60 行 `add_executable(htc_wifi_app ${WIFI_APP_SOURCES} ${WIFI_APP_HELPER_SOURCES})`。
  - sim link（197-210）：`common_misc mcu logger common_utils_base64 crc16 jsoncpp sdk_stub pthread rt gcc stdc++`。
  - T32 link（358-371）：同上把 `sdk_stub` 换 `system_call`。
  - 第 393-396 行 `set_target_properties(htc_wifi_app ... OUTPUT_DIRECTORY bin)`。
- `test_wifi_app_logic`：`tests/CMakeLists.txt:106-126`，源 = `test_wifi_app_logic.cpp` + `wifi_app_logic.cpp`，仅 link `pthread rt gcc stdc++`（无 BUILD_FOR_SIMULATION gate，全平台编，但因只链纯逻辑层，安全）。
- `network` 库（src/network/CMakeLists.txt:3-11）含 `UsbDongle.cpp`，link 依赖 `setting env disk mcu common_time_rtc common_time_timezone power jsoncpp md5 pthread common_utils_base64 common_misc common_utils_serial`（第 37 行）。net_app 需新增 link `network`。
- **关键**：net_app 只 link 预编的 `network.so`（不重编 `UsbDongle.cpp`），其依赖链由 `.so` 承载；sim 侧参照 `htc_main_app` 的 sim link 块（app/CMakeLists.txt:102-150，已含 `network`）确认 stub 闭合。
- 全仓 grep `htc_wifi_app|wifi_app_logic|wifi_reconnect|test_wifi_app`：引用全部自包含在 `src/app/` + `tests/`，**无外部调用者**，改名安全。

---

## 2. 源 → 目标映射表

| 当前（source） | 目标（target） | 动作 |
|---|---|---|
| `src/app/wifi_app.cpp` | `src/app/net_app.cpp` | **改名 + 扩展**：保留 WiFi 分支（runWifi，原样）；新增 `runEth()` / `runUsb()`；CLI 增 `--type`/`--usb-bringup`/`--usb-model`；include/namespace 换 |
| `src/app/wifi_app_logic.h` | `src/app/net_app_logic.h` | **改名 + 扩展**：namespace `wifi_app_logic→net_app_logic`；保留 `decide/decisionExitCode/mayWriteBack/normalizeSsid/ExitCode/Decision/LinkState/Target`（WiFi 专属）；新增 `NetType/parseNetType/ptypeToNetType/netTypeIfname/isNetworkUp/ethNeedsConnect/usbNeedsStart` |
| `src/app/wifi_app_logic.cpp` | `src/app/net_app_logic.cpp` | **改名 + 扩展**：namespace 同改；原 4 函数原样；新增 6 函数实现（纯函数，无 syscall） |
| `src/app/wifi_reconnect.{h,cpp}` | `src/app/wifi_reconnect.{h,cpp}` | **保留不改**（WiFi 专属语义，ETH/USB 不用；改名 net_reconnect 反而误导） |
| `src/app/CMakeLists.txt` | 同 | target `htc_wifi_app→htc_net_app`；source list 改名；T32/sim link 块加 `network` |
| `tests/test_wifi_app_logic.cpp` | `tests/test_net_app_logic.cpp` | **改名 + 扩展**：include/namespace 换；原 10 case 全迁；新增 NetType/Eth/USB 决策 case |
| `tests/CMakeLists.txt` | 同 | target `test_wifi_app_logic→test_net_app_logic`；源文件名换 |
| `src/app/main_app.cpp` | — | **零改动**（git diff 验证） |
| `src/hal/**` / `MCU.cpp` / `Misc.*` / `src/network/**` | — | **零改动** |
| `script/regress_wifi_real.sh` | `script/regress_net_real.sh`（新增） | 参照 wifi 脚本，增 Eth/USB 真机回归用例 |

---

## 3. CLI 统一入口（待设计点 1，推荐方案）

**推荐：`--type wifi|eth|usb` 显式 + 缺省读 INI `BOOT/PType`。**

### 3.1 CLI 形态（net_app.cpp 新增）
```
htc_net_app --type {wifi|eth|usb} [上行专属选项] [通用选项]
  --type <t>      上行类型 wifi|eth|usb；缺省读 INI BOOT/PType(映射同 main_app)
                  (WIFI=1→wifi, USB_DONGLE=4→usb, ETHERNET=8→eth, 其它/缺省→报错退 6)
  --- WiFi 专属 ---
  --ssid <SSID>   (沿用) 目标 SSID，缺省读 MCU UPID
  --pwd <PWD>     (沿用) 目标密码，缺省读 MCU UPWD
  --if <name>     (沿用) wlan 接口名，缺省 wlan0
  --write-mcu     (沿用) 成功后回写 MCU(仅 wifi 有意义)
  --- Ethernet 专属 ---
  (无；--if 缺省 eth0)
  --- USB 专属 ---
  --usb-bringup   额外调 UsbDongle::start()(激活 4G context)；默认关(忠实搬运 main_app 现状)
  --usb-model <m> dongle 型号 EC20|EC200A|EG800K|RG255AA；仅 --usb-bringup 时生效；缺省 EC20
  --- 通用 ---
  --no-dhcp       跳过 DHCP
  -v, --verbose
  -h, --help
```

### 3.2 理由
- 与 main_app `program_type`（INI `BOOT/PType`，main_app.cpp:1220）同源 → 下一阶段 main_app 切换零语义差。
- 显式 `--type` 便于工具手动调试 / CI 单上行验证。
- INI 兜底避免工具必须带参（与现 wifi_app "无参读 MCU" 风格一致）。
- **不读 INI 的 WiFi 凭据坑保留**（wifi_app 刻意不读 INI `SYS/UPID+UPWD`，net_app 的 wifi 分支同样只 CLI/MCU 取凭据）。INI 只读 `BOOT/PType`（类型，非凭据），不引入 `INI_KEY_UPWD="PWD"` 命名坑。

### 3.3 类型解析（纯函数，可单测）
- `parseNetType(string)→NetType|nullopt`：`"wifi"→WIFI`，`"eth"→ETH`，`"usb"→USB`，其它 invalid。
- `ptypeToNetType(int ptype)→NetType|nullopt`：`1→WIFI`，`4→USB`，`8→ETH`，其它(0/2/3/...) invalid。
- main：`--type` 给定 → `parseNetType`；否则读 INI `BOOT/PType` → `ptypeToNetType`；都无效 → exit 6。

---

## 4. Ethernet 连接序列（待设计点 2，坐实）

**`runEth()`（net_app.cpp）严格照搬 main_app 对 ETH 的处理：**
1. `Misc::setNetworkInterfaceName("eth0")`（= ETH_IFNAME, app.h:32）。
2. （可选）若 `!args.noDhcp`：`Misc::startDHCP("eth0")`；失败 → exit 4。
3. `Misc::isWifiConnected("eth0")`（= isNetworkUp：IP+gateway 非空）为真 → exit 0；否则 exit 3（连接失败，这里指链路没起来）。

**不做的事（与 main_app 一致）**：无 `ifconfig up`、无静态 IP、无 connect 步骤。Eth 全靠 DHCP 拉起（DHCP client 通常会自动 bring up 接口）。

**exit code**：0 成功 / 3 链路未起 / 4 DHCP 失败 / 6 参数错。无 MCU 回写（不走 5）。

**sim 行为**：eth0 可能不存在/无 IP → startDHCP 失败 → exit 4（预期）；纯逻辑层 `netTypeIfname(ETH)=="eth0"`、`ethNeedsConnect()==false` 可单测。执行层 sim 无设备即 false/早退，不 segfault。

---

## 5. USB dongle 序列（待设计点 3，坐实 + 完整性结论）

### 5.1 默认序列（忠实搬运 main_app 现状，`--usb-bringup` 关）
**`runUsb()`（net_app.cpp）：**
1. `Misc::setNetworkInterfaceName("usb0")`（= USB_DONGLE_IFNAME, app.h:33）。
2. `auto dongle = UsbDongle::getInstance();`
3. `dongle->loadDriver()`；失败 → exit 2（driver load fail，与 WiFi driver load fail 语义对齐）。
4. `dongle->open()`；失败 → exit 3。
5. `dongle->preconfig()`；失败 → exit 3。
6. （默认不调 `start()` —— 忠实搬运 main_app 现状）
7. `!args.noDhcp` → `Misc::startDHCP("usb0")`；失败 → exit 4。
8. `Misc::isWifiConnected("usb0")` 为真 → exit 0；否则 exit 3。

**注意（main_app 既有缺陷，照搬即继承）**：步骤 6 不调 `start()` → context 未激活 → usb0 无 carrier → 步骤 7 udhcpc 大概率超时 → exit 4。**这是 main_app 既有行为，非本任务回归。** `usbNeedsStart()` 纯函数反映"main_app 现状不调 start()=false"。

### 5.2 `--usb-bringup`（用户显式补 start()，默认关）
当 `--usb-bringup` 给定：
- 步骤 4 `open()` 后、步骤 5 `preconfig()` 前，先 `dongle->setModel(<--usb-model 解析>)`（缺省 EC20）。
- 步骤 5 `preconfig()` 后、步骤 7 前，插入：
  - （可选）`--usb-sim-pin` 给定则 `dongle->setSimPin(pin)`（`start()` 内 SIM PIN 时 `setPinInternal`）。
  - `dongle->start()`（UsbDongle.cpp:1060：querySimReady→getApn→setContextProfile→activateContextProfile/AT+QIACT=1）；失败 → exit 3。
- 其余不变。

**为何默认关**：硬约束"WiFi 行为等价 / 忠实搬运 main_app / 不偷偷补行为"。补 `start()` 是行为变更，必须用户拍板。本规划**不擅自打开默认**，留 decision `T7-usb-no-start` + risk `T7-usb-no-start` 给用户在 implement 前定 `--usb-bringup` 默认值（建议默认关，与 main_app 一致）。

### 5.3 exit code
0 成功 / 2 loadDriver 失败 / 3 open/preconfig/start 失败 / 4 DHCP 失败 / 6 参数错。无 MCU 回写（不走 5）。

### 5.4 sim 行为
sim 下 `UsbDongle::probe()`(open) 找 /dev/ttyUSBx 全失败 → `open()` 返回 false → exit 3（预期，sim 无硬件）。`loadDriver` 的 modprobe/insmod 在 PC 失败但不 segfault。纯逻辑层 `netTypeIfname(USB)=="usb0"`、`usbNeedsStart()` 可单测。

### 5.5 关键风险（详见 report card risk）
- `T7-usb-no-start`（high）：照搬缺 start() → 无 IP。
- `T7-usb-setmodel-unset`（medium）：model 默认 EC20，实际硬件非 EC20 时 preconfig 分支走错；`--usb-bringup`+`--usb-model` 让用户显式设。

---

## 6. 纯逻辑层扩展（待设计点 5）

`net_app_logic.{h,cpp}` 在保留原 WiFi 专属函数（`decide/decisionExitCode/mayWriteBack/normalizeSsid` + `Decision/LinkState/Target/ExitCode`）基础上，新增以下**纯函数（无 syscall，可 PC 单测）**：

```cpp
namespace net_app_logic {
// ... 原 WiFi 符号原样保留 ...

enum NetType { NET_WIFI, NET_ETH, NET_USB, NET_INVALID };

// CLI "--type" 字符串 → NetType；invalid 返回 NET_INVALID（或用 optional，implementer 定）
NetType parseNetType(const std::string &s);      // "wifi"/"eth"/"usb"

// INI PType 整数 → NetType；坐实 WIFI=1/USB_DONGLE=4/ETHERNET=8
NetType ptypeToNetType(int ptype);

// NetType → 接口名（与 app.h:31-33 同源）
std::string netTypeIfname(NetType t);            // WIFI→wlan0 ETH→eth0 USB→usb0

// "网络可用"判定语义：IP 非空 且 gateway 非空（= Misc::isWifiConnected，与上行类型无关）。
// 纯函数版：caller 传入 ip/gateway 字符串（执行层从 Misc::getIPAddress/getGatewayAddress 取）。
bool isNetworkUp(const std::string &ip, const std::string &gateway);

// Eth 是否需要 connect 步骤：main_app 现状=否（仅 setIfname+DHCP）。记录决策，便于单测锁定语义。
bool ethNeedsConnect();

// USB 是否需要 start()：反映 main_app 现状（不调 start()=false）。--usb-bringup 在执行层翻转，
// 此函数返回"main_app 现状基准"=false，便于单测记录"默认照搬不补"。
bool usbNeedsStartDefault();
} // namespace net_app_logic
```

**设计原则**：
- 所有新函数无 syscall（`isNetworkUp` 只比较两个 string，不调 Misc），保证 `test_net_app_logic` 只链 `net_app_logic.cpp` 即可全测（沿用 test_wifi_app_logic 风格）。
- 原 WiFi 函数 `decide/mayWriteBack` 等**不改**（WiFi 行为等价）；只是 namespace 换名。
- `NetType`/`parseNetType`/`ptypeToNetType` 让"类型决策"可单测，避免逻辑埋在 main 的 syscall 路径里。

---

## 7. 命名重构联动清单（待设计点 4，最小且一致）

**改名**：
| 旧 | 新 |
|---|---|
| `src/app/wifi_app.cpp` | `src/app/net_app.cpp` |
| `src/app/wifi_app_logic.h` | `src/app/net_app_logic.h` |
| `src/app/wifi_app_logic.cpp` | `src/app/net_app_logic.cpp` |
| `tests/test_wifi_app_logic.cpp` | `tests/test_net_app_logic.cpp` |
| 二进制 `htc_wifi_app` | `htc_net_app` |
| namespace `wifi_app_logic` | `net_app_logic` |
| test target `test_wifi_app_logic` | `test_net_app_logic` |

**保留不改**：
- `src/app/wifi_reconnect.{h,cpp}` + `namespace wifi_reconnect`（WiFi 专属；net_app 的 wifi 分支仍 `#include "wifi_reconnect.h"` 调 `wifi_reconnect::currentSSID/reconnectSSID`。改名 net_reconnect 会让"ETH/USB 也用 reconnect"的误导产生，而实际上只有 WiFi 用 → 保留更诚实）。

**CMake 联动（src/app/CMakeLists.txt）**：
- 第 5 行 `WIFI_APP_SOURCES` → `NET_APP_SOURCES` = `${SOURCES}/app/net_app.cpp`。
- 第 6-9 行 `WIFI_APP_HELPER_SOURCES` → `NET_APP_HELPER_SOURCES` = `net_app_logic.cpp` + `wifi_reconnect.cpp`（wifi_reconnect 文件名不变）。
- 第 60 行 `add_executable(htc_wifi_app ...)` → `add_executable(htc_net_app ${NET_APP_SOURCES} ${NET_APP_HELPER_SOURCES})`。
- sim link 块（197-210）：target 名 `htc_wifi_app→htc_net_app`；**新增 `network`**（UsbDongle 所在）。
- T32 link 块（358-371）：target 名换；**新增 `network`**。
- 第 393-396 行 `set_target_properties(htc_wifi_app ...)` → `htc_net_app`。
- 注释里 `htc_wifi_app` → `htc_net_app`（193,195,355,356 行）。

**CMake 联动（tests/CMakeLists.txt）**：
- 第 106-109 行 target `test_wifi_app_logic` → `test_net_app_logic`，源 `test_net_app_logic.cpp` + `${CMAKE_SOURCE_DIR}/src/app/net_app_logic.cpp`。
- 第 111-113 行 include 目录不变（`${CMAKE_SOURCE_DIR}/src/app`）。
- 第 115-126 行 target 名换、`set_target_properties` 换。

**注释/字符串联动（net_app.cpp）**：
- 顶部块注释 `htc_wifi_app` → `htc_net_app`，更新分层说明（加 Eth/USB 分支）。
- `wifi_app_logic` → `net_app_logic`（include + using）。
- 日志里 `"htc_wifi_app ..."` 字符串 → `"htc_net_app ..."`（第 156,166,219,353 行等）。

**脚本联动**：
- 新增 `script/regress_net_real.sh`（参照 `regress_wifi_real.sh` 结构），换 `BIN=/mnt/huntcam/bin/htc_net_app`，增 Eth/USB 用例。原 `regress_wifi_real.sh` 保留（仍可跑 wifi 子集，或并入新脚本）。

**零改动验证**：
- `git diff --stat main -- src/app/main_app.cpp src/hal src/hardware/mcu/MCU.cpp src/common/misc/Misc.cpp src/network` 必须 0 行。
- `grep -rn htc_wifi_app src tests`（排除 build）应只剩历史 doc/script 引用（无源码/CMake 残留）。

---

## 8. 实现步骤（编号、可执行，给 implementer）

> 每步可独立编译验证。implementer 严格按序，不跳步。

**S1. 命名迁移（机械改名，零行为变化）**
1. `git mv src/app/wifi_app.cpp src/app/net_app.cpp`；`git mv src/app/wifi_app_logic.{h,cpp} src/app/net_app_logic.{h,cpp}`；`git mv tests/test_wifi_app_logic.cpp tests/test_net_app_logic.cpp`。
2. `net_app_logic.h/.cpp`：namespace `wifi_app_logic→net_app_logic`；include guard 不变（WIFI_APP_LOGIC_H→NET_APP_LOGIC_H 防御性改）。
3. `net_app.cpp`：`#include "wifi_app_logic.h"→"net_app_logic.h"`；`using namespace wifi_app_logic→net_app_logic`；块注释/日志字符串 app 名换；`#include "wifi_reconnect.h"` 不变。
4. `tests/test_net_app_logic.cpp`：include/using 换；文件头注释换。
5. `src/app/CMakeLists.txt`：按 §7 改 target/source 名（**先不加 network**，本步只验改名）。
6. `tests/CMakeLists.txt`：按 §7 改 test target/源名。
7. 验证：`cmake --build build_sim -j$(nproc) --target htc_net_app` + `--target test_net_app_logic` 全绿；`./build_sim/bin/test_net_app_logic` 全 PASS；`./build_sim/bin/htc_net_app --help` 退 0。**此时 WiFi 行为应与改名前完全等价（因还没加 --type，CLI 仍是旧 wifi 语义）。**

**S2. 纯逻辑层扩展（net_app_logic）**
1. `net_app_logic.h`：新增 `enum NetType` + 6 函数声明（§6）。
2. `net_app_logic.cpp`：实现 6 函数（纯，无 syscall）。`ptypeToNetType` 用 `PTYPE_WIFI=1/PTYPE_USB_DONGLE=4/PTYPE_ETHERNET=8`（include Common.h 或本地常量，避免引入重依赖；implementer 可在 .cpp 内 `#include "Common.h"`，net_app_logic 当前只 `#include <algorithm>/<string>`，加 Common.h 需确认 test include 路径含 common——tests/CMakeLists.txt:111-113 当前只含 `src/app`，故 **implementer 应在 ptypeToNetType 实现里用字面常量 1/4/8 + 注释指明 Common.h 出处，避免 test 链 Common.h**，保持 test 零重依赖）。
3. `test_net_app_logic.cpp`：新增 case（§9）。
4. 验证：`test_net_app_logic` 全绿（含新 case）。

**S3. CLI 扩展（net_app.cpp main）**
1. `CliArgs` 增 `std::string typeStr; bool haveType=false; bool usbBringup=false; std::string usbModel="EC20";`（保留原 wifi 字段）。
2. `parseArgs` 增 `--type`/`--usb-bringup`/`--usb-model` 解析；未知选项仍 exit 6。
3. `printUsage` 按 §3.1 更新。
4. main：解析 NetType（`--type`→`parseNetType`，否则读 INI `BOOT/PType`→`ptypeToNetType`；都无效 exit 6）。
5. **读 INI**：net_app 需读 `BOOT/PType`。当前 wifi_app 不读 INI（刻意）。implementer 用 `DeviceConfig::getInstance()->get(INI_SECTION_BOOT, INI_KEY_PTYPE, 0)`（同 main_app.cpp:1220）或 `Settings`。**注意**：读 INI 引入 config 依赖，需在 CMake link 块加 `setting`/`env`/`devconf`/`jsoncpp`（参照 daemon_app link）。若 implementer 评估读 INI 依赖太重，**退化方案**：本阶段 `--type` 必填（不读 INI），把 INI 自动选留到下一阶段（与 main_app 切换一起做）。→ **建议 implementer 先试 `--type` 必填 + 不读 INI（最小依赖、零 INI 坑），INI 自动选作为可选增强；若用户要 INI 兜底再加 config 依赖。此点列入"需用户拍板"。**
6. 验证：`./htc_net_app --type wifi --no-dhcp`（sim 下退 6 或 3，不 segfault）；`--type bogus` 退 6；`--help` 退 0。

**S4. Ethernet 分支（runEth）**
1. net_app.cpp 加 `runEth(const CliArgs&)`：按 §4 序列。
2. main：`switch(netType)` 调 `runWifi/runEth/runUsb`。
3. 验证：sim `--type eth --no-dhcp` 不 segfault（startDHCP 失败或 isWifiConnected false → 退 3/4）。

**S5. USB 分支（runUsb）**
1. net_app.cpp 加 `#include "UsbDongle.h"`；`runUsb(const CliArgs&)`：按 §5.1 默认序列。
2. `--usb-bringup` 路径：按 §5.2 插入 setModel/setSimPin(可选)/start()。
3. CMake：net_app link 块加 `network`（sim + T32 两块）。
4. 验证：sim `--type usb --no-dhcp` 不 segfault（loadDriver/open 失败 → 退 2/3）；双平台编译通过。

**S6. WiFi 分支等价性回归**
1. 确认 `runWifi` = 原 wifi_app main 体（原样搬入函数，仅 namespace/include/日志换）。
2. `--type wifi` 路径与改名前 htc_wifi_app 逐行等价。
3. 验证：`test_net_app_logic` 全绿；真机 `regress_wifi_real.sh` 换 BIN 名后重跑（留用户）。

**S7. 真机回归脚本**
1. 新增 `script/regress_net_real.sh`：参照 `regress_wifi_real.sh`，BIN=`/mnt/huntcam/bin/htc_net_app`，加 Eth 用例（setIfname+DHCP→断言 IP/gateway）、USB 用例（loadDriver→open→preconfig→（--usb-bringup start）→DHCP→断言 IP/gateway；记录"不带 --usb-bringup 预期无 IP"为已知缺陷断言）。
2. 脚本留用户执行（同 T5/T6 口径，不卡 CI）。

**S8. 双平台编译 + 验收 grep**
1. `cmake -DBUILD_FOR_SIMULATION=ON -B build_sim -S . && cmake --build build_sim -j$(nproc) --target htc_net_app --target test_net_app_logic`。
2. `cmake -DCMAKE_TOOLCHAIN_FILE=toolchain.cmake -B build -S . && cmake --build build -j$(nproc) --target htc_net_app`。
3. `git diff --stat main -- src/app/main_app.cpp src/hal src/hardware/mcu/MCU.cpp src/common/misc src/network` → 0 行。
4. `grep -rn "htc_wifi_app" src tests`（排除 build）→ 无源码/CMake 残留。
5. `test ! -e build/bin/htc_wifi_app && test -e build/bin/htc_net_app`。

---

## 9. 测试用例清单

### 9.1 PC 单测（test_net_app_logic，纯逻辑，无 syscall）
**迁移自 test_wifi_app_logic（保持断言，namespace 换）**：
- testDecisionReuse / Reconnect / ReconnectWhenConnectedButNoSsidReported / FreshConnect / AbortNoCredentials / CaseSensitiveSsid
- testDecisionExitCodeMapping（0/3/3/6）
- testMayWriteBackGate（5 子条件）
- testNormalizeSsidTrimsOnly（7 断言）
- testEndToEndDecisionThenGate

**新增（NetType / Eth / USB 决策）**：
- testParseNetType：`"wifi"→NET_WIFI`、`"eth"→NET_ETH`、`"usb"→NET_USB`、`"ETH"/"WiFi"/""/"bogus"→NET_INVALID`（大小写敏感，与 SSID 一致风格）。
- testPtypeToNetType：`1→WIFI`、`4→USB`、`8→ETH`、`0/2/3/9→NET_INVALID`（锁定 Common.h 常量）。
- testNetTypeIfname：`WIFI→"wlan0"`、`ETH→"eth0"`、`USB→"usb0"`、`NET_INVALID→""`（app.h:31-33 同源）。
- testIsNetworkUp：`("192.168.1.5","192.168.1.1")→true`、`("","1.2.3.4")→false`、`("1.2.3.4","")→false`、`("","")→false`（与 Misc::isWifiConnected 语义锁定）。
- testEthNeedsConnect：`ethNeedsConnect()==false`（锁定"ETH 无 connect 步骤"语义）。
- testUsbNeedsStartDefault：`usbNeedsStartDefault()==false`（锁定"main_app 现状不调 start()"，--usb-bringup 在执行层翻转）。

### 9.2 sim 冒烟（不 segfault 即过）
- `./build_sim/bin/htc_net_app --help` → 退 0。
- `--type wifi --no-dhcp`（无 MCU/sim）→ 退 6（无凭据）或 3，不崩。
- `--type eth --no-dhcp` → 退 3/4（无 eth0/无 IP），不崩。
- `--type usb --no-dhcp` → 退 2/3（无 /dev/ttyUSBx），不崩。
- `--type bogus` → 退 6。

### 9.3 真机回归（regress_net_real.sh，留用户）
- **WiFi**（等价回归）：沿用 regress_wifi_real.sh 用例 A-F（切网/wpa_supplicant PID 不变/MCU 回写）。
- **Ethernet**：`htc_net_app --type eth` → 断言 `getIPAddress(eth0)` 非空 且 gateway 非空；`--no-dhcp` 跳过 DHCP。
- **USB（默认序列）**：`htc_net_app --type usb` → 记录 exit code；**预期（main_app 既有缺陷）：无 IP（start() 未调）**，脚本标注为"已知缺陷断言"——若用户希望 USB 真能用，跑 `--type usb --usb-bringup --usb-model <实机型>` 并断言 IP/gateway。
- **USB（--usb-bringup）**：`htc_net_app --type usb --usb-bringup --usb-model EC200A` → 断言 `getIPAddress(usb0)` 非空 + gateway 非空。

---

## 10. 风险（详见 report card add_risk，此处补缓解动作）

| key | severity | 缓解（implementer 动作） |
|---|---|---|
| T7-usb-no-start | high | 默认照搬不补；`--usb-bringup` 开关默认关；full §5.2 给补 start() 序列；真机脚本断言 IP/gateway 暴露缺陷；**implement 前请用户拍板默认值** |
| T7-usb-setmodel-unset | medium | 照搬不设 model；`--usb-bringup`+`--usb-model` 让用户显式设；不擅自默认 |
| T7-eth-no-carrier-on-sim | medium | sim 只验编译+纯逻辑+不 segfault；ETH 真机回归留用户 |
| T7-rename-cmake-linkage | medium | full §7 精确联动清单；验收 grep 证无 wifi_app 残留 |
| T7-network-lib-sim-deps | medium | net_app 只 link 预编 network.so（不重编 UsbDongle.cpp）；参照 htc_main_app sim link 闭合；若 serial 无 stub 由 .so 承载 |
| T7-wifi-equivalence-regression | high | runWifi 原样搬；test 迁移保断言；真机回归换名重跑；--type wifi=旧行为 |
| T7-ini-credential-pitfall | medium | wifi 分支仍不读 INI 凭据（CLI/MCU）；INI 只读 BOOT/PType（类型）；若退化到 --type 必填则根本不读 INI，零坑 |

---

## 11. 回滚点
- 全部改动在 `src/app/net_app*`、`src/app/wifi_reconnect.*`（保留）、`src/app/CMakeLists.txt`、`tests/test_net_app_logic.cpp`、`tests/CMakeLists.txt`、`script/regress_net_real.sh`。
- 回滚 = `git checkout` 上述路径 + `git mv` 还原 wifi_app* 命名。
- `main_app` / `hal` / `MCU.cpp` / `Misc.*` / `src/network` 零改动 → 回滚干净，无副作用。

---

## 12. 需用户拍板的关键决策点（implement 前）
1. **`--usb-bringup` 默认值**：默认关（忠实搬运 main_app，USB 无 IP，已知缺陷）还是默认开（补 start()，USB 能用但偏离 main_app 现状）？→ **planner 推荐默认关**（守"不偷偷补行为"硬约束），让 USB 真正可用作为用户显式 opt-in。
2. **CLI 是否读 INI `BOOT/PType` 兜底**：读 INI（与 main_app 同源、需加 config 依赖）还是 `--type` 必填（最小依赖、零 INI 坑）？→ **planner 推荐本阶段 `--type` 必填**（最小依赖、避 INI 坑、与"不读 INI 凭据"风格一致），INI 自动选留到下一阶段与 main_app 切换一起做。
3. **`regress_wifi_real.sh` 去留**：并入新 `regress_net_real.sh` 还是保留？→ **推荐保留 + 新增 net 脚本**（wifi 脚本仍可单独跑 wifi 子集，减少回归面）。
