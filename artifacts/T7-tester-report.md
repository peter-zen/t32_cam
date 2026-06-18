---
contract: report
contract_version: "1"
task_id: T7
node: tester
flow: feature
status: success
summary: |
  T7 tester 独立复跑全部通过。把 htc_wifi_app 扩展为统一 htc_net_app(WiFi/Eth/USB 三上行)，
  WiFi 行为等价审计通过(runWifi 与原 wifi_app main 逐段一致；纯逻辑层
  decide/mayWriteBack/normalizeSsid/decisionExitCode/ExitCode 逐字一致)，Eth/USB/CLI
  分支严格符合用户 3 决策，双平台编译过，16 case 单测全绿，sim 冒烟 10 路径无 segfault，
  受保护路径零改动，改名干净，无 uclibc 陷阱。status=success。

  独立复跑(不信 implementer 自报，全部亲自重跑):
  1. sim 构建: cmake --build build_sim -j --target htc_net_app --target test_net_app_logic -> rc=0
  2. T32 交叉构建: cmake --build build -j --target htc_net_app -> rc=0 (MIPS uclibc ELF)
  3. 单测: ./build_sim/bin/test_net_app_logic -> ALL PASS (16 case, rc=0)
  4. sim 冒烟 10 路径: --help(0) / 无参(6) / wifi --no-dhcp(6) / wifi --ssid X --pwd Y --no-dhcp(2，
     WiFi 等价: sim 无 8189fs 驱动 -> connectWifi 失败 -> exit 2，与原 wifi_app 一致) /
     eth --no-dhcp(3) / usb --no-dhcp(2) / bogus(6) / usb --usb-bringup --usb-model EC20(2) /
     usb --usb-bringup --usb-model BOGUS(6) / wifi --bogus-opt(6)。全部正常退出，无 segfault。
  5. 零改动: git diff HEAD --stat 对 src/app/main_app.cpp / src/hal / src/hardware/mcu/MCU.cpp /
     src/common/misc / src/network -> 空。
  6. 残留: grep htc_wifi_app|wifi_app_logic in src tests -> 仅注释历史指代，无 target/source/include 残留；
     CMake 零残留。grep std::to_string|stoi -> clean。grep DeviceConfig|Settings|PType INI 读取 -> 仅注释。

  WiFi 等价审计(读代码逐段对比 git show HEAD:src/app/wifi_app.cpp vs net_app.cpp::runWifi):
  - 纯逻辑层 decide/mayWriteBack/normalizeSsid/decisionExitCode/ExitCode/Decision/LinkState/Target
    全部逐字一致。
  - runWifi 执行序列 = 原 main WiFi 部分: 凭据解析(CLI 优先 else MCU readUPID/readUPWD retry 3x) ->
    decide switch(ABORT->6/FRESH->connectWifi/RECONNECT->reconnectSSID/REUSE) -> FRESH 失败
    isWifiDriverLoaded? false->2 else->3 -> post-probe L2 校验 -> DHCP(->4) -> MCU write-back
    gate(->5) -> write+verify(->5) -> 成功(0)。exit code 0/2/3/4/5/6 全保留，连接序列/凭据来源/
    reconnect/DHCP/writeback 无偷改。唯一差异: 日志 app 名 htc_wifi_app->htc_net_app(预期)。

  分支审计:
  - Eth(runEth) = setNetworkInterfaceName("eth0") + startDHCP("eth0")，无 connect，符合 planner 决策。
  - USB(runUsb) 默认 = loadDriver->open->preconfig->DHCP(无 start())；--usb-bringup 才插
    setModel(--usb-model)+start()。符合用户决策 #1。--usb-model BOGUS -> exit 6。
  - CLI(main) 不读 INI(--type 必填，缺失/无法解析 -> exit 6)，无 DeviceConfig/Settings/PType 读取路径。
    符合用户决策 #2。

  脚本审计: script/regress_net_real.sh bash -n 语法过；覆盖 Eth(--type eth，断言 IP+gw 非空) +
  USB 默认(exit {4,3,2}，标注 T7-usb-no-start 为 KNOWN 非 FAIL) + USB --usb-bringup(断言 IP+gw 非空) +
  bad args(->6)。regress_wifi_real.sh 保留零改动。符合用户决策 #3。

  残余风险(不卡验收): 真机 Eth/USB 回归(Eth/USB 真正连上网)PC 跑不了，留用户在 T32 执行
  regress_net_real.sh；T7-usb-no-start(USB 默认无 start，真机无 IP)是 main_app 既有行为，
  --usb-bringup opt-in，本任务按用户决策正确处理。wifi_reconnect.{h,cpp} 保留零改动(WiFi 专属)。
deliverables:
  - artifacts/T7-tester-evidence.md
  - artifacts/T7-tester-report.md
