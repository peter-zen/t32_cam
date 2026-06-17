---
contract: report
contract_version: "1"
task_id: T4
node: implementer
flow: feature
status: success
summary: |
  实现 T4：新增独立诊断程序 htc_mcu_api_test，数据驱动穷举 src/hardware/mcu/MCU.h 全部
  public API（自检计数 read-class 99 + write-class 33 = 132 方法，与 inventory §1 的 132 一致，
  无遗漏）。Phase 1 逐个调用全部 read（int/string/uint/tm/bool-stub/pure-fn/void 分组 reader 表），
  按 inventory §3 期望范围做四级判定（OK/REVIEW/FAIL/SKIP）；Phase 2 默认 --no-write 关闭，
  --write 开启时对全部 write 用「启动快照原值」写回 + 回读比对（≈空操作，最不易破坏配置）。
  核心难题解决：MCU.cpp 每个 read 在 iic->read()<=0 时 return 0，真实 0 值也是 0 不可区分，
  故 read 返回 0 一律 REVIEW（readWorkingMode 返 -1 为例外，越界 REVIEW）；readCDS_DN 等
  语义上 0 合法的用 allowZero 通道判 OK。写阶段安全门控齐全：默认 --no-write、--write 警告横幅
  + stdin 确认（--yes 跳过）、Phase 2 前对所有将写参数 read 快照（内存 map + 落盘 json）、
  --restore 子模式写回原值、sim 下 --write 自动跳过。输出逐行表 + 末尾汇总 + 落盘日志。
  双平台编译均 exit 0 无 warning（PC sim x86-64 + T32 交叉 MIPS/uclibc）；sim 冒烟 Phase 1
  跑完 99 read 无崩溃（OK=16/REVIEW=83）、Phase 2 全 SKIP、exit 0。
deliverables:
  - tools/mcu_api_test.cpp
  - tools/CMakeLists.txt
  - tools/README.md
  - CMakeLists.txt
  - artifacts/T4-implementer-evidence.md
  - artifacts/T4-implementer-report.md
verification:
  commands:
    - cmake --build build_sim -j$(nproc) --target htc_mcu_api_test
    - cmake --build build -j$(nproc) --target htc_mcu_api_test
    - cd build_sim && LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH ./bin/htc_mcu_api_test --no-write
    - grep -n '#include' tools/mcu_api_test.cpp
    - git status --short src/hardware/mcu/MCU.h src/hardware/mcu/MCU.cpp
    - file build/bin/htc_mcu_api_test build_sim/bin/htc_mcu_api_test
  evidence_ref: artifacts/T4-implementer-evidence.md
state_delta:
  set_task_status: {}
  add_decision:
    - key: T4-api-enumeration
      value: "MCU.h public API 实测 132 方法（排除 getInstance/ctor/dtor）：read-class 99 调用点（int=81/string=7 含 readGps/uint=1/bool-stub=6/tm=1/pure-fn=2/void=1）+ write-class 33。启动自检横幅固化打印计数，与 inventory §1 的 132 一致。"
    - key: T4-four-level-classification
      value: "四级判定 OK/REVIEW/FAIL/SKIP。read 返回 0 一律 REVIEW（MCU.cpp read 在 iic->read()<=0 时 return 0，与真实 0 不可区分，见 MCU.cpp:107/133/160/476）；非 0 在 inventory §3 范围内 OK、越界 REVIEW；readCDS_DN/readCDS_Value/readVTSAlarm/readSOR_AL/readSOR_UVL 语义 0 合法走 allowZero 通道判 OK；readWorkingMode 返 -1 越界 REVIEW。write 返 false→FAIL、回读不等→FAIL（writeESOR_WID 备注疑似 MCU.cpp:1518 sizeof(int) 溢出 2 字节寄存器）。--no-write/sim 下 write→SKIP。"
    - key: T4-write-safety-gates
      value: "默认 --no-write；--write 开启时 ASCII 警告横幅 + stdin 'yes' 确认（--yes 跳过）；Phase 2 前对所有将写参数 read 快照（内存 map + 落盘 mcu_snapshot_<ts>.json）；--restore <f> 子模式写回原值；安全测试值优先取快照原值（write 原值≈空操作）；sim 下 --write 自动跳过 Phase 2。"
    - key: T4-cmake-integration
      value: "tools/ 目录此前从未接入 CMake（既有 json_stress_test.cpp 自 commit d04f46c 起游离）。本任务顶层 CMakeLists.txt 加 add_subdirectory(tools)（无平台门控，满足双平台编译），tools/CMakeLists.txt 定义 htc_mcu_api_test（link mcu/logger/easylogger/pthread，include src/src/common/utils/string/time-rtc/logger/easylogger）并顺势把既有 json_stress_test 一并纳入（link jsoncpp）。"
    - key: T4-uclibc-portability
      value: "T32 uclibc 工具链（gcc 5.4）<string> 无 std::to_string/std::stoi（与 MCU.cpp 用 to_string_custom/stoi_custom 同因），用本地 toStr/fromStr（snprintf/strtol）替换；未用 std::thread/chrono，去掉对应 include 避免 uclibc 额外 link。"
  add_risk:
    - key: T4-hw-unverified
      severity: medium
      description: "双平台编译 + sim 冒烟已过，但真机 Phase 1（read 实数据 range 分类）、Phase 2（write+回读，定位 writeESOR_WID 已知 bug）、--restore 回路均需在 T32 硬件验证，PC 不能跑 MIPS，留用户执行。"
