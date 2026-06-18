# T7 Reviewer — Evidence

owner: reviewer · task: T7 · node: reviewer · flow: feature · date: 2026-06-17

末端审查。不信 implementer/tester 自报，独立复跑 + 逐段读码对比。审查重点按
dispatch 顺序逐项给结论。

---

## 0. 审查基准

用户拍板 3 决策(审查基准):
1. USB 默认忠实搬运(loadDriver→open→preconfig，**不调 start**)，`--usb-bringup`(默认关)才补 start()
2. CLI `--type wifi|eth|usb` **必填**，本阶段**不读 INI**
3. 保留 regress_wifi_real.sh + 新增 regress_net_real.sh

---

## 1. WiFi 行为等价(审查重点 #1,核心承诺) — **PASS**

### 1.1 源/目标逐段对比
源: `git show HEAD:src/app/wifi_app.cpp`(原 main body) vs `src/app/net_app.cpp::runWifi`(line 204-355)。

逐段核对(读码坐实):
| 段 | 原 wifi_app main | net_app runWifi | 等价 |
|---|---|---|---|
| readMcuStrWithRetry helper | 3x retry / 50ms usleep / WARNING 格式 | 逐字一致(line 59-75) | ✓ |
| 凭据解析(CLI 优先 else MCU readUPID/readUPWD retry) | — | 同(line 211-223) | ✓ |
| Target 构造(ssid/password/hasCredentials) | — | 同(line 225-228) | ✓ |
| LinkState(isWifiConnected + currentSSID) | — | 同(line 231-233) | ✓ |
| decide switch(ABORT→6 / FRESH→connectWifi / RECONNECT→reconnectSSID / REUSE→noop) | — | 同(line 244-282) | ✓ |
| FRESH 失败分支(connectWifi false → isWifiDriverLoaded? false→2 else→3) | — | 同(line 259-266) | ✓ |
| RECONNECT 失败(reconnectSSID false → 3) | — | 同(line 272-275) | ✓ |
| post-probe L2 校验(currentSSID 非空且==target，否则 3) | — | 同(line 285-294) | ✓ |
| DHCP(!noDhcp → startDHCP，false→4) | — | 同(line 297-302) | ✓ |
| MCU write-back gate(stillConnected+freshSsid→mayWriteBack，false→5) | — | 同(line 305-322) | ✓ |
| write 执行(writeUPID/writeUPWD，失败→5) | — | 同(line 323-334) | ✓ |
| write verify(200ms settle + readUPID/readUPWD retry + mismatch→5) | — | 同(line 335-350) | ✓ |
| 成功 exit 0 | — | 同(line 353-354) | ✓ |

### 1.2 纯逻辑层逐字对比
`git show HEAD:src/app/wifi_app_logic.{h,cpp}` vs `src/app/net_app_logic.{h,cpp}`:
- `decide()` / `decisionExitCode()` / `mayWriteBack()` / `normalizeSsid()`: **逐字一致**(仅 namespace wifi_app_logic→net_app_logic、include guard WIFI_APP_LOGIC_H→NET_APP_LOGIC_H)。
- `ExitCode` enum(0/2/3/4/5/6)、`Decision`/`LinkState`/`Target`: **一致**。

**结论: WiFi 连接序列/凭据来源/reconnect/DHCP/MCU-writeback/exit code 全部原样搬入，无偷改。**
唯一差异: 日志字符串 `htc_wifi_app`→`htc_net_app`(预期，非语义改变)。**无行为漂移。**

### 1.3 sim 等价验证(交叉佐证)
`--type wifi --ssid X --pwd Y --no-dhcp` → exit **2**(sim 无 8189fs 驱动 → connectWifi 失败 →
isWifiDriverLoaded() false → EXIT_DRIVER_FAIL=2)。这与原 wifi_app FRESH_CONNECT 失败路径
**逐行一致**(net_app.cpp:259-266)，是 WiFi 行为等价的正确表现，非回归。

---

