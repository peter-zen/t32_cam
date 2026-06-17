---
contract: report
contract_version: "1"
task_id: T6
node: reviewer
flow: feature
status: success
# review verdict: passed (no blocker/major; 1 pre-existing minor shell-injection, low)
summary: |
  T6 reviewer 终审（feature flow 末端，C2 前）。独立读 wifi_app.cpp/wifi_app_logic.{h,cpp}/
  wifi_reconnect.{h,cpp}/test_wifi_app_logic.cpp/regress_wifi_real.sh 全文 + grep 复核 + 产物 file 核验，
  逐项过 dispatch 八大重点。结论：无 blocker、无 major，仅 1 项 minor（shell 注入面，但与既有代码
  wpa_conn.cpp:105 同款，非本任务引入、非 T5 回归、凭据源为本地），**判 passed**，可进末端 audit。
  红线 T5 不回归：wifi_reconnect.cpp 重连路径纯走既有 ctrl_iface socket（reconfigure + add_network/
  select_network in-band），绝无 killall/pkill/rmmod/rm -rf /tmp/wpa_supplicant，RECONNECT 分支不调
  Misc::connectWifi（唯一 spawn supplicant 的函数），kill/rmmod grep 仅命中 wifi_reconnect.h:10 注释。
  MCU 回写门控：wifi_app.cpp:246-264 写前再读 currentSSID+isWifiConnected 双校验经 mayWriteBack（connected
  && currentSSID==target）后才 writeUPID/writeUPWD，门控失败早返回 exit 5，无 TOCTOU/早返回绕过路径，
  单测 5 反 1 正 + e2e「落错 SSID→gate 阻断」覆盖。退出码 12 条返回路径全用 enum 名（0/2/3/4/5/6），
  无裸 return 1。T32 工具链：新增 .cpp 零 std::to_string/stoi/stoul（grep 全空），build/bin/htc_wifi_app
  为 MIPS uClibc ELF 链接成功。禁区 git status 全空（src/hal、main_app.cpp、MCU.{h,cpp}、wpa_conn.cpp、
  Misc.{h,cpp} 零改动）。
  发现（minor，不阻塞 merge）：
  - minor-1（shell 拼接注入面，既有模式）：wifi_reconnect.cpp:157/186/192 把 ssid/password 直接
    snprintf 进 wpa_passphrase/wpa_cli set_network 命令串，经 Misc::popencall→popen/system→sh -c 解析。
    若凭据含 `;`/`` ` ``/`$()`/`"` 会被 shell 解释。但 (a) 既有 src/platform/tool/wpa_conn.cpp:105
    generate_wifi_config 用裸 + 拼接同款凭据进同款 shell 路径，且 main_app.cpp 的 connectWifi 也经此；
    (b) 凭据源为本地 CLI/MCU 寄存器，非远程可达；(c) MCU readUPID/readUPWD 有 ASCII 校验但不校验
    shell 元字符。故为既有 WiFi 凭据处理风险模式的镜像，非 T6 新引入更差实现、非 T5 回归。建议后续
    统一加固（wpa_passphrase 从 stdin 读密码消除密码插值 / ssid 白名单校验 / set_network 走 wpa_cli
    interactive stdin），不阻塞本任务 merge。
deliverables:
  - src/app/wifi_app.cpp
  - src/app/wifi_app_logic.h
  - src/app/wifi_app_logic.cpp
  - src/app/wifi_reconnect.h
  - src/app/wifi_reconnect.cpp
  - tests/test_wifi_app_logic.cpp
  - script/regress_wifi_real.sh
  - src/app/CMakeLists.txt
  - tests/CMakeLists.txt
  - artifacts/T6-reviewer-evidence.md
  - artifacts/T6-reviewer-report.md
