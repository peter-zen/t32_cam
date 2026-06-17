---
task_id: T4
node: planner (dispatch)
flow: feature
created: 2026-06-16
---

# T4 — planner dispatch

## 功能一句话
编写独立测试程序，逐个调用 `src/hardware/mcu/MCU.h` 的全部公开 API，
输出「调了哪个 API → 返回什么 → 正常/异常」。

## 已确认范围（用户拍板）
- **平台**：仅 T32 真机（sim 下 I2C 被绕过、读返回 0，验证数据无意义；但**双平台必须能编译**）。
- **顺序**：Phase 1 先穷举所有**读** API；Phase 2 再遍历所有**写** API，
  **每个写 API 写完必须读回验证**（调对应 read 比对）。

## PM 已查清的事实（planner 不必重做发现，直接用）
- `MCU.h` 公开 API 约 120 个：读 ~90 + 写 ~30。
- `MCU::readAllTestData()`（`MCU.cpp:2318-2412`）是现有最接近物，但：
  ① 全仓零调用者（死代码）；② 只覆盖 ~45 个读，**所有写都没测**，还漏一批读
  （RMID/RMType/Event*/TDS_*/TIMER_1~5/DEVICE_NAME/UPID…）；③ 无正常/异常判定
  （I2C 读失败时各 `read*` 统一 `return 0`，失败与真实 0 无法区分）；④ 返回 void、无结构化结果。
- MCU 经 I2C 设备 `/dev/hc32l13x`（`MCU.cpp:13`）；读返回约定见 MCU.cpp
  （`iic->read(...) <= 0` 视为失败 → return 0；部分返回 -1）。
- 已有测试基建：`tests/test_mcu_service.cpp`、`src/app/snap_test.cpp`；CMake 接入方式见
  tests 与 src/app 的 CMakeLists。
- 参考文档：`doc/knowledge/refs/mcu-api-and-register-inventory.md`（29KB，寄存器清单，
  **应挖掘其中 read/write 配对与期望取值范围**）。

## 硬约束
- **不动被测对象**：`src/hardware/mcu/MCU.{h,cpp}` 是 API under test，**只调用不改**。
- `src/hal/**` PIC-owned，本次不应触碰。
- 双平台编译（T32 build/ + sim build_sim）必须通过（CLAUDE.md 硬性）。
- 未经许可不 git commit。

## 验收
- 程序枚举 MCU.h 全部 API；每个 API 输出 调用→结果→正常/异常 + 汇总(总数/通过/失败/待人工)。
- 读阶段全覆盖；写阶段写后读回验证。
- 真机可跑；sim 可编译（数据为 0 属预期）。
- CMake 接入，产出独立可执行。

## planner 产出（report card @v1）
- 测试程序设计：文件位置、数据驱动枚举结构、读阶段 + 写阶段(读回)流程、
  输出格式样例、**异常判定策略**（0/-1/越界如何归类）、CMake 接入、调用方式与写阶段安全门控、风险与缓解。
- deliverables / verification commands / state_delta。
