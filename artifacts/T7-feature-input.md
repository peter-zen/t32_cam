# T7 — Feature Input

## 一句话
把独立的 `htc_wifi_app` 扩展为统一网络管理应用 **`htc_net_app`**,在保留现有 WiFi 能力的基础上**新增 Ethernet + USB dongle(4G)两种上行**,使其能 cover `htc_main_app` 当前所有"建立网络上行"的逻辑,作为独立可用的网络管理工具。

## 范围(In / Out of scope)
- **In**:扩展 `htc_wifi_app` → `htc_net_app`;新增 Ethernet 上行、USB dongle 上行;统一入口/接口选择/状态判定;双平台编译;PC 可单测的纯逻辑层 + 真机回归脚本。
- **Out(本阶段坚决不做)**:**不修改 `htc_main_app`**。main_app 的连网代码(connectWifi/startDHCP/UsbDongle/setNetworkInterfaceName)本阶段保持原样,main_app 切换到 htc_net_app 留到下一阶段任务。`src/hal/**` PIC-owned,禁改。

## 已确认决策(用户拍板,不要再问)
1. **命名**:二进制 `htc_net_app`(与 htc_main_app/htc_media_app/htc_daemon_app 风格一致)。源文件/逻辑层改名范围由 planner 评估(见"待设计 §命名")。
2. **本阶段不动 main_app**。
3. **网络可用判定标准**:接口有 IP **且** 有 gateway(= 现 `Misc::isWifiConnected()`,底层 `getIPAddress+getGatewayAddress`,与上行类型无关)。
4. **WiFi 行为必须等价搬运**,不得偷偷改连接序列/凭据来源/exit code 契约(wifi_app.cpp:15-17 的 exit code 0/2/3/4/5/6 契约稳定,脚本依赖)。

## 三种上行现有连接序列(planner 必须忠实搬运,源 = main_app)
| 上行 | PTYPE(Common.h) | main_app 现有序列(文件:行) | 说明 |
|---|---|---|---|
| WiFi | PTYPE_WIFI=1 | `Misc::connectWifi(ssid,pwd)` → `Misc::startDHCP()` | main_app.cpp:1375/1403;凭据来自 INI `SYS/UPID+UPWD`。**wifi_app 已 cover**(决策/reconnect/DHCP/MCU-writeback),保留即可 |
| Ethernet | PTYPE_ETHERNET=8 | 仅 `Misc::setNetworkInterfaceName(eth0)`,**无 connect 步骤**;DHCP 在 CMD_DHCP 位跑 `Misc::startDHCP()` | main_app.cpp:1244-1245;app.h:32 ETH_IFNAME="eth0"。**待核实**:Eth 是否只需 setIfname+DHCP,还是需要 ifconfig up / 静态 IP |
| USB dongle(4G) | PTYPE_USB_DONGLE | `UsbDongle->loadDriver()` → `open()` → `preconfig()` → DHCP | main_app.cpp:1380-1394;`UsbDongle`(src/network/UsbDongle.h)是移远 EC20/EC200/EG800K/RG255AA 的 AT 命令驱动。**待核实**:main_app 只调到 preconfig,**没调 `start()`/`activateContextProfile()`**——4G 数据连接激活是否在别处?序列是否完整? |

## 关键文件指针
- 现有 wifi app:`src/app/wifi_app.cpp`(main,355行)、`src/app/wifi_app_logic.{h,cpp}`(纯逻辑,可单测)、`src/app/wifi_reconnect.{h,cpp}`(WiFi 优雅切换,不重启 wpa_supplicant)
- 连网底层:`src/common/misc/Misc.h` / `Misc.cpp`
  - `connectWifi(ssid,pwd)` @Misc.cpp:425(isWifiConnected 短路 → isWifiDriverLoaded→insmod 8189fs.ko→recheck → wpa_conn wlan0 ...)
  - `startDHCP(ifname="")` @Misc.cpp:473(udhcpc -i <if> -t 10;已有 IP 则跳过)
  - `isWifiConnected(ifname="wlan0")` @Misc.cpp:300(IP+gateway 非空)
  - `setNetworkInterfaceName/getNetworkInterfaceName/findUsableNetworkInterface`
