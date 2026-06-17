---
contract: report
contract_version: "1"
task_id: T5
node: reviewer
flow: bug
status: success
summary: |
  T5(WiFi 驱动/连接状态复用)终审通过(passed)。独立重编译双平台(build T32 + build_sim)
  均 exit 0。逐项审查全过:探测器 isWifiDriverLoaded(grep '^8189fs/cywdhd' /proc/modules,
  与 UsbDongle::loaded 同范式)+ isWifiConnected(getIPAddress+getGatewayAddress 双非空,
  防 link-local)只读 POSIX、SIM-safe、幂等无副作用;connectWifi 入口 isWifiConnected() 短路、
  驱动守卫 isWifiDriverLoaded()、insmod 后复检吸收 EEXIST 竞态、startDHCP 入口 getIPAddress
  短路,逻辑正确;already_inited_wifi 已全删;调用点(:1375/1403/1574/1578)零改动;wpa_conn.cpp
  未动;退出路径仅注释不碰 WiFi(无 pkill/rmmod/清 /tmp);行尾干净(--ignore-all-space stat ==
  普通 stat = 46/4,无整段同内容噪音);无新 SIM 守卫。无实质问题、无 must-fix、无需 loopback。
  真机连跑 6 次回归留设备侧(已记 T5-hw-unverified)。
deliverables:
  - artifacts/T5-reviewer-evidence.md
  - artifacts/T5-reviewer-report.md
verification:
  commands:
    - cmake --build build -j$(nproc)       # T32_BUILD_EXIT=0
    - cmake --build build_sim -j$(nproc)   # SIM_BUILD_EXIT=0
    - git diff --stat -- src/common/misc/Misc.cpp src/common/misc/Misc.h   # 46/4
    - git diff --ignore-all-space --stat -- src/common/misc/Misc.cpp src/common/misc/Misc.h  # == 46/4 (无噪音)
    - grep -rn "already_inited_wifi" src/   # 空
    - git diff --stat -- src/platform/tool/wpa_conn.cpp   # 空(未动)
    - git diff -- src/app/main_app.cpp | grep -E "^\+.*Misc::(connectWifi|startDHCP)\("  # 空(调用点零改动)
    - git diff -- src/common/misc/Misc.cpp src/common/misc/Misc.h | grep -E "^[+-].*BUILD_FOR_SIMULATION"  # 空(无新 SIM 守卫)
    - git diff -- src/app/main_app.cpp | grep -iE "^\+.*(pkill|rmmod)"   # 仅注释,无实际命令
  evidence_ref: artifacts/T5-reviewer-evidence.md
state_delta:
  set_task_status:
    T5: done
  add_decision:
    - key: T5-reviewer-passed
      value: "终审通过:双平台独立重编译 exit 0 + 逐项审计全过(探测器只读 POSIX/SIM-safe/幂等、短路逻辑正确、EEXIST 复检、调用点零改动、wpa_conn 未动、退出仅注释、行尾干净、无 pkill/新 SIM 守卫)。无 must-fix,无需 loopback。真机连跑回归留设备侧。"
  add_risk:
    - key: T5-hw-unverified
      severity: medium
      description: "PC 侧验证齐备(双平台 build exit 0 + 代码审计 + grep 校验),但 T32 真机连跑回归(htc_main_app -m 连跑 ≥6 次:第 2 次起出现 'WiFi already connected, skip connectWifi' + 'already has IP, skip DHCP',无 insmod/File exists/ctrl_iface exists/oops,dmesg 无 DbusProcess/robust-futex,退出后 pgrep wpa_supplicant + lsmod|grep 8189fs 仍在)需设备侧执行,见 evidence 第 9 节脚本。"
artifact_path: artifacts/T5-reviewer-report.md
next: ":end"
---

# T5 Reviewer Report Card (WiFi 驱动/连接状态复用 — 终审)

## 结论

**status: success / passed** — 无实质问题,无 must-fix,无需 loopback,可结束(`:end`)。

## 审查覆盖(逐项 passed,详见 evidence)

| 必查项 | 结果 |
|--------|------|
| 独立重编译 build(T32)+ build_sim(SIM) | **均 exit 0**(独立自跑) |
| isWifiDriverLoaded(grep /proc/modules,宏分支 8189fs/cywdhd,只读幂等) | **passed**(与 UsbDongle::loaded 同范式) |
| isWifiConnected(IP+网关双非空,防 link-local,纯 POSIX) | **passed** |
| SIM-safe(无 wlan0→false 不短路,无新 SIM 守卫) | **passed** |
| connectWifi 入口 isWifiConnected 短路 / 驱动守卫 / insmod 后 EEXIST 复检 | **passed** |
| startDHCP 入口 getIPAddress 短路 | **passed** |
| already_inited_wifi 全删 | **passed**(grep src/ 空) |
| 调用点(:1375/1403/1574/1578)零改动 | **passed**(diff 仅注释提及) |
| wpa_conn.cpp 未动 | **passed**(diff stat 空) |
| 无 pkill / rmmod 实际命令 | **passed**(仅注释) |
| 退出路径仅注释不碰 WiFi | **passed** |
| 行尾干净(--ignore-all-space == 普通 stat = 46/4) | **passed**(无整段同内容噪音) |
| src/hal/** 未被 T5 触及 | **passed** |

## 关键发现

- 探测器设计正确:`isWifiDriverLoaded` 复用 `UsbDongle::loaded`(src/network/UsbDongle.cpp:25-29)的成熟 `grep -q '^name' /proc/modules` + `syscall==0` 范式;`isWifiConnected` 用默认路由网关(`destination=="00000000"`)双判据,有效防 link-local 误判。
- EEXIST 竞态吸收正确:insmod 后 `if (!isWifiDriverLoaded())` 复检(Misc.cpp:450),内核报 File exists 但模块确实在 → 视为成功,仅复检仍 absent 才 return false。
- 半状态(驱动在、网络没连)行为正确:跳 insmod + 走 wpa_conn 重连一次 + startDHCP 无 IP 跑 udhcpc,符合"驱动复用 + 重连"预期。
- 行尾噪音已根治:implementer 用字节级精确 apply 恢复 HEAD 混合行尾,`--ignore-all-space` 输出与普通 diff 完全一致(46/4),无任何 `-foo/+foo` 同内容噪音。
- main_app.cpp 中除 T5 注释外的 `rtsp_singleton_used`/`RtspServer::shutdown()` 改动属 **T2**(RtspServer shutdown 接入,已单独审查),非本次范围,与 T5 一起编译通过。

## 遗留(留设备侧)

真机连跑回归(htc_main_app -m 连跑 ≥6 次不 oops、第 2 次起出现短路日志、退出后 wpa_supplicant/8189fs 仍在)无法在 PC 验,留 T32 设备侧执行(脚本见 evidence 第 9 节)。已记风险 T5-hw-unverified(medium)。

详见 `artifacts/T5-reviewer-evidence.md`。
