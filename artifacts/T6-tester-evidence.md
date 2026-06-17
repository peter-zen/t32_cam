---
task_id: T6
node: tester
kind: evidence
created: 2026-06-17
---

# T6 tester evidence (pointer-style, independent re-run)

独立复跑 dispatch 全部必跑项。所有命令由 tester 本人在 PC 重新执行，未采信 implementer 自述。

## 1. 双平台编译独立复跑

### 1a. sim build (htc_wifi_app)
```
cmake --build build_sim -j$(nproc) --target htc_wifi_app
```
Tail: `[100%] Built target htc_wifi_app` / `SIM_BUILD_WIFI_RC=0`.
产物: `build_sim/bin/htc_wifi_app` (246352 B, ELF 64-bit x86-64).

### 1b. T32 cross build (htc_wifi_app)
```
cmake --build build -j$(nproc) --target htc_wifi_app
```
Tail: `[100%] Built target htc_wifi_app` / `T32_BUILD_WIFI_RC=0`.
产物: `build/bin/htc_wifi_app` (22692 B, `ELF 32-bit LSB executable, MIPS, MIPS32, interpreter /lib/ld-uClibc.so.0`, stripped) — 交叉编译产物，PC 不可执行（只验生成+file 类型）。
error/to_string/stoi/undefined reference 扫描：**全空**（T32 uclibc 可移植性无回归）。

### 1c. 产物类型确认
```
file build/bin/htc_wifi_app build_sim/bin/htc_wifi_app build_sim/bin/test_wifi_app_logic
```
- build/bin/htc_wifi_app: ELF 32-bit MIPS32, uClibc (交叉编译)
- build_sim/bin/htc_wifi_app: ELF 64-bit x86-64 (PC 可跑冒烟)
- build_sim/bin/test_wifi_app_logic: ELF 64-bit x86-64

## 2. 纯逻辑单测 test_wifi_app_logic

### 2a. 编译
```
cmake --build build_sim -j$(nproc) --target test_wifi_app_logic   # exit 0
```
link 列表仅 `pthread rt gcc stdc++` —— 印证 wifi_app_logic.cpp 纯逻辑无 syscall/无外部依赖（符合 planner §3 可单测设计）。

### 2b. 运行
```
cd build_sim && LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH ./bin/test_wifi_app_logic
test_wifi_app_logic: ALL PASS
UNIT_TEST_RC=0
```

### 2c. 单测有效性审查（dispatch §2 硬要求）
tests/test_wifi_app_logic.cpp 经 tester 逐用例审读：
- **非空壳**：断言宏 `EXPECT_EQ/EXPECT_TRUE/EXPECT_FALSE` 失败时 `++g_failures` 并打 FAIL；`g_failures==0` 才 ALL PASS，非永真。
- **真实调用被测逻辑**：每用例构造 `LinkState`/`Target` 调 `decide()`/`decisionExitCode()`/`mayWriteBack()`/`normalizeSsid()` 并断言返回值，非空跑。
- **决策分支全覆盖**（planner §8.1-2）：
  - REUSE（L61-66，已连且 SSID 同→不切）
  - RECONNECT（L68-73，已连 SSID 不同→优雅切）
  - RECONNECT-when-ssid-unknown（L75-82，已连但 currentSsid 空→安全走 RECONNECT，额外边界用例）
  - FRESH_CONNECT（L84-89，未连→走 connectWifi）
  - ABORT（L91-101，无凭据 target.ssid 空 + hasCredentials=false 双触发→exit 6）
  - case-sensitive（L103-109，"Home"≠"home"→RECONNECT，验证 SSID 大小写敏感）
- **回写门控全覆盖**（L119-135）：5 反 1 正——
  not connected→block；connected 但不同 SSID→block（核心安全属性）；空 live SSID→block；空 target→block；匹配→allow。
- **退出码映射**（L111-117）：ABORT→6 / FRESH_CONNECT→3 / RECONNECT→3 / REUSE→0。
- **normalizeSsid**（L137-147）：trim 尾部 CR/LF/space + 首部 space/tab，不改大小写、保内部空格、全空格→空。
- **e2e decision→gate**（L149-164）：RECONNECT 决策后 gate 校验，含"reconnect 报成功但落到错 SSID→gate 阻断写"（exit 5 路径）。