verification:
  commands:
    - cmake --build build_sim -j$(nproc) --target htc_net_app --target test_net_app_logic
    - cmake --build build -j$(nproc) --target htc_net_app
    - ./build_sim/bin/test_net_app_logic
    - ./build_sim/bin/htc_net_app --help ; ./build_sim/bin/htc_net_app ; ./build_sim/bin/htc_net_app --type wifi --no-dhcp ; ./build_sim/bin/htc_net_app --type eth --no-dhcp ; ./build_sim/bin/htc_net_app --type usb --no-dhcp ; ./build_sim/bin/htc_net_app --type bogus ; ./build_sim/bin/htc_net_app --type usb --usb-bringup --usb-model EC20 ; ./build_sim/bin/htc_net_app --type usb --usb-bringup --usb-model BOGUS
    - git diff HEAD --stat -- src/app/main_app.cpp src/hal src/hardware/mcu/MCU.cpp src/common/misc src/network
    - grep -rn "htc_wifi_app\|wifi_app_logic" src tests
    - grep -nE "std::to_string|std::stoi|[^_]stoi\(" src/app/net_app.cpp src/app/net_app_logic.cpp src/app/net_app_logic.h tests/test_net_app_logic.cpp
    - bash -n script/regress_net_real.sh
  evidence_ref: artifacts/T7-tester-evidence.md
state_delta:
  set_task_status: {}
  add_decision: []
  add_risk: []
artifact_path: artifacts/T7-tester-report.md
next: reviewer
---

# T7 Tester Report (card)

本文件为 report card（只持指针）。完整复跑证据(命令真实输出、git diff、grep、WiFi 等价逐段对比、
脚本审计)见 `artifacts/T7-tester-evidence.md`。

## 验证基准

用户拍板 3 决策(验证基准):
1. USB 默认忠实搬运(loadDriver→open→preconfig，不调 start)，`--usb-bringup`(默认关)才补 start()。
2. CLI `--type wifi|eth|usb` 必填，本阶段不读 INI。
3. 保留 regress_wifi_real.sh + 新增 regress_net_real.sh。

## 独立复跑结果

| # | 复跑项 | 结果 |
|---|---|---|
| 1 | sim 构建 htc_net_app + test_net_app_logic | rc=0 ✓ |
| 2 | T32 交叉构建 htc_net_app | rc=0 (MIPS uclibc) ✓ |
| 3 | test_net_app_logic 单测 | ALL PASS (16 case) ✓ |
| 4 | sim 冒烟 10 路径 exit code | 全正常退出，无 segfault ✓ |
| 5 | 受保护路径零改动(main_app/hal/MCU/misc/network) | git diff 空 ✓ |
| 6 | 改名残留 + uclibc 陷阱 + INI 读取 | 仅注释残留 / clean / 仅注释 ✓ |

## WiFi 行为等价审计

读 `git show HEAD:src/app/wifi_app.cpp` 与新 `src/app/net_app.cpp::runWifi` 逐段对比:
- 纯逻辑层(decide/mayWriteBack/normalizeSsid/decisionExitCode/ExitCode/Decision/LinkState/Target)
  逐字一致。
- runWifi 执行序列 = 原 main WiFi 部分，exit code 0/2/3/4/5/6 全保留，连接序列/凭据来源/
  reconnect/DHCP/writeback 无偷改。唯一差异是日志 app 名(预期)。
- wifi_reconnect.{h,cpp} 零改动保留(WiFi 专属)。

## 分支审计

- **Eth**: runEth = setNetworkInterfaceName("eth0")+startDHCP("eth0")，无 connect。✓
- **USB**: runUsb 默认 = loadDriver→open→preconfig→DHCP(无 start)；--usb-bringup 才插
  setModel+start()。--usb-model BOGUS→exit 6。✓
- **CLI**: main 不读 INI，--type 必填(缺失/无法解析→exit 6)，无 DeviceConfig/Settings/PType 路径。✓

## 脚本审计

- `script/regress_net_real.sh`: bash -n 语法过；覆盖 Eth + USB(默认 KNOWN + --usb-bringup) + bad args，
  断言 IP/gateway 非空(isWifiConnected 同款判定)，exit code 断言合理。
- `script/regress_wifi_real.sh`: 保留零改动。

## 约束核查

双平台编译 ✓ / sim 不 segfault ✓ / 无 std::to_string/stoi ✓ / 不动 hal/MCU/misc/network/main_app ✓ /
WiFi 行为等价 ✓ / CLI 不读 INI ✓ / 改名干净 ✓。

## 残余风险(不卡验收)

- 真机 Eth/USB 回归(Eth/USB 真正连上网)PC 跑不了，留用户在 T32 执行 regress_net_real.sh。
- T7-usb-no-start(USB 默认无 start，真机无 IP)是 main_app 既有行为，--usb-bringup opt-in，
  本任务按用户决策 #1 正确处理。

## 结论

所有独立复跑通过 + WiFi 等价审计通过 + 分支审计通过 + 约束全守。**status = success**，流转 reviewer。