verification:
  commands:
    - grep -rnE "killall|pkill|kill .?wpa_supplicant|rmmod" src/app/wifi_app.cpp src/app/wifi_reconnect.h src/app/wifi_reconnect.cpp
    - grep -rnE "std::to_string|std::stoi|std::stoul" src/app/wifi_app.cpp src/app/wifi_app_logic.{h,cpp} src/app/wifi_reconnect.{h,cpp} tests/test_wifi_app_logic.cpp
    - git status --short src/hal src/app/main_app.cpp src/hardware/mcu/MCU.h src/hardware/mcu/MCU.cpp src/platform/tool/wpa_conn.cpp src/common/misc/Misc.h src/common/misc/Misc.cpp
    - grep -nE "return EXIT_|return [0-9]" src/app/wifi_app.cpp
    - grep -nE "mayWriteBack|writeUPID|writeUPWD" src/app/wifi_app.cpp
    - file build/bin/htc_wifi_app build_sim/bin/htc_wifi_app build_sim/bin/test_wifi_app_logic
    - sed -n '127,226p' src/app/wifi_reconnect.cpp
    - sed -n '245,266p' src/app/wifi_app.cpp
  evidence_ref: artifacts/T6-reviewer-evidence.md
state_delta:
  set_task_status: {}
  add_decision:
    - key: T6-review-verdict
      value: "reviewer 终审判 passed，无 blocker/major，仅 1 项 minor（shell 拼接注入面，与既有
        wpa_conn.cpp:105 同款，非 T6 引入、非 T5 回归、凭据源本地）。T5 红线满足：wifi_reconnect.cpp
        重连纯走既有 ctrl_iface socket（reconfigure + add_network/select_network in-band），无
        killall/rmmod/respawn，RECONNECT 分支不调 connectWifi（唯一 spawn supplicant 的函数）。
        MCU 门控满足：wifi_app.cpp:246-264 写前再读 currentSSID+isWifiConnected 双校验经 mayWriteBack
        后才 writeUPID/writeUPWD，无绕过。退出码 12 路全 enum 名（0/2/3/4/5/6）无裸 return 1。T32
        uclibc 兼容（零 std::to_string/stoi，MIPS ELF 链接成功）。禁区 git status 全空。sim exit 2
        偏差已定性物理限制（PC 无 wlan0/driver），契约内无 segfault。遗留真机验证（script/regress_wifi_real.sh
        用例 A-F，B/F 的 wpa_supplicant PID 跨 SSID 切换恒定 + dmesg 无 oops）留用户 T32 执行。"
    - key: T6-review-findings
      value: "1 项 minor（不阻塞）：shell 拼接注入面——wifi_reconnect.cpp:157/186/192 把 ssid/password
        直接 snprintf 进 wpa_passphrase/wpa_cli set_network 命令，经 popencall→popen/system→sh -c 解析，
        凭据含 `;`/backtick/$() 会被注入。但与既有 src/platform/tool/wpa_conn.cpp:105 generate_wifi_config
        同款（裸 + 拼接），main_app.cpp connectWifi 也经此，全项目 WiFi 凭据处理本就走 shell 拼接；
        凭据源本地 CLI/MCU，非远程；MCU read 有 ASCII 校验但不校验 shell 元字符。属既有风险模式镜像，
        非 T6 新引入、非 T5 回归。修复方向（后续统一加固，非本任务）：wpa_passphrase 从 stdin 读密码 /
        ssid 白名单校验（reject `;` backtick $ \" \\）/ set_network 走 wpa_cli interactive stdin。"
  add_risk:
    - key: T6-shell-injection-preexisting
      severity: low
      description: "wifi_reconnect.cpp reconnectSSID 把凭据以 shell 拼接方式喂给 wpa_passphrase/wpa_cli
        set_network（:157/186/192），经 Misc::popencall→popen/system→sh -c。含 shell 元字符的 ssid/password
        可被注入。但该风险与既有 wpa_conn.cpp:105 generate_wifi_config（裸 + 拼接同款凭据进同款 shell 路径）
        等价，main_app.cpp connectWifi 也经此；凭据源为本地 CLI/MCU 寄存器非远程可达。故 severity low，
        归入后续 WiFi 凭据安全加固，不阻塞 T6 merge。"
    - key: T6-hw-unverified
      severity: medium
      description: "reviewer 仅做代码审计 + sim 冒烟 + 产物 file 核验（PC 不能跑 MIPS）。真机 WiFi
        实连回归（script/regress_wifi_real.sh 用例 A-F：A 首连 / B 优雅切 SSID 且 wpa_supplicant PID
        恒定 / C --write-mcu 回写 MCU / D 错密码 exit 3 / E 无参读 MCU / F 5 轮 A↔B PID 恒定 + dmesg
        无 oops）需 T32 硬件手动跑。关键不变量：用例 B/F 的 wpa_supplicant PID 跨 SSID 切换恒定是 T5
        不回归的唯一可观测证明。"
