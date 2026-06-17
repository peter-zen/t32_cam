---
contract: report
contract_version: "1"
task_id: T6
node: tester
flow: feature
status: success
summary: |
  T6 tester 独立复测（对 implementer 全部产物的独立重跑 + grep 审计 + 单测有效性审查 + sim 冒烟），
  全部硬要求通过，判 success，移交 reviewer。
  独立验证结果（未采信 implementer 自述）：
  (1) 双平台编译 exit 0——sim build_sim/bin/htc_wifi_app(246KB x86-64) exit 0、
      T32 build/bin/htc_wifi_app(22KB ELF 32-bit MIPS32 uClibc 交叉编译产物, PC 不可执行只验生成+file) exit 0；
      T32 error/to_string/stoi/undefined reference 扫描全空（uclibc 可移植性无回归）。
  (2) test_wifi_app_logic ALL PASS exit 0。单测有效性逐用例审读：非空壳（真实断言宏 EXPECT_EQ/TRUE/FALSE，
      失败 ++g_failures 打 FAIL）、真实调被测逻辑（decide/decisionExitCode/mayWriteBack/normalizeSsid）；
      决策分支全覆盖 REUSE/RECONNECT/RECONNECT-ssid未知/FRESH_CONNECT/ABORT(无凭据)/case-sensitive；
      回写门控 5 反 1 正（not-connected/不同 SSID/空 live/空 target 全 block + 匹配 allow）；
      退出码映射 ABORT→6/FRESH→3/RECONNECT→3/REUSE→0；normalizeSsid 仅 trim 不改大小写；e2e
      decision→gate 含"reconnect 落到错 SSID→gate 阻断写(exit 5)"。覆盖 dispatch §2 全部要求。
  (3) grep 审计全过：A. T6 改动文件 kill/rmmod/pkill 严格空(RC=1)，dispatch 宽范围 grep 命中的两行是
      main_app.cpp T5 既有注释(L890/L1842 "does not kill wpa_supplicant")非可执行代码、本任务未碰 main_app；
      B. 优雅重连原语 currentSSID/reconnectSSID/iwgetid 命中(wifi_reconnect.cpp)，ctrl_iface path
      /tmp/wpa_supplicant 与 wpa_conn.cpp 既有 socket 一致；C. main 全委托底层(connectWifi/reconnectSSID/
      startDHCP)无自造 shell；D. 读 wifi_app.cpp L245-266 确认 writeUPID/writeUPWD(L255/256)严格在
      mayWriteBack(isWifiConnected && currentSSID==target) 门控(L248)之后，任一不满足 return 5 才到不了写。
  (4) 禁区工作树零改动：git status --short src/hal / main_app.cpp / MCU.{h,cpp} / wpa_conn.cpp / Misc.{h,cpp}
      全空。（dispatch 原文 git diff --stat main 会把整条分支历史算进来，非本任务改动；以工作树口径为准，
      implementer 同口径。）Misc 未被本任务触碰（implementer 把优雅重连放独立 wifi_reconnect 模块而非
      planner §2 原计划的 Misc 新增方法，report decision T6-graceful-reconnect-placement 已记录，符合
      "不破坏 Misc"）。
  (5) sim 冒烟：--help exit 0 ✓；无参 exit 6（MCU readUPID sim 空凭据→ABORT）不 segfault ✓；
      --ssid foo --pwd bar exit 2（dispatch §5 预期 3）不 segfault。
  唯一偏差：sim --ssid foo --pwd bar 实际 exit 2 而非 dispatch 预期 3。tester 独立代码追踪定性为
  sim 物理环境限制（PC 无 8189fs.ko，isWifiDriverLoaded/isWifiConnected 均 false）→ decide 走
  FRESH_CONNECT（非 RECONNECT）→ connectWifi insmod 失败 → exit 2(EXIT_DRIVER_FAIL)。退出码在
  planner §4 契约集合 {0,2,3,4,5,6} 内、语义正确、不 segfault/不崩溃。dispatch 预期 3 的前提（走
  RECONNECT sim 桩）在 sim 下因 isWifiConnected()=false 物理上不可达。属 dispatch 预期与 sim 物理路径
  的合理分歧，非被测代码缺陷。真机错密码才 exit 3，留 regress_wifi_real.sh 用例 D。
