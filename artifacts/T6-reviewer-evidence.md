---
task_id: T6
node: reviewer
kind: evidence
created: 2026-06-17
---

# T6 reviewer evidence (independent re-verification, pointer-style)

reviewer 独立读全文 + grep 复核 + 产物核验。未采信 implementer/tester 自述（仅作对照）。

## 1. 红线：T5 不回归（kill/rmmod/respawn）— PASS

### 1a. T6 改动文件 kill/rmmod grep（严格范围）
```
grep -rnE "killall|pkill|kill .?wpa_supplicant|rmmod|rm .?-rf .?/tmp/wpa_supplicant" \
    src/app/wifi_app.cpp src/app/wifi_app_logic.h src/app/wifi_app_logic.cpp \
    src/app/wifi_reconnect.h src/app/wifi_reconnect.cpp
```
命中：仅 `wifi_reconnect.h:10` 一行**注释**（`// ...NEVER kill wpa_supplicant... NEVER rmmod...`），
非可执行代码。exit 0 但为注释命中。**无可执行 kill/respawn/rmmod。PASS。**

### 1b. 读 wifi_reconnect.cpp 全文确认优雅重连原语（wifi_reconnect.cpp:127-226）
- 重连序列全部经既有 ctrl_iface socket（`kCtrlIfacePath="/tmp/wpa_supplicant"`，:19，与 `wpa_conn.cpp:155 -C` / `:172/201 -p` 完全一致）：
  1. `wpa_passphrase ... > /tmp/wpa_supplicant_htc_wifi.conf`（:157）→ `wpa_cli -p /tmp/wpa_supplicant reconfigure`（:162）— resident supplicant re-read conf，**不重启进程**。
  2. fallback `add_network`(:174) / `set_network ... ssid`(:185) / `set_network ... psk`(:191) / `enable_network`(:197) / `select_network`(:202) — 标准 in-band 切换，**不重启进程**。
  3. poll `wpa_cli ... status` 找 `wpa_state=COMPLETED`（:212, :78-81）并以 `Misc::isWifiConnected()` 双校验（:217）。
- **无** `killall`/`pkill`/`kill -9`/`rmmod`/`rm -rf /tmp/wpa_supplicant`/`rm /tmp/wpa_supplicant`。
- **无** 调 `Misc::connectWifi`（该函数 RTL 分支会 `wpa_conn ... 1` respawn supplicant，是 T5 回归源——重连路径正确绕开它，FRESH_CONNECT 分支除外那是首次连接）。

### 1c. 读 wifi_app.cpp 确认 main 的 RECONNECT 分支走 reconnectSSID（不 respawn）
- `wifi_app.cpp:205-215` `case RECONNECT:` → `wifi_reconnect::reconnectSSID(...)`，**不**经 `Misc::connectWifi`。
- `connectWifi` 仅出现在 `FRESH_CONNECT`（:192），那是首次连接的合法 spawn 路径（非 SSID 切换）。
- 判定：**T5 红线满足。**

## 2. MCU 回写门控（高）— PASS

### 2a. 门控顺序（wifi_app.cpp:245-266，逐行读码）
```
:246  freshSsid   = currentSSID(ifname)           // 写前再读一次 live SSID
:247  stillConnected = isWifiConnected(ifname)     // 写前再读一次 connected
:248  if (!mayWriteBack(stillConnected, freshSsid, target.ssid))
:249-253      return EXIT_WRITE_GATED (5)          // 门控失败 → 写前早返回
:255  writeUPID(target.ssid)                       // 仅门控通过后
:256  writeUPWD(target.password)
```
写严格在 `mayWriteBack` 双校验之后；门控失败在 L252 `return`，**无绕过路径**。

### 2b. mayWriteBack 实现（wifi_app_logic.cpp:39-48）
```
if (!connected) return false;                       // isWifiConnected 必须真
if (liveSsid.empty() || targetSsid.empty()) return false;
return normalizeSsid(liveSsid) == normalizeSsid(targetSsid);  // currentSSID==target
```
即 `isWifiConnected && currentSSID==target` 双真才放行。**与 dispatch 要求一致。**

### 2c. TOCTOU 复核
- `freshSsid`/`stillConnected` 在 L246-247 同步读，紧接 L248 判、L255-256 写，窗口极小；
- 即便窗口内 SSID 漂移，`mayWriteBack` 用的是**刚读到的** freshSsid，不是缓存值；
- `writeUPID/writeUPWD` 失败（返 false）走 L264 `return EXIT_WRITE_GATED`，不掩盖。
- **无早返回/TOCTOU 绕过误写。PASS。**

### 2d. 单测覆盖（test_wifi_app_logic.cpp:119-135, 149-164）
5 反（not connected / 不同 SSID / 空 live / 空 target）+ 1 正（匹配）+ e2e「reconnect 报成功但落错 SSID→gate 阻断写」。
核心安全属性「connected 但不同 SSID → block」有专门用例（:128）。**有效。**