## 2. correctness / 边界(审查重点 #2) — **PASS**(1 处 CONCERN，不卡验收)

### 2.1 分支正确性
- **runEth**(net_app.cpp:362-384): `netTypeIfname(NET_ETH)=="eth0"` → setNetworkInterfaceName
  → (!noDhcp) startDHCP false→4 → isNetworkUp(getIPAddress,getGatewayAddress) true→0 / false→3。
  严格 = setIfname+DHCP，无 connect，符合 planner 决策。✓
- **runUsb 默认**(net_app.cpp:403-465): setIfname("usb0") → loadDriver false→2 → open false→3 →
  preconfig false→3 → (!noDhcp) startDHCP false→4 → isNetworkUp true→0/false→3。**无 start()**。✓
- **runUsb --usb-bringup**: preconfig 前插 setModel，preconfig 后插 start() false→3。✓

### 2.2 接口名与 app.h 一致
netTypeIfname(NET_WIFI)="wlan0" / NET_ETH="eth0" / NET_USB="usb0"(net_app_logic.cpp:109-119)
= app.h:31-33 WIFI_IFNAME/ETH_IFNAME/USB_DONGLE_IFNAME。✓ 已 grep 坐实。

### 2.3 exit code 映射(2/3/4/6)
- 2(driver load fail): USB loadDriver 失败 + WiFi FRESH 后 isWifiDriverLoaded false。✓
- 3(connect fail): WiFi FRESH/RECONNECT/post-probe/USB open/preconfig/start/链路未起。✓
- 4(DHCP fail): 三类上行 startDHCP false。✓
- 5(write gated, WiFi only): runWifi 独有，runEth/runUsb 不走。✓
- 6(arg error): --type 缺失/非法/未知选项/--usb-model 非法。✓

### 2.4 边界处理
- 空 ssid/pwd: runWifi 经 decide→ABORT→6(hasCredentials/ssid empty)。✓
- `--type` 缺失: main line 485-489 → 6。✓(sim 冒烟 noargs→6 坐实)
- `--type` 非法(bogus): main line 491-497 parseNetType→NET_INVALID→6。✓(sim 冒烟 bogus→6 坐实)
- `--usb-model` 非法(BOGUS): runUsb line 415-419 parseUsbModel false → 6。✓(sim 冒烟坐实)
- `--usb-bringup` 无 `--usb-model`: 默认 EC20(CliArgs.usbModel="EC20")，parseUsbModel("EC20")→EC20 true。✓
  与 planner-full §3.1 "缺省 EC20" 一致。

### 2.5 CONCORN(不卡验收): `--usb-bringup` 的 model 校验时机
runUsb 在 `args.usbBringup && !parseUsbModel(...)` 时才校验 model(line 415)。即 `--usb-model BOGUS`
**不带** `--usb-bringup` 时不会被拒绝(model 仅 bringup 路径用)。这是设计意图(model 仅 bringup 生效，
help 文本 line 125 已写明 "Only effective with --usb-bringup")，非 bug。但用户若误传 `--type usb
--usb-model BOGUS`(无 bringup)会静默忽略 model。可接受(harmless)，无需改。

---

## 3. 回归风险 / CMake(审查重点 #3) — **PASS**

### 3.1 CMake diff 逐行审(`git diff HEAD -- src/app/CMakeLists.txt`)
- 改名: WIFI_APP_SOURCES→NET_APP_SOURCES、WIFI_APP_HELPER_SOURCES→NET_APP_HELPER_SOURCES、
  add_executable(htc_wifi_app→htc_net_app)、set_target_properties(htc_wifi_app→htc_net_app)。✓ 仅改名。
- **sim link 块**(diff line 193-222): `htc_wifi_app`→`htc_net_app`；新增 `network setting env devconf disk
  common_time_rtc common_time_timezone power md5 common_utils_crc common_utils_serial`(network 的 PRIVATE
  传递依赖不暴露给本 target，故显式补全，注释说明清楚)。`common_utils_base64 crc16 logger jsoncpp sdk_stub
  pthread rt gcc stdc++` 保留。✓