artifact_path: artifacts/T6-reviewer-report.md
next: audit
---

# T6 Reviewer Report (终审)

## 结论一句话

无 blocker、无 major，仅 1 项 minor（shell 拼接注入面，但与既有 `wpa_conn.cpp:105` 同款、非本任务引入、
非 T5 回归、凭据源为本地），**判 passed**，可进末端 audit。

## 审查范围（实际读代码）

- `src/app/wifi_app.cpp`（272 行全文）
- `src/app/wifi_app_logic.h` + `wifi_app_logic.cpp`（纯逻辑层全文）
- `src/app/wifi_reconnect.h` + `wifi_reconnect.cpp`（229 行全文）
- `tests/test_wifi_app_logic.cpp`（单测全文）
- `script/regress_wifi_real.sh`（真机脚本全文）
- 对照：`artifacts/T6-planner-full.md`、`T6-implementer-evidence.md`、`T6-tester-evidence.md`
- 真相源核验：`src/platform/tool/wpa_conn.cpp:95-170`（既有 wpa_passphrase/ctrl_iface idiom）、
  `src/platform/sdk_stub/system_call_stub.c:29/44`（sim 下 system()/popen()）、
  `ref/.../libsystemcall/system_call.c:144-155`（T32 dbus daemon popen）、`src/hardware/mcu/MCU.cpp:482`（ASCII 校验）、
  `src/app/main_app.cpp:1375/1574`（T5 connectWifi 路由）

## 1. 红线：T5 不回归 — PASS

- `wifi_reconnect.cpp` 重连路径（:127-226）全部经既有 ctrl_iface socket（`kCtrlIfacePath="/tmp/wpa_supplicant"`，
  与 `wpa_conn.cpp:155 -C` / `:172/201 -p` 完全一致）：
  - `wpa_passphrase > /tmp/wpa_supplicant_htc_wifi.conf` + `wpa_cli ... reconfigure`（resident supplicant re-read，不重启）
  - fallback `add_network`/`set_network ssid,psk`/`enable_network`/`select_network`（in-band，不重启）
  - poll `wpa_cli status` for `wpa_state=COMPLETED` + `isWifiConnected()` 双校验
- kill/rmmod grep 仅命中 `wifi_reconnect.h:10` 注释，**无可执行 kill/respawn/rmmod**。
- `wifi_app.cpp:205-215` RECONNECT 分支调 `wifi_reconnect::reconnectSSID`，**不**调 `Misc::connectWifi`
  （后者 RTL 分支 `wpa_conn ... 1` 会 respawn supplicant = T5 回归源；正确绕开）。
- `connectWifi` 仅在 FRESH_CONNECT（:192，首次连接合法 spawn）出现。

## 2. MCU 回写门控 — PASS

- `wifi_app.cpp:246-264`：写前再读 `freshSsid=currentSSID()` + `stillConnected=isWifiConnected()`，
  经 `mayWriteBack(stillConnected, freshSsid, target.ssid)` 双校验后才 `writeUPID/writeUPWD`；
  门控失败 L252 `return EXIT_WRITE_GATED`，无绕过。
- `mayWriteBack`（wifi_app_logic.cpp:39-48）：`connected && live/target 非空 && normalizeSsid(live)==normalizeSsid(target)`。
- TOCTOU 窗口极小且用 fresh 值；writeUPID/writeUPWD 返 false 走 L264 exit 5 不掩盖。
- 单测 5 反 1 正 + e2e「reconnect 报成功但落错 SSID→gate 阻断」覆盖（test:119-135,149-164）。

