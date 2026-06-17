# T6 — htc_wifi_app planner full plan

> 单一真相指针：本文件是方案全文。dispatch 见 `artifacts/T6-planner-dispatch.md`。
> 已核实事实来源：`src/common/misc/Misc.{h,cpp}`、`src/hardware/mcu/MCU.{h,cpp}`、
> `src/platform/tool/wpa_conn.cpp`、`src/common/Common.h`、`src/app/CMakeLists.txt`、
> `src/platform/tool/CMakeLists.txt`、`tools/CMakeLists.txt`、`CMakeLists.txt`、
> `src/app/main_app.cpp`(T5 注释 L889-895/L1841-1846)、`sdk/include/systemcall/system_call.h`。

## 0. 事实核验结论（planner 复核 dispatch，全部成立）

| dispatch 断言 | 核实点 | 结论 |
|---|---|---|
| `Misc::isWifiDriverLoaded()` 静态、幂等、SIM-safe | `Misc.cpp:284-298` grep `/proc/modules`，纯读 | 成立 |
| `Misc::isWifiConnected()` 已连短路 | `Misc.cpp:300-307`，IP+gateway 非空 | 成立 |
| `connectWifi` 已连即短路、未加载才 insmod 且 re-check | `Misc.cpp:425-471` | 成立 |
| `connectWifi` RTL 分支 shell 调 `wpa_conn` **会 spawn wpa_supplicant** | `Misc.cpp:459-464`：`wpa_conn wlan0 ... 1` → wpa_conn.cpp:155 `wpa_supplicant ... -C /tmp/wpa_supplicant &` | **关键**：connectWifi 重连路径=重 spawn wpa_supplicant，正是 T5 要规避的 |
| `startDHCP` 有 IP 即短路 | `Misc.cpp:473-491` | 成立 |
| MCU `readUPID/readUPWD/writeUPID/writeUPWD` 真实实现 | `MCU.cpp:492/515/554/570`，ASCII 校验+IIC，空串写返 false | 成立 |
| `Common.h` UPWD 坑 | `Common.h:45` `INI_KEY_UPWD "PWD"`（非 "UPWD"） | 成立 |
| T5 退出刻意不 kill wpa_supplicant / 不清 `/tmp/wpa_supplicant` | `main_app.cpp:889-895,1841-1846` 双注释 | 成立 |
| `wpa_cli` 已是既有调用习惯 | `wpa_conn.cpp:172,201` 用 `wpa_cli -i <if> -p /tmp/wpa_supplicant status` | 成立，ctrl_iface path 固定 `/tmp/wpa_supplicant` |
| `system_call`/`popen_call` 签名 | `system_call.h:18/32` `int system_call(char*,int)` / `int popen_call(char*,char*,int,int)` | 成立 |
| `common_misc` 是 SHARED lib，链接 `system_call`(T32)/`sdk_stub`(sim) | `src/common/misc/CMakeLists.txt` | 成立 |

**派生关键事实（设计依据）**：`wpa_supplicant` 的 ctrl_iface socket 路径在仓库里是**固定的 `/tmp/wpa_supplicant`**（wpa_conn.cpp:155 启动时 `-C /tmp/wpa_supplicant`，wpa_conn.cpp:172/201 探测时 `-p /tmp/wpa_supplicant`）。这意味着一个长期常驻的 wpa_supplicant 实例可被 `wpa_cli -i wlan0 -p /tmp/wpa_supplicant <cmd>` 在**不重启进程**的前提下重配置。这就是"优雅切 SSID 不破坏 T5"的物理基础。

---

## 1. Goals / Non-goals

### Goals
1. 新增独立可执行 `htc_wifi_app`，双平台（T32 `build/` + sim `build_sim/`）编译通过。
2. 4 条用户需求逐条可映射到代码路径（见 §5 映射表）。
3. **重连难点**：SSID 不同时优雅切网，**绝不 kill/re-spawn wpa_supplicant**，保持 T5 的 DbusProcess-oops 修复不被回归。
4. 需求③回写 MCU 严格"连接成功后才写"。
5. sim 侧以"编译通过 + 审计 + 纯逻辑单测 + grep 校验"为验收；真机 WiFi 回归留脚本（同 T5/T4 口径）。

