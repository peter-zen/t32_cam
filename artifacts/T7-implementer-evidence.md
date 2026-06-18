# T7 Implementer — Evidence

owner: implementer · task: T7 · node: implementer · flow: feature · date: 2026-06-17

本文件汇总 T7 implementer 节点的全部验证证据（编译输出、测试输出、grep / git diff 证据、文件清单）。

---

## 1. 双平台编译

### 1.1 PC 仿真 (build_sim)
```
$ cmake -DBUILD_FOR_SIMULATION=ON -B build_sim -S .   # rc=0, Configuring done
$ cmake --build build_sim -j$(nproc) --target htc_net_app --target test_net_app_logic
... (仅 Logger deprecated 警告，无 error)
[100%] Linking CXX executable ../../bin/htc_net_app
[100%] Built target htc_net_app
[100%] Linking CXX executable ../bin/test_net_app_logic
[100%] Built target test_net_app_logic
build_rc=0
```
（首次链接失败：libnetwork.so 的 PRIVATE 传递依赖未暴露给 htc_net_app，缺
`DeviceConfig::*`(devconf) 与 `CRC::calculate_crc16`(common_utils_crc)。已在
src/app/CMakeLists.txt 的 sim/T32 两块显式补 network 的完整依赖链
`setting env devconf disk common_time_rtc common_time_timezone power md5
common_utils_base64 common_utils_crc common_utils_serial crc16`，重编通过。）

### 1.2 T32 交叉编译 (build/, toolchain.cmake, uclibc)
```
$ cmake -DCMAKE_TOOLCHAIN_FILE=toolchain.cmake -B build -S .   # rc=0
$ cmake --build build -j$(nproc) --target htc_net_app
[100%] Linking CXX executable ../../bin/htc_net_app
[100%] Built target htc_net_app
build_rc=0
```
T32 二进制架构确认：
```
$ file build/bin/htc_net_app
build/bin/htc_net_app: ELF 32-bit LSB executable, MIPS, MIPS32 rel2 version 1 (SYSV),
  dynamically linked, interpreter /lib/ld-uClibc.so.0, stripped
```
（uclibc 目标，与 build/ 既有配置一致。）

---

## 2. 单元测试

```
$ cmake --build build_sim -j$(nproc) --target test_net_app_logic   # rc=0
$ ./build_sim/bin/test_net_app_logic
test_net_app_logic: ALL PASS
test_rc=0
```
覆盖 16 个 case：WiFi 专属 10 个（从 test_wifi_app_logic 原样迁移，断言不变）+
T7 新增 6 个（testParseNetType / testPtypeToNetType / testNetTypeIfname /
testIsNetworkUp / testEthNeedsConnect / testUsbNeedsStartDefault）。

---

## 3. sim 冒烟（全部不 segfault）

```
--help                                  -> exit=0
(no --type)                             -> exit=6   (REQUIRED --type enforced)
--type bogus                            -> exit=6   (parseNetType -> NET_INVALID)
--type wifi --no-dhcp                   -> exit=6   (sim 无 MCU 凭据 -> ABORT)
--type wifi --ssid X --pwd Y --no-dhcp  -> exit=0   (sim stub connectWifi 成功)
--type eth  --no-dhcp                   -> exit=3   (sim 无 eth0 IP/gw)
--type eth   (DHCP on)                  -> exit=3   (sim udhcpc/无 IP)
--type usb  --no-dhcp                   -> exit=2   (sim loadDriver 失败)
--type usb --usb-bringup --usb-model BOGUS -> exit=6  (bad model 校验)
--type wifi --bogus-opt                 -> exit=6   (未知选项)
```
所有路径均正常退出，无 segfault。sim 下执行层无设备即 false/早退（符合
planner-full §5.4 / §4 sim 行为预期）。

---

## 4. 二进制存在性 / 旧产物清除

```
build/bin/htc_net_app      OK
build_sim/bin/htc_net_app  OK
build/bin/htc_wifi_app     absent (good)   # 旧产物已 rm（gitignored build/ 残留）
build_sim/bin/htc_wifi_app absent (good)
```
（CMake target 已重命名，不再生成 htc_wifi_app；build*/bin 里的旧二进制是上次
构建的 gitignored 残留，已手动 rm 以通过验收断言。）

---

## 5. 零改动验证（硬约束）

```
$ git diff HEAD --stat -- src/app/main_app.cpp src/hal src/hardware/mcu/MCU.cpp \
                              src/common/misc src/network
(empty — rc=0)
```
受保护路径零改动坐实。

`git diff HEAD --stat`（本次 T7 全部改动）：
```
 .gitignore                                         |   3 +   (非本次：T7 流程既有工作区状态)
 orchestration-state.yaml                           |  36 +-  (非本次：planner/PM 既有)
 src/app/CMakeLists.txt                             |  63 ++-
 src/app/net_app.cpp                                | 511 +++++  (新建/改名)
 src/app/net_app_logic.cpp                          | 144 +++++ (新建/改名)
 src/app/net_app_logic.h                            | 122 +++++ (新建/改名)
 src/app/wifi_app.cpp                               | 355 ----- (git mv -> net_app.cpp)
 src/app/wifi_app_logic.cpp                         |  75 ---   (git mv -> net_app_logic.cpp)
 src/app/wifi_app_logic.h                           |  71 ---   (git mv -> net_app_logic.h)
 tests/CMakeLists.txt                               |  14 +-
 tests/test_wifi_app_logic.cpp => test_net_app_logic.cpp | 98 +++-  (改名+扩 case)
 11 files changed
```
注：`.gitignore` 与 `orchestration-state.yaml` 是 T7 flow 早先节点
（PM/planner）写入的工作区状态，非 implementer 本次改动；implementer 未触碰它们。

