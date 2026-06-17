---
task_id: T6
node: planner (dispatch)
flow: feature
created: 2026-06-17
---

# T6 — planner dispatch

## 功能一句话
新增一个独立的 WiFi 连接管理应用（`htc_wifi_app`），封装现有 WiFi 底层 API：
driver 单次幂等加载 → 按参数(或 MCU)的 SSID/password 连接(连接前校验当前 SSID，不同才断开重连) → DHCP；
连接成功后可选把 SSID/password 回写 MCU。**不触碰 htc_main_app 现有 WiFi 逻辑**（剥离留作后续单独 task）。

## 用户需求（逐条，验收基准）
1. 判断 WiFi driver 是否加载，没加载就加载一次，加载好了不再加载（幂等）。
2. 按调用参数 SSID/password 连接 WiFi；**若当前已连接但 SSID 与目标不同，断开当前、重连目标**；连上后默认 DHCP。
3. 可由参数决定：连接成功后把 SSID/password 回写 MCU（必须先测试连接成功才写）。
4. 不带 SSID/password 参数时，使用 MCU 的参数(readUPID/readUPWD)连接 WiFi + DHCP。

## PM 已查清的事实（planner 直接用，不必重做发现）

### 用户两个前置问题的结论
- **daemon app 没有 WiFi 管理。** `htc_daemon_app`(`src/app/daemon_app.cpp`) 文件头即
  `// daemon_example.cpp // 进程守护模块使用示例`，是**进程守护(看门狗/关机 GPIO)**模块的示例，
  `main()` 只注册 shutdown 回调把若干 GPIO(POWER_HOLD/IR_CUT/IR_LED/RGB_LED)置为 INPUT。
  `src/service/daemon/`(daemon_api/DeamonServer/DeamonClient)是进程监督+GPIO/电源下电，**零网络/WiFi 代码**。
  ⇒ 没有"现有 WiFi daemon"可复用，单独建 `htc_wifi_app` **合理且无冲突**。
- **可复用的底层 API 已经存在**（静态方法，新 app 直接调用即可；这正满足用户"其他应用可直接整合 API"的诉求）：

### 可复用 WiFi/MCU API（src/common + src/hardware，**非 src/hal，可改可调**）
- `Misc::isWifiDriverLoaded()` —— `src/common/misc/Misc.h:34` / `Misc.cpp:284`
  （内部 `grep '^8189fs' /proc/modules` 或 cywdhd 分支）。
- `Misc::isWifiConnected(ifname="wlan0")` —— `Misc.h:35` / `Misc.cpp:300`。
- `Misc::connectWifi(ssid, password)` —— `Misc.h:31` / `Misc.cpp:425`。
  内部已做：①`isWifiConnected()` 为真就**短路返回**(INFO "WiFi already connected, skip connectWifi")；
  ②否则 `isWifiDriverLoaded()` 为假才 `insmod`，并在 insmod 后 re-check 吸收 EEXIST 竞态。
  ⇒ driver 幂等加载 + "已连不重连"语义已具备。
- `Misc::startDHCP(ifname="")` —— `Misc.h:32` / `Misc.cpp:473`（内部 `udhcpc -i <if> -t 10`）。
- MCU：`readUPID()/readUPWD()` —— `src/hardware/mcu/MCU.h:46-47`（从 MCU 读 SSID/password）；
  `writeUPID(s)/writeUPWD(s)` —— `MCU.h:49-50`（回写）；`IsWifiStationReady()` —— `MCU.h:14`。
- 参考实现 `src/platform/tool/wpa_conn.cpp`：自带 `main()`，shell 直连
  load_wifi_driver→connect_wifi→configure_dhcp（wpa_supplicant/udhcpc）。**未接 MCU/config**，
  是 raw 工具；以 `Misc` 封装层为集成入口更干净（保留 wpa_conn 作行为参考）。

### main_app 现有 WiFi 接线（仅参考，**本任务不改**）
- `-w/--wifi`(L1111)、`-d/--dhcp`(L1113-1114)；connectWifi L1375(用 `[SYSTEM] UPID/PWD`)、
  DHCP L1403；另一路径 L1567-1579 用 `[DEVICE] CSSID/CPWD`；MCU 同步 L487-488(readUPID/readUPWD)。
- T5 的修复(L889-895, L1841-1846) **刻意**不在退出时 rmmod / kill wpa_supplicant / 清
  /tmp/wpa_supplicant，以复用 live 连接、根治 DbusProcess oops。

### 配置键坑（用户已点名）
- `src/common/Common.h:44-45`：`INI_KEY_UPID="UPID"` 但 `INI_KEY_UPWD="PWD"`(**不是 "UPWD"**)；
  section `INI_SECTION_SYS="SYSTEM"`。另 `INI_SECTION_DEVICE="DEVICE"` + `INI_KEY_CSSID/CPWD`(L13-16)
  是 main_app 的另一条连接路径用的。`readPID()` 是设备标识(主机名/序列号/mDNS)，**非 WiFi**，别与 UPID 混。

## 设计核心难点（planner 必须给出明确方案）
**需求②(SSID 不同则断开重连) 与现有 API + T5 复用语义存在张力：**
- 现有 `connectWifi` 只判"连没连"，**不判连的是哪个 SSID** → 直接复用满足不了"切到不同 SSID"。
- 而 T5 刻意保持 wpa_supplicant 常驻以避 DbusProcess oops → **重连绝不能简单 `kill wpa_supplicant`+重 spawn**
  (会重新触发 ctrl_iface 冲突/oops 回归)。
- 需要设计：①读当前 SSID(`iwgetid -r wlan0` / `wpa_cli -i wlan0 status | grep ssid`)；
  ②不同 SSID 时用**优雅重配置**(如 `wpa_cli` 切 network / reconfigure，而非 kill 进程)重连。
  ⇒ planner 评估是否新增 `Misc::currentSSID()` + `Misc::reconnectWifi()`(优雅，不动 wpa_supplicant 进程)
  或给 `connectWifi` 加 force-if-different 模式；**务必不破坏 T5 的 oops 修复**。

## 硬约束
- 双平台编译必须通过：T32 `build/`(`-DBUILD_FOR_SIMULATION=OFF`) + sim `build_sim/`(`ON`)。
- `src/hal/**` PIC-owned，**不碰**（WiFi/MCU 代码在 `src/common`、`src/platform`、`src/network`、`src/hardware`，OK）。
- **本任务不改 htc_main_app 的 WiFi 逻辑**（剥离是后续单独 task）。
- 未经许可不 git commit。
- MCU 回写(需求③)严格"连接成功后才写"。
- sim 下无真实 WiFi 驱动/wpa_supplicant/I2C 实数据 → sim 侧验证以"编译通过 + 代码审计 + grep 校验 + 可 mock 的纯逻辑单测"为准；真机 WiFi 连接回归留脚本给用户(同 T5 口径)。

## 交付物期望
- `artifacts/T6-planner-full.md`：方案(应用结构/CLI 设计/4 需求→代码映射/重连难点方案/文件清单/CMake 接入/双平台策略/测试计划/风险)。
- `artifacts/T6-planner-report.md`：report-card@v1(frontmatter 见 T4-planner-report，含 state_delta)。