### Non-goals
- **不**改 `src/hal/**`（PIC-owned）。
- **不**改 `htc_main_app` 的 WiFi 逻辑（剥离留后续 task；main_app 仍是 WiFi 的主运行体）。
- **不**实现常驻 daemon / 后台服务（本应用是"调一次连一次"的前台工具，呼应 dispatch §"无现有 WiFi daemon 可复用"）。
- **不**改 MCU.cpp 的写入语义/寄存器布局（仅调用既有 writeUPID/writeUPWD）。
- **不**做 NTP / mDNS / 上行（只覆盖连接+DHCP+回写）。
- 真机连接成功率/弱信号处理不在本任务回归口径（留脚本，人工验收）。

---

## 2. Impacted files（影响范围）

### 新增（implementer 创建）
| 文件 | 作用 |
|---|---|
| `src/app/wifi_app.cpp` | `htc_wifi_app` 的 `main()`：CLI 解析 + 编排（调用 Misc/MCU）。**不**自带 WiFi shell 逻辑，全部委托底层。 |
| `src/app/wifi_app_logic.h` / `wifi_app_logic.cpp` | 纯逻辑层：SSID 解析/比较/回写门控/退出码语义。**无系统调用**，便于 sim 单测。 |
| `src/app/wifi_reconnect.h` / `wifi_reconnect.cpp` | 优雅重连原语：`currentSSID(if)` / `reconnectSSID(if,ssid,pwd)`（封装 `iwgetid`/`wpa_cli`，**不动进程**）。sim 下桩返回空/false。 |
| `tests/test_wifi_app_logic.cpp` | 纯逻辑单测（SSID 比较决策、回写门控、CLI 退出码映射）。仅 sim 构建（`BUILD_FOR_SIMULATION` 子目录，同既有 tests/ 约定）。 |
| `script/regress_wifi_real.sh` | 真机回归脚本（手动跑），见 §9。 |
| `reviews/<date>-htc_wifi_app.md` | 按 CLAUDE.md 文档纪律留一条 migration 笔记。 |

### 修改（最小接入）
| 文件 | 改动 |
|---|---|
| `src/app/CMakeLists.txt` | 新增 `htc_wifi_app` 的 `add_executable` + 双平台 `target_link_libraries`（参照 `htc_daemon_app` 写法）+ `set_target_properties(... bin)`。 |
| `src/common/misc/Misc.h` / `Misc.cpp` | **新增** `static std::string currentSSID(if="wlan0")` 与 `static bool reconnectSSID(if,ssid,pwd)`（或更窄的 `static bool wpaSelectDifferent(...)`）。**不**改既有 connectWifi/startDHCP 行为。见 §6 取舍。 |

### 不改（硬约束）
- `src/hal/**`、`src/app/main_app.cpp`、`src/hardware/mcu/MCU.{h,cpp}`、`src/platform/tool/wpa_conn.cpp`（仅作行为参考）。

---

## 3. 应用结构（分层）

```
htc_wifi_app main (wifi_app.cpp)        ← CLI 解析、退出码、日志
        │ 委托
        ▼
wifi_app_logic (纯逻辑, 无 syscall)      ← 决策: 该不该重连? 回写门控? → 可单测
        │ 调用
        ▼
Misc::{isWifiDriverLoaded,isWifiConnected,currentSSID,reconnectSSID,connectWifi,startDHCP}
MCU::{readUPID,readUPWD,writeUPID,writeUPWD}   ← 复用既有
```

**分层动机**：把"是否需要切网"的决策（纯函数：当前SSID vs 目标SSID）从"如何切网"（系统调用）剥离，使 sim 侧能对决策层做真单测（T4 同款思路：可测部分尽量纯逻辑）。

---

## 4. CLI 设计

```
htc_wifi_app [--ssid <SSID> --pwd <PASS>] [--no-dhcp] [--write-mcu] [--if <wlan0>] [-v]
  默认(无 --ssid):          从 MCU 读 readUPID/readUPWD 作为目标 (需求④)
  --ssid + --pwd:           用参数 SSID/密码 (需求②)
  --write-mcu:              连接成功后回写 writeUPID/writeUPWD (需求③); 不带则只连不写
  --no-dhcp:                跳过 startDHCP (默认开 DHCP, 需求①末"连上后默认 DHCP")
  --if <name>:              指定 wlan 接口名, 默认 wlan0
  -v:                       详细日志
  --help:                   用法

退出码 (稳定契约, 供脚本/daemon 判定):
  0  成功 (已连到目标 SSID 或新连到目标, DHCP 按需完成)
  2  driver 加载失败
  3  连接失败 (含优雅重连失败)
  4  DHCP 失败 (--no-dhcp 不触发)
  5  --write-mcu 指定但连接未成功而跳过写 (不写, 视为可恢复, 退出非 0 提示)
  6  参数错误
```

