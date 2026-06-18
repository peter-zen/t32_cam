---
contract: report
contract_version: "1"
task_id: T7
node: reviewer
flow: feature
status: success
summary: |
  T7 reviewer 末端审查通过。把 htc_wifi_app 扩展为统一 htc_net_app(WiFi/Eth/USB 三上行)，
  WiFi 行为等价独立再核通过(runWifi 与原 wifi_app main 逐段对比，纯逻辑层
  decide/mayWriteBack/normalizeSsid/decisionExitCode/ExitCode 逐字一致，无行为漂移)，
  Eth/USB/CLI 分支严格符合用户 3 决策，CMake 只动 htc_net_app target(不误伤
  main_app/media_app/daemon_app link)，双平台编译通过(sim rc=0 / T32 MIPS uclibc ELF rc=0)，
  16 case 单测全绿，sim 冒烟 11 路径无 segfault，受保护路径(main_app/hal/MCU/misc/network)
  与 wifi_reconnect 零改动，改名干净，无 uclibc 陷阱，不读 INI。已知风险(T7-usb-no-start 等)
  缓解均到位。status=success，建议闭环。

  7 项审查重点逐项结论:
  1. WiFi 行为等价 — PASS(逐段读码对比，sim --type wifi --ssid X --pwd Y --no-dhcp→exit 2 与原
     FRESH_CONNECT 失败路径一致，交叉佐证)。
  2. correctness/边界 — PASS(exit code 2/3/4/6 映射正确，ifname 与 app.h:31-33 一致，边界
     空 ssid/缺 type/非法 type/非法 model/bringup 无 model 默认 EC20 均正确)。
  3. 回归风险/CMake — PASS(diff 仅触及 htc_wifi_app→htc_net_app 的 target 定义+两 link 块+
     set_target_properties；新增 network 传递依赖只在 net_app PRIVATE 链；main_app/media_app
     块未出现在 diff)。
  4. 可维护性 — PASS(6 纯函数设计合理命名清晰，CLI 解析健壮)。
  5. 安全 — PASS(--usb-model 仅 4 字面量比较不拼 shell/AT；不读 INI 无新凭据源注入)。
  6. 缺失测试 — PASS(6 新函数正常+边界全覆盖；parseUsbModel 靠 sim 冒烟间接验证)。
  7. 约束遵守 — PASS(受保护路径零改动/双平台编译/不读 INI/USB 默认不 start/无 to_string-stoi)。

  3 处 CONCORN 均 nice-to-have 不卡验收: (a) --usb-model 校验仅 bringup 时触发(无 bringup 静默忽略，
  harmless，help 已写明); (b) runEth/runUsb 可抽 bringUpGenericIf(收益低); (c) parseUsbModel 可
  下沉 net_app_logic 加单测。

  独立复跑(不信 implementer/tester 自报，全部亲自重跑): sim 构建 rc=0 / T32 构建 rc=0 / 单测
  16 case ALL PASS rc=0 / sim 冒烟 11 路径 exit code 全符合预期无 segfault / git diff 受保护
  路径空 / grep wifi_app 残留(clean)/grep uclibc 陷阱(clean)/grep INI 读取(clean)/bash -n
  regress_net_real.sh OK。
deliverables:
  - artifacts/T7-reviewer-report.md
  - artifacts/T7-reviewer-evidence.md
verification:
  commands:
    - cmake --build build_sim -j$(nproc) --target htc_net_app --target test_net_app_logic
    - cmake --build build -j$(nproc) --target htc_net_app
    - ./build_sim/bin/test_net_app_logic
    - ./build_sim/bin/htc_net_app --help ; ./build_sim/bin/htc_net_app ; ./build_sim/bin/htc_net_app --type wifi --no-dhcp ; ./build_sim/bin/htc_net_app --type wifi --ssid X --pwd Y --no-dhcp ; ./build_sim/bin/htc_net_app --type eth --no-dhcp ; ./build_sim/bin/htc_net_app --type usb --no-dhcp ; ./build_sim/bin/htc_net_app --type usb --usb-bringup --usb-model EC20 ; ./build_sim/bin/htc_net_app --type usb --usb-bringup --usb-model BOGUS ; ./build_sim/bin/htc_net_app --type bogus ; ./build_sim/bin/htc_net_app --type wifi --bogus-opt
    - git diff HEAD --stat -- src/app/main_app.cpp src/hal src/hardware/mcu/MCU.cpp src/common/misc src/network src/app/wifi_reconnect.cpp src/app/wifi_reconnect.h
    - grep -rn "htc_wifi_app\|wifi_app_logic" src tests | grep -v "//"
    - grep -rn "wifi_app" src/app/CMakeLists.txt tests/CMakeLists.txt
    - grep -nE "std::to_string|std::stoi|[^_]stoi\(" src/app/net_app.cpp src/app/net_app_logic.cpp src/app/net_app_logic.h tests/test_net_app_logic.cpp
    - grep -nE "DeviceConfig|->get\(|Settings" src/app/net_app.cpp src/app/net_app_logic.cpp
    - bash -n script/regress_net_real.sh
    - file build/bin/htc_net_app
  evidence_ref: artifacts/T7-reviewer-evidence.md
