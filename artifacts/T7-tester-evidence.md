# T7 Tester — Evidence

owner: tester · task: T7 · node: tester · flow: feature · date: 2026-06-17

本文件汇总 T7 tester 节点的全部独立复跑证据(命令真实输出、git diff、grep、WiFi 等价审计、脚本审计)。
tester 不信 implementer 自报,所有关键项均亲自重跑。

---

## 0. 工作区状态(复跑起点)

```
$ git status --short
 M .gitignore                                        (T7 流程既有，非 implementer 改)
 M orchestration-state.yaml                          (PM/planner 既有)
 M src/app/CMakeLists.txt
RM src/app/wifi_app.cpp -> src/app/net_app.cpp
RM src/app/wifi_app_logic.cpp -> src/app/net_app_logic.cpp
RM src/app/wifi_app_logic.h -> src/app/net_app_logic.h
 M tests/CMakeLists.txt
RM tests/test_wifi_app_logic.cpp -> tests/test_net_app_logic.cpp
?? script/regress_net_real.sh
```
HEAD = `8df7fe8 feat(wifi): add standalone htc_wifi_app`。工作区与 implementer 自报一致。

---

## 1. 复跑 #1 — PC 仿真构建 (build_sim)

```
$ cmake --build build_sim -j$(nproc) --target htc_net_app --target test_net_app_logic
[ 13%] Built target crc16 / easylogger / sdk_stub / env / md5 / jsoncpp
[ 33%] Built target logger / devconf
[ 40%] Built target common_misc
[ 46%] Built target setting
[ 53%] Built target common_time_timezone / common_time_rtc / common_utils_base64
[ 60%] Built target common_utils_crc / common_utils_serial
[ 66%] Built target disk
[ 80%] Built target power / mcu
[ 93%] Built target network
[100%] Built target htc_net_app
[100%] Built target test_net_app_logic
===SIM_BUILD_RC=0===
```
**结果: 重建成功 (rc=0)。** htc_net_app + test_net_app_logic 均生成。

## 2. 复跑 #2 — T32 交叉构建 (build/, toolchain.cmake, uclibc)

```
$ cmake --build build -j$(nproc) --target htc_net_app
[  0%] Built target crc16 ... (同 sim 链路)
[ 88%] Built target network
[100%] Built target htc_net_app
===T32_BUILD_RC=0===
```
**结果: T32 重建成功 (rc=0)。** build/ 已预配(BUILD_FOR_SIMULATION=OFF + toolchain.cmake),无需重新 configure。

T32 二进制架构:
```
$ file build/bin/htc_net_app
build/bin/htc_net_app: ELF 32-bit LSB executable, MIPS, MIPS32 rel2 version 1 (SYSV),
  dynamically linked, interpreter /lib/ld-uClibc.so.0, stripped
$ file build_sim/bin/htc_net_app
build_sim/bin/htc_net_app: ELF 64-bit LSB pie executable, x86-64, ... for GNU/Linux 3.2.0
```
两个平台二进制均生成; 旧 htc_wifi_app 已清除:
```
build/bin/htc_net_app      32340 bytes  (T32 MIPS uclibc)
build_sim/bin/htc_net_app 337552 bytes  (x86-64 sim)
build/bin/htc_wifi_app     absent (No such file or directory)   ✓
build_sim/bin/htc_wifi_app absent (No such file or directory)   ✓
```

## 3. 复跑 #3 — 纯逻辑单测 (16 case)

```
$ ./build_sim/bin/test_net_app_logic
test_net_app_logic: ALL PASS
===TEST_RC=0===
```
**结果: 16 case 全绿 (ALL PASS, rc=0)。**
覆盖: WiFi 专属 10 (decide 6 + decisionExitCode + mayWriteBack + normalizeSsid + e2e) +
T7 新增 6 (parseNetType / ptypeToNetType / netTypeIfname / isNetworkUp / ethNeedsConnect / usbNeedsStartDefault)。

## 4. 复跑 #4 — sim 冒烟各路径 exit code (全不 segfault)

```
=== --help (expect 0) ===                       exit=0      ✓
=== no args (expect 6) ===                      exit=6      ✓ (--type 必填)
=== --type wifi --no-dhcp (0 or 6) ===          exit=6      ✓ (sim 无 MCU 凭据 -> ABORT)
=== --type wifi --ssid X --pwd Y --no-dhcp ===  exit=2      ✓ (见下 WiFi 等价说明)
=== --type eth --no-dhcp ===                    exit=3      ✓ (sim 无 eth0 IP/gw，非致命)
=== --type usb --no-dhcp ===                    exit=2      ✓ (sim loadDriver 失败)
=== --type bogus (6) ===                        exit=6      ✓
=== --type usb --usb-bringup --usb-model EC20 === exit=2    ✓ (sim loadDriver 先失败，不崩)
=== --type usb --usb-bringup --usb-model BOGUS === exit=6   ✓ (model 校验)
=== --type wifi --bogus-opt ===                 exit=6      ✓ (未知选项)
```
**所有路径正常退出，无 segfault。**

