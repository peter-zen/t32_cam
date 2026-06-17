---
contract: report
contract_version: "1"
task_id: T6
node: implementer
flow: feature
status: success
summary: |
  实现 htc_wifi_app（双平台编译 exit 0）。分层：wifi_app.cpp(main 编排/CLI/退出码) →
  wifi_app_logic(纯逻辑 decide/mayWriteBack/decisionExitCode, 无 syscall, 可单测) →
  wifi_reconnect(优雅重连 currentSSID/reconnectSSID, 经既有 ctrl_iface socket
  -p /tmp/wpa_supplicant，**绝不 kill/respawn wpa_supplicant/不删 socket/不 rmmod**) →
  Misc(connectWifi/isWifi*/startDHCP)/MCU(readUPID/readUPWD/writeUPID/writeUPWD) 原样复用。
  退出码契约 0/2/3/4/5/6 严格遵守。回写 MCU 三重门控：isWifiConnected==true 且
  currentSSID==target 且写前再读一次 currentSSID 比对，任一不满足→跳过写、退出 5。
  凭据来源仅 CLI 参数 或 MCU 寄存器，不读 ini（天然规避 Common.h INI_KEY_UPWD="PWD" 坑）。
  核实关键 API 真实签名：Misc::popencall(char*,char*,int,int)=Misc.cpp:542 名字与 planner 一致、
  MCU::getInstance() 返 shared_ptr<MCU>、connectWifi RTL 分支 Misc.cpp:459-464 经 wpa_conn
  spawn wpa_supplicant(=不走它切网的理由)、ctrl_iface path 固定 /tmp/wpa_supplicant(wpa_conn.cpp:155/172/201)。
  无偏差。验证：build_sim x86-64 + build MIPS/uclibc 双平台 exit 0；
  test_wifi_app_logic ALL PASS(覆盖 ABORT/FRESH_CONNECT/RECONNECT/REUSE + 回写门控 5 反 1 正 +
  normalizeSsid 仅 trim 不改大小写 + 退出码映射 + e2e)；sim 冒烟 --help exit 0 / 无参 exit 6 不 segfault；
  grep 审计 killall/kill wpa_supplicant/rmmod/pkill 全空(T5 不回归)、writeUPID/writeUPWD 仅在
  mayWriteBack 门控之后、禁区 git diff 空(未碰 src/hal/main_app.cpp/MCU.{h,cpp}/wpa_conn.cpp)。
  真机回归 A-F 留 script/regress_wifi_real.sh 手动跑(关键断言 wpa_supplicant PID 恒定)。
  未 commit(无授权)。
deliverables:
  - src/app/wifi_app.cpp
  - src/app/wifi_app_logic.h
  - src/app/wifi_app_logic.cpp
  - src/app/wifi_reconnect.h
  - src/app/wifi_reconnect.cpp
  - tests/test_wifi_app_logic.cpp
  - script/regress_wifi_real.sh
  - reviews/2026-06-17-htc_wifi_app.md
  - src/app/CMakeLists.txt
  - tests/CMakeLists.txt
  - artifacts/T6-implementer-evidence.md
  - artifacts/T6-implementer-report.md