- **T32 link 块**(diff line 373-399): 同 sim，把 `sdk_stub` 换 `system_call`(与原 htc_wifi_app T32 块一致)，
  新增 network 全依赖。✓

### 3.2 是否误伤 htc_main_app / htc_media_app
- diff 范围**仅**触及 htc_wifi_app/htc_net_app target 的定义与两处 link 块 + set_target_properties。
- htc_main_app(htc_main_app ${MAIN_APP_SOURCES}...)/htc_media_app/htc_daemon_app/snap_test 的
  `add_executable` 与各自 link 块**未出现在 diff**。✓
- 新增 link network 的传递依赖(setting/env/devconf/disk/common_time_*/power/md5/common_utils_crc/
  common_utils_serial)**只在 htc_net_app 的两块 PRIVATE 链上**，main_app/media_app 块未动。✓
- 无共享变量被改(WIFI_APP_* 是 target 私有变量，改名不影响其它 target)。✓

**结论: CMake 只动 htc_net_app target，不会误伤 main_app/media_app/daemon_app 的 link。**

### 3.3 tests/CMakeLists diff
仅 test_wifi_app_logic→test_net_app_logic 改名(源文件、target、include、link、set_target_properties)，
test_mcu_service 等其它 target 未动。✓

---

## 4. 可维护性(审查重点 #4) — **PASS**

### 4.1 纯函数设计
net_app_logic 6 个新函数(NetType/parseNetType/ptypeToNetType/netTypeIfname/isNetworkUp/
ethNeedsConnect/usbNeedsStartDefault):
- 全无 syscall，可 PC 单测(仅链 net_app_logic.cpp)。✓
- PTYPE 用字面常量 1/4/8 + 注释指 Common.h，避免链 Common.h，保持 test 零重依赖。✓ 设计合理。
- 命名清晰(netTypeIfname/ethNeedsConnect/usbNeedsStartDefault 语义自解释)。✓
- parseNetType 大小写敏感，与 normalizeSsid/SSID 风格一致。✓

### 4.2 CLI 解析健壮性
- 最小 long-option parser(net_app.cpp:138-198)。未知 `--xxx`→err→6；位置参数→err→6；
  `--opt` 缺值(next 越界)→err→6。✓
- sim 冒烟 `--type wifi --bogus-opt`→6 坐实。✓

### 4.3 run* 公共逻辑
- setNetworkInterfaceName + (DHCP) + isNetworkUp 三段在 runEth/runUsb 重复(runWifi 因 WiFi 语义不同
  未复用)。可接受(runWifi 行为等价硬约束，不宜抽公共；runEth/runUsb 各 ~20 行，抽取收益低)。
  **CONCORN(nice-to-have，不卡)**: 未来若加第 4 类上行可抽 `bringUpGenericIf(NetType)`。

### 4.4 parseUsbModel 放在匿名 namespace(net_app.cpp:394-401)
独立纯函数，4 个 model 逐一比较，清晰。返回 bool + out 参数，与 UsbDongleModel enum 对齐。✓

---

## 5. 安全(审查重点 #5) — **PASS**

### 5.1 注入面核查
- USB `--usb-model`(net_app.cpp:415 parseUsbModel): 仅与 4 个字面量字符串比较，不拼进 shell/AT。
  UsbDongle::setModel 接收 enum，无字符串拼接。✓ 无新增注入。
- `--usb-model BOGUS` → parseUsbModel false → exit 6，不进入 AT 路径。✓
- UsbDongle 的 AT 命令路径(setContextProfile 等)接收 apn 字符串，但那是 dongle 内部从 SIM 读的(getApn)，
  非 net_app CLI 注入。net_app 不传任何用户字符串进 AT。✓
- WiFi 凭据(ssid/pwd)经 connectWifi/reconnectSSID: 与原 wifi_app 路径一致(T6 已知 wifi_reconnect 拼接
  是 low risk，本任务未改 wifi_reconnect，git diff 空——见 §8)。✓ 无新增面。
