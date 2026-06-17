---
contract: dispatch
contract_version: "1"
task_id: T5
node: reviewer
flow: bug
upstream:
  - from: implementer
    artifact: artifacts/T5-implementer-report.md
description: |
  审查 T5(WiFi 驱动/连接状态复用)。独立重编译双平台 + 逐项审查探测器正确性/SIM-safe/幂等/
  调用点零改动/退出不碰 WiFi/行尾干净。聚焦 T5 改动(Misc.h/.cpp + main_app 注释);
  工作树里 RtspServer/IngenicVideo/main_app shutdown 是 T2/T3 既存未 commit 改动,
  已各自审查过,不属本次范围,但一起编译。
acceptance:
  - 独立重编译 build(T32)+ build_sim(SIM)均 exit 0
  - isWifiDriverLoaded/isWifiConnected 只读 POSIX、SIM-safe、幂等无副作用;PC 上返回 false 不短路
  - connectWifi 入口 isWifiConnected() 短路正确;驱动守卫 isWifiDriverLoaded()+insmod 后复检(EEXIST 吸收)正确;startDHCP 入口 getIPAddress 短路正确;已删 already_inited_wifi
  - main_app 调用点(1566/1367 等)零改动;wpa_conn.cpp 未动;无 pkill;无新 SIM 守卫
  - performCleanup/main_exit 仅注释、退出仍不碰 WiFi
  - 行尾干净:`git diff --stat` Misc.cpp≈46 行 / Misc.h≈4 行,无整段同内容 +/- 噪音
  - 输出 report card(passed→结束;failed→loopback implementer)
output_contract: report-card@v1
artifact_path: artifacts/T5-reviewer-report.md
pointers:
  files:
    - src/common/misc/Misc.h
    - src/common/misc/Misc.cpp
    - src/app/main_app.cpp
    - src/platform/tool/wpa_conn.cpp
  grep:
    - "isWifiDriverLoaded|isWifiConnected|already_inited_wifi|connectWifi|startDHCP"
    - "getIPAddress|getGatewayAddress|/proc/modules"
constraints:
  - 独立验证,不只信 implementer 报告
  - 重编译用既有 build/ 与 build_sim/(别 rm build/)
  - 不 commit/push
---

# Reviewer 任务 (T5)

读 implementer 产物 + diff + 批准 plan,独立审查:
- `artifacts/T5-implementer-report.md`、`artifacts/T5-implementer-evidence.md`
- 批准 plan:`~/.claude/plans/joyful-stirring-starlight.md`
- T5 diff:`git diff -- src/common/misc/Misc.h src/common/misc/Misc.cpp src/app/main_app.cpp`
  (Misc.cpp≈46 / Misc.h≈4 / main_app.cpp≈32 纯注释)
- 注:工作树另有 T2(RtspServer+main_app shutdown)/T3(IngenicVideo exit)未 commit 改动,已各自审查过,
  非本次范围,但与 T5 一起编译。

## 必查项
1. **独立重编译**:`cmake --build build -j$(nproc)` + `cmake --build build_sim -j$(nproc)`,exit 0,记进 evidence。
2. **探测器正确性**:`isWifiDriverLoaded`(grep /proc/modules,宏分支 8189fs/cywdhd,syscall 1000ms)只读幂等;
   `isWifiConnected`(`!getIPAddress().empty() && !getGatewayAddress().empty()`)防 link-local。两者纯 POSIX、SIM-safe。
3. **SIM-safe**:PC 上无 wlan0→getIPAddress 返回""/无 8189fs→/proc/modules grep 失败→都 false→不短路,走原路径,SIM 行为不变;确认无新 `#ifndef BUILD_FOR_SIMULATION`(不该加)。
4. **短路逻辑**:`connectWifi` 入口 isWifiConnected() 短路;驱动守卫 isWifiDriverLoaded();insmod 后复检(EEXIST 吸收,真没加载才 return false);`startDHCP` netif 后 getIPAddress 短路。已删 already_inited_wifi 及其定义。
5. **边界**:调用点零改动(main_app.cpp:1566/1570、1367/1395 等 connectWifi/startDHCP 调用未动);wpa_conn.cpp 未动;无 pkill;半状态(驱动在、网络没连)行为正确(跳 insmod、走 wpa_conn 重连一次)。
6. **退出**:performCleanup/main_exit 仅注释,退出仍不碰 WiFi(无 rmmod/kill wpa/清 /tmp)。
7. **行尾干净**:`git diff --stat` Misc.cpp/Misc.h 行数合理(46/4 量级),抽查无整段 `-foo/+foo` 同内容噪音行(行尾 CRLF/LF 混乱)。main_app.cpp/RtspServer/IngenicVideo 仍纯 LF 未被破坏。
8. **EEXIST 复检**:确认 insmod 失败但 isWifiDriverLoaded()=true 时视为成功(吸收 File exists 竞态),不会误判失败。
9. **缺测试**:本仓无 WiFi/内核单测;给设备侧回归脚本(连跑 6 次、dmesg 检查、退出不变性)。

## 结论
- 通过→status=success,next=`:end`,evidence 含 passed/success。
- 实质问题→status=failed,next=implementer(loopback),evidence 列清要改什么。
- 小瑕疵→status=partial。

## 交付物(必写)
- `artifacts/T5-reviewer-evidence.md`:独立 build 成功 + 逐项审查结论(含 success/passed)。
- `artifacts/T5-reviewer-report.md`:report card(report-card@v1,verification.evidence_ref → evidence,
  artifact_path = 本 report,next 视结论)。注意 set_task_status 设 `{T5: done}`(若通过)。

返回给我:≤300 字中文审查结论(passed/failed/partial + 关键发现 + 是否需 loopback)。