artifact_path: artifacts/T4-implementer-report.md
next: tester
---

# T4 Implementer Report

## 改动一句话
新增 `htc_mcu_api_test`：数据驱动穷举 MCU.h 全部 132 个 public API，Phase 1 读 + Phase 2 写（默认关闭），四级判定（OK/REVIEW/FAIL/SKIP）+ 末尾汇总 + 落盘日志 + 写阶段安全门控（默认 --no-write / 警告横幅+确认 / 启动快照 / --restore）。

## API 枚举（以实测为准）
- read-class **99**：int=81、string=7（含 readGps 复合串）、uint=1、bool-stub=6、tm=1、pure-fn=2（convertVersion/convertVoltage）、void=1（readAllTestData）
- write-class **33**：ID(4) + Wakeup(1) + ESOR(2) + PIR(4) + Timer(13) + Policy(8) + RTC(1) + GPS(1)
- **132 = 99 + 33**，与 inventory §1 一致；启动横幅固化计数自检

## 四级判定（核心）
- read 0 → REVIEW（MCU.cpp read 在 `iic->read()<=0` 时 return 0，与真实 0 不可区分）
- read 非 0 在 inventory §3 范围内 → OK；越界 → REVIEW
- readCDS_DN 等 5 个语义 0 合法 → allowZero 通道判 OK
- write false → FAIL；write 后回读不等 → FAIL（writeESOR_WID 备注疑似 MCU.cpp:1518 溢出 bug）
- --no-write/sim → SKIP

## 写阶段安全门控
默认 --no-write；--write 开启 → ASCII 警告横幅 + stdin 'yes'（--yes 跳过）→ Phase 2 前对所有将写参数 read 快照（内存 map + 落盘 json）→ 安全值优先取快照原值（空操作）；--restore 写回；sim 下 --write 自动跳过。

## 验证
- 双平台编译：build_sim（x86-64）+ build（MIPS/uclibc）`--target htc_mcu_api_test` 均 exit 0，无 warning
- sim 冒烟：Phase 1 跑完 99 read 无崩溃（OK=16/REVIEW=83/FAIL=0），Phase 2 全 SKIP=33，exit 0
- `--group`/`--help`/`--write`（sim 跳过）行为均符合预期
- 硬约束：MCU.{h,cpp} 未改、src/hal 未碰、tools/mcu_api_test.cpp 只 include MCU.h、未 commit

## 附带
`tools/` 首次接入 CMake，顺势把既有游离的 `json_stress_test.cpp`（commit d04f46c）一并纳入（两平台编译 + sim 冒烟通过），避免目录接入但源文件游离。

## 遗留
- 真机 Phase 1/Phase 2/restore 回路未验证（PC 不能跑 MIPS），留用户在 T32 执行
- 发现的 MCU.cpp 缺陷（writeESOR_WID 溢出）只会在真机 Phase 2 显现为 FAIL 备注，不在本工具修复（符合 non-goals）
- 未 git commit（未授权）

详见 `artifacts/T4-implementer-evidence.md`。