`--type wifi --ssid X --pwd Y --no-dhcp` → exit 2 的 WiFi 等价说明:
sim 无 8189fs 驱动，`Misc::connectWifi()` 失败 → 探测 `isWifiDriverLoaded()` 返回 false →
`EXIT_DRIVER_FAIL=2`。这与原 `htc_wifi_app` 的 FRESH_CONNECT 失败路径(wifi_app.cpp:230-238)
**逐行一致**(代码审计坐实，见 §7)，是 WiFi 行为等价的正确表现，非回归。

## 5. 复跑 #5 — 零改动约束 (受保护路径)

```
$ git diff HEAD --stat -- src/app/main_app.cpp src/hal src/hardware/mcu/MCU.cpp \
                              src/common/misc src/network
(空 — rc=0)
```
**结果: 必须为空 → 确认为空。** main_app / hal / MCU.cpp / common/misc / network 全部零改动。

补充显式核查:
```
$ git diff HEAD --stat -- src/app/main_app.cpp        (空，rc=0)
$ git diff HEAD --stat -- src/hal src/hardware/mcu/MCU.cpp src/common/misc src/network
                                                        (空，rc=0)
```

## 6. 复跑 #6 — 残留检查 (改名干净)

```
$ grep -rn "htc_wifi_app\|wifi_app_logic" src tests
src/app/net_app.cpp:16:// ... (former htc_wifi_app deliberately ...)        [注释]
src/app/net_app.cpp:85:// WiFi-specific (preserved from htc_wifi_app)       [注释]
src/app/net_app.cpp:201:// ... (former htc_wifi_app main body).             [注释]
tests/test_net_app_logic.cpp:4:// ... (ported verbatim from ... test_wifi_app_logic)  [注释]
tests/test_net_app_logic.cpp:69:// ===== WiFi-specific (ported ... test_wifi_app_logic) [注释]
src/app/net_app_logic.h:10:// ... (former htc_wifi_app);                   [注释]
```
**结果: 仅注释中的历史指代("former htc_wifi_app"/"ported from")，无任何 target / 源码 / include 残留。**
CMake (src/app/CMakeLists.txt + tests/CMakeLists.txt) 完全无 wifi_app 残留(grep 空)。

uclibc 陷阱检查:
```
$ grep -nE "std::to_string|std::stoi|[^_]stoi\(" \
    src/app/net_app.cpp src/app/net_app_logic.cpp src/app/net_app_logic.h tests/test_net_app_logic.cpp
(空 — rc=1，clean)
```
**结果: 无 std::to_string / stoi (uclibc link 陷阱规避)。**

