---
contract: evidence
contract_version: "1"
task_id: T5
node: reviewer
flow: bug
status: success
summary: |
  T5(WiFi 驱动/连接状态复用)独立审查通过。双平台独立重编译均 exit 0;
  探测器(isWifiDriverLoaded/isWifiConnected)只读 POSIX、SIM-safe、幂等无副作用,
  与 UsbDongle::loaded 同范式;短路逻辑(connectWifi 入口 isWifiConnected、驱动守卫
  isWifiDriverLoaded、insmod 后复检吸收 EEXIST、startDHCP 入口 getIPAddress)正确;
  调用点零改动;wpa_conn.cpp 未动;退出路径仅注释不碰 WiFi;无 pkill/rmmod;
  行尾干净(--ignore-all-space stat == 普通 stat = 46/4,无整段同内容噪音)。
  无实质问题,passed。真机回归留设备侧。
---

# T5 — Reviewer 证据 (evidence)

status: **success / passed** — 独立重编译双平台 exit 0,逐项审查全过,无实质问题,无超范围改动。

## 1. 独立重编译(双平台 exit 0)

### T32 cross-build(`cmake --build build -j$(nproc)`)
```
[ 99%] Built target http_server
[100%] Built target htc_main_app
T32_BUILD_EXIT=0
```

### PC SIM build(`cmake --build build_sim -j$(nproc)`)
```
[ 99%] Built target test_http_server
[100%] Built target htc_main_app
SIM_BUILD_EXIT=0
```

(非首次 reviewer 自跑,与 implementer 报告独立验证一致。仅有预先存在的无关警告。)

## 2. 探测器正确性

### isWifiDriverLoaded(Misc.cpp:284-298)
- `grep -q '^<module>' /proc/modules` + `syscall(command.c_str(), 1000) == 0`。
- 宏分支:`WIFI_TYPE_CYW43012`→`cywdhd`、`WIFI_TYPE_RTL8189FS`→`8189fs`,`#else #error`。
- 宏来源 `CMakeLists.txt:67` 定义 `WIFI_TYPE_RTL8189FS`(双平台编译均过 → `#error` 未触发,宏定义正常)。
- 与 `UsbDongle::loaded`(src/network/UsbDongle.cpp:25-29)范式**完全一致**(同 `^name` 前缀 grep + `syscall==0`),复用成熟模式。
- `syscall` 返回 `system_call` 退出码(见 Misc.cpp:522-540,与 `mountSDCard:607-608` `ret=syscall(...); if(ret==0)` 同语义),`==0` 判定正确。
- 只读(`/proc/modules`)、幂等、无副作用。**passed**。

### isWifiConnected(Misc.cpp:300-308)
- `return !getIPAddress(ifname).empty() && !getGatewayAddress(ifname).empty();`
- `getIPAddress`(Misc.cpp:229)用 `getifaddrs`(POSIX);`getGatewayAddress`(Misc.cpp:257)读 `/proc/net/route` 判 `destination=="00000000"`(默认路由网关)。
- IP + 网关双非空 → 防 link-local 误判(link-local 通常无默认网关)。纯 POSIX。**passed**。

## 3. SIM-safe

- PC 上无 `wlan0` → `getIPAddress` 返回 `""`;`/proc/modules` 无 `8189fs` → grep 失败 → `isWifiDriverLoaded`=false。
- `isWifiConnected()` = `!"" && !""` = false → connectWifi 入口不短路;`isWifiDriverLoaded()`=false → 仍尝试 insmod(SIM 无 /system/bin/wifi/insmod 路径但这是现状,SIM 不走 connectWifi 真实路径)。
- 确认 T5 diff **无新增** `#ifndef/#ifdef BUILD_FOR_SIMULATION`:
  ```
  $ git diff -- src/common/misc/Misc.cpp src/common/misc/Misc.h | grep -E "^[+-].*BUILD_FOR_SIMULATION"
  (空)
  ```
  Misc.cpp 中既有的 4 处 `#ifdef BUILD_FOR_SIMULATION`(:343/592/626/637)非本次新增。
- SIM 行为不变。**passed**。

## 4. 短路逻辑(Misc.cpp:425-491)

- `connectWifi`(Misc.cpp:425):
  - 入口 `if (isWifiConnected()) { log INFO; return true; }`(:429-432)— 已连整段跳过,落实用户②③。**passed**。
  - 驱动守卫 `if (!isWifiDriverLoaded())`(:434)— 驱动已加载跳 insmod,落实用户①。**passed**。
  - insmod 后复检 `if (!isWifiDriverLoaded()) { return false; }`(:450-453)— 吸收 EEXIST 竞态(insmod 报 File exists 但模块确实在 → 视为成功,仅复检仍 absent 才 return false)。**passed**。
  - wpa_conn 调用(:460 `wpa_conn wlan0 ... 1`)原样不动,仍传 `driver_loaded=1`。**passed**。
