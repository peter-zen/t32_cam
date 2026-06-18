---
contract: report
contract_version: "1"
task_id: T7
node: planner
flow: feature
status: success
summary: |
  T7 把独立 htc_wifi_app 扩展为统一网络管理应用 htc_net_app，在保留 WiFi 能力(行为等价)的
  前提下新增 Ethernet + USB dongle(4G) 两种上行。本阶段不动 main_app(src/app/main_app.cpp 零改动)、
  不动 src/hal/**、不动 MCU.cpp。

  调研坐实三个关键事实(均亲自读码确认):
  (1) Ethernet 序列 = Misc::setNetworkInterfaceName("eth0") + Misc::startDHCP("eth0")。main_app 对
      PTYPE_ETHERNET 只 setIfname(1244-1245)，且 CMD_CONN_NET 块根本不处理 ETH(直接 goto main_exit,
      1396 "not support")——ETH 没有任何 connect 步骤，全靠 DHCP 拉起。全仓无 eth0 的 ifconfig up /
      静态 IP(wpa_conn.cpp:140 的 ifconfig up 是 wlan0 专用，与 eth 无关)。
  (2) USB dongle 序列在 main_app 中本就不完整(高风险):main_app.cpp:1380-1394 仅 loadDriver→open→
      preconfig，从不调 start()/activateContextProfile()/setModel()/setSimPin()。而 UsbDongle.cpp:1060
      start() 才是真正激活 4G 数据连接(querySimReady→getApn→setContextProfile→activateContextProfile
      即 AT+QIACT=1)的入口；preconfig()(1082) 只设 netType/scanMode/wakeup，不激活 context。全仓
      grep 证实 main_app/tests 无任何 ->start()/activateContextProfile() 调用。结论:照搬 main_app
      现有调用(loadDriver→open→preconfig)→拿不到 IP(数据 context 未激活)；但本阶段硬约束"不偷偷补
      行为/忠实搬运"，故 net_app 默认严格照搬 main_app 现状序列，USB 完整性缺陷标为 T7-usb-no-start
      高风险，提供 --usb-bringup 可选开关(默认关)让用户拍板是否补 start()。详见 full §5。
  (3) Usb4gDongle.{h,cpp} 已废弃:全仓仅自引用，无任何 #include/调用，真正在用的是 UsbDongle
      (src/network/UsbDongle.cpp，network 共享库内)。

  推荐方案要点:
  - CLI: --type wifi|eth|usb 必填(本阶段不读 INI,INI 自动选留下一阶段与 main_app 切换一起做)。三种 PType
    常量已坐实(Common.h:77-80 WIFI=1/USB_DONGLE=4/ETHERNET=8)。
  - 命名重构(最小且一致):wifi_app.cpp→net_app.cpp、wifi_app_logic.{h,cpp}→net_app_logic.{h,cpp}、
    namespace wifi_app_logic→net_app_logic；wifi_reconnect.{h,cpp} 保留原名(WiFi 专属语义，ETH/USB
    不用，改名反而误导)。二进制 htc_wifi_app→htc_net_app。CMake/tests/注释联动清单见 full §7。
  - 纯逻辑层新增(可 PC 单测、无 syscall):parseNetType/ptypeToNetType、netTypeIfname(映射 wifi→wlan0/
    eth→eth0/usb→usb0，与 app.h:31-33 同源)、isNetworkUp(=现 isWifiConnected 同义：IP+gateway 非空，
    与上行类型无关)、ethNeedsConnect(恒 false，记录"ETH 无 connect 步骤")、usbNeedsStart(反映
    main_app 现状 vs start() 缺失的决策，让用户开关驱动)。
  - exit code 契约:WiFi 0/2/3/4/5/6 不变；ETH/USB 复用同一套(3=连接/驱动失败、4=DHCP 失败、6=参数/类型
    错误；ETH/USB 无 MCU 回写故不走 5)。USB loadDriver 失败映射 2(与 WiFi driver load fail 语义一致)。
  - 双平台编译:net_app 需新增 link network 库(UsbDongle 所在)；sim 下 SerialPort/syscall/AT 全失败
    但不 segfault(参照 wifi_reconnect sim 退化模式)，USB/Eth 路径 sim 下早退(类型决策纯函数可测，
    执行层 sim 无设备即 false)。

deliverables:
  - artifacts/T7-planner-full.md
  - artifacts/T7-planner-report.md
verification:
  commands:
    - cmake -DBUILD_FOR_SIMULATION=ON -B build_sim -S . && cmake --build build_sim -j$(nproc) --target htc_net_app
    - cmake -DCMAKE_TOOLCHAIN_FILE=toolchain.cmake -B build -S . && cmake --build build -j$(nproc) --target htc_net_app
    - cmake --build build_sim -j$(nproc) --target test_net_app_logic && ./build_sim/bin/test_net_app_logic
    - ./build_sim/bin/htc_net_app --help
    - ./build_sim/bin/htc_net_app --type wifi --no-dhcp ; echo "exit=$?"
    - grep -rn "killall\|kill.*wpa_supplicant\|rmmod.*8189fs\|pkill" src/app/ src/common/misc/Misc.cpp
    - git diff --stat main -- src/app/main_app.cpp src/hal src/hardware/mcu/MCU.cpp
    - test ! -e build/bin/htc_wifi_app && test -e build/bin/htc_net_app
  evidence_ref: artifacts/T7-planner-full.md
state_delta:
  set_task_status: {}
  add_decision:
    - key: T7-netapp-three-uplink-unified
      value: "htc_net_app 统一三上行(WiFi/Eth/USB dongle)，保留 WiFi 行为等价，新增 Eth+USB。CLI --type
        wifi|eth|usb 必填(本阶段不读 INI,INI 自动选留下一阶段)。命名
        wifi_app→net_app、wifi_app_logic→net_app_logic(namespace 同改)；wifi_reconnect 保留原名(WiFi
        专属)。二进制 htc_wifi_app→htc_net_app。本阶段不动 main_app/hal/MCU。"
    - key: T7-eth-sequence-confirmed
      value: "Eth 序列坐实= setNetworkInterfaceName(eth0)+startDHCP(eth0)。main_app 对 PTYPE_ETHERNET
        仅 setIfname(main_app.cpp:1244-1245)，CMD_CONN_NET 块根本不处理 ETH(1396 not support)，全仓无
        eth0 ifconfig up/静态 IP。net_app 的 --type eth 严格照搬:setIfname(eth0)+startDHCP(eth0)。"
    - key: T7-usb-sequence-incomplete-mainapp
      value: "USB dongle 序列在 main_app 本就不完整(高风险 T7-usb-no-start):main_app.cpp:1380-1394 只
        loadDriver→open→preconfig，从不调 start()(UsbDongle.cpp:1060 才真正激活 4G:querySimReady→
        getApn→setContextProfile→activateContextProfile/AT+QIACT=1)，且从不 setModel()/setSimPin()。
        全仓 grep 证实无任何 start()/activateContextProfile() 调用。net_app 默认严格照搬 main_app 现状
        (不偷偷补行为)，另提供 --usb-bringup 开关(默认关)让用户拍板是否补 start()，本阶段不擅自打开。"
    - key: T7-usb4gdongle-deprecated
      value: "Usb4gDongle.{h,cpp}(src/network/) 已废弃:全仓仅自引用，无任何外部 #include/调用。真正在
        用的是 UsbDongle(network 共享库)。net_app 只用 UsbDongle，不碰 Usb4gDongle。"
    - key: T7-exit-code-reuse
      value: "exit code 复用 WiFi 0/2/3/4/5/6 契约，不新增码。ETH/USB 走 3(连接/驱动失败)/4(DHCP)/6(参数);
        ETH/USB 无 MCU 回写故不走 5；USB loadDriver 失败→2(与 WiFi driver load fail 语义对齐)。"
    - key: T7-pure-logic-extend
      value: "net_app_logic 新增可 PC 单测纯函数(无 syscall):parseNetType/ptypeToNetType(INI PType→上行类型，
        坐实 WIFI=1/USB_DONGLE=4/ETHERNET=8)、netTypeIfname(→wlan0/eth0/usb0, app.h:31-33 同源)、
        isNetworkUp(IP+gateway 非空，=现 isWifiConnected 与上行类型无关)、ethNeedsConnect(恒 false)、
        usbNeedsStart(反映 start() 缺失决策)。原 decide/mayWriteBack/normalizeSsid/ExitCode 不动(WiFi 专属)。
        test_net_app_logic 沿用 test_wifi_app_logic 的 EXPECT_* 风格。"
    - key: T7-user-decisions-confirmed
      value: "用户拍板(planner→implementer 交接前):(1) USB 默认忠实搬运 main_app 序列(loadDriver→open→
        preconfig),--usb-bringup opt-in 才补 start()+activateContextProfile(),默认关。(2) CLI --type
        wifi|eth|usb 必填,本阶段不读 INI(与 wifi_app 刻意不读 INI 一致),INI 自动选留下一阶段。(3) 保留
        script/regress_wifi_real.sh + 新增 script/regress_net_real.sh(Eth/USB 真机回归)。"
  add_risk:
    - key: T7-usb-no-start
      severity: high
      description: "main_app 的 USB 序列缺 start()/activateContextProfile()，照搬后 net_app 的 --type usb
        同样激活不了 4G 数据 context→拿不到 IP(usb0 无 carrier，udhcpc 超时→exit 4)。这是 main_app 既有
        行为，非本任务引入的回归。缓解:net_app 默认忠实搬运(不偷偷补)；--usb-bringup 开关(默认关)留给
        用户显式补 start()；full §5 给出补 start() 的精确序列；真机回归脚本断言 IP/gateway，失败即暴露。
        本风险需用户在 implement 前拍板 --usb-bringup 默认值。"
    - key: T7-usb-setmodel-unset
      severity: medium
      description: "UsbDongle::model 默认未初始化(enum class 零值=EC20)。main_app 从不 setModel()，net_app
        照搬同样不设；若实际硬件是 EC200A/EG800K/RG255AA，preconfig 的 setDevCtrl(3,1) 分支(UsbDongle.cpp:
        1088)与 setWakeupConfig 分支会走错。属 main_app 既有缺陷。缓解:照搬不补；--usb-bringup 路径顺带
        接受 --usb-model 参数(EC20/EC200A/EG800K/RG255AA)由用户显式设；标风险不擅自默认。"
    - key: T7-eth-no-carrier-on-sim
      severity: medium
      description: "sim 下 eth0 可能不存在或无 IP，startDHCP(eth0) 的 udhcpc 失败→exit 4，属预期(sim 无硬件)。
        缓解:sim 验收只验编译+纯逻辑单测+--help+决策；ETH 执行层真机回归脚本留用户跑。不卡 CI。"
    - key: T7-rename-cmake-linkage
      severity: medium
      description: "改名 wifi_app→net_app 涉及 CMake target(add_executable/set_target_properties/
        target_link_libraries 两处 sim/T32)+tests/CMakeLists(test_net_app_logic)+注释。漏改一处→构建断或
        残留 wifi_app。缓解:full §7 给出精确联动清单；验收 grep 证实 build/bin 无 htc_wifi_app 残留且
        htc_net_app 生成；test_net_app_logic 全绿。"
    - key: T7-network-lib-sim-deps
      severity: medium
      description: "net_app 要 link network 库(UsbDongle)，network 依赖 setting/env/disk/mcu/rtc/timezone/
        power/jsoncpp/md5/common_misc/common_utils_serial(network/CMakeLists.txt:37)。sim 侧需确认这些 stub
        库都在(common_utils_serial 在 sim 是否有 stub 要确认)。缓解:参照 htc_main_app sim link 块(app/
        CMakeLists.txt:102-150 已含 network)的依赖闭合；sim 编译验证；若 serial 无 stub，net_app 仍可编(
        UsbDongle.cpp 调 SerialPort，但若 net_app 不编译 UsbDongle.cpp 只 link 预编 network.so，依赖由
        .so 自带)。net_app 只 link network(不重编 UsbDongle.cpp)，依赖链由 network.so 承载。"
    - key: T7-wifi-equivalence-regression
      severity: high
      description: "重构改名/新增分支可能误伤 WiFi 路径(connectWifi/reconnect/DHCP/writeback/exit code)。
        缓解:WiFi 分支代码原样搬到 net_app 的 runWifi()(只改 namespace/include/日志里的 app 名)；原
        test_wifi_app_logic 全部 case 迁到 test_net_app_logic 并保持断言；真机回归脚本(regress_wifi_real.sh)
        换二进制名后重跑；--type wifi 默认即旧行为。"
artifact_path: artifacts/T7-planner-report.md
next: implementer
---

# T7 Planner Report (card)

本文件为 report card（只持指针）。完整实现方案见 `artifacts/T7-planner-full.md`。

## 目标复述
把独立 `htc_wifi_app` 扩展为统一网络管理应用 `htc_net_app`，保留 WiFi 能力（行为等价）的同时新增 Ethernet + USB dongle(4G) 两种上行，使其能 cover `htc_main_app` 当前所有"建立网络上行"的逻辑，作为独立可用的网络管理工具。本阶段不动 `main_app`、`src/hal/**`、`MCU.cpp`。

## 范围确认
- **In**：扩展为 `htc_net_app`；新增 Eth / USB dongle 上行；统一入口（`--type` / INI `BOOT/PType`）；状态判定复用 `Misc::isWifiConnected`（IP+gateway）；双平台编译；PC 可单测纯逻辑层 + 真机回归脚本。
- **Out**：不改 `main_app`；`src/hal/**` PIC-owned 禁改；不改 `MCU.cpp`；main_app 切换到 net_app 留下一阶段。

## 方案摘要（6 个待设计点明确推荐）
1. **CLI 统一入口**：`--type wifi|eth|usb` 显式选；缺省读 INI `BOOT/PType`（与 main_app.cpp:1220 `program_type` 同源）。理由：与 main_app 现有上行判定机制一致，便于下一阶段 main_app 切换；显式 `--type` 便于工具手动调试。
2. **Eth 序列（坐实）**：`Misc::setNetworkInterfaceName("eth0")` + `Misc::startDHCP("eth0")`。main_app 对 ETH 无 connect 步骤（CMD_CONN_NET 不处理 ETH），严格照搬。
3. **USB 序列（坐实 + 完整性结论）**：main_app 现状 = `loadDriver→open→preconfig`，**不完整**（缺 `start()`→无 IP）。默认严格照搬；提供 `--usb-bringup`（默认关）开关 + `--usb-model` 让用户显式补 `start()`。**需用户拍板默认值。**
4. **命名重构（最小且一致）**：`wifi_app.cpp→net_app.cpp`、`wifi_app_logic.{h,cpp}→net_app_logic.{h,cpp}`、namespace 同改；`wifi_reconnect.{h,cpp}` **保留原名**（WiFi 专属语义）；二进制 `htc_wifi_app→htc_net_app`。
5. **纯逻辑层扩展**：新增 `parseNetType`/`ptypeToNetType`、`netTypeIfname`、`isNetworkUp`、`ethNeedsConnect`、`usbNeedsStart`（全无 syscall，可 PC 单测）。
6. **exit code 契约**：复用 WiFi 的 0/2/3/4/5/6，不新增；ETH/USB 无 MCU 回写故不走 5；USB loadDriver 失败→2。

## 影响文件清单（指针，详见 full §7）
- 新增/改名：`src/app/net_app.cpp`、`src/app/net_app_logic.{h,cpp}`；保留 `src/app/wifi_reconnect.{h,cpp}`。
- 改 CMake：`src/app/CMakeLists.txt`（target 重命名 + link network）、`tests/CMakeLists.txt`（test 重命名）、`tests/test_net_app_logic.cpp`（改名+扩 case）。
- 新增脚本：`script/regress_net_real.sh`（Eth/USB 真机回归；参照 `regress_wifi_real.sh`）。
- **零改动**：`src/app/main_app.cpp`、`src/hal/**`、`src/hardware/mcu/MCU.cpp`、`src/common/misc/Misc.*`、`src/network/**`。

## 验收标准
- 双平台编译通过（build_sim + build 都生成 `htc_net_app`，无 `htc_wifi_app` 残留）。
- `test_net_app_logic` 全绿（含迁移的 WiFi case + 新增 NetType/Eth/USB 决策 case）。
- WiFi 行为与现 `htc_wifi_app` 等价（`--type wifi` = 旧无参/旧 WiFi 行为；真机回归脚本换名后不回归）。
- `git diff` 证实 `src/app/main_app.cpp`、`src/hal`、`MCU.cpp` 零改动。
- sim 下 `--type eth` / `--type usb` 不 segfault（执行层 sim 无设备即 false/早退）。

## state_delta
planner 节点不改 task 状态（留 implementer/reviewer）。上方 `add_decision`/`add_risk` 为建议写入 orchestration-state 的决策与风险记录（供后续节点与 audit 读取）。`set_task_status: {}` 表示本节点不动状态。