---

## 6. grep 残留检查

```
$ grep -rn "htc_wifi_app" src tests
src/app/net_app.cpp:16,85,201           # 注释里的历史引用 ("former htc_wifi_app")
src/app/net_app_logic.h:10              # 注释里的历史引用
$ grep -rn "wifi_app_logic" src tests
tests/test_net_app_logic.cpp:4,69       # 注释 ("ported from test_wifi_app_logic")
$ grep -rn "wifi_app" src/app/CMakeLists.txt tests/CMakeLists.txt
(none — CMake 零残留)
```
结论：src/tests 里 `htc_wifi_app` / `wifi_app_logic` 只剩注释中的设计说明
（"former htc_wifi_app" 指代历史），无任何 target / 源码 / include 残留。
CMake 完全无 wifi_app 残留。

`wifi_reconnect.{h,cpp}` 按计划保留（WiFi 专属语义，net_app 的 wifi 分支仍
`#include "wifi_reconnect.h"` 调 `wifi_reconnect::currentSSID/reconnectSSID`）。

---

## 7. uclibc 陷阱检查

```
$ grep -nE "std::to_string|std::stoi|[^_]stoi\(" \
    src/app/net_app.cpp src/app/net_app_logic.cpp src/app/net_app_logic.h \
    tests/test_net_app_logic.cpp
(none — clean)
```
未使用 `std::to_string` / `stoi`（已知 uclibc link 陷阱）。新代码不需要数字↔字符串
转换（CLI 解析用字符串比较，PTYPE 用整数字面量）。

---

## 8. 新增 / 改名文件清单

**改名（git mv）：**
- `src/app/wifi_app.cpp` → `src/app/net_app.cpp`
- `src/app/wifi_app_logic.h` → `src/app/net_app_logic.h`
- `src/app/wifi_app_logic.cpp` → `src/app/net_app_logic.cpp`
- `tests/test_wifi_app_logic.cpp` → `tests/test_net_app_logic.cpp`

**保留不改（计划内）：**
- `src/app/wifi_reconnect.{h,cpp}`（WiFi 专属语义）

**内容扩展：**
- `net_app_logic.{h,cpp}`：namespace `wifi_app_logic→net_app_logic`；保留 WiFi 专属
  `decide/decisionExitCode/mayWriteBack/normalizeSsid` + `Decision/LinkState/Target/ExitCode`
  原样；新增 `NetType/parseNetType/ptypeToNetType/netTypeIfname/isNetworkUp/
  ethNeedsConnect/usbNeedsStartDefault`（全纯函数，无 syscall，PTYPE 用字面常量 1/4/8
  避免链 Common.h，保持 test 零重依赖）。
- `net_app.cpp`：WiFi 分支原样搬入 `runWifi()`（仅 namespace/include/日志 app 名换）；
  新增 `runEth()`（setNetworkInterfaceName("eth0")+startDHCP，无 connect）；
  新增 `runUsb()`（loadDriver→open→preconfig→startDHCP；`--usb-bringup` 才插
  setModel(--usb-model)+start()）；CLI `--type` 必填（不读 INI）+ `--usb-bringup` +
  `--usb-model`；exit code 0/2/3/4/5/6 复用。

**CMake 联动：**
- `src/app/CMakeLists.txt`：target `htc_wifi_app→htc_net_app`；source list 改名；
  sim/T32 两块 link 加 `network` 及其完整传递依赖；注释更新。
- `tests/CMakeLists.txt`：target `test_wifi_app_logic→test_net_app_logic`；源文件名换。

**新增脚本：**
- `script/regress_net_real.sh`（Eth/USB 真机回归，参照 regress_wifi_real.sh；
  USB-default 标为 T7-usb-no-start 已知缺陷断言；USB-bringup 断言 IP/gw）。

**零改动：** `src/app/main_app.cpp`、`src/hal/**`、`src/hardware/mcu/MCU.cpp`、
`src/common/misc/Misc.*`、`src/network/**`。

---

## 9. 与 planner-full 的偏离说明

用户拍板的 3 个最终决策覆盖了 planner-full 里部分矛盾措辞，implementer 严格按
**用户决策**执行（已在代码与注释落实），偏离点如下（均为用户决策优先）：

1. **CLI 不读 INI（planner-full §3/§8.S3 有"缺省读 INI BOOT/PType"措辞）**：
   按 user-decision #2，`--type` 必填，本阶段完全不读 INI。net_app.cpp main 里
   `--type` 缺失即 exit 6，不调任何 DeviceConfig/Settings。与 wifi_app 刻意不读 INI
   一致，零 INI 凭据坑。
2. **USB 默认忠实搬运（planner-full §5.1 本就如此，user-decision #1 再确认）**：
   `--type usb` 默认 loadDriver→open→preconfig→startDHCP，**不调 start()**。
   `--usb-bringup`（默认关）才在 preconfig 后插 setModel(--usb-model)+start()。
3. **保留 regress_wifi_real.sh + 新增 regress_net_real.sh（user-decision #3）**：
   未合并脚本，wifi 脚本原样保留，net 脚本独立新增。

其它均严格照 planner-full §2 映射表 / §7 命名联动 / §6 纯逻辑层 / §9 测试用例执行，
无额外偏离。
