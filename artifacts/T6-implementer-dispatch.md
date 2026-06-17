---
task_id: T6
node: implementer (dispatch)
flow: feature
created: 2026-06-17
---

# T6 — implementer dispatch

## 输入（唯一真相指针，读全文）
- 方案全文：`artifacts/T6-planner-full.md`（11 节：事实核验/影响范围/分层/CLI/4需求映射/重连难点/CMake/测试/风险/验收）
- PM 事实底稿：`artifacts/T6-planner-dispatch.md`
- 范本（双平台+单测口径）：`artifacts/T4-planner-full.md`、`src/app/CMakeLists.txt`(htc_daemon_app 写法)

## 你要做
按 planner-full 实现 `htc_wifi_app`，使 §5 的 4 条用户需求逐条可映射、双平台编译通过、sim 可单测。

### 文件清单（planner §2）
新增：`src/app/wifi_app.cpp`、`src/app/wifi_app_logic.{h,cpp}`(纯逻辑,无 syscall,可单测)、
优雅重连原语 `currentSSID()/reconnectSSID()`、`tests/test_wifi_app_logic.cpp`、
`script/regress_wifi_real.sh`、`reviews/<date>-htc_wifi_app.md`。
修改：`src/app/CMakeLists.txt`(新增 htc_wifi_app, 参照 htc_daemon_app)。

### 一处需你拍板的取舍（planner §2 与 §6.2 措辞略有重叠）
`currentSSID()/reconnectSSID()` 放哪：放共享的 `Misc`(§6.2) 还是独立新模块 `wifi_reconnect.{h,cpp}`(§2)。
**自行择一并保持自洽**：若放新模块，则把 planner-report §verification 里指向 `Misc.cpp` 的 grep 同步改为新模块路径。
**不可妥协的不变量**：重连**绝不 kill/respawn wpa_supplicant、不删 /tmp/wpa_supplicant、不 rmmod**，
全程经既有 ctrl_iface socket(`wpa_cli -i <if> -p /tmp/wpa_supplicant reconfigure` 或 add_network/select_network 序列)优雅切网。
（理由：connectWifi 的 RTL 分支会重 spawn wpa_supplicant = 回归 T5 的 DbusProcess oops 修复。）

## 硬约束
- 不改 `src/hal/**`、不改 `src/app/main_app.cpp`、不改 `src/hardware/mcu/MCU.{h,cpp}`、不改 `src/platform/tool/wpa_conn.cpp`。
- 双平台编译必须过：`cmake --build build_sim -j$(nproc) --target htc_wifi_app` 与
  `cmake --build build -j$(nproc) --target htc_wifi_app`。
- 回写 MCU(需求③)严格门控在 `isWifiConnected()==true && currentSSID()==目标` 之后，且**写前再读一次 currentSSID 比对**；
  不满足→跳过写、退出码 5。
- 凭据来源只有 CLI 参数 或 MCU 寄存器(readUPID/readUPWD)，**不读 ini**（天然绕开 `INI_KEY_UPWD="PWD"` 坑）。
- T32 工具链已在 `toolchain/`，`build/` 已配好；sim 用 `build_sim/`。别用其他 build 目录名。
- sim 下无真实 wpa_supplicant/iwgetid/I2C 实数据 → `currentSSID()` 桩返回空、`reconnectSSID()` 返回 false、
  无参模式预期退出码 6。别让 sim 路径 segfault。
- 未经许可不 git commit。

## 退出码契约（planner §4，稳定契约，别改）
0 成功 / 2 driver 加载失败 / 3 连接失败 / 4 DHCP 失败 / 5 连成功但回写被门控跳过 / 6 参数/凭据错误。

## 必须自跑（写到 evidence）
1. 双平台编译两段都 exit 0（产出 build/bin/htc_wifi_app 与 build_sim/bin/htc_wifi_app）。
2. sim 纯逻辑单测 `test_wifi_app_logic` 全绿（覆盖 Decision: REUSE/RECONNECT/FRESH_CONNECT/ABORT + 回写门控 + 退出码映射）。
3. grep 审计（planner §8.1-3）：
   - `grep -rn "killall\|kill.*wpa_supplicant\|rmmod.*8189fs\|pkill" src/app/ <新模块>` → **必须空**。
   - 优雅重连 grep 命中；`writeUPID/writeUPWD` 仅在双校验之后。
4. sim 冒烟：`./build_sim/bin/htc_wifi_app --help` 退出 0；无参不 segfault。

## 交付物（写到 artifacts/）
1. `artifacts/T6-implementer-evidence.md` —— 命令+输出摘要（指针式，贴关键 grep/编译尾部/单测结果，不堆全量日志）。
2. `artifacts/T6-implementer-report.md` —— report-card@v1（frontmatter 照 `artifacts/T4-implementer-report.md`）。
   deliverables 放新增/修改文件路径指针；verification.commands 列你实际跑过的命令；evidence_ref 指向 evidence。
   state_delta 只在确实改了状态时填（implementer 通常 set_task_status:{} + 可能 add_risk）。

完成后只返回 report-card frontmatter + 一句话结论 + 实际新增/修改文件清单 + 三个交付物路径。