退出码设计取舍：区分"连接失败(3)"与"连接成功但回写被门控(5)"，让上游能区分"网没通"与"网通了但 MCU 没写"，便于回归脚本断言。

---

## 5. 4 条用户需求 → 代码映射（逐条）

| 需求 | 映射到代码 | 依据/核实 |
|---|---|---|
| ① driver 幂等加载 | `Misc::isWifiDriverLoaded()` 为假才走 `connectWifi` 内部 insmod 分支；`connectWifi` 已做 re-check 吸收 EEXIST。app 层**不**自调 insmod，仅靠 connectWifi 的内建幂等。`--help` 路径外，主流程第一步打日志 `driver_loaded=<bool>`。 | `Misc.cpp:434-454` |
| ② 参数 SSID 连接，不同则断开重连，连上默认 DHCP | 见 §6 重连难点。简：`currentSSID()` 读现状 → 与目标不同 → `reconnectSSID()`(wpa_cli 优雅切，**不 kill**) → 再 `isWifiConnected()` 校验 → `startDHCP()`(除非 `--no-dhcp`)。相同则复用，直接进 DHCP。 | 需求② + §6 |
| ③ 连接成功后回写 MCU（必须先成功才写） | `--write-mcu` 且 `isWifiConnected()==true` 且 `currentSSID()==目标` → `MCU::writeUPID/writeUPWD`；任一不满足→跳过写并退出码 5。**写前再读一次 `currentSSID()` 与目标比对**，防"连上但落到别的 SSID"误写。 | `MCU.cpp:554/570`；空串 write 返 false |
| ④ 无参数用 MCU 的 SSID/密码连接 + DHCP | 无 `--ssid` → `MCU::getInstance()->readUPID()`(SSID) + `readUPWD()`(密码)；二者非空才继续，否则退出码 6 并提示"MCU 无可用凭据"。 | `MCU.cpp:492/515` |

**Common.h UPWD 坑规避**：本应用**不**直接读 ini，凭据来源只有"CLI 参数"或"MCU 寄存器"，天然绕开 `INI_KEY_UPWD="PWD"` 的命名坑。若后续需读 ini，必须用宏 `INI_KEY_UPWD` 而非字面量，并在 reviews 笔记里点名（防新人踩）。

---

## 6. 重连难点方案（核心）

### 6.1 张力复述
- 需求②要"切到不同 SSID"。
- 现有 `connectWifi` 只判连没连（`Misc.cpp:429`），不判 SSID；且其 RTL 分支会**重 spawn wpa_supplicant**（经 `wpa_conn` 二进制，`Misc.cpp:459`）。
- T5（`main_app.cpp:889-895`）刻意保持 wpa_supplicant 常驻，根治 DbusProcess oops / ctrl_iface 冲突。
- ⇒ 若用 `connectWifi` 重连不同 SSID，等于 kill+respawn wpa_supplicant，**直接回归 T5 修复的 bug**。

### 6.2 方案：新增优雅重连原语，绝不重启进程

**在 `Misc` 新增两个静态方法（非破坏性扩展）：**

```cpp
// Misc.h (新增, 不动既有声明)
static std::string currentSSID(const std::string &ifname="wlan0");
static bool reconnectSSID(const std::string &ifname,
                          const std::string &ssid,
                          const std::string &password);
```

**`currentSSID(ifname)` 实现**（读当前关联 SSID，SIM-safe 桩返回空）：
- 首选 `iwgetid -r <if>`（busybox 自带，无副作用，输出纯 SSID）。
- 兜底（无 iwgetid）：`wpa_cli -i <if> -p /tmp/wpa_supplicant status`，grep `^ssid=`，取值。
- 通过 `Misc::popencall`（既有，`Misc.cpp:542`）取 stdout，trim 换行。
- `BUILD_FOR_SIMULATION` 下直接 `return ""` 并打 `[SIM]` 日志（无 wpa_supplicant/iwgetid）。