deliverables:
  - artifacts/T6-tester-evidence.md
  - artifacts/T6-tester-report.md
verification:
  commands:
    - cmake --build build_sim -j$(nproc) --target htc_wifi_app
    - cmake --build build -j$(nproc) --target htc_wifi_app
    - cmake --build build_sim -j$(nproc) --target test_wifi_app_logic
    - file build/bin/htc_wifi_app build_sim/bin/htc_wifi_app build_sim/bin/test_wifi_app_logic
    - cd build_sim && LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH ./bin/test_wifi_app_logic
    - cd build_sim && LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH ./bin/htc_wifi_app --help
    - cd build_sim && LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH ./bin/htc_wifi_app
    - cd build_sim && LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH ./bin/htc_wifi_app --ssid foo --pwd bar
    - cd build_sim && LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH ./bin/htc_wifi_app --bogus-flag
    - grep -rn "killall\|kill.*wpa_supplicant\|rmmod.*8189fs\|pkill" src/app/wifi_app.cpp src/app/wifi_app_logic.h src/app/wifi_app_logic.cpp src/app/wifi_reconnect.h src/app/wifi_reconnect.cpp src/common/misc/Misc.cpp
    - grep -n "kill.*wpa_supplicant" src/app/main_app.cpp
    - grep -n "currentSSID\|reconnectSSID\|wpa_cli.*-p /tmp/wpa_supplicant\|iwgetid" src/app/wifi_reconnect.cpp
    - grep -n "currentSSID\|reconnectSSID\|connectWifi\|startDHCP" src/app/wifi_app.cpp
    - grep -n "writeUPID\|writeUPWD\|mayWriteBack" src/app/wifi_app.cpp
    - git status --short src/hal src/app/main_app.cpp src/hardware/mcu/MCU.h src/hardware/mcu/MCU.cpp src/platform/tool/wpa_conn.cpp src/common/misc/Misc.h src/common/misc/Misc.cpp
    - cmake --build build -j$(nproc) --target htc_wifi_app 2>&1 | grep -iE "error|to_string|stoi|undefined reference"
  evidence_ref: artifacts/T6-tester-evidence.md
