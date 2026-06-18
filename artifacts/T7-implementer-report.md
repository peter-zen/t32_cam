---
contract: report
contract_version: "1"
task_id: T7
node: implementer
flow: feature
status: success
summary: |
  T7 implementer 按 planner-full §8 S1-S8 落地，把 htc_wifi_app 扩展为统一网络管理应用
  htc_net_app，保留 WiFi 行为等价(runWifi 原样搬入)，新增 Ethernet(--type eth:
  setIfname+DHCP，无 connect)与 USB dongle(--type usb: loadDriver→open→preconfig→
  DHCP，--usb-bringup 才补 setModel+start())两种上行。纯逻辑层 net_app_logic 新增
  NetType/parseNetType/ptypeToNetType/netTypeIfname/isNetworkUp/ethNeedsConnect/
  usbNeedsStartDefault(全纯函数无 syscall，PTYPE 用字面常量 1/4/8 避免链 Common.h)。
  CLI --type 必填(本阶段不读 INI，按用户决策 #2)。命名 wifi_app→net_app、
  wifi_app_logic→net_app_logic(namespace 同改)；wifi_reconnect 保留原名(WiFi 专属)。
  二进制 htc_wifi_app→htc_net_app。CMake sim/T32 两块 link network 及其完整传递依赖
  (setting/env/devconf/disk/common_time_*/power/md5/common_utils_crc/common_utils_serial)。
  新增 script/regress_net_real.sh(Eth/USB 真机回归)，保留 regress_wifi_real.sh。

  双平台编译全过(build_sim + build/ 均生成 htc_net_app，无 htc_wifi_app 残留)。
  test_net_app_logic 全绿(16 case: WiFi 10 迁移 + T7 新增 6)。sim 冒烟全路径不 segfault
  (--help 0 / 无 --type 6 / bogus 6 / wifi 0或6 / eth 3 / usb 2 / bad model 6)。
  零改动坐实: git diff HEAD 对 src/app/main_app.cpp、src/hal、src/hardware/mcu/MCU.cpp、
  src/common/misc、src/network 完全为空。无 std::to_string/stoi(uclibc 陷阱)。

  偏离 planner-full 处均因用户拍板 3 决策优先:(1) CLI 不读 INI(--type 必填，
  full §3/§8.S3 的"缺省读 INI"措辞被否决);(2) USB 默认忠实搬运不调 start()(full §5.1
  本就如此，user #1 再确认);(3) 保留 regress_wifi_real.sh + 新增 regress_net_real.sh
  (未合并)。其余严格照 full §2/§6/§7/§9 执行。
deliverables:
  - src/app/net_app.cpp
  - src/app/net_app_logic.h
  - src/app/net_app_logic.cpp
  - src/app/wifi_reconnect.h
  - src/app/wifi_reconnect.cpp
  - src/app/CMakeLists.txt
  - tests/test_net_app_logic.cpp
  - tests/CMakeLists.txt
  - script/regress_net_real.sh
verification:
  commands:
    - cmake -DBUILD_FOR_SIMULATION=ON -B build_sim -S . && cmake --build build_sim -j$(nproc) --target htc_net_app --target test_net_app_logic
    - cmake -DCMAKE_TOOLCHAIN_FILE=toolchain.cmake -B build -S . && cmake --build build -j$(nproc) --target htc_net_app
    - ./build_sim/bin/test_net_app_logic
    - ./build_sim/bin/htc_net_app --help
    - ./build_sim/bin/htc_net_app ; echo exit=$?
    - ./build_sim/bin/htc_net_app --type wifi --no-dhcp ; echo exit=$?
    - test -e build/bin/htc_net_app && test -e build_sim/bin/htc_net_app && ! test -e build/bin/htc_wifi_app
    - git diff HEAD --stat -- src/app/main_app.cpp src/hal src/hardware/mcu/MCU.cpp src/common/misc src/network
    - grep -rn "htc_wifi_app\|wifi_app_logic" src tests
  evidence_ref: artifacts/T7-implementer-evidence.md
state_delta:
  set_task_status: {}
  add_decision: []
  add_risk: []
artifact_path: artifacts/T7-implementer-report.md
next: tester
---

# T7 Implementer Report (card)

本文件为 report card（只持指针）。完整证据（编译输出/test 输出/grep/git diff/文件清单/
偏离说明）见 `artifacts/T7-implementer-evidence.md`。

## 改了哪些文件
- 改名(git mv)：`src/app/wifi_app.cpp→net_app.cpp`、`wifi_app_logic.{h,cpp}→net_app_logic.{h,cpp}`、
  `tests/test_wifi_app_logic.cpp→test_net_app_logic.cpp`。
- 内容扩展：`net_app.{cpp,logic.h,logic.cpp}`、`tests/test_net_app_logic.cpp`。
- CMake 联动：`src/app/CMakeLists.txt`(target 重命名 + sim/T32 两块 link network 全依赖)、
  `tests/CMakeLists.txt`(test target 重命名)。
- 新增脚本：`script/regress_net_real.sh`。
- 保留不改：`src/app/wifi_reconnect.{h,cpp}`(WiFi 专属)。
- 零改动：`src/app/main_app.cpp`、`src/hal/**`、`src/hardware/mcu/MCU.cpp`、`src/common/misc/Misc.*`、`src/network/**`。

## 双平台编译
全过。build_sim/bin/htc_net_app + build/bin/htc_net_app 均生成；build*/bin/htc_wifi_app 已清。
T32 二进制为 ELF 32-bit MIPS uclibc。

## 有无偏离 planner-full
有，均因用户拍板 3 决策优先（详见 evidence §9）：CLI 不读 INI、USB 默认不调 start()、
保留 wifi 脚本 + 新增 net 脚本。其余照 full 执行。