**`reconnectSSID(ifname, ssid, password)` 实现**（**不 kill 进程**，经既有 ctrl_iface 重配置）：
1. 用 `wpa_passphrase` 生成新 network 块写到临时 conf（沿用 wpa_conn.cpp:103-115 的 `wpa_passphrase` 习惯）。
2. `wpa_cli -i <if> -p /tmp/wpa_supplicant reconfigure`（让常驻 supplicant 重读 conf，**进程不动**）。
   - 兜底链：若 `reconfigure` 不生效，再 `wpa_cli ... add_network` + `set_network ... ssid/psk` + `enable_network` + `select_network`（标准的 wpa_cli 在线切网序列，全程不重启进程）。
3. 轮询 `wpa_cli ... status | grep wpa_state=COMPLETED` 直到超时（复用 wpa_conn.cpp:166-198 的轮询范式）。
4. 返回 `isWifiConnected()`（既有，IP+gateway 双判）作为最终成功判据。

**为什么这是对的 / 为什么不破坏 T5：**
- 全程**不**触碰 wpa_supplicant 进程的生存期（不 kill、不 respawn、不删 `/tmp/wpa_supplicant`）。T5 的 oops 触发条件（ctrl_iface 冲突 + respawn 竞态）根本不被进入。
- `wpa_cli` 连的是**既有** ctrl_iface socket（`-p /tmp/wpa_supplicant`），与 wpa_conn.cpp 探测时用的是**同一个 socket**，无新增冲突源。
- 与 T5 的"state-driven re-entry"哲学一致：我们靠 `isWifiConnected()`/`currentSSID()` 探测状态，而非靠"杀进程重置"。

### 6.3 既有 connectWifi 的角色（不动）
- **首次连接 / driver 未加载 / 完全没连上** → 仍走 `Misc::connectWifi`（它负责 insmod 幂等 + 首连 spawn supplicant，这是它的职责）。
- **已连但 SSID 不同** → 走新增 `reconnectSSID`（优雅切）。
- 决策由 `wifi_app_logic` 的纯函数做：`Decision decide(const State&, const Target&)`，可单测（见 §8）。

### 6.4 备选方案与取舍（记录决策）
| 备选 | 取舍 |
|---|---|
| A. 给 `connectWifi` 加 `force` 参数，true 时跳过短路直接重连 | **否决**：connectWifi 的 RTL 分支会 respawn wpa_supplicant，force=true 仍回归 T5 bug；且改变既有 API 语义，影响 main_app（破坏"不改 main_app WiFi 逻辑"约束的边界感）。 |
| B. app 层直接 `killall wpa_supplicant` 再 connectWifi | **否决**：直接回归 T5。 |
| C. 新增 `currentSSID` + `reconnectSSID`（本方案） | **采纳**：非破坏性扩展，决策/执行分层可单测，与 T5 哲学一致。 |

---

## 7. CMake 接入

### 7.1 `src/app/CMakeLists.txt`（参照 `htc_daemon_app` 写法，L51-53/L64-93/L187-223）
新增（插在既有 add_executable 之后、双平台 link 分支各加一段）：

```cmake
file(GLOB WIFI_APP_SOURCES "${SOURCES}/app/wifi_app.cpp")
add_executable(htc_wifi_app ${WIFI_APP_SOURCES})

if(BUILD_FOR_SIMULATION)
  target_link_libraries(htc_wifi_app PRIVATE
      common_misc            # connectWifi/isWifi*/startDHCP/currentSSID/reconnectSSID
      mcu                    # readUPID/readUPWD/writeUPID/writeUPWD (PUBLIC 带 logger)
      logger common_utils_base64 crc16 jsoncpp sdk_stub
      pthread rt gcc stdc++)
else()
  target_link_libraries(htc_wifi_app PRIVATE
      common_misc mcu logger common_utils_base64 crc16 jsoncpp
      system_call            # Misc 内部 system_call_init/system_call/popen_call
      pthread rt gcc stdc++)
endif()

set_target_properties(htc_wifi_app PROPERTIES
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
```