## 3. 退出码契约（中）— PASS

main 全部返回路径（`grep -nE "return EXIT_|return [0-9]" wifi_app.cpp`）：
| 行 | 返回 | 语义 |
|---|---|---|
| 131 | EXIT_ARG_ERROR(6) | parse 失败 |
| 135 | EXIT_OK(0) | --help |
| 183 | EXIT_ARG_ERROR(6) | ABORT 无凭据 |
| 198 | EXIT_DRIVER_FAIL(2) | connectWifi 后 driver 仍未加载 |
| 201 | EXIT_CONNECT_FAIL(3) | connectWifi 失败但 driver 在 |
| 212 | EXIT_CONNECT_FAIL(3) | 优雅重连失败 |
| 228 | EXIT_CONNECT_FAIL(3) | 连后探测 not connected |
| 235 | EXIT_DHCP_FAIL(4) | DHCP 失败 |
| 252 | EXIT_WRITE_GATED(5) | 门控阻断写 |
| 264 | EXIT_WRITE_GATED(5) | MCU write 返 false |
| 271 | EXIT_OK(0) | 成功 |

- **无裸 `return 1`**，无裸数字（全用 enum 名）。
- 退出码集合 {0,2,3,4,5,6}，与 planner §4 契约一致；1 被刻意排除（避免与 shell "misc error" 混淆）。
- `decisionExitCode`（wifi_app_logic.cpp:28-37）映射 ABORT→6/FRESH→3/RECONNECT→3/REUSE→0，与 main 一致。
- **PASS。**

## 4. T32 工具链兼容（高，已知坑）— PASS

### 4a. std::to_string/stoi/stoul grep（全空）
```
grep -rnE "std::to_string|std::stoi|std::stoul|std::stoll|std::stof" \
    src/app/wifi_app.cpp src/app/wifi_app_logic.{h,cpp} \
    src/app/wifi_reconnect.{h,cpp} tests/test_wifi_app_logic.cpp
```
**exit 1（全空）。** 新增代码用 `snprintf`/`std::string::size`/`.size()`，无禁用 STL 转换。
（注：`wifi_app.cpp:155` 用 `%zu` 打 `std::string::size()` 返回的 `size_t`，uclibc 兼容。）

### 4b. T32 交叉产物核验
```
file build/bin/htc_wifi_app
→ ELF 32-bit LSB executable, MIPS, MIPS32 rel2, interpreter /lib/ld-uClibc.so.0, stripped
```
（22692 B，2026-06-17 03:23，与 tester 报告一致。）uclibc 链接成功 = T32 可移植性无回归。
**PASS。**

## 5. 边界与安全（shell 注入面）— 已有风险模式，非本任务引入，记为 minor

### 5a. 执行路径定性：shell 拼接（非参数化）
`Misc::popencall` → `popen_call` → sim 下 `popen()`（`src/platform/sdk_stub/system_call_stub.c:44`），
T32 下经 dbus daemon（`ref/.../libsystemcall/system_call.c:144-155`）。两条路径最终都把 cmd 字符串
交给 **`sh -c "<cmd>"`** 解析。`Misc::syscall` 同理（stub `system()` :29）。

### 5b. 注入点（wifi_reconnect.cpp，凭据进 shell）
| 行 | 命令模板 | 插入的不可信串 |
|---|---|---|
| 157 | `wpa_passphrase "<ssid>" "<password>" > <conf>` | ssid + password |
| 186 | `wpa_cli ... set_network <id> ssid '"<ssid>"'` | ssid |
| 192 | `wpa_cli ... set_network <id> psk '"<password>"'` | password |
| 113/162/174/197/202/212 | `wpa_cli -i <if> -p /tmp/wpa_supplicant ...` | ifname（默认 wlan0，CLI 可控） |

若 ssid/password 含 `"`、`` ` ``、`$()`、`;`、`\` 等 shell 元字符，会被 sh 解释。

### 5c. 风险定级：minor（非 blocker），理由
1. **既有代码同款**：`src/platform/tool/wpa_conn.cpp:105` `generate_wifi_config` 用裸 `+` 拼接
   `wpa_passphrase " + ssid + " " + password + "` → 同一 `execute_command`/`popen_call` shell 路径。
   本任务的 snprintf 拼接是**同一既有风险模式的镜像**，非新引入的更差实现。
2. `main_app.cpp`（T5）的 `connectWifi(wifi_ssid, wifi_pwd)`（:1375/1574）最终也经 `wpa_conn.cpp:105`
   同款拼接，即**全项目 WiFi 凭据处理本就走 shell 拼接**。
3. 凭据来源是**本地**：CLI 参数（设备操作者本人）或 MCU 寄存器（设备自持），非远程/网络可达输入。
   攻击者要利用需先拿到设备 shell 或写 MCU 寄存器，此时已 game-over。
4. MCU readUPID/readUPWD 有 ASCII 合法性校验（`MCU.cpp:482` 打 "invalid ASCII character"），
   但 **不校验 shell 元字符**，故 MCU 源仍可携 `;` 等。

### 5d. 结论与修复方向（记 minor，建议后续统一加固，不阻塞 merge）
- 结论：**shell 注入面真实存在但与既有代码同款，非 T6 回归、非 T6 引入的更差模式。**
- 修复方向（不在本任务改，归入后续 WiFi 安全加固）：
  - 首选：`wpa_passphrase` 本就支持 stdin 读密码（`wpa_passphrase <ssid>` 然后从 stdin 喂密码），
    可消除密码的 shell 插值；ssid 仍需校验（SSID 合法字符集 `[ -~] 且不含 `"``$` 等）。
  - 或：对 ssid/password 做白名单校验（拒绝含 `;` `` ` `` `$` `\` `"` 的值，在 CLI 解析层 reject，
    退出码 6）。
  - 或：`set_network` 改走 wpa_cli 交互式 stdin（`wpa_cli -p ...` 起 interactive，逐行喂命令，
    避免每条都过 sh）。

