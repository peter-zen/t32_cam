---
task_id: T6
node: reviewer (dispatch)
flow: feature
created: 2026-06-17
---

# T6 — reviewer dispatch

## 输入（指针）
- 实现产物：`artifacts/T6-implementer-evidence.md`、`artifacts/T6-implementer-report.md`
- 测试产物：`artifacts/T6-tester-evidence.md`、`artifacts/T6-tester-report.md`（PASS；一处 sim exit 2 偏差已定性为物理限制）
- 方案：`artifacts/T6-planner-full.md`（§4 退出码契约、§6 重连难点、§10 验收）

## 你的职责（末端深度审查，C2 前）
你是流程最后一关。审 **correctness / 安全 / 边界 / 回归风险 / 测试充分性**，不只看"能编过"。
重点（按优先级）：

1. **T5 不回归（最高优先）**：读 `src/app/wifi_reconnect.cpp` 与 `wifi_app.cpp`，确认重连路径**绝不** kill/respawn wpa_supplicant、不删 `/tmp/wpa_supplicant`、不 rmmod；优雅切网用的是 `wpa_cli reconfigure` / `add_network/select_network` 序列，经既有 ctrl_iface socket。这是本任务成败的红线。

2. **MCU 回写门控（高）**：确认 `writeUPID/writeUPWD` 仅在 `isWifiConnected()==true && currentSSID()==target` 双真之后调用，且**写前再读一次 currentSSID 比对**。检查有无 TOCTOU/早返回路径能绕过门控误写寄存器。

3. **退出码契约（中）**：读 main 的每条返回路径，确认 0/2/3/4/5/6 语义与 planner §4 一致，无裸 `return 1`、无错码遗漏。

4. **T32 工具链兼容（高，已知坑）**：T32 uclibc **缺 `std::to_string` / `std::stoi`**（sim 能过、T32 链接失败）。grep 确认新增 .cpp 用的是 `snprintf`/`strtol` 等 C 风格，无 `std::to_string`/`std::stoi`/`std::stoul`。T32 build exit 0 已是强证据，但仍要 grep 复核。

5. **边界与安全**：CLI 参数解析（空 SSID/空密码/超长输入/--pwd 含特殊字符是否被 shell 注入——`reconnectSSID` 是否走 `system()`/`popen()` 拼接字符串？若是，SSID/密码含 `;`/反引号/`$()` 是否注入？这关系到写 wpa_passphrase conf 的安全性）。区分"shell 拼接"与"参数化"，给结论。

6. **分层与可测性（中）**：`wifi_app_logic` 是否真无 syscall（可单测的前提）；Decision 枚举与退出码映射是否清晰。

7. **禁区未触（低，硬约束）**：`git diff --stat` 不含 `src/hal/`、`main_app.cpp`、`MCU.{h,cpp}`、`wpa_conn.cpp`（tester 已验空，复核）。

8. **真机回归脚本**：`script/regress_wifi_real.sh` 用例 A-F 是否可执行、关键断言（用例 B/F 的 wpa_supplicant PID 跨 SSID 切换恒定）是否到位。

## 判定
- 无阻断问题 → status: passed（next: :end）。
- 有阻断问题（T5 回归风险 / MCU 误写 / shell 注入 / T32 链接坑 / 退出码契约破裂）→ status: changes_requested，在 report 里逐条给位置 + 修复方向（不直接改代码）。

## 交付物（写到 artifacts/）
1. `artifacts/T6-reviewer-evidence.md`（grep 结果 + 关键代码摘录指针，不堆全文）。
2. `artifacts/T6-reviewer-report.md`（report-card@v1，照 `artifacts/T4-reviewer-report.md`）。

完成后只返回：report-card frontmatter + passed/changes_requested 一句话结论 + 发现的阻断项（若有，逐条位置+方向）+ 两个交付物路径。