- net_app **不读 INI**(grep DeviceConfig/Settings/->get 空——见 §7.2)，故无 INI 凭据源引入。✓

**结论: 无新增 shell/AT 注入面；不读不安全凭据源。**

---

## 6. 缺失测试(审查重点 #6) — **PASS**

### 6.1 6 个新纯函数覆盖(test_net_app_logic.cpp:178-239)
- testParseNetType(178-193): wifi/eth/usb 正常 + WIFI/WiFi/ETH/USB/""/bogus/ethernet 边界。✓
- testPtypeToNetType(195-207): 1/4/8 正常 + 0/2/3/9 边界(非法 PType)。✓
- testNetTypeIfname(209-217): WIFI/ETH/USB 正常 + NET_INVALID→""(空 ifname 边界)。✓
- testIsNetworkUp(219-226): ip+gw 正常 + 空 ip / 空 gw / 双空 各组合。✓
- testEthNeedsConnect(228-232): false。✓
- testUsbNeedsStartDefault(234-239): false。✓

### 6.2 决策分支覆盖
- parseNetType 覆盖了所有 NET_* 返回分支(含 default NET_INVALID)。✓
- ptypeToNetType 覆盖 case 1/4/8 + default。✓
- netTypeIfname 覆盖 3 case + default。✓

**结论: 新增 6 函数的正常 + 边界(非法 PType、空 ifname、isNetworkUp 各组合)全覆盖，无遗漏决策分支。**
WiFi 专属 10 case 从 test_wifi_app_logic 原样迁移(断言不变)。test 全绿(16 case ALL PASS)。

### 6.3 CONCORN(不卡): parseUsbModel 未单测
parseUsbModel(net_app.cpp:394) 是 net_app.cpp 匿名 namespace 里的函数，不在 net_app_logic，
故 test_net_app_logic 不覆盖。它的正确性靠 sim 冒烟 `--usb-model BOGUS`→6 间接验证。若追求完整，
可把 parseUsbModel 下沉到 net_app_logic 加单测。nice-to-have。

---

## 7. 约束遵守(审查重点 #7) — **PASS**

### 7.1 不动 main_app/hal/MCU/misc/network
```
git diff HEAD --stat -- src/app/main_app.cpp src/hal src/hardware/mcu/MCU.cpp src/common/misc src/network
(空)
```
✓ 受保护路径零改动坐实(亲自重跑)。

### 7.2 不读 INI
```
grep -nE "DeviceConfig|->get\(|Settings" src/app/net_app.cpp src/app/net_app_logic.cpp  → rc=1(clean)
```
main 无任何 DeviceConfig/Settings/读 INI BOOT/PType 代码路径。✓ 符合用户决策 #2。

### 7.3 双平台编译
- build_sim htc_net_app + test_net_app_logic: rc=0(亲自复跑)。✓
- build htc_net_app(T32 MIPS uclibc ELF): rc=0(亲自复跑)。✓

### 7.4 USB 默认不 start
runUsb 默认路径(net_app.cpp:403-465)无 dongle->start()调用；仅 `args.usbBringup` 时(line 442-450)
才调。✓ 符合用户决策 #1。sim 冒烟 `--type usb --no-dhcp`→2 坐实默认路径走 loadDriver 失败，未触 start。

### 7.5 无 std::to_string/stoi
```
grep -nE "std::to_string|std::stoi|[^_]stoi\(" src/app/net_app*.cpp src/app/net_app_logic.h tests/test_net_app_logic.cpp
→ rc=1(clean)
```
✓ uclibc link 陷阱规避。

---

## 8. 已知风险缓解确认(不卡验收)

### 8.1 T7-usb-no-start(high) — 缓解到位 ✓
- 默认忠实搬运(不偷偷补): runUsb 默认无 start()。✓
- `--usb-bringup` opt-in(默认关): CliArgs.usbBringup=false(net_app.cpp:92)。✓
- regress_net_real.sh 标 KNOWN: line 66-78 USB-default 标 `known()`(非 fail)，注释明 T7-usb-no-start。✓
- help 文本明示: net_app.cpp:118-123。✓