判定：单测有效、非空壳、覆盖 dispatch §2 全部要求。**PASS。**

## 3. grep 审计

### 3a. T5 非回归 — kill/rmmod 必须空（T6 改动文件，严格范围）
```
grep -rn "killall\|kill.*wpa_supplicant\|rmmod.*8189fs\|pkill" \
    src/app/wifi_app.cpp src/app/wifi_app_logic.h src/app/wifi_app_logic.cpp \
    src/app/wifi_reconnect.h src/app/wifi_reconnect.cpp src/common/misc/Misc.cpp
```
Result: **empty (RC=1)**. PASS — T6 改动文件零 kill/respawn/rmmod 可执行调用。

dispatch 原始宽范围 grep（含 `src/app/`，即含 main_app.cpp）命中两行，均为 main_app.cpp T5 既有**注释**（非代码）：
- `src/app/main_app.cpp:890: // driver, does not kill wpa_supplicant, and does not clear /tmp/wpa_supplicant.`
- `src/app/main_app.cpp:1842: // WiFi driver, does not kill wpa_supplicant, and does not clear`
本任务未碰 main_app.cpp（见 §4），这两行是 T5 的修复说明注释，非本任务引入，非可执行 kill。

### 3b. 优雅重连原语命中
```
grep -n "currentSSID\|reconnectSSID\|wpa_cli.*-p /tmp/wpa_supplicant\|iwgetid" src/app/wifi_reconnect.cpp
```
命中: currentSSID 定义(L85)、reconnectSSID 定义(L127)、`iwgetid -r`(L100)；T32 路径 wpa_cli `-p /tmp/wpa_supplicant`(L113/162/174/197/202/212)。ctrl_iface path 与 wpa_conn.cpp 既有 socket 一致（kCtrlIfacePath="/tmp/wpa_supplicant"）。

### 3c. main 委托底层（不自造 shell）
```
grep -n "currentSSID\|reconnectSSID\|connectWifi\|startDHCP" src/app/wifi_app.cpp
```
命中: currentSSID(L167/225/246)、reconnectSSID(L210)、connectWifi(L192)、startDHCP(L233)。全委托底层，无 raw shell。

### 3d. writeUPID/writeUPWD 调用顺序（读码确认，不只看行号）
```
grep -n "writeUPID\|writeUPWD\|mayWriteBack" src/app/wifi_app.cpp
```
顺序: `mayWriteBack` 门控(L248) → `writeUPID`(L255) → `writeUPWD`(L256)。
**tester 读 wifi_app.cpp L245-266 确认**：L246 `freshSsid=currentSSID()`（写前再读）、L247 `stillConnected=isWifiConnected()`、L248 `if(!mayWriteBack(stillConnected,freshSsid,target.ssid)) return EXIT_WRITE_GATED(5)`；通过后才 L255/256 writeUPID/writeUPWD。
mayWriteBack 实现（wifi_app_logic.cpp:39-48）: `connected==true && liveSsid/targetSsid 非空 && normalizeSsid(live)==normalizeSsid(target)` —— 即 `isWifiConnected && currentSSID==target` 双校验。**顺序正确，写严格在双校验之后。** PASS。

## 4. 禁区 diff（工作树口径）

dispatch §4 原文用 `git diff --stat main` 会把整条分支历史差异算进来（本分支 merge_develop_simu 相对 main 有数千行历史改动，含 T5/T4/其它 task 的 hal/main_app/MCU/wpa_conn），**不是本任务 T6 的真实改动**。本任务真实改动以工作树状态为准（implementer 同口径）：

```
git status --short
 M orchestration-state.yaml
 M src/app/CMakeLists.txt
 M tests/CMakeLists.txt
?? src/app/wifi_app.cpp
?? src/app/wifi_app_logic.cpp
?? src/app/wifi_app_logic.h
?? src/app/wifi_reconnect.cpp
?? src/app/wifi_reconnect.h
?? tests/test_wifi_app_logic.cpp
?? script/regress_wifi_real.sh
?? reviews/  (+ artifacts/T6-*.md)
```