## 6. 分层可测性（中）— PASS

- `wifi_app_logic.{h,cpp}` 无 syscall：仅 `<algorithm>`+`<string>`，纯函数 `decide`/`decisionExitCode`/
  `mayWriteBack`/`normalizeSsid`。test target `test_wifi_app_logic`（`tests/CMakeLists.txt:106-115`）
  link 列表仅 `pthread rt gcc stdc++`（tester 核验），印证无 syscall 依赖。
- Decision 枚举（ABORT/FRESH_CONNECT/REUSE/RECONNECT）与退出码映射清晰（`wifi_app_logic.h:35-40` + `:28-37`）。
- 单测覆盖 decide 全 4 分支 + RECONNECT-when-ssid-unknown 边界 + case-sensitive（:61-109）。
- **PASS。**

## 7. 禁区未触（低，硬约束）— PASS

```
git status --short src/hal src/app/main_app.cpp \
    src/hardware/mcu/MCU.h src/hardware/mcu/MCU.cpp \
    src/platform/tool/wpa_conn.cpp src/common/misc/Misc.h src/common/misc/Misc.cpp
```
**全空。** src/hal、main_app.cpp、MCU.{h,cpp}、wpa_conn.cpp、Misc.{h,cpp} 本任务零改动。
（dispatch 原口径 `git diff --stat main` 会混入分支历史，不可用；tester 已改用工作树口径，reviewer 同口径复核。）

## 8. 真机回归脚本（中）— PASS

`script/regress_wifi_real.sh` 逐用例核：
- 用例 A（:42-46）首连 S1，断言 exit 0 + ssid==S1。
- **用例 B（:48-59）核心 T5 断言**：记 `PID_B_BEFORE=$(wpa_pid)` → 切 S2 → 记 `PID_B_AFTER`，
  断言 `PID_B_BEFORE == PID_B_AFTER` 且非空（:55）→ **wpa_supplicant PID 跨 SSID 切换恒定到位。**
- 用例 C（:61-65）--write-mcu exit 0。
- 用例 D（:67-71）错密码 exit ∈ {3,5}。
- 用例 E（:73-77）无参读 MCU exit ∈ {0,6}。
- **用例 F（:79-90）5 轮 A↔B**：每轮记 `pid_now` 比对 `PID_F0`，任一轮漂移即 fail+break（:86-88）。
- dmesg oops 扫描（:92-98）查 `DbusProcess|ctrl_iface.*oops|wpa_supplicant.*crash`。
- `set -u`（:20）+ 末尾 `FAIL==0 ? exit 0 : exit 1`（:102）。脚本可执行、断言到位。

## 9. 产物核验（独立 ls/file）
```
build/bin/htc_wifi_app        → ELF 32-bit MIPS uClibc stripped (22692 B)   T32 ✓
build_sim/bin/htc_wifi_app    → ELF 64-bit x86-64 (246352 B)                 sim ✓
build_sim/bin/test_wifi_app_logic → ELF 64-bit x86-64 (193000 B)             单测 ✓
```
CMake wiring（`src/app/CMakeLists.txt:5/7/8/60/197/358`、`tests/CMakeLists.txt:106-123`）双平台 target 齐。

## 偏差复核
- sim `--ssid foo --pwd bar` exit 2 而非 dispatch §5 预期 3：reviewer 追踪同 tester 结论——
  sim 下 `isWifiConnected("wlan0")` 物理为 false（PC 无 wlan0）→ decide 走 FRESH_CONNECT（非 RECONNECT）
  → `Misc::connectWifi` 在 sim insmod 8189fs.ko 失败 → exit 2（DRIVER_FAIL）。契约内 {0,2,3,4,5,6}，
  语义正确，无 segfault。**exit 3 在 sim 物理不可达（无 IP 不算 connected），真机有 driver 时可达，留 script 用例 D。非缺陷。**