state_delta:
  set_task_status: {}
  add_decision:
    - key: T6-tester-passed
      value: "tester 独立复测全部硬要求通过：双平台编译 exit 0（sim x86-64 + T32 MIPS32/uClibc 交叉编译产物，T32 error/to_string/stoi 扫描空）；test_wifi_app_logic ALL PASS exit 0 且单测经有效性审查非空壳（真实断言+真实调被测逻辑，覆盖全决策分支 REUSE/RECONNECT/FRESH_CONNECT/ABORT + 回写门控 5 反 1 正 + 退出码映射 + e2e）；grep 审计 T6 文件 kill/rmmod 严格空(T5 不回归)、优雅重连原语命中、main 全委托底层、writeUPID/writeUPWD 读码确认在 mayWriteBack(isWifiConnected && currentSSID==target) 双校验门控之后；禁区工作树零改动(hal/main_app/MCU/wpa_conn/Misc 全空)；sim 冒烟 --help exit 0 / 无参 exit 6 / --ssid foo --pwd bar exit 2 均不 segfault。判 success 移交 reviewer。"
    - key: T6-sim-exit2-vs-dispatch-expect3
      value: "sim 冒烟 --ssid foo --pwd bar 实际 exit 2(EXIT_DRIVER_FAIL) 而非 dispatch §5 预期的 3。tester 独立代码追踪：sim 下 isWifiConnected(wlan0)=false(PC 无 wlan0) + isWifiDriverLoaded()=false(PC /proc/modules 无 8189fs) → decide 走 FRESH_CONNECT(非 RECONNECT) → connectWifi insmod /system/bin/wifi/8189fs.ko 失败 → exit 2。退出码在 planner §4 契约集合内、语义正确、不 segfault。dispatch 预期 3 的前提(走 RECONNECT sim 桩 false)在 sim 下因 isWifiConnected=false 物理不可达。定性为 dispatch 预期与 sim 物理路径的合理分歧，非被测代码缺陷；真机错密码 insmod 成功但 wpa_conn 失败才 exit 3，留 regress_wifi_real.sh 用例 D。"
    - key: T6-forbidden-zone-workingtree-scope
      value: "禁区 diff 判据以工作树状态为准(git status --short src/hal main_app.cpp MCU.{h,cpp} wpa_conn.cpp Misc.{h,cpp} 全空=PASS)，非 git diff --stat main。后者会把整条 merge_develop_simu 分支历史差异(含 T5/T4/其它 task 的 hal/main_app/MCU/wpa_conn 改动)算进来，不是本任务 T6 的真实改动。implementer 同口径。本任务真实改动：已跟踪改 src/app/CMakeLists.txt + tests/CMakeLists.txt + orchestration-state.yaml；新增 src/app/wifi_app.cpp + wifi_app_logic.{h,cpp} + wifi_reconnect.{h,cpp} + tests/test_wifi_app_logic.cpp + script/regress_wifi_real.sh。Misc 未被本任务触碰(implementer 把优雅重连放独立 wifi_reconnect 模块而非 planner §2 原计划的 Misc 新增方法)。"
  add_risk:
    - key: T6-real-wifi-unverified
      severity: high
      description: "双平台编译 + sim 单测 ALL PASS + 代码审计(kill/rmmod 空, write 仅在门控后, 委托底层) + sim 冒烟(--help exit 0, 无参 exit 6, --ssid foo --pwd bar exit 2 均不 segfault) 全过；但真机 WiFi 实连(用例 A 首连/B 优雅切 SSID + wpa_supplicant PID 恒定/C 回写 MCU/D 错密码 exit 3/E 无参读 MCU/F 多轮 A↔B PID 恒定 + dmesg 无 DbusProcess/ctrl_iface oops) PC 不能跑 MIPS、无真实链路，留 script/regress_wifi_real.sh 由用户在 T32 手动执行。关键回归断言：wpa_supplicant PID 跨 SSID 切换恒定(证明 T5 不回归) + exit 3(连接失败)在真机错密码场景验证(sim 下因 driver 不可加载提前 exit 2 不可达)。"
artifact_path: artifacts/T6-tester-report.md
next: reviewer
---

# T6 Tester Report — htc_wifi_app 独立复测

## 结论一句话

dispatch 全部硬要求通过：双平台编译 exit 0、单测 ALL PASS（且经有效性审查非空壳）、grep 审计 T5 不回归、禁区工作树零改动、sim 冒烟不 segfault；唯一 sim exit 2 vs dispatch 预期 3 的偏差已定性为 sim 物理环境限制非代码缺陷。判 success，移交 reviewer。

## 必跑项结果

| 项 | 结果 | 证据 |
|----|------|------|
| sim build htc_wifi_app | exit 0 | build_sim/bin/htc_wifi_app 246KB x86-64 |
| T32 build htc_wifi_app | exit 0 | build/bin/htc_wifi_app 22KB MIPS32 uClibc |
| T32 error/portability 扫描 | 空 | 无 error/to_string/stoi/undefined ref |
| test_wifi_app_logic | ALL PASS, exit 0 | 单测有效非空壳，覆盖全分支+门控+退出码 |
| grep A kill/rmmod（T6 文件） | 严格空(RC=1) | main_app 命中仅 T5 注释 L890/L1842 |
| grep B 优雅重连原语 | 命中 | currentSSID/reconnectSSID/iwgetid + ctrl_iface path |
| grep C 委托底层 | 命中 | connectWifi/reconnectSSID/startDHCP 全委托 |
| grep D + 读码 write 顺序 | 正确 | mayWriteBack 门控(L248) → writeUPID/UPWD(L255/256) |
| 禁区工作树 diff | 空 | hal/main_app/MCU/wpa_conn/Misc 零改动 |
| sim --help | exit 0 ✓ | |
| sim 无参 | exit 6 ✓ 不 segfault | MCU 空凭据→ABORT |
| sim --ssid foo --pwd bar | exit 2（预期 3）不 segfault | sim driver 不可加载→FRESH_CONNECT→exit 2 |

