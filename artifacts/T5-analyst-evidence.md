# T5 — Analyst 根因 + 方案 (evidence)

status: success / 根因落在 `src/common/misc/`(非 PIC-owned),方案已获用户批准。

## 根因(Root cause)
APP 反复重启时,`Misc::connectWifi`(Misc.cpp:401-436)唯一的加载保护是**进程内静态标志**
`already_inited_wifi`(Misc.cpp:26 / Misc.h:50)——进程一重启就清零。后果:
1. 每次重 `insmod /system/bin/wifi/8189fs.ko` → 内核报 `File exists`(debug.log L12 / debug1.log L12)。
2. 每次重新 spawn `wpa_supplicant ... -C /tmp/wpa_supplicant &`(wpa_conn.cpp:155,后台、无 -B、退出从不清理)
   → 撞上次残留的 ctrl socket,报 `ctrl_iface exists and seems to be in use`(debug.log L27/32)。
3. wpa_supplicant 反复创建销毁 + dbus 线程(`SystemCall_Dbus_ReadWrite_Thread ... maybe client is close`)
   累积出内核态 robust-futex 损坏,第4次触发 oops(debug1.log:DbusProcess 退出路径 `exit_robust_list`,
   ExcCode 0b CP Unusable)。

根因性质:**进程内状态标志无法反映跨重启的真实系统状态** → 重复加载/连接 → 内核态累积损坏。
与 T2/T3(用户态 IMP teardown)无关。

## 关键代码定位
- `Misc::connectWifi`:Misc.cpp:401-436;`already_inited_wifi` 守卫 Misc.cpp:405/418;宏分支
  8189fs(Misc.cpp:409)/cywdhd(Misc.cpp:407);调 wpa_conn 传 `driver_loaded=1`(Misc.cpp:425)。
- `Misc::startDHCP`:Misc.cpp:438-452,`udhcpc -i <netif> -t 10`(Misc.cpp:444)。
- `wpa_conn` 工具:wpa_conn.cpp,argv `<ifname> <ssid> <pass> <timeout> <driver_loaded>`;
  已支持 driver_loaded(wpa_conn.cpp:59);spawn wpa_supplicant(wpa_conn.cpp:155);
  轮询 wpa_state==COMPLETED(wpa_conn.cpp:170-228)。**本身无 SIM 守卫,只被真机 Misc 调起。**
- **可复用状态检测**(已存在,别新造):
  - 驱动已加载:`UsbDongle::loaded(name)`(UsbDongle.cpp:25)grep `/proc/modules` 的范式。
  - 网络已连:`Misc::getIPAddress(ifname)`(Misc.cpp:229,`getifaddrs`,无 IP 返回"");
    `Misc::getGatewayAddress(ifname)`(Misc.cpp:258,`/proc/net/route`)。
  - 注意:`MCU::IsWifiStationReady()`(MCU.cpp:82)是桩恒 true,**不可**作判据。
- **生效 WiFi 调用点**:B=mobile `-m`(main_app.cpp:1566/1570,`#ifndef SIM` 包裹);A=`CMD_CONN_NET`
  (main_app.cpp:1367/1395,无 SIM 守卫);C=rtsp-server(1660-1664 注释)。用户测 `-m`(B)。
- **退出**:performCleanup(main_app.cpp:888-931)+ main_exit(:1832-1879)**从不碰 WiFi**。已是用户要的"退出不动 WiFi"。
- 驱动选择:编译期宏 `WIFI_TYPE_RTL8189FS`(CMakeLists.txt:67 启用),无运行期判断。

## 修复方向(已获用户批准 → 详见 plan 文件)
状态驱动探测,作为 `Misc::connectWifi`/`startDHCP` **入口短路**:
1. `Misc` 加 `isWifiDriverLoaded()`(grep `/proc/modules`,宏分支查 `8189fs`/`cywdhd`)
   + `isWifiConnected(ifname)`(`!getIPAddress().empty() && !getGatewayAddress().empty()`)。
2. `connectWifi` 入口 `isWifiConnected()` 短路(已连跳过整个连接);驱动守卫
   `if(!already_inited_wifi)`→`if(!isWifiDriverLoaded())`;删 `already_inited_wifi`。
3. `startDHCP` 入口 `getIPAddress(netif)` 短路。
4. main_app 退出路径加注释(0 代码改动)。
5. 不动 wpa_conn.cpp、不改调用点、不加 pkill、不加新 SIM 守卫(新代码纯 POSIX 自带 SIM-safe)。

## 复现 / 回归
- 复现:T32,连跑 `htc_main_app -m` 4+ 次,`^C` 退出,第4次 oops(debug1.log)。
- pass:连跑 ≥6 次,第2次起出现 `WiFi already connected, skip connectWifi` +
  `already has IP, skip DHCP`,无 `insmod`/`File exists`/`ctrl_iface exists`/oops;
  dmesg 无 DbusProcess/robust-futex;退出后 `pgrep wpa_supplicant` 与 `lsmod|grep 8189fs` 仍在。

## 是否触及 src/hal/**
不触及。改动在 `src/common/misc/`(Misc.h/.cpp)+ `src/app/main_app.cpp`(注释)。