CLI 不读 INI 检查:
```
$ grep -nE "DeviceConfig|Settings|getPType|PType|readIni|INI" src/app/net_app.cpp src/app/net_app_logic.cpp
src/app/net_app.cpp:19:// ... INI_KEY_UPWD="PWD" naming pitfall.       [注释]
src/app/net_app_logic.cpp:77:// ... PType integers ... pointer to Common.h  [注释]
```
**结果: 仅有注释提及，无任何 DeviceConfig/Settings/PType 读取的代码路径。** CLI `--type` 必填
决策坐实(用户决策 #2)。

---

## 7. WiFi 行为等价审计 (最重要 — 读代码逐段对比)

源: `git show HEAD:src/app/wifi_app.cpp` (355 行) vs 新 `src/app/net_app.cpp::runWifi` (line 204-355)。

### 7.1 纯逻辑层逐字对比 (wifi_app_logic.{h,cpp} → net_app_logic.{h,cpp})
- `decide()`: 逻辑完全一致(ABORT/FRESH_CONNECT/REUSE/RECONNECT 四分支，normalizeSsid 比较 + currentSsid 非空判定)。✓
- `decisionExitCode()`: ABORT→6, FRESH_CONNECT→3, RECONNECT→3, REUSE→0。✓ 一致。
- `mayWriteBack()`: connected + liveSsid 非空 + targetSsid 非空 + normalizeSsid 相等。✓ 一致。
- `normalizeSsid()`: 仅 trim 尾部 CR/LF/space/tab + 前导 space/tab，不 lowercase，不破坏内部空格。✓ 一致。
- `ExitCode` enum (0/2/3/4/5/6)、`Decision`/`LinkState`/`Target` 结构体: ✓ 一致。

### 7.2 runWifi() 执行序列对比 (= 原 main 的 WiFi 部分)
| 步骤 | 原 wifi_app.cpp main | 新 net_app.cpp runWifi | 等价? |
|---|---|---|---|
| 凭据解析 | CLI 优先，否则 MCU readUPID/readUPWD (retry 3x, 50ms) | 同 | ✓ |
| Target 构造 | ssid/password/hasCredentials | 同 | ✓ |
| LinkState | isWifiConnected + currentSSID | 同 | ✓ |
| decide switch | ABORT→6 / FRESH→connectWifi / RECONNECT→reconnectSSID / REUSE→noop | 同 | ✓ |
| FRESH_CONNECT 失败 | connectWifi false → isWifiDriverLoaded()? false→exit 2, else→exit 3 | 同 (line 259-266) | ✓ |
| RECONNECT 失败 | reconnectSSID false → exit 3 | 同 (line 272-275) | ✓ |
| post-probe L2 校验 | currentSSID 非空且==target，否则 exit 3 | 同 (line 285-294) | ✓ |
| DHCP | !noDhcp → startDHCP，false→exit 4 | 同 (line 297-302) | ✓ |
| MCU write-back gate | writeMcu → freshSsid+isWifiConnected → mayWriteBack → false→exit 5 | 同 (line 305-322) | ✓ |
| write-back 执行 | writeUPID/writeUPWD，失败→exit 5 | 同 (line 323-334) | ✓ |
| write-back verify | 200ms settle + readUPID/readUPWD retry + mismatch→exit 5 | 同 (line 335-348) | ✓ |
| 成功 | exit 0 | 同 (line 353-354) | ✓ |

**结论: WiFi 连接序列 / 凭据来源 / reconnect / DHCP / MCU-writeback / exit code (0/2/3/4/5/6) 全部原样搬入，
无偷改。** `readMcuStrWithRetry` helper 也逐字一致(包括 50ms usleep / 3 次 retry / WARNING 日志)。
唯一差异: 日志字符串里的 app 名 "htc_wifi_app" → "htc_net_app"(预期，非语义改变)。

### 7.3 wifi_reconnect 保留
```
$ git diff HEAD --stat -- src/app/wifi_reconnect.cpp src/app/wifi_reconnect.h
(空 — rc=0)
```
`wifi_reconnect.{h,cpp}` 零改动，WiFi 专属语义保留(net_app 的 wifi 分支仍 `#include "wifi_reconnect.h"`
调 `currentSSID`/`reconnectSSID`)。✓ 符合 planner 决策。

---

## 8. 分支审计 (Eth / USB / CLI)

### 8.1 Eth 分支 (net_app.cpp runEth, line 362-384)
```cpp
const std::string ifname = netTypeIfname(NET_ETH);   // "eth0"
Misc::setNetworkInterfaceName(ifname);
if (!args.noDhcp) { if (!Misc::startDHCP(ifname)) return EXIT_DHCP_FAIL; }   // exit 4
if (isNetworkUp(getIPAddress, getGatewayAddress)) return EXIT_OK;            // exit 0
return EXIT_CONNECT_FAIL;                                                     // exit 3
```
**严格 = setNetworkInterfaceName("eth0") + startDHCP("eth0")，无 connect / ifconfig up / 静态 IP。**
与 planner 坐实的 main_app PTYPE_ETHERNET 行为一致。✓ 无多余 connect。

### 8.2 USB 分支 (net_app.cpp runUsb, line 403-465)
默认路径:
```cpp
Misc::setNetworkInterfaceName("usb0");
loadDriver()  -> false -> exit 2
open()        -> false -> exit 3
preconfig()   -> false -> exit 3
(!noDhcp) startDHCP("usb0") -> false -> exit 4
isNetworkUp? -> exit 0 / exit 3
```
**默认路径 = loadDriver→open→preconfig→DHCP，无 start()。** ✓ 符合用户决策 #1 (忠实搬运 main_app)。

`--usb-bringup` 路径(在 preconfig 前插 setModel，preconfig 后插 start):
```cpp
if (args.usbBringup) { dongle->setModel(model); }     // preconfig 前
if (!dongle->preconfig()) return EXIT_CONNECT_FAIL;
if (args.usbBringup) { if (!dongle->start()) return EXIT_CONNECT_FAIL; }   // preconfig 后
```
**`--usb-bringup`(默认关）才补 setModel + start()。** ✓ 符合用户决策 #1。
`--usb-model` 校验: BOGUS → exit 6 (parseUsbModel false → EXIT_ARG_ERROR)。✓

### 8.3 CLI 不读 INI (net_app.cpp main, line 469-511)
```cpp
if (!args.haveType) { ...; return EXIT_ARG_ERROR; }   // --type 缺失 -> exit 6
NetType netType = parseNetType(args.typeStr);
if (netType == NET_INVALID) { ...; return EXIT_ARG_ERROR; }   // 无法解析 -> exit 6
switch (netType) { NET_WIFI: runWifi; NET_ETH: runEth; NET_USB: runUsb; }
```
**main 里无任何 DeviceConfig::get / Settings / 读 INI BOOT/PType 的代码路径。** ✓ 符合用户决策 #2。
`--type` 必填，本阶段完全不读 INI。

---

## 9. regress_net_real.sh 审计 (读脚本 + bash -n)

```
$ bash -n script/regress_net_real.sh && echo "syntax OK rc=0"
syntax OK rc=0
```
脚本内容审计:
- **Eth 路径** (line 45-53): `${BIN} --type eth`，断言 `rc==0 && IP 非空 && GW 非空`
  (ip_of/gw_of 用 `ip -4 -o addr show` + `ip route`，与 isWifiConnected 同款 IP+gw 非空判定)。✓
- **Eth --no-dhcp** (line 55-64): 断言 exit ∈ {0,3}。✓
- **USB 默认** (line 67-78): `--type usb`，断言 exit ∈ {4,3,2}，标注为 KNOWN(T7-usb-no-start 已知缺陷，
  非 FAIL)。✓ 正确区分已知缺陷与回归。
- **USB --usb-bringup** (line 81-89): `--type usb --usb-bringup --usb-model ${USB_MODEL}`，
  断言 `rc==0 && IP 非空 && GW 非空`。✓
- **bad args** (line 92-100): `--type bogus`→6，无 `--type`→6。✓
- 末尾 `RESULT: PASS/FAIL/KNOWN` 汇总，`FAIL==0` 才 exit 0。✓

**脚本覆盖 Eth + USB 两条路径，断言 IP/gateway 非空(用 isWifiConnected 同款判定)，exit code 断言合理，
USB 默认缺陷正确标注为 KNOWN。** ✓ 符合用户决策 #3 (保留 regress_wifi_real.sh + 新增 regress_net_real.sh)。

```
$ ls -la script/regress_wifi_real.sh script/regress_net_real.sh
-rwxrwxr-x regress_net_real.sh  4363 bytes  (新增)
-rwxrwxr-x regress_wifi_real.sh 4241 bytes  (保留，零改动)
```

---

## 10. 约束核查汇总

| 约束 | 核查方式 | 结果 |
|---|---|---|
| 双平台编译 | build_sim + build 各 --target htc_net_app | ✓ rc=0 / rc=0 |
| sim 不 segfault | 10 条 sim 冒烟路径 | ✓ 全正常退出 |
| 无 std::to_string/stoi | grep net_app* | ✓ clean |
| 不动 hal/MCU/misc/network | git diff HEAD --stat | ✓ 空 |
| 不动 main_app | git diff HEAD --stat | ✓ 空 |
| WiFi 行为等价 | 逐段代码对比(§7) | ✓ 原样搬入 |
| CLI 不读 INI | grep + 读 main | ✓ 无 INI 读取路径 |
| 改名干净 | grep htc_wifi_app/wifi_app_logic | ✓ 仅注释残留 |
| regress 脚本 | bash -n + 读内容 | ✓ 语法过，断言合理 |

---

## 11. 残余风险 / 留用户执行

- **真机回归(Eth/USB 真正连上网)PC 跑不了**: regress_net_real.sh 留用户在 T32 执行。
  tester 已确认脚本正确(sim 路径不崩)、纯逻辑单测覆盖了决策、USB 默认缺陷(T7-usb-no-start)
  在脚本中正确标注为 KNOWN。此项不构成 FAIL。
- **T7-usb-no-start(高风险，planner 已记录)**: USB 默认路径忠实搬运 main_app(不调 start())，
  真机拿不到 IP。这是 main_app 既有行为，非 T7 引入。`--usb-bringup` 是显式 opt-in。
  本任务按用户决策 #1 正确处理，不卡验收。
- **T7-usb-setmodel-unset(中风险)**: --usb-bringup 路径接受 --usb-model 参数由用户显式设，
  默认 EC20。属 planner 记录的既有缺陷，不卡验收。

---

## 12. tester 结论

所有独立复跑通过(sim 构建 rc=0、T32 构建 rc=0、16 case 全绿、sim 冒烟 10 路径无 segfault、
受保护路径零改动、改名干净、无 uclibc 陷阱)。WiFi 行为等价审计通过(runWifi 与原 wifi_app main
逐段一致，纯逻辑层 decide/mayWriteBack/normalizeSsid/exit code 逐字一致)。Eth/USB/CLI 分支
审计通过(严格符合用户 3 决策)。regress_net_real.sh 语法过 + 断言合理。

**status = success。**