- `common_misc` 已封装 `system_call`/`sdk_stub`（见 `src/common/misc/CMakeLists.txt`），故 app 不必直接 link `system_call` 也能跑；为显式化（与 daemon_app 一致）T32 分支保留 `system_call`。
- `wifi_app_logic.cpp` / `wifi_reconnect.cpp` 的源直接并入 `wifi_app.cpp` 同目录的 GLOB，或显式列在 `WIFI_APP_SOURCES`（implementer 取后者更稳，避免 GLOB 漏）。
- include 目录：`${CMAKE_CURRENT_SOURCE_DIR}/../common`、`../hardware/mcu`、`../common/utils/string`（StringConvert.h，connectWifi 间接用），与 daemon_app 的 `include_directories` 复用。

### 7.2 tests（仅 sim）
在 `tests/CMakeLists.txt`（既有，仅 `BUILD_FOR_SIMULATION` 下 `add_subdirectory`，见 `CMakeLists.txt:101-103`）追加：
```cmake
add_executable(test_wifi_app_logic test_wifi_app_logic.cpp)
target_link_libraries(test_wifi_app_logic PRIVATE
    wifi_app_logic        # 若拆成独立 lib; 否则直接编译 .cpp
    logger pthread rt gcc stdc++)
```
取舍：若 `wifi_app_logic` 拆成独立静态 lib（`wifi_app_logic` target），app 与 test 都能 link，最干净；implementer 据此在 app/CMakeLists 加 `add_library(wifi_app_logic STATIC ...)`。**推荐拆 lib**（便于单测，T4 同款理由）。

---

## 8. 测试计划

### 8.1 sim 侧（PC，CI 口径）
1. **双平台编译**：
   - `cmake -DBUILD_FOR_SIMULATION=ON -B build_sim -S . && cmake --build build_sim -j$(nproc) --target htc_wifi_app`
   - `cmake -DCMAKE_TOOLCHAIN_FILE=toolchain.cmake -B build -S . && cmake --build build -j$(nproc) --target htc_wifi_app`
2. **纯逻辑单测**（`test_wifi_app_logic`，仅 sim 构建）：
   - `decide(已连且SSID同, 目标) == REUSE`（短路，不切）
   - `decide(已连但SSID不同, 目标) == RECONNECT`（优雅切）
   - `decide(未连, 目标) == FRESH_CONNECT`（走 connectWifi）
   - `decide(无凭据) == ABORT(6)`
   - 回写门控：`isWifiConnected=false` → 永不调 writeUPID；`currentSSID≠目标` → 永不写。
   - 退出码映射：每种 Decision → 对应退出码。
3. **grep 审计校验**（C1/C2 证据）：
   - `grep -rn "killall\|kill.*wpa_supplicant\|rmmod.*8189fs\|pkill" src/app/wifi_reconnect.cpp src/common/misc/Misc.cpp` → **必须空**（证明不破坏 T5）。
   - `grep -n "wpa_cli.*-p /tmp/wpa_supplicant\|iwgetid" src/common/misc/Misc.cpp` → 命中（证明走优雅重连）。
   - `grep -rn "connectWifi\|startDHCP\|currentSSID\|reconnectSSID" src/app/wifi_app.cpp` → 命中（委托底层，不自造 shell）。
   - `grep -n "writeUPID\|writeUPWD" src/app/wifi_app.cpp` → 命中点**仅在 isWifiConnected+currentSSID 双校验之后**（人工/审计确认顺序）。
4. **sim 运行冒烟**（无真 WiFi，仅验证不崩 + 退出码语义）：
   - `./build_sim/bin/htc_wifi_app --help` → 退出 0。
   - `./build_sim/bin/htc_wifi_app`（无 MCU 真数据）→ 退出 6（凭据不可用）或安全降级，不 segfault。

### 8.2 真机侧（留脚本，人工验收；同 T5 口径，不进 CI）
`script/regress_wifi_real.sh`（T32 上手跑）：
- 用例 A：未连 → `htc_wifi_app --ssid S1 --pwd P1` → 退出 0，`iwgetid -r wlan0 == S1`，有 IP。
- 用例 B：已连 S1 → `htc_wifi_app --ssid S2 --pwd P2` → 退出 0，SSID 切到 S2；**关键回归断言**：`pgrep wpa_supplicant` 的 PID 在 B 前后**不变**（证明优雅切，未 respawn）。
- 用例 C：连上 S2 → `htc_wifi_app --ssid S2 --pwd P2 --write-mcu` → MCU readUPID==S2 / readUPWD==P2。
- 用例 D：连接失败（错密码）→ `--write-mcu` → 退出码 3 或 5，且 MCU 未被写（readUPID 不变）。
- 用例 E：`htc_wifi_app`（无参，从 MCU 读）→ 用 MCU 现存 SSID 连上。
- 用例 F（T5 回归）：连 A→B→A 多轮，每轮检查 `pgrep wpa_supplicant` PID 恒定，dmesg 无 `DbusProcess`/`ctrl_iface` oops。

