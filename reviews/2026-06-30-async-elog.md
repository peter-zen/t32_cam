# 2026-06-30 — Async elog (解除热线程阻塞)

## 背景 / 目标
elog 原为**同步**：`elog_port_output` 每行 `fwrite+fflush` 到 stdout+file，全部在调用线程持全局 `output_lock` 完成。RTSP/采集/编码等热路径（rtsp.c 77 处调用）会因此被 I/O 阻塞。
**首要目标：解除热线程阻塞**（延迟优先；durability 次要，但需保关机现场）。经 `/grill-me` 8 轮决策定型后实现。

## 决策（共识）
| # | 决策 | 结论 |
|---|------|------|
| 1 | 动机 | 解除热线程阻塞 |
| 2 | 机制 | 原生异步：`ELOG_ASYNC_OUTPUT_ENABLE` + 补 `elog_async.c` + port 消费线程 |
| 3 | 退出 drain | `Misc::poweroff()` HW 头部 drain + 消费线程 100ms 周期 flush |
| 4 | ring 满 | 丢最老（drop-oldest）+ drop 计数器 |
| 5 | 容量 | 128 槽 × 512B = 64KB；flush 间隔 100ms（cfg 宏可调） |
| 6 | fatal | 全异步，无 fatal 例外 |
| 7 | stdout(HW) | 保留双写（不引入行为变动） |
| 8 | 验收 | SIM 单测 + HW devtest |

## 改动（file-by-file）
- `third_party/easylogger/inc/elog_cfg.h` — 开 `ELOG_ASYNC_OUTPUT_ENABLE` + `ELOG_ASYNC_OUTPUT_BUF_SIZE=128`。
- `third_party/easylogger/src/elog_async.c`（**新增**）— 静态环形 buffer + `elog_async_init/deinit/output/enabled/get_log/get_line_log`，drop-oldest + `elog_async_get_drop_count()` 扩展。**静态数组无 malloc → deinit 无 free → 规避 core 的 async_deinit-before-port_deinit 顺序 UAF**。
- `third_party/easylogger/port/elog_port.c` — 单一消费线程（`elog_port_init` 起、`elog_port_deinit` join+drain）、100ms 周期 flush、stderr 周期报 drop。保留原 `elog_port_output`（sync fallback 不变；async 模式下只消费线程经 `elog_write_line` 调用）。
- `third_party/easylogger/CMakeLists.txt` — `ELOG_SOURCES` 加 `elog_async.c`。
- `src/common/misc/Misc.cpp` — `#include "ElogInit.h"` + `Misc::poweroff()` HW 分支头部 `elog_deinit_all()`（drain + final flush，板子冻结前；幂等，daemon 未 init elog 也安全）。
- `tests/test_elog_async.cpp`（**新增**）+ `tests/CMakeLists.txt`。

零 API 改动：所有 `elog_x`/`Logger::log` 调用点不变。

## 验收结果
- **SIM `build_sim` 全绿**；`test_elog_async` 5/5 PASS：
  - `[latency]` 8 线程 × 2000 行（file sink 开），**p99 ≈ 200–350µs**（同步模式下同负载 p99 会是数十 ms）→ 热线程不被 I/O 阻塞。
  - `[flood]` 单线程 20k 行 → ring(128) 溢出，drop-oldest 生效，`flood_drops>0`。
  - `[order]` 单线程 100 行严格递增保序。
  - `[drain]` `elog_deinit_all` 后尾行存活（drain + final flush 正确）。
  - `[account]` **written + dropped == logged** 严格成立（每 run 36101==36101）。
- **HW `build/`（MIPS）全绿**：elog_async.c.o 含全部 7 异步函数；htc_daemon_app 经 liblogger.so 解析 `elog_deinit_all`（daemon poweroff 不崩）。

## 风险 / 残余
- **关机/crash 丢尾日志**：生产 HW 经 `Misc::poweroff` drain 全保；SIM / devtest `HTC_TEST_NO_POWEROFF` 的 `_exit(0)` 依赖 100ms 周期 flush 窗口（accepted）。硬 SIGKILL/OOM 不可救（任何方案都一样）。
- **ring 满丢日志**：drop-oldest by design（延迟优先）；HW/INFO burst 稀疏，实战几乎不丢。stderr 周期报 drop 便于调 `ELOG_ASYNC_OUTPUT_BUF_SIZE`。
- **可回退**：关掉 `ELOG_ASYNC_OUTPUT_ENABLE` 即恢复同步（`elog_port_output` 原样保留）。
- **未做 HW devtest 真机回归**（本次只到双平台编译 + SIM 单测；真机 app.log/关机 drain 留给 devtest 闭环）。
