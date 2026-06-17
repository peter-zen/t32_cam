---
contract: report
contract_version: "1"
task_id: T5
node: implementer
flow: bug
status: success
summary: |
  WiFi 驱动/连接状态复用实现完成（根因：进程内静态标志 already_inited_wifi 重启清零，导致重复
  insmod 撞 File exists、重 spawn wpa_supplicant 撞 ctrl_iface exists，累积触发内核 oops）。
  把 Misc::connectWifi/startDHCP 的进程内标志守卫换成实时系统状态探测，两个生效调用点
  （mobile -m 的 B 点 main_app.cpp:1574、CMD_CONN_NET 的 A 点 :1375）自动受益，调用点零改动。
  改动：Misc.h 加 isWifiDriverLoaded()/isWifiConnected() 声明、删 already_inited_wifi；
  Misc.cpp 删 already_inited_wifi 定义，加两探测器实现（isWifiDriverLoaded 内联 grep /proc/modules
  宏分支 8189fs/cywdhd，isWifiConnected 复用 getIPAddress+getGatewayAddress 双非空），connectWifi
  入口 isWifiConnected() 短路、驱动守卫改 isWifiDriverLoaded() 且 insmod 后复检吸收 EEXIST 竞态、
  删 already_inited_wifi=true，startDHCP 入口 getIPAddress(netif) 短路；main_app performCleanup/
  main_exit 加 0 代码改动注释说明故意不碰 WiFi 以便复用。双平台编译均 exit 0，未运行 T32 硬件二进制。
  （行尾修正：Misc.cpp/Misc.h 已恢复 HEAD 原始混合行尾并字节级精确 apply，`git diff --stat`
  现为 Misc.cpp 46 行 / Misc.h 4 行纯逻辑改动，无 CRLF 规范化噪音；逻辑与首次实现完全一致。）
deliverables:
  - src/common/misc/Misc.h
  - src/common/misc/Misc.cpp
  - src/app/main_app.cpp
  - artifacts/T5-implementer-evidence.md
  - artifacts/T5-implementer-report.md
verification:
  commands:
    - cmake --build build -j$(nproc)
    - cmake --build build_sim -j$(nproc)
    - grep -rn "already_inited_wifi" src/   # expect empty
    - grep -rn "isWifiDriverLoaded\|isWifiConnected" src/
    - grep -n "WiFi already connected, skip connectWifi\|already has IP, skip DHCP" src/common/misc/Misc.cpp
    - git diff --stat src/platform/tool/wpa_conn.cpp   # expect empty
    - git diff --stat --ignore-all-space src/common/misc/Misc.cpp src/common/misc/Misc.h
    - git diff --stat src/app/main_app.cpp   # expect +32 comments only
  evidence_ref: artifacts/T5-implementer-evidence.md
state_delta:
  set_task_status: {}
  add_decision:
    - key: T5-state-driven-wifi-reuse
      value: "connectWifi/startDHCP 入口短路判据从进程内静态标志 already_inited_wifi 改为实时只读 POSIX 状态探测：isWifiDriverLoaded()=grep '^8189fs|cywdhd' /proc/modules 退出码==0；isWifiConnected(ifname)=getIPAddress+getGatewayAddress 双非空。已连整段跳过 connectWifi（return true）、驱动已加载跳过 insmod（无 File exists）、已有 IP 跳过 udhcpc。重启后真实系统状态复用，消除重复加载/连接累积导致的内核 oops。"
    - key: T5-eexist-race-absorption
      value: "connectWifi 的 insmod 守卫由 if(!already_inited_wifi) 改为 if(!isWifiDriverLoaded())，insmod 后再用 isWifiDriverLoaded() 复检——内核在模块已加载时报 'File exists' 但模块确实在，视为成功（吸收 EEXIST 竞态），仅当复检仍 absent 才 return false。宏分支 insmod 命令、wpa_conn 调用(driver_loaded=1)原样不动。"
    - key: T5-exit-path-wifi-preserved
      value: "main_app performCleanup(:886)与 main_exit(:1832)各加注释（0 代码改动）说明故意不 rmmod 驱动、不 kill wpa_supplicant、不清 /tmp/wpa_supplicant，以便下次启动复用；状态驱动重入使复用安全。退出层本就不碰 WiFi（已是现状），仅文档化意图。"
  add_risk:
    - key: T5-hw-unverified
      severity: medium
      description: "双平台编译 exit 0 + 代码审计 + grep 校验已过，但真机回归（htc_main_app -m 连跑 ≥6 次：第 2 次起出现 'WiFi already connected, skip connectWifi' + 'already has IP, skip DHCP'，无 insmod/File exists/ctrl_iface exists/oops，dmesg 无 DbusProcess/robust-futex，退出后 pgrep wpa_supplicant 与 lsmod|grep 8189fs 仍在）无法在 PC 验，留 T32 设备侧执行（见 plan 验证项 2-4）。"
    - key: T5-module-name-assumption
      severity: low
      description: "isWifiDriverLoaded 按 /proc/modules 报告名（8189fs/cywdhd）用 ^name 前缀 grep 匹配；若设备实际模块名异名（罕见），探测器恒 false 会退化为每次 insmod（原行为），不会误短路，安全降级。"
artifact_path: artifacts/T5-implementer-report.md
next: reviewer
---

# T5 Implementer Report (WiFi 状态复用)

## 本次改动一句话