---

## 9. 风险与回滚

| key | severity | 描述 | 缓解 |
|---|---|---|---|
| T6-wpa-cli-not-present | medium | T32 `/system/bin/wifi/` 下若缺 `wpa_cli` 或 `iwgetid`，优雅重连失败。 | 既有 `wpa_conn.cpp` 已用 `wpa_cli`（L172/201），证明镜像里有；`iwgetid` 走 busybox 兜底 + `wpa_cli status` 双路径。脚本用例 B 前先 `ls /system/bin/wifi/`。 |
| T6-reconfigure-stale | medium | `wpa_cli reconfigure` 在某些 supplicant 版本不立刻生效。 | 兜底链：reconfigure → add_network/set_network/select_network 手动序列。 |
| T6-real-wifi-unreproducible | high | 真机弱信号/路由器侧问题在 PC 完全不可复现。 | sim 只验编译/逻辑/审计；真机留脚本由用户人工跑，不卡 CI（同 T5/T4）。 |
| T5-oops-regression | **high** | 若实现不慎 kill/respawn wpa_supplicant，回归 T5 修复。 | §8 grep 审计硬校验（`killall/kill wpa_supplicant/rmmod` 必须空）；脚本用例 B/F 断言 PID 不变。 |
| T6-mcu-write-gate | high | 连接未成功却误写 MCU 会污染寄存器。 | 回写在 `isWifiConnected() && currentSSID()==target` 双 true 之后；退出码 5 专用于"连成功但门控跳过"；单测覆盖门控。 |
| T6-sim-iic-bypass | medium | sim 下 readUPID/readUPWD 走 IIC bypass 返回空（T4 已知），无参模式 sim 下不可真正测。 | sim 下无参模式预期退出 6；真机用例 E 覆盖。 |
| T6-upwd-ini-pitfall | low | 后续若有人给本 app 加 ini 读取，易用字面量 "UPWD"。 | reviews 笔记点名；本任务不读 ini，天然规避。 |
| T6-dual-platform-link | medium | link 漏 `system_call` 或 sdk_stub 导致一边编不过。 | 双平台 target_link 分支显式列（参照 daemon_app）；§8 编译校验两边都跑。 |

**回滚点**：本任务**纯新增 + Misc 非破坏性新增方法**，不改既有行为。回滚 = 删除新增文件 + 还原 `src/app/CMakeLists.txt` + 移除 Misc 新增方法。不影响 main_app/daemon_app 运行。

---

## 10. 验收标准（Acceptance）

1. **双平台编译通过**：`htc_wifi_app` 在 `build/bin/` 与 `build_sim/bin/` 均生成。
2. **grep 审计全绿**：§8.1 第 3 条所有 grep 断言成立（尤其"不 kill wpa_supplicant"为空集）。
3. **sim 单测通过**：`test_wifi_app_logic` 全绿，覆盖 §8.1 第 2 条所有 Decision/门控/退出码 case。
4. **sim 冒烟**：`--help` 退出 0；无参模式不 segfault 且退出码 ∈ {6}（MCU 空时）。
5. **真机脚本用例 A-F 由用户人工确认**（留 script/regress_wifi_real.sh，不卡 CI）；关键：用例 B/F 的 wpa_supplicant PID 不变（T5 不回归）。
6. **需求映射可追溯**：§5 表每条需求在代码中有对应调用点（grep 可验）。
7. **不碰禁区**：`git diff --stat` 不含 `src/hal/`、`src/app/main_app.cpp`、`src/hardware/mcu/MCU.{h,cpp}`、`src/platform/tool/wpa_conn.cpp`。

---

## 11. 交付物指针（给 implementer/tester/reviewer）
- 本计划：`artifacts/T6-planner-full.md`
- report card：`artifacts/T6-planner-report.md`
- dispatch（PM 事实）：`artifacts/T6-planner-dispatch.md`
- 同范本：`artifacts/T4-planner-full.md`（数据驱动/单测/双平台口径）。
