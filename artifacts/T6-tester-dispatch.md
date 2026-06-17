---
task_id: T6
node: tester (dispatch)
flow: feature
created: 2026-06-17
---

# T6 — tester dispatch

## 输入（指针）
- 实现产物：`artifacts/T6-implementer-evidence.md`、`artifacts/T6-implementer-report.md`
- 方案：`artifacts/T6-planner-full.md`（§8 测试计划、§10 验收、§4 退出码契约）
- dispatch 范式：`artifacts/T6-implementer-dispatch.md`

## 你的职责（独立验证，不轻信 implementer 自述）
你是独立关卡。自己重跑命令、自己 grep、自己读代码确认。implementer 写的单测你要审其**有效性**（是否真的断言了被测行为，还是空壳/永真）。

## 必跑项（全过才算 pass）
1. **双平台编译独立复跑**（exit 0）：
   - `cmake --build build_sim -j$(nproc) --target htc_wifi_app`
   - `cmake --build build -j$(nproc) --target htc_wifi_app`
   确认产物存在：`build_sim/bin/htc_wifi_app`、`build/bin/htc_wifi_app`。
2. **纯逻辑单测**：编译并运行 `test_wifi_app_logic`，必须全 PASS。
   **审查有效性**：用例是否覆盖 planner §8.1-2 的全部决策分支——
   REUSE(已连且 SSID 同→不切) / RECONNECT(已连但 SSID 不同→优雅切) / FRESH_CONNECT(未连→走 connectWifi) /
   ABORT(无凭据→退出 6)；回写门控(isWifiConnected=false 或 currentSSID≠目标→**永不** writeUPID/writeUPWD)；
   退出码映射(每 Decision→对应码)。若发现单测是空壳/永真/没真正调被测逻辑，**判 fail 并打回 implementer**。
3. **grep 审计**（T5 不回归的硬证据）：
   - `grep -rn "killall\|kill.*wpa_supplicant\|rmmod.*8189fs\|pkill" src/app/ src/app/wifi_reconnect.cpp src/common/misc/Misc.cpp` → **必须空**。
   - `grep -n "currentSSID\|reconnectSSID\|wpa_cli.*-p /tmp/wpa_supplicant\|iwgetid" src/app/wifi_reconnect.cpp src/common/misc/Misc.cpp` → 命中优雅重连路径。
   - `grep -n "writeUPID\|writeUPWD\|connectWifi\|startDHCP" src/app/wifi_app.cpp` → write 调用点必须在 `isWifiConnected && currentSSID==target` 双校验之后（读代码确认顺序，不只看 grep 行号）。
4. **禁区 diff 为空**：
   `git diff --stat main -- src/hal src/app/main_app.cpp src/hardware/mcu/MCU.h src/hardware/mcu/MCU.cpp src/platform/tool/wpa_conn.cpp` → **必须空**（本任务不改这些）。
5. **sim 冒烟**：
   - `./build_sim/bin/htc_wifi_app --help` → exit 0。
   - `./build_sim/bin/htc_wifi_app`（无参，sim 下 MCU 无实数据）→ 不 segfault，退出码合理(预期 6)。
   - `./build_sim/bin/htc_wifi_app --ssid foo --pwd bar`（sim 下重连桩 false）→ 退出码 ∈ {3} 且不 segfault。

## 判定
- 全过 → status: success，next: reviewer。
- 任一不过（编译失败/单测失败或空壳/审计 grep 非空/禁区被改/冒烟 segfault）→ status: failed，next: implementer(loopback)，在 report 里列**最小复现命令 + 期望 vs 实际**。

## 交付物（写到 artifacts/）
1. `artifacts/T6-tester-evidence.md`（命令 + 输出摘要，指针式；附单测输出尾部与各 grep 结果）。
2. `artifacts/T6-tester-report.md`（report-card@v1，frontmatter 照 `artifacts/T4-tester-report.md`）。
   failed 时 state_delta 标 tests_failed 触发 loopback。

完成后只返回：report-card frontmatter + pass/fail 一句话结论 + 最关键的证据（如双平台 build exit code、单测 PASS 数、禁区 diff 是否空）+ 两个交付物路径。