state_delta:
  set_task_status:
    T7: done
  add_decision:
    - key: T7-done
      value: "T7 feature flow 闭环。htc_wifi_app 扩展为统一 htc_net_app(WiFi/Eth/USB 三上行)，WiFi 行为
        等价(reviewer 逐段对比坐实)，Eth/USB/CLI 分支符合用户 3 决策(USB 默认忠实搬运不 start/--type
        必填不读 INI/保留 wifi 脚本+新增 net 脚本)。双平台编译过，16 case 单测全绿，sim 冒烟无 segfault，
        受保护路径零改动。3 处 CONCORN 均 nice-to-have。真机 Eth/USB 回归留用户跑 regress_net_real.sh。"
  add_risk: []
artifact_path: artifacts/T7-reviewer-report.md
next: done
---

# T7 Reviewer Report (card)

本文件为 report card（只持指针）。逐项审查结论、git diff 关键行、代码引用 file:line、对比要点见
`artifacts/T7-reviewer-evidence.md`。

## 审查基准

用户拍板 3 决策(审查基准):
1. USB 默认忠实搬运(loadDriver→open→preconfig，不调 start)，`--usb-bringup`(默认关)才补 start()
2. CLI `--type wifi|eth|usb` 必填，本阶段不读 INI
3. 保留 regress_wifi_real.sh + 新增 regress_net_real.sh

## 7 项审查重点结论

| # | 审查重点 | 结论 | 关键证据(evidence 指针) |
|---|---|---|---|
| 1 | WiFi 行为等价(核心) | **PASS** | evidence §1(runWifi 逐段对比 + 纯逻辑逐字一致 + sim exit 2 交叉佐证) |
| 2 | correctness/边界 | **PASS** | evidence §2(分支/ifname/exit code/边界处理) |
| 3 | 回归风险/CMake | **PASS** | evidence §3(diff 只动 net_app target，main_app/media_app 块未动) |
| 4 | 可维护性 | **PASS** | evidence §4(纯函数设计/CLI 健壮) |
| 5 | 安全 | **PASS** | evidence §5(无新注入面/不读 INI) |
| 6 | 缺失测试 | **PASS** | evidence §6(6 新函数正常+边界全覆盖) |
| 7 | 约束遵守 | **PASS** | evidence §7(零改动/双平台/不读 INI/USB 不 start/无 uclibc 陷阱) |

## 已知风险缓解确认(不卡验收)

- T7-usb-no-start(high): 默认忠实搬运 + --usb-bringup opt-in(默认关) + regress 脚本标 KNOWN + help 明示。✓ 到位
- T7-eth-no-carrier-on-sim / 真机回归: sim 不崩 + 脚本正确 + 纯逻辑单测覆盖。✓ 到位
- T7-usb-setmodel-unset(medium): 默认 EC20 照搬 + --usb-model 显式设。✓ 到位
- T7-wifi-equivalence-regression(high): 逐段等价 + test 迁移保断言 + wifi 脚本保留。✓ 到位

## CONCORN(nice-to-have，不卡验收)

1. `--usb-model` 校验仅 `--usb-bringup` 时触发(无 bringup 静默忽略 model，harmless，help 已写明)。
2. runEth/runUsb 可抽 `bringUpGenericIf(NetType)`(当前各 ~20 行，收益低)。
3. parseUsbModel(net_app.cpp:394) 可下沉 net_app_logic 加单测(当前靠 sim 冒烟 BOGUS→6 间接验证)。

## 留用户的真机回归项

regress_net_real.sh(留用户在 T32 执行，PC 跑不了真机 Eth/USB):
- ETH: `--type eth` 断言 rc==0 + eth0 IP + gateway 非空。
- USB 默认: `--type usb` 断言 rc∈{2,3,4}(KNOWN，T7-usb-no-start，预期无 IP)。
- USB --usb-bringup: `--type usb --usb-bringup --usb-model <实机>` 断言 rc==0 + usb0 IP + gateway。
- bad args: `--type bogus`→6，无 `--type`→6。

## verdict

**status = success。** 7 项审查重点全 pass/concern(无 fail)，已知风险缓解到位，独立复跑全过。
建议闭环，task T7 → done。