verification:
  commands:
    - cmake -DBUILD_FOR_SIMULATION=ON -B build_sim -S .
    - cmake --build build_sim -j$(nproc) --target htc_wifi_app
    - cmake -DCMAKE_TOOLCHAIN_FILE=toolchain.cmake -B build -S .
    - cmake --build build -j$(nproc) --target htc_wifi_app
    - cmake --build build_sim -j$(nproc) --target test_wifi_app_logic
    - cd build_sim && LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH ./bin/test_wifi_app_logic
    - cd build_sim && LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH ./bin/htc_wifi_app --help
    - cd build_sim && LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH ./bin/htc_wifi_app
    - grep -rn "killall\|kill.*wpa_supplicant\|rmmod.*8189fs\|pkill" src/app/wifi_app.cpp src/app/wifi_app_logic.h src/app/wifi_app_logic.cpp src/app/wifi_reconnect.h src/app/wifi_reconnect.cpp
    - grep -n "currentSSID\|reconnectSSID\|wpa_cli.*-p /tmp/wpa_supplicant\|iwgetid" src/app/wifi_reconnect.cpp
    - grep -n "currentSSID\|reconnectSSID\|connectWifi\|startDHCP" src/app/wifi_app.cpp
    - grep -n "writeUPID\|writeUPWD\|mayWriteBack" src/app/wifi_app.cpp
    - git status --short src/hal/ src/app/main_app.cpp src/hardware/mcu/MCU.h src/hardware/mcu/MCU.cpp src/platform/tool/wpa_conn.cpp
    - file build/bin/htc_wifi_app build_sim/bin/htc_wifi_app
  evidence_ref: artifacts/T6-implementer-evidence.md
state_delta:
  set_task_status: {}
  add_decision:
    - key: T6-htc-wifi-app
      value: "新增独立可执行 htc_wifi_app(双平台编译 exit 0)。分层 main→纯逻辑 wifi_app_logic(无 syscall 可单测)→wifi_reconnect(优雅重连)→Misc/MCU 原样复用。退出码契约 0/2/3/4/5/6。优雅重连经既有 ctrl_iface socket(wpa_cli -i <if> -p /tmp/wpa_supplicant reconfigure + add_network/select_network 兜底 + 轮询 wpa_state=COMPLETED)，绝不 kill/respawn wpa_supplicant/不删 /tmp/wpa_supplicant/不 rmmod(保 T5 不回归)。回写 MCU 三重门控(isWifiConnected && currentSSID==target && 写前再读 currentSSID)，不满足退出 5。凭据仅来自 CLI 或 MCU 寄存器(不读 ini，规避 Common.h INI_KEY_UPWD=PWD 坑)。"
    - key: T6-graceful-reconnect-placement
      value: "优雅重连原语 currentSSID/reconnectSSID 放独立新模块 src/app/wifi_reconnect.{h,cpp}(非共享 Misc)，理由：保持 Misc.{h,cpp} 不动、git diff 最小、决策/执行分层可单测。故 planner-report verification 里指向 Misc.cpp 的 grep 同步改为 wifi_reconnect.cpp。"
    - key: T6-mcu-write-triple-gate
      value: "回写 MCU 严格三重门控：connect 成功后 → mayWriteBack(connected, freshSsid, targetSsid) 要求 isWifiConnected==true 且 currentSSID==target → 写前再 read 一次 currentSSID(mcu 写入循环里 freshSsid) 比对 → 通过才 writeUPID/writeUPWD。任一不满足 return 5。writeUPID/writeUPWD 在 wifi_app.cpp 仅出现于 mayWriteBack 之后(L248 门控→L255/256 写)。"
  add_risk:
    - key: T6-real-wifi-unverified
      severity: high
      description: "双平台编译 + sim 单测(ALL PASS) + 代码审计(kill/rmmod 空, write 仅在门控后) + sim 冒烟(--help exit 0, 无参 exit 6 不 segfault) 全过；但真机 WiFi 实连(用例 A 首连/B 优雅切 SSID/C 回写/D 错密码/E 无参读 MCU/F 多轮 A<->B PID 恒定 + dmesg 无 oops) PC 不能跑 MIPS、无真实链路，留 script/regress_wifi_real.sh 由用户在 T32 手动执行。关键断言：wpa_supplicant PID 跨 SSID 切换恒定(证明 T5 不回归)。"
    - key: T6-wpa-cli-reconfigure-portability
      severity: medium
      description: "优雅重连靠 wpa_cli reconfigure + add_network/select_network 兜底链；不同 wpa_supplicant 版本 reconfigure 见效速度不一。脚本用例 B 前建议先 ls /system/bin/wifi/ 确认 wpa_cli/wpa_passphrase 存在(既有 wpa_conn.cpp 已用 wpa_cli，证明镜像里有)。"