把 `Misc::connectWifi`/`startDHCP` 的进程内静态标志守卫 `already_inited_wifi` 换成实时只读 POSIX 状态探测（`isWifiDriverLoaded` / `isWifiConnected`），已连整段跳过、驱动已加载不 insmod、已有 IP 不 DHCP；退出路径加注释（0 代码）说明故意保留 WiFi 状态以便复用。

## 改动明细

### 1. `src/common/misc/Misc.h`
- 加声明（connectWifi/startDHCP 后）：
  - `static bool isWifiDriverLoaded();`
  - `static bool isWifiConnected(const std::string &ifname="wlan0");`
- 删 `static bool already_inited_wifi;`（私有静态成员）。

### 2. `src/common/misc/Misc.cpp`
- 删 `bool Misc::already_inited_wifi = false;` 定义。
- 加 `isWifiDriverLoaded()`（getGatewayAddress 后）：内联 `grep -q '^<module>' /proc/modules` 范式（同 UsbDongle::loaded，不 #include），宏分支 `WIFI_TYPE_CYW43012`→`cywdhd`、`WIFI_TYPE_RTL8189FS`→`8189fs`，`return syscall(command.c_str(), 1000) == 0;`。
- 加 `isWifiConnected(ifname)`：`return !getIPAddress(ifname).empty() && !getGatewayAddress(ifname).empty();`（IP+网关双判据防 link-local 误判，纯 POSIX SIM-safe）。
- `connectWifi`：入口加 `if (isWifiConnected()) { log INFO "WiFi already connected, skip connectWifi"; return true; }`；驱动守卫 `if(!already_inited_wifi)`→`if(!isWifiDriverLoaded())`（宏分支 insmod 命令原样留）；insmod 后加 `if(!isWifiDriverLoaded()) { return false; }` 复检吸收 EEXIST；删 `already_inited_wifi = true;`。wpa_conn 调用（传 driver_loaded=1）**不动**。
- `startDHCP`：netif 解析后加 `if(!getIPAddress(netif).empty()) { log INFO "%s already has IP, skip DHCP"; return true; }`。

### 3. `src/app/main_app.cpp`（0 代码改动）
- `performCleanup`（:886）、`main_exit`（:1832）各加 NOTE 注释：故意不 rmmod 驱动 / 不 kill wpa_supplicant / 不清 `/tmp/wpa_supplicant`，状态驱动重入使复用安全。
- 调用点 `connectWifi`(:1375/1574)、`startDHCP`(:1403/1578) **逻辑零改动**。

## 半状态行为（驱动在、网络没连）

`isWifiConnected()`=false → 继续；`isWifiDriverLoaded()`=true → 跳 insmod（无 File exists）；走 wpa_conn 重连一次；`startDHCP` 见无 IP → 跑 udhcpc。失败则 `goto main_exit`（正确）。即期望的"驱动复用 + 重连"。

## 验证结果

| 验证项 | 结果 |
|--------|------|
| T32 cross-build（`cmake --build build`）| **exit 0**，`[100%] Built target htc_main_app` |
| PC SIM build（`cmake --build build_sim`）| **exit 0**，`[100%] Built target htc_main_app` |
| `already_inited_wifi` 残留 | **无**（grep 空）|
| `isWifiDriverLoaded` / `isWifiConnected` 声明+定义 | 齐全（Misc.h:34-35, Misc.cpp:284/300）|
| connectWifi 入口 isWifiConnected 短路 | 在（Misc.cpp:429）|
| connectWifi 驱动守卫 isWifiDriverLoaded + insmod 后复检 | 在（Misc.cpp:434/450）|
| startDHCP 入口 getIPAddress 短路 | 在（Misc.cpp:478）|
| wpa_conn.cpp 改动 | **零**（`git diff --stat` 空）|
| main_app 改动 | **+32 行纯注释**（`git diff --stat`），调用点逻辑未动 |
| 新 SIM 守卫 | **未加**（探测器纯 POSIX 自带 SIM-safe）|
| pkill wpa_supplicant | **未加** |
| src/hal/** | **未触及** |
| commit/push | **未执行** |

## 约束符合性

- 双平台编译通过；新探测器纯 POSIX（getifaddrs / 读 /proc 文件），SIM-safe，未加新 `#ifndef BUILD_FOR_SIMULATION`。
- `src/platform/tool/wpa_conn.cpp` 零改动；main_app 调用点逻辑零改动；未加 pkill。
- 复用 `Misc::getIPAddress` / `getGatewayAddress`，未新造网络检测。
- 未 commit / push。

## 行尾说明

`Misc.cpp`/`Misc.h` 在 HEAD 即为混合行尾（CRLF+LF）。Edit 写入新行用 LF，故 `git diff`（不带 ignore-space）显示部分原有 CRLF 行整行变化——为行尾噪音非逻辑改动；`git diff --ignore-all-space` 证明真实逻辑改动恰为 plan 要求的最小集。`main_app.cpp` HEAD 为纯 LF，注释也用 LF，无噪音。

## 遗留（留设备侧 / reviewer）

- 真机回归（连跑 ≥6 次不 oops、第 2 次起出现短路日志、退出后 wpa_supplicant/8189fs 仍在）无法在 PC 验，留 T32 设备侧执行。
- 模块名取 `/proc/modules` 报告名（8189fs/cywdhd），用 `^name` 前缀匹配兜底变体。
- 未 git commit（未授权）。

详见 `artifacts/T5-implementer-evidence.md`。
