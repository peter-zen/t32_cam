---
contract: report
contract_version: "1"
task_id: T4
node: planner
flow: feature
status: success
summary: |
  T4 产出独立测试程序 htc_mcu_api_test，逐个调用 MCU.h 全部 public API 并分类判定。
  方案：数据驱动（按返回类型分组：int/uint/string/tm/bool/tool），表驱动枚举每个 API 的
  分类/期望范围/read-back 配对。异常判定用四级状态 OK/REVIEW/FAIL/SKIP 解决「read I2C 失败
  与真实 0 值都返回 0 无法区分」的核心难题（依赖 inventory §3 的取值范围做 range check）。
  Phase 1 穷举读，Phase 2 遍历写且每个 write 后调对应 read 比对（写回验证）。
  写阶段默认关闭（破坏性），需 --write 显式开启 + 醒目警告 + 安全测试值。
  文件放 tools/mcu_api_test.cpp（独立可执行，非 tests/ 单元测试范畴），CMake 两平台都构建。
  调用 mcu 共享库（已封装 MCU.cpp+IIC.cpp+依赖），不碰被测对象、不碰 src/hal。
deliverables:
  # planner 产出物（计划文档）；下游 implementer 将创建的源文件见 full plan §文件清单
  - artifacts/T4-planner-full.md
  - artifacts/T4-planner-report.md
verification:
  commands:
    - cmake --build build_sim -j$(nproc) --target htc_mcu_api_test
    - cmake --build build -j$(nproc) --target htc_mcu_api_test
    - ./build_sim/bin/htc_mcu_api_test --no-write
    - grep -n "g_int_readers\|g_string_readers\|g_write_specs\|kSafetyTestValue" tools/mcu_api_test.cpp
    - grep -rn "MCU\.\(h\|cpp\)" tools/ # 确认只 include MCU.h，不修改
  evidence_ref: artifacts/T4-planner-full.md
state_delta:
  set_task_status: {}
  add_decision:
    - key: T4-test-location
      value: "放 tools/mcu_api_test.cpp + tools/CMakeLists.txt（新建 tools/ 子目录 + guide）。理由：独立可执行的 API 穷举探针，运行时产物而非 CI 单元测试；tests/ 留给断言型 unit test（test_mcu_service 范式），tools/ 与 snap_test（已属 app/）这类独立可执行约定一致。"
    - key: T4-data-driven-by-return-type
      value: "不写 120 个 if/else。按返回类型分 5 组：int-readers/uint-readers/string-readers/tm-readers/tools，每组一个 std::vector 表项{name, 调用lambda, 期望范围[min,max]或枚举集合, 备注}，循环驱动。write 单独 g_write_specs 表{name, write-lambda(取值), read-back-lambda, 安全值}。bool 工具类(Is*/powerEnough/waitFor/useGpsTime)单独列为 no-i2c 或桩函数组。"
    - key: T4-status-classification
      value: "四级状态解决 read 失败=0 与真实 0 不可区分：OK(在 inventory §3 期望范围内)、REVIEW(=0 或越界，待人工，因 I2C 失败也返 0)、FAIL(不适用 read，仅 write 回读不匹配或 write 返 false)、SKIP(--no-write 或 sim 下写阶段)。string 组：空串→REVIEW，可打印且长度合理→OK。"
    - key: T4-write-safety-gate
      value: "写阶段默认关闭（破坏性：改 PID/Timer/DeviceName/RTC/TDS/PID 等真机配置）。必须 --write 显式开启，启动打印 ASCII 警告横幅 + 需用户 stdin 确认（--yes 跳过）。每个 write 用「安全测试值」（尽量接近当前读回值或合理默认，见 §安全测试值表），write 后立即 read-back 比对不等则 FAIL。提供 --restore 提示（写回程序启动时快照的原始值）。"
    - key: T4-cmake-link-mcu-lib
      value: "tools/CMakeLists.txt: add_executable(htc_mcu_api_test mcu_api_test.cpp) + target_link_libraries(mcu easylogger logger pthread rt) + include src/hardware/mcu + src。复用现有 mcu 共享库 target（已封装 MCU.cpp+IIC.cpp+PUBLIC logger，双平台可编译，IIC sim bypass 已具备）。两平台都构建（非 sim 下默认不自动运行，仅编译通过）。"
    - key: T4-phase-order
      value: "严格两阶段：Phase 1 穷举全部 read API（含 string/tm/int/uint/tool），Phase 2 仅在 --write 时遍历 write API 且每个 write 必读回。Phase 2 开始前对将被写的参数先做 read 快照（用于事后 --restore）。"
  add_risk:
    - key: T4-write-destructive
      severity: high
      description: "write API 改真机持久配置（PID/UPID/UPWD/DeviceName/RTC/Timer/PIR/TDS/HeartRate）。缓解：默认 --no-write、启动快照原始值供 --restore、安全测试值保守、启动确认门控。"
    - key: T4-writeESOR_WID-bytes-bug
      severity: medium
      description: "MCU.cpp:1518 writeESOR_WID memcpy(buf,&wid,sizeof(wid)) 写 sizeof(int)=4 字节，但寄存器 PARAM_MCU_ESOR_WID=2 字节，多写 2 字节到相邻寄存器。测试若发现写 WID 后 ESOR_ADD 被破坏即定位此 bug。属被测对象缺陷，本任务不修，仅记录并在 FAIL 备注标注 'suspected MCU.cpp overflow bug'。"
    - key: T4-sim-reads-unverifiable
      severity: medium
      description: "sim 下 IIC read bypass 返回 0，所有 read 判 REVIEW，数据无意义但能验证编译/表结构/调用路径。需在 sim 运行时打印横幅说明。"
    - key: T4-perm-dev-hc32l13x
      severity: low
      description: "真机 /dev/hc32l13x 需权限（root 或 udev 规则）。运行步骤注明以 root 运行或 chmod。"
artifact_path: artifacts/T4-planner-report.md
next: implementer
---

# T4 Planner Report (card)

本文件为 report card（只持指针）。完整实现方案见 `artifacts/T4-planner-full.md`。
