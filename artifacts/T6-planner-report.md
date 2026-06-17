---
contract: report
contract_version: "1"
task_id: T6
node: planner
flow: feature
status: success
summary: |
  T6 新增独立 WiFi 连接管理应用 htc_wifi_app（前台工具，非常驻 daemon），双平台编译。
  复用既有 Misc::connectWifi/isWifi*/startDHCP + MCU::read/writeUPID/UPWD，4 条用户需求逐条映射。
  核心难点(SSID 不同则重连) 的方案：新增 Misc::currentSSID() + Misc::reconnectSSID() 两个非破坏性
  静态方法，经既有 wpa_cli ctrl_iface socket (/tmp/wpa_supplicant) 做 reconfigure/add_network 优雅切网，
  全程不 kill/respawn wpa_supplicant，从物理上不进入 T5 修复的 DbusProcess-oops/ctrl_iface 冲突
  触发条件。决策层(wifi_app_logic)拆成纯函数库可单测(sim 侧 Decision/回写门控/退出码)，
  执行层(Misc/MCU)委托既有 API。回写 MCU 严格门控在 isWifiConnected()+currentSSID()==target 双真之后。
  sim 验收口径=编译通过+grep 审计(禁 killall/kill wpa_supplicant/rmmod)+纯逻辑单测；真机留脚本。
deliverables:
  - artifacts/T6-planner-full.md
  - artifacts/T6-planner-report.md
verification:
  commands:
    - cmake -DBUILD_FOR_SIMULATION=ON -B build_sim -S . && cmake --build build_sim -j$(nproc) --target htc_wifi_app
    - cmake -DCMAKE_TOOLCHAIN_FILE=toolchain.cmake -B build -S . && cmake --build build -j$(nproc) --target htc_wifi_app
    - cmake --build build_sim -j$(nproc) --target test_wifi_app_logic && ./build_sim/bin/test_wifi_app_logic
    - ./build_sim/bin/htc_wifi_app --help
    - grep -rn "killall\|kill.*wpa_supplicant\|rmmod.*8189fs\|pkill" src/app/ src/common/misc/Misc.cpp
    - grep -n "currentSSID\|reconnectSSID\|wpa_cli.*-p /tmp/wpa_supplicant\|iwgetid" src/common/misc/Misc.h src/common/misc/Misc.cpp
    - grep -n "writeUPID\|writeUPWD\|connectWifi\|startDHCP" src/app/wifi_app.cpp
    - git diff --stat main -- src/hal src/app/main_app.cpp src/hardware/mcu/MCU.h src/hardware/mcu/MCU.cpp src/platform/tool/wpa_conn.cpp
  evidence_ref: artifacts/T6-planner-full.md