- USB dongle:`src/network/UsbDongle.{h,cpp}`(29KB,AT 驱动全套)、`src/network/Usb4gDongle.{h,cpp}`(旧版,疑似废弃,planner 确认哪个在用)
- main_app 连网点(只读参照,不改):`src/app/main_app.cpp` 1242-1251(接口选择)、1366-1407(CONN_NET+DHCP)、1546-1582(CMD_MOBILE)
- 现有测试:`tests/test_wifi_app_logic.cpp`(纯逻辑层单测,10 个 case)
- 构建:`src/app/CMakeLists.txt`(htc_wifi_app target)、`tests/CMakeLists.txt`

## 待 planner 设计/澄清
1. **CLI 统一入口**:htc_net_app 如何选择上行?方案候选:`--type wifi|eth|usb`、按 `--iface`、或读 INI `BOOT/PType`(PTYPE_WIFI/ETHERNET/USB_DONGLE)自动选。给推荐 + 理由。
2. **Ethernet 连接序列**:确认是否 = setIfname(eth0)+startDHCP(eth0)。读 main_app 行为核实,不要臆造。
3. **USB dongle 序列完整性**:读 `UsbDongle.cpp` 全貌,确认 loadDriver→open→preconfig 之后是否需要 `start()`/`activateContextProfile()` 才能拿到 IP(4G 拨号)。若 main_app 现状不完整,planner 要在方案里标注风险(不在本阶段偷偷补行为 → 严格照搬 main_app 现有调用;若必须补,单独标为风险让用户定)。
4. **命名重构范围**:wifi_app.cpp→net_app.cpp、wifi_app_logic→net_app_logic、wifi_reconnect→net_reconnect(或保留,wifi_reconnect 是 WiFi 专用语义)。评估 CMakeLists/tests/注释联动成本,给最小且一致的改名方案。
5. **纯逻辑层扩展**:net_app_logic 需新增哪些可单测的纯函数(如"按 PType 选上行策略"的决策、isNetworkUp 的判定)?保持 PC 可单测、无 syscall。
6. **exit code 契约**:WiFi 现有 0/2/3/4/5/6 不变。Eth/USB 是否复用同一套,还是新增码?给方案(尽量复用)。

## 约束
- 双平台编译:`BUILD_FOR_SIMULATION=ON`(build_sim) + T32 交叉(build)。sim 下 USB/Eth/wpa 全 stub,不得 segfault。
- 不改 `src/hal/**`、不改 main_app、不改 MCU.cpp。
- WiFi 路径行为等价(决策/reconnect/DHCP/writeback/exit code 不变)。
- 不读 INI 的 WiFi 凭据坑保留(wifi_app.cpp 注释:刻意不读 INI,避开 INI_KEY_UPWD="PWD" 命名坑)——若 Eth/USB 也需凭据,设计一致的凭据来源(CLI/MCU),避免引入 INI 坑。

## 验收标准
- `htc_net_app` 双平台编译通过(build_sim + build)。
- WiFi 行为与现 htc_wifi_app 等价(test_wifi_app_logic 全绿 + 真机回归脚本不回归)。
- Ethernet / USB dongle 上行:PC 单测覆盖纯逻辑;真机回归脚本覆盖连接建立序列(留用户执行,给脚本)。
- 命名一致(net_app / net_app_logic);CMake/tests 联动更新。
- 不动 main_app(git diff 验证 src/app/main_app.cpp 无改动)。

## 回滚点
- 全部新增/改名在 src/app/net_app*、src/app/CMakeLists.txt、tests/。回滚 = `git checkout` 这些路径 + 恢复 wifi_app*(若改名)。main_app 零改动确保回滚干净。

## 产出要求
- **report card**:`artifacts/T7-planner-report.md`(frontmatter 按 doc/contracts.md §6,owner=planner,status=success)。
- 完整规划可附 `artifacts/T7-planner-full.md`(源→目标映射 / 影响范围 / 步骤 / 测试策略 / 风险)。
- 不要写代码,只规划。规划批准后由 implementer 执行。