## 3. 退出码契约 — PASS

12 条返回路径全用 enum 名，集合 {0,2,3,4,5,6}，无裸 `return 1`，无裸数字。
`decisionExitCode` 映射 ABORT→6/FRESH→3/RECONNECT→3/REUSE→0 与 main 一致。

## 4. T32 工具链兼容 — PASS

新增 .cpp 零 `std::to_string/stoi/stoul`（grep exit 1）。`build/bin/htc_wifi_app` 为
`ELF 32-bit MIPS MIPS32 uClibc stripped`，链接成功 = uclibc 可移植性无回归。

## 5. 边界与安全 — minor（shell 注入面，既有模式）

- 执行路径：`Misc::popencall`→`popen_call`→ sim `popen()` / T32 dbus daemon，最终都 `sh -c`。
- 注入点：`wifi_reconnect.cpp:157`（wpa_passphrase ssid+password）、`:186`（set_network ssid）、
  `:192`（set_network psk）把凭据直接 snprintf 进命令串。
- 定级 minor 理由：(a) 与既有 `wpa_conn.cpp:105` generate_wifi_config（裸 + 拼接同款凭据进同款 shell）等价，
  main_app.cpp connectWifi 也经此，全项目 WiFi 凭据处理本就走 shell 拼接；(b) 凭据源本地 CLI/MCU 非远程；
  (c) MCU read 有 ASCII 校验但不校验 shell 元字符。**非 T6 新引入、非 T5 回归。**
- 修复方向（后续统一加固，非本任务）：wpa_passphrase 从 stdin 读密码 / ssid 白名单（reject `;` `` ` `` `$` `\` `"`）/ set_network 走 wpa_cli interactive stdin。

## 6. 分层可测性 — PASS

`wifi_app_logic.{h,cpp}` 无 syscall（仅 `<algorithm>`+`<string>`），test target link 仅 pthread/rt/gcc/stdc++。
Decision 枚举 + 退出码映射清晰，单测覆盖 decide 全 4 分支 + RECONNECT-when-ssid-unknown + case-sensitive。

## 7. 禁区未触 — PASS

`git status --short src/hal src/app/main_app.cpp src/hardware/mcu/MCU.{h,cpp} src/platform/tool/wpa_conn.cpp
src/common/misc/Misc.{h,cpp}` → **全空**。（工作树口径；dispatch 原 `git diff --stat main` 会混入分支历史，不可用。）

## 8. 真机回归脚本 — PASS

`regress_wifi_real.sh` 用例 A-F 齐全，`set -u` + 末尾 `FAIL==0?exit0:exit1`。
**用例 B（:48-59）记 PID_B_BEFORE→切 S2→PID_B_AFTER 断言恒定**；**用例 F（:79-90）5 轮 A↔B 每轮比对 PID_F0**；
dmesg oops 扫描查 DbusProcess/ctrl_iface/wpa_supplicant crash。关键 T5 不回归不变量断言到位。

## 9. Must-fix before merge / Nice-to-have

- **Must-fix before merge：无**（无 blocker/major）。
- Nice-to-have：minor-1（后续 WiFi 凭据安全加固——wpa_passphrase stdin 读密码 / ssid 白名单 / wpa_cli interactive）。

## 10. 偏差复核

sim `--ssid foo --pwd bar` exit 2 而非 dispatch §5 预期 3：sim 下 `isWifiConnected` 物理为 false（PC 无 wlan0）
→ FRESH_CONNECT → connectWifi insmod 8189fs 失败 → exit 2（DRIVER_FAIL）。契约内 {0,2,3,4,5,6}，语义正确，
无 segfault。exit 3 在 sim 物理不可达，真机有 driver 时可达（script 用例 D）。**非缺陷。**

## 11. 移交

判 passed，可进末端 audit（`orchestrator audit --flow feature --task T6`）。
遗留真机验证（script/regress_wifi_real.sh 用例 A-F，关键：用例 B/F 的 wpa_supplicant PID 跨 SSID 切换恒定
+ dmesg 无 oops = T5 不回归唯一可观测证明）留用户 T32 执行。