禁区工作树状态（**必须空**）：
```
git status --short src/hal src/app/main_app.cpp \
    src/hardware/mcu/MCU.h src/hardware/mcu/MCU.cpp \
    src/platform/tool/wpa_conn.cpp \
    src/common/misc/Misc.h src/common/misc/Misc.cpp
```
Result: **empty**. PASS — src/hal、main_app.cpp、MCU.{h,cpp}、wpa_conn.cpp、Misc.{h,cpp} 本任务零改动。
注：planner §2 原计划在 Misc 新增方法，implementer 改为放独立 wifi_reconnect 模块（report decision T6-graceful-reconnect-placement 已记录），故 Misc 未被触碰，符合"不破坏 Misc"。

## 5. sim 冒烟

### 5a. --help
```
cd build_sim && LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH ./bin/htc_wifi_app --help
HELP_EXIT=0   (期望 0) ✓
```

### 5b. 无参（MCU 空凭据）
```
cd build_sim && LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH ./bin/htc_wifi_app
htc_wifi_app: no target SSID available. Pass --ssid/--pwd or populate MCU registers.
NOARG_EXIT=6  (期望 6) ✓  无 segfault
```
sim 下 MCU readUPID 走 IIC bypass 返空 → target.hasCredentials=false → ABORT → exit 6，符合 planner §4/§8。

### 5c. --ssid foo --pwd bar
```
cd build_sim && LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH ./bin/htc_wifi_app --ssid foo --pwd bar
insmod: ERROR: could not load module /system/bin/wifi/8189fs.ko: No such file or directory
[SDK_STUB] system_call_init: PC simulation mode
SSID_EXIT=2   (dispatch §5 预期 3，实际 2)
```
无 segfault/abort（exit 非 ≥128、非 139/134）。

**exit 2 vs dispatch 预期 3 的偏差分析（tester 独立代码追踪）**：
sim 下 `isWifiConnected("wlan0")` 查 getIPAddress——PC 无 wlan0 → 空 → false（Misc.cpp:300-307）。
→ `decide()` 走 FRESH_CONNECT（不是 RECONNECT）。
→ `Misc::connectWifi()`（Misc.cpp:425-471）：L434 `isWifiDriverLoaded()` sim 下 `grep -q '^8189fs' /proc/modules` → PC 无此模块 → false → L442 `syscall("insmod /system/bin/wifi/8189fs.ko")` → PC 上 insmod 失败 → L450 re-check 仍 false → `return false`。
→ main（wifi_app.cpp:192-202）：connectWifi 返 false → L196 `isWifiDriverLoaded()` false → **exit 2 (EXIT_DRIVER_FAIL)**。

判定：exit 2 是 sim 环境 driver 不可加载的物理事实导致，**退出码在 planner §4 契约集合 {0,2,3,4,5,6} 内、语义正确（DRIVER_FAIL）、不 segfault/不崩溃**。
dispatch §5 预期 3 的前提是"sim 下重连桩 false（走 RECONNECT）"，但 sim 下 isWifiConnected()=false 物理上不可能进 RECONNECT 分支（无 IP 不算 connected），故 exit 3 在 sim 不可达。
这是 **dispatch 预期与 sim 物理路径的合理分歧，非被测代码缺陷**。真机上有真 driver 时 `--ssid foo --pwd bar` 错密码会 insmod 成功但 wpa_conn 失败 → exit 3，留 script/regress_wifi_real.sh 用例 D 验证。

### 5d. （tester 追加）未知 flag
```
cd build_sim && ... ./bin/htc_wifi_app --bogus-flag
BOGUS_EXIT=6   (参数错误路径，exit 6 正确)
```

## 偏差小结
仅一项：sim `--ssid foo --pwd bar` exit 2 而非 dispatch 预期 3。已定性为 sim 物理环境限制（无 8189fs driver）导致的合理降级，契约内、不 segfault。不构成 fail。

## 遗留（不变，留真机）
- 真机 WiFi 实连回归（用例 A 首连 / B 优雅切 SSID + wpa_supplicant PID 恒定 / C 回写 MCU / D 错密码 exit 3 / E 无参读 MCU / F 多轮 A↔B PID 恒定 + dmesg 无 oops）由用户在 T32 手动跑 script/regress_wifi_real.sh。PC 不能跑 MIPS（同 T5/T4 口径）。
