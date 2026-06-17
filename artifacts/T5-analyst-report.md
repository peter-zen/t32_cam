---
contract: report
contract_version: "1"
task_id: T5
node: analyst
flow: bug
status: success
summary: 第四次启动是内核 oops(DbusProcess 退出路径 robust-futex 损坏),根因是 Misc::connectWifi 用进程内静态标志 already_inited_wifi 守卫 insmod+无脑重连,重启清零导致每次重复 insmod(报 File exists)+ 重 spawn wpa_supplicant(撞残留 ctrl socket),累积触发内核态损坏;修复方向为状态驱动探测(查驱动/网络真实状态,已连跳过),退出层已是现状+注释,不触及 src/hal/**。
deliverables:
  - artifacts/T5-bug-input.md
  - artifacts/T5-analyst-evidence.md
  - artifacts/T5-analyst-report.md
verification:
  commands:
    - grep -n "already_inited_wifi\|insmod\|wpa_conn\|connectWifi\|startDHCP" src/common/misc/Misc.cpp
    - grep -n "connectWifi\|startDHCP" src/app/main_app.cpp
    - grep -n "wpa_supplicant\|driver_loaded\|COMPLETED" src/platform/tool/wpa_conn.cpp
    - cat sdk/include/imp/imp_system.h   # N/A 仅供 T3 参考;T5 不涉 IMP
  evidence_ref: artifacts/T5-analyst-evidence.md
state_delta:
  set_task_status: {}
  add_decision:
    - key: T5-root-cause
      value: "第四次内核 oops(DbusProcess robust-futex)根因=Misc::connectWifi(Misc.cpp:401)用进程内静态标志 already_inited_wifi(Misc.cpp:26/.h:50)守卫,重启清零→每次重 insmod(File exists)+重 spawn wpa_supplicant(撞残留 /tmp/wpa_supplicant ctrl socket),累积内核态损坏。非用户态、与 T2/T3 无关。"
    - key: T5-fix-direction
      value: "状态驱动探测:Misc 加 isWifiDriverLoaded(grep /proc/modules)+isWifiConnected(getIPAddress+getGatewayAddress),作 connectWifi/startDHCP 入口短路(已连/已加载跳过),删 already_inited_wifi;退出层现状+注释;不动 wpa_conn.cpp/调用点/不加 pkill。用户已批准 plan(joyful-stirring-starlight.md)。"
  add_risk:
    - key: T5-hw-regression-pending-user
      severity: medium
      description: "双平台编译可验,但连跑 6 次不 oops 的真实回归需 T32 硬件执行(imp_stub/wpa 无真实内核驱动),留用户设备侧回归。"
artifact_path: artifacts/T5-analyst-report.md
next: implementer
---

# T5 Analyst Report — 给 implementer

## 根因(一句话)
`Misc::connectWifi` 用进程内标志 `already_inited_wifi`(重启清零)守卫 insmod+连接 → 每次重启重复
`insmod`(`File exists`)+ 重 spawn `wpa_supplicant`(撞残留 ctrl socket `ctrl_iface exists`),
wpa/dbus 反复创建销毁累积出内核 robust-futex 损坏,第4次 oops(`DbusProcess`,debug1.log)。

## 修复方向(用户已批准)
状态驱动探测(`Misc::isWifiDriverLoaded` / `isWifiConnected`)作 `connectWifi`/`startDHCP` 入口短路。
详见批准 plan:`~/.claude/plans/joyful-stirring-starlight.md` + `artifacts/T5-analyst-evidence.md`。

## 触及范围
`src/common/misc/Misc.{h,cpp}` + `src/app/main_app.cpp`(仅注释)。**不触及 src/hal/\*\***。

## 验收
- 双平台编译通过(T32 build/ + SIM build_sim/)。
- 设备连跑 ≥6 次:第2次起 `WiFi already connected, skip connectWifi` + `already has IP, skip DHCP`,
  无 insmod/File exists/ctrl_iface exists/oops;退出后 wpa_supplicant/驱动仍在。