state_delta:
  set_task_status: {}
  add_decision:
    - key: T6-graceful-reconnect-not-respawn
      value: "重连难点定案：新增 Misc::currentSSID(if)/reconnectSSID(if,ssid,pwd)，经既有 wpa_cli ctrl_iface socket(/tmp/wpa_supplicant, 与 wpa_conn.cpp:172/201 同源) 做 reconfigure 兜底 add_network/set_network/select_network 优雅切网。全程不 kill/respawn wpa_supplicant、不清 /tmp/wpa_supplicant，故不进入 T5 修复的 DbusProcess-oops/ctrl_iface 冲突触发条件。否决给 connectWifi 加 force 参数(其 RTL 分支 Misc.cpp:459 经 wpa_conn 会 respawn wpa_supplicant, force=true 仍回归 T5)与 killall 方案。"
    - key: T6-app-is-foreground-tool
      value: "htc_wifi_app 是前台'调一次连一次'工具(CLI 解析→编排→退出码)，非常驻 daemon。呼应 dispatch 结论：htc_daemon_app 是进程守护/GPIO 下电示例(src/app/daemon_app.cpp)，零 WiFi 代码，无现有 WiFi daemon 可复用，单独建 app 合理无冲突。退出码契约: 0成功/2driver/3连接/4DHCP/5回写被门控/6参数。"
    - key: T6-logic-execution-layering
      value: "分层: wifi_app.cpp(CLI/退出码) → wifi_app_logic(纯函数 Decision decide(state,target), 回写门控, 退出码映射, 无 syscall) → Misc/MCU(既有)。wifi_app_logic 拆成独立 STATIC lib 供 app+test 都 link(同 T4 可测性理由)。Decision 枚举: REUSE(已连同 SSID)/RECONNECT(已连不同)/FRESH_CONNECT(未连走 connectWifi)/ABORT(无凭据)。"
    - key: T6-mcu-write-gate-double-check
      value: "回写 MCU 严格门控：--write-mcu 且 isWifiConnected()==true 且 currentSSID()==target 才 writeUPID/writeUPWD；防'连上但落到别的 SSID'误写，写前再读 currentSSID 与目标比对。任一不满足跳过写且退出码 5。MCU.cpp:554/570 空串 write 返 false 已是天然保护。本 app 不读 ini，天然规避 Common.h:45 INI_KEY_UPWD='PWD'(非'UPWD') 命名坑。"
    - key: T6-cmake-daemon-app-pattern
      value: "src/app/CMakeLists.txt 参照 htc_daemon_app 写法接入: add_executable(htc_wifi_app) + 双平台 target_link_libraries(sim: common_misc mcu logger sdk_stub ...; T32: + system_call) + set_target_properties(...bin)。wifi_app_logic 用 add_library STATIC。tests/CMakeLists.txt 仅 sim 加 test_wifi_app_logic(同 CMakeLists.txt:101-103 tests 仅 BUILD_FOR_SIMULATION 约定)。"
    - key: T6-sim-verification-scope
      value: "sim 侧验收=编译通过(build+build_sim 都生成 htc_wifi_app)+纯逻辑单测(Decision/门控/退出码)+grep 审计(禁 killall/kill wpa_supplicant/rmmod 8189fs，须有 wpa_cli -p /tmp/wpa_supplicant/iwgetid)+冒烟(--help 退 0, 无参模式不崩退 6)。真机 WiFi 回归(切网/wpa_supplicant PID 不变/MCU 回写)留 script/regress_wifi_real.sh 给用户人工跑，同 T5/T4 口径不卡 CI。"
  add_risk:
    - key: T5-oops-regression
      severity: high
      description: "若实现不慎 kill/respawn wpa_supplicant 或清 /tmp/wpa_supplicant，直接回归 T5 修复的 DbusProcess-oops/ctrl_iface 冲突(main_app.cpp:889-895,1841-1846 刻意保留)。缓解: grep 审计硬校验(killall/kill wpa_supplicant/rmmod 必须空集)；真机用例 B/F 断言 pgrep wpa_supplicant PID 切网前后不变；新增 reconnectSSID 设计上不碰进程生存期。"
    - key: T6-real-wifi-unreproducible
      severity: high
      description: "真机弱信号/路由器侧/wpa_cli reconfigure 时序问题在 PC 完全不可复现。缓解: sim 只验编译/逻辑/审计；真机留脚本由用户人工跑用例 A-F，不卡 CI。"
    - key: T6-wpa-cli-reconfigure-stale
      severity: medium
      description: "wpa_cli reconfigure 在某些 supplicant 版本不立刻生效，切网失败。缓解: 兜底链 reconfigure→add_network/set_network/enable/select_network 手动序列；最终以 isWifiConnected() 为判据而非 wpa_state。"
    - key: T6-mcu-write-gate
      severity: high
      description: "连接未成功却误写 MCU 会污染 UPID/UPWD 寄存器。缓解: 双门控(isWifiConnected+currentSSID==target)+退出码 5+单测覆盖；MCU.cpp 空串 write 返 false 兜底。"
    - key: T6-dual-platform-link
      severity: medium
      description: "link 漏 system_call(T32) 或 sdk_stub(sim) 导致一边编不过。缓解: 双平台分支显式列(参照 daemon_app)，§8 两边都跑编译校验。"
    - key: T6-sim-iic-bypass
      severity: medium
      description: "sim 下 readUPID/readUPWD 走 IIC bypass 返回空(T4 已知)，无参模式(需求④)sim 下不可真正测。缓解: sim 下无参模式预期退 6；真机用例 E 覆盖。"
artifact_path: artifacts/T6-planner-report.md
next: implementer
---

# T6 Planner Report (card)

本文件为 report card（只持指针）。完整实现方案见 `artifacts/T6-planner-full.md`。