- `startDHCP`(Misc.cpp:473):netif 解析(:475)后 `if (!getIPAddress(netif).empty()) { return true; }`(:477-480)。**passed**。
- `already_inited_wifi` 定义/声明/赋值全删(grep src/ 空)。**passed**。

## 5. 边界(调用点零改动 + wpa_conn 未动 + 无 pkill)

- main_app.cpp 调用点 `connectWifi`(:1375/1574)、`startDHCP`(:1403/1578)**逻辑零改动**:
  ```
  $ git diff -- src/app/main_app.cpp | grep -E "^\+.*Misc::(connectWifi|startDHCP)\("
  (空 — 仅 NOTE 注释中提及,非代码行)
  ```
- `src/platform/tool/wpa_conn.cpp`:`git diff --stat` 空。**passed**。
- 无新增 `pkill`/`rmmod` 实际命令(diff 中 rmmod/pkill 仅出现在注释 "INTENTIONALLY does not rmmod")。**passed**。
- `src/hal/**` 未被 T5 触及(IngenicVideo.cpp/IspOsdManager.cpp 的 diff 属 T3 既存,与 WiFi 无关:grep wifi 词为空)。
- 半状态(驱动在、网络没连):isWifiConnected=false 继续 → isWifiDriverLoaded=true 跳 insmod → 走 wpa_conn 重连一次 → startDHCP 无 IP 跑 udhcpc。行为正确。**passed**。

## 6. 退出(仅注释,不碰 WiFi)

- `performCleanup`(main_app.cpp:888-)NOTE、`main_exit`(main_app.cpp:1838-)NOTE:均 0 代码改动,说明故意不 rmmod/不 kill wpa/不清 /tmp/wpa_supplicant。
- 退出路径无实际 WiFi 操作(无 rmmod/kill wpa/清 /tmp 命令)。**passed**。

## 7. 行尾干净

```
$ git diff --stat -- src/common/misc/Misc.cpp src/common/misc/Misc.h
 src/common/misc/Misc.cpp | 46 +++++++++++++++++++++++++++++++++++++++++++---
 src/common/misc/Misc.h   |  4 +++-
 2 files changed, 46 insertions(+), 4 deletions(-)

$ git diff --ignore-all-space --stat -- src/common/misc/Misc.cpp src/common/misc/Misc.h
(与上面完全相同:46/4)
```
`--ignore-all-space` 输出 == 普通输出 → **无整段 `-foo/+foo` 同内容噪音**(CRLF/LF 翻转已消除)。逐行抽查 Misc.cpp/Misc.h diff 均为真实逻辑改动。**passed**。

## 8. EEXIST 复检

insmod 后 `if (!isWifiDriverLoaded())` 复检(Misc.cpp:450):内核报 File exists 但 grep 确认模块在 → 复检 true → 不 return false → 继续走 wpa_conn。仅复检仍 absent 才 return false。正确吸收竞态,不误判失败。**passed**。

## 9. 测试缺口(本仓无 WiFi/内核单测)

设备侧回归脚本(留 T32 执行):
```bash
# 连跑 6 次,第 2 次起应出现短路日志、无 oops
for i in 1 2 3 4 5 6; do
  htc_main_app -m -wm 0 -rtc 1 &
  PID=$!
  sleep 15   # 等到 RTSP server started
  kill -INT $PID; wait $PID 2>/dev/null
  sleep 2
done
# 断言
dmesg | tail -200 | grep -iE "DbusProcess|robust.futex|oops|segfault"  # 应空
pgrep -a wpa_supplicant    # 仍在(退出不变性)
ls /tmp/wpa_supplicant     # 仍在
lsmod | grep -E "8189fs|cywdhd"  # 仍在
# 日志(第 2 次起)
grep -c "WiFi already connected, skip connectWifi" logs/debug1.log  # >=5
grep -c "already has IP, skip DHCP" logs/debug1.log                 # >=5
grep -c "File exists\|ctrl_iface exists" logs/debug1.log            # 应 0 或仅首次
```

## 约束符合性

- 独立验证(双平台自编译 + 逐项审计,非仅信 implementer 报告)。
- 只读审查 + 重编译,未改代码。
- 未 commit / push。
- `src/hal/**` 未触及;`wpa_conn.cpp` 未动;调用点零改动;无新 SIM 守卫;无 pkill/rmmod 实际命令。
- 复用 `Misc::getIPAddress`/`getGatewayAddress`/`syscall` 与 `UsbDongle::loaded` 范式,未新造网络检测。

## 结论

**passed / success** — 无实质问题,无 must-fix,无需 loopback。真机连跑回归留设备侧(已记 implementer 风险 T5-hw-unverified)。