artifact_path: artifacts/T6-implementer-report.md
next: tester
---

# T6 Implementer Report — htc_wifi_app

## 一句话
实现 `htc_wifi_app`（双平台编译 exit 0），分层清晰、优雅重连绝不重启 wpa_supplicant（保 T5）、回写 MCU 三重门控、退出码契约稳定、sim 单测全绿、grep 审计全过、禁区零改动。

## 实现要点

### 分层（planner §3）
- `src/app/wifi_app.cpp` — `main()`：CLI 解析 + 编排 + 退出码 + 日志，全部委托底层。
- `src/app/wifi_app_logic.{h,cpp}` — 纯逻辑（`decide`/`mayWriteBack`/`decisionExitCode`/`normalizeSsid`），无 syscall，可单测。
- `src/app/wifi_reconnect.{h,cpp}` — `currentSSID`/`reconnectSSID`：经既有 ctrl_iface socket 优雅切，**绝不 kill/respawn/删 socket/rmmod**。
- `Misc`/`MCU` — 原样复用（`connectWifi`/`isWifi*`/`startDHCP`/`readUPID`/`readUPWD`/`writeUPID`/`writeUPWD`）。

### 优雅重连（planner §6，核心）
- `currentSSID`：首选 `iwgetid -r`，兜底 `wpa_cli -i <if> -p /tmp/wpa_supplicant status` 解析 `^ssid=`。
- `reconnectSSID`：`wpa_passphrase` 生成临时 conf → `wpa_cli ... reconfigure`（常驻 supplicant 重读，进程不动）→ 兜底 `add_network/set_network/enable_network/select_network` → 轮询 `wpa_state=COMPLETED` → 最终以 `isWifiConnected()` 判定。
- **取舍**：放独立新模块（非共享 `Misc`），保持 `Misc.{h,cpp}` 不动、diff 最小；故 verification grep 指向 `wifi_reconnect.cpp`。

### 回写 MCU 三重门控（planner §5 需求③）
`isWifiConnected()==true && currentSSID()==target` 之后，**写前再读一次 `currentSSID()`** 比对（`mayWriteBack(stillConnected, freshSsid, target.ssid)`），通过才 `writeUPID`/`writeUPWD`；任一不满足→退出 5。

### 退出码契约（planner §4）
0/2/3/4/5/6 严格遵守，在 `wifi_app_logic.h` 枚举固化；`--help` exit 0，无参（MCU 空）exit 6。

### 凭据来源
仅 CLI `--ssid/--pwd` 或 MCU `readUPID/readUPWD`；**不读 ini**（天然规避 `Common.h INI_KEY_UPWD="PWD"` 坑）。

## 验证结果（详见 evidence）

| 项 | 结果 |
|----|------|
| build_sim 编译 (x86-64) | exit 0，`build_sim/bin/htc_wifi_app` 246KB |
| build 编译 (MIPS/uclibc) | exit 0，`build/bin/htc_wifi_app` 22KB ELF MIPS |
| test_wifi_app_logic (sim) | ALL PASS，exit 0 |
| `--help` (sim) | exit 0 |
| 无参 (sim) | exit 6，不 segfault |
| grep kill/rmmod/pkill | **空**（T5 不回归）|
| writeUPID/writeUPWD 仅在门控后 | 是（L248 门控→L255/256 写）|
| 禁区 git diff | **空**（未碰 hal/main_app/MCU/wpa_conn）|
| 真机 A-F | 留 `script/regress_wifi_real.sh` 手动 |

## 偏差说明
无。关键 API 真实签名（`Misc::popencall`、`MCU::getInstance` 返 shared_ptr、connectWifi RTL 分支 spawn supplicant、ctrl_iface path）均与 planner 一致。

## 遗留
- 真机 WiFi 实连回归（A-F，含 wpa_supplicant PID 恒定断言）由用户在 T32 手动跑，PC 不能跑 MIPS（同 T5/T4 口径）。
- 未 git commit（无授权）。