### 8.2 T7-eth-no-carrier-on-sim / 真机回归留用户 — 缓解到位 ✓
- sim 不崩: sim 冒烟 `--type eth`→3(无 eth0 IP/gw)，正常退出。✓
- 脚本正确: regress_net_real.sh ETH case(line 45-53)断言 rc==0 && IP && GW。✓
- 纯逻辑单测覆盖: testNetTypeIfname/testEthNeedsConnect/testIsNetworkUp 全绿。✓

### 8.3 T7-usb-setmodel-unset(medium) — 缓解到位 ✓
- 默认 EC20(CliArgs.usbModel="EC20")，照搬不擅自默认其它。✓
- `--usb-bringup`+`--usb-model` 让用户显式设。✓

### 8.4 T7-wifi-equivalence-regression(high) — 缓解到位 ✓
- WiFi 行为逐段等价(见 §1)。✓
- test 迁移保断言(10 case ALL PASS)。✓
- regress_wifi_real.sh 保留零改动(git diff 空)。✓

---

## 9. 独立复跑结果汇总(亲自重跑，不信自报)

| # | 复跑项 | 结果 |
|---|---|---|
| 1 | sim 构建 htc_net_app + test_net_app_logic | rc=0 ✓ |
| 2 | T32 交叉构建 htc_net_app(MIPS uclibc ELF) | rc=0 ✓ |
| 3 | test_net_app_logic 单测 | ALL PASS(16 case) rc=0 ✓ |
| 4 | sim 冒烟 11 路径 exit code | --help(0)/noargs(6)/wifi--no-dhcp(6)/wifi+ssid+pwd--no-dhcp(2)/eth--no-dhcp(3)/eth(3)/usb--no-dhcp(2)/usb--usb-bringup--usb-model EC20(2)/usb--usb-bringup--usb-model BOGUS(6)/bogus(6)/wifi--bogus-opt(6) 全正常退出无 segfault ✓ |
| 5 | 受保护路径零改动(main_app/hal/MCU/misc/network) | git diff 空 ✓ |
| 6 | wifi_reconnect 零改动 | git diff 空 ✓ |
| 7 | wifi_app 残留(src/tests 非注释) | grep rc=1(clean) ✓ |
| 8 | CMake wifi_app 残留 | grep rc=1(clean) ✓ |
| 9 | uclibc 陷阱(to_string/stoi) | grep rc=1(clean) ✓ |
| 10 | INI 读取(DeviceConfig/Settings/get) | grep rc=1(clean) ✓ |
| 11 | bash -n regress_net_real.sh | syntax OK ✓ |
| 12 | regress_wifi_real.sh 保留 | 零改动，存在 ✓ |

---

## 10. reviewer 结论

7 项审查重点:
1. WiFi 行为等价 — **PASS**(逐段对比无漂移)
2. correctness/边界 — **PASS**(1 处 model 校验时机 CONCORN，无害)
3. 回归风险/CMake — **PASS**(只动 htc_net_app target，不误伤 main_app/media_app)
4. 可维护性 — **PASS**(纯函数设计合理；1 处 run* 抽公共 nice-to-have)
5. 安全 — **PASS**(无新增注入面)
6. 缺失测试 — **PASS**(6 新函数正常+边界全覆盖；parseUsbModel 未单测 nice-to-have)
7. 约束遵守 — **PASS**(不动受保护路径/双平台编译/不读 INI/USB 默认不 start/无 uclibc 陷阱)

已知风险(T7-usb-no-start/T7-eth-no-carrier-on-sim/T7-usb-setmodel-unset/
T7-wifi-equivalence-regression)缓解均到位。

无 FAIL 项。3 处 CONCORN 均 nice-to-have，不卡验收。

**verdict = PASS(status=success)，建议闭环。**
