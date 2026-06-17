---
contract: dispatch
contract_version: "1"
task_id: T5
node: implementer
flow: bug
upstream:
  - from: analyst
    artifact: artifacts/T5-analyst-report.md
description: |
  实现 WiFi 驱动/连接状态复用:把 Misc::connectWifi/startDHCP 的进程内标志守卫换成
  实时系统状态探测(isWifiDriverLoaded/isWifiConnected),已加载不 insmod、已连跳过整个连接流程,
  删 already_inited_wifi,退出路径加注释。用户已批准 plan。
acceptance:
  - Misc 加 isWifiDriverLoaded()(grep /proc/modules,宏分支 8189fs/cywdhd)+ isWifiConnected(ifname)(getIPAddress+getGatewayAddress 非空)
  - connectWifi 入口 isWifiConnected() 短路(已连 return true);驱动守卫改 isWifiDriverLoaded();删 already_inited_wifi
  - startDHCP 入口 getIPAddress(netif) 短路(已有 IP return true)
  - main_app performCleanup/main_exit 加注释(0 代码改动),说明故意不碰 WiFi 以便复用
  - 不动 wpa_conn.cpp、不改任何调用点、不加 pkill、不加新 SIM 守卫
  - 双平台编译通过(build/ T32 + build_sim/ SIM)
  - 不 commit/push
output_contract: report-card@v1
artifact_path: artifacts/T5-implementer-report.md
pointers:
  files:
    - src/common/misc/Misc.h
    - src/common/misc/Misc.cpp
    - src/app/main_app.cpp
  grep:
    - "already_inited_wifi|connectWifi|startDHCP|getIPAddress|getGatewayAddress"
    - "WIFI_TYPE_RTL8189FS|WIFI_TYPE_CYW43012|insmod 8189fs|insmod.*cywdhd"
constraints:
  - 双平台编译通过;新探测器纯 POSIX(getifaddrs/读 /proc 文件),SIM-safe,不加新 SIM 守卫
  - 不动 src/platform/tool/wpa_conn.cpp、不改 main_app 调用点逻辑、不加 pkill wpa_supplicant
  - 不 commit/push/rollback
  - 改动最小化、贴合既有风格;探测器幂等、只读、无副作用
  - 复用 Misc::getIPAddress(Misc.cpp:229)/getGatewayAddress(Misc.cpp:258),别新造网络检测
---

# Implementer 任务 (T5)

**权威依据**:批准的 plan `~/.claude/plans/joyful-stirring-starlight.md`(逐文件、逐函数、边界、验证全在里面),
以及 `artifacts/T5-analyst-evidence.md`(根因 + file:line + 可复用函数)。先读这两份。

## 要改的(严格按 plan,最小改动)

### 1. `src/common/misc/Misc.h`
- 加声明(connectWifi 附近,约 :31):`static bool isWifiDriverLoaded();` 和 `static bool isWifiConnected(const std::string& ifname = "wlan0");`
- 删 `static bool already_inited_wifi;`(:50)。

### 2. `src/common/misc/Misc.cpp`
- 删 `bool Misc::already_inited_wifi = false;`(:26)。
- 加 `isWifiDriverLoaded()`(getGatewayAddress 附近,约 :283):**内联** `/proc/modules` grep 范式
  (参考 UsbDongle.cpp:25,但**别 #include UsbDongle**,避免依赖环)。按编译期宏:
  `RTL8189FS` → `grep -q '^8189fs' /proc/modules`;`CYW43012` → `grep -q '^cywdhd' /proc/modules`。
  返回 grep 退出码==0。用 Misc::syscall(..., 1000) 执行。
- 加 `isWifiConnected(ifname)`: `return !getIPAddress(ifname).empty() && !getGatewayAddress(ifname).empty();`
- `connectWifi`(:401):入口加 `if (isWifiConnected()) { log INFO "WiFi already connected, skip connectWifi"; return true; }`;
  驱动守卫 `if(!already_inited_wifi)`→`if(!isWifiDriverLoaded())`(宏分支 insmod 命令原样留);
  insmod 后用 `isWifiDriverLoaded()` 复检(吸收 EEXIST 竞态,真没加载才 return false);
  删 `already_inited_wifi = true;`(:418)。wpa_conn 调用(:425)**不动**。
- `startDHCP`(:438):netif 解析(:440)后加 `if (!getIPAddress(netif).empty()) { log INFO "... already has IP, skip DHCP"; return true; }`。

### 3. `src/app/main_app.cpp`(仅注释)
- performCleanup(:888)、main_exit(:1832)各加注释:故意不卸载驱动/不 kill wpa_supplicant/不清
  `/tmp/wpa_supplicant`,以便下次启动复用;状态驱动重入(Misc::isWifiDriverLoaded/isWifiConnected)使复用安全。

## 验证(写入证据)
1. 双平台 build:`cmake --build build -j$(nproc)`(T32)+ `cmake --build build_sim -j$(nproc)`(SIM)。
2. grep 确认:isWifiDriverLoaded/isWifiConnected 存在;connectWifi/startDHCP 入口短路;无 already_inited_wifi;
   wpa_conn.cpp 无改动;main_app 调用点(1566/1367)无改动。
3. 把双平台 build 成功输出(含 "Built target"/"exit 0")+ grep + 关键 diff 写进 `artifacts/T5-implementer-evidence.md`
   (含 success/exit 0)。**不运行** T32 硬件二进制。

## 交付物(必写)
- `artifacts/T5-implementer-evidence.md`:diff 摘要 + 双平台 build 成功 + grep 证据(含 success/exit 0)。
- `artifacts/T5-implementer-report.md`:report card(report-card@v1,status ∈ success/partial/failed/blocked,
  verification.evidence_ref → evidence,artifact_path = 本 report,deliverables 用文件路径,next 建议 reviewer)。

返回给我:≤300 字中文摘要(改了哪些、探测器判据、短路接在哪、双平台 build 是否通过、遗留风险)。