## 单测有效性审查（dispatch §2 硬要求）

tests/test_wifi_app_logic.cpp 逐用例审读结论：**有效、非空壳、覆盖全要求**。
- 断言机制真实：`EXPECT_EQ/EXPECT_TRUE/EXPECT_FALSE` 失败 `++g_failures` 打 FAIL，`g_failures==0` 才 ALL PASS。
- 真实调被测逻辑：每用例构造 LinkState/Target 调 decide()/decisionExitCode()/mayWriteBack()/normalizeSsid() 并断言返回。
- 决策分支：REUSE / RECONNECT / RECONNECT-ssid未知 / FRESH_CONNECT / ABORT(无凭据+hasCredentials=false) / case-sensitive 全覆盖。
- 回写门控：5 反(not-connected/不同 SSID/空 live/空 target) + 1 正(匹配)。
- 退出码映射：ABORT→6 / FRESH→3 / RECONNECT→3 / REUSE→0。
- normalizeSsid：trim 尾部 CR/LF/space + 首部 space/tab，不改大小写、保内部空格、全空格→空。
- e2e：decision→gate 串联，含"reconnect 落到错 SSID→gate 阻断写(exit 5)"。

## 偏差分析（sim exit 2 vs dispatch 预期 3）

tester 独立代码追踪（非采信 implementer）：
- sim 下 `Misc::isWifiConnected("wlan0")`（Misc.cpp:300-307）查 getIPAddress——PC 无 wlan0 → 空 → false。
- → `decide()`（wifi_app_logic.cpp:8-26）走 FRESH_CONNECT（target 非空、未连）。
- → `Misc::connectWifi()`（Misc.cpp:425-471）：L434 `isWifiDriverLoaded()`（Misc.cpp:284-298，grep /proc/modules 的 8189fs）→ PC 无 → false → L442 insmod /system/bin/wifi/8189fs.ko → PC 失败 → L450 re-check false → return false。
- → main（wifi_app.cpp:192-202）：connectWifi false → L196 isWifiDriverLoaded() false → **exit 2 (EXIT_DRIVER_FAIL)**。

判定：exit 2 在 planner §4 契约集合 {0,2,3,4,5,6} 内、语义正确（driver 加载失败）、不 segfault。dispatch §5 预期 3 的前提（走 RECONNECT sim 桩）在 sim 下因 isWifiConnected=false 物理不可达。**dispatch 预期与 sim 物理路径的合理分歧，非被测代码缺陷**。真机错密码 insmod 成功但 wpa_conn 失败才 exit 3，留 regress_wifi_real.sh 用例 D。

## 未误伤确认

- main_app.cpp 的两行 kill grep 命中是 T5 既有注释（"does not kill wpa_supplicant"），本任务未碰 main_app（工作树空），非本任务引入、非可执行 kill。
- Misc.{h,cpp} 未被本任务触碰（工作树空）；优雅重连在独立 wifi_reconnect 模块，符合"不破坏 Misc"。
- case-sensitive SSID 比较（"Home"≠"home"）单测验证未误伤；normalizeSsid 不改大小写。
- --bogus-flag 走参数错误路径 exit 6，未误伤正常退出码。

## 遗留（不变，留真机）

T32 真机 WiFi 实连回归（A-F，含 wpa_supplicant PID 恒定断言 + dmesg 无 oops + exit 3 错密码场景）由用户手动跑 script/regress_wifi_real.sh。PC 不能跑 MIPS（同 T5/T4 口径）。

详见 `artifacts/T6-tester-evidence.md`。
