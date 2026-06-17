---
contract: report
contract_version: "1"
task_id: T3
node: reviewer
flow: bug
status: success
summary: |
  T3 最终代码审查（passed/success）。独立重编译双平台（build/ exit 0 + build_sim/ exit 0，非信报告）。
  核心：IngenicVideo::exit() teardown 顺序 SDK 合规——对照 sdk/include/imp/imp_system.h:130/131
  原文，fsMgr.disable(Disable FS, L1213) → flush/StopRecvPic(L1228) → UnBind(L1253，在 FS Disable 后，满足 L130)
  → DestroyGroup/Chn(L1277，在 UnBind 后，满足 L131) → ISP/OSD → fsMgr.destroy → IMP_System_Exit → sensor/ISP
  → MultiProcessExit。teardown 路径(析构+exit 兜底)的 IMP_Encoder_Query(registered) 幂等门控已全部移除，
  改无条件 UnRegister/Destroy；唯一保留的 Query(L1082) 在 getInfo() 运行期查询，保留正确。flush 配对完整：
  析构 ReleaseStream(last_stream_)+StopRecvPic、exit 兜底 StopRecvPic，无在途帧泄漏到 System_Exit。幂等无双重
  destroy：析构先 erase 三张 ref-count map，exit 兜底遍历已空 map no-op；exitCalled_/g_video_init_ref_count/
  configured_/空指针守卫完整。SIM 不受影响(IngenicVideo.cpp 不编入 SIM)、对正常启动/第1-2次退出无回归
  (新顺序比 T2 更严格)。1 个 nice-to-have(exit 兜底 UnBind outputID 硬编码 0，当前与 configure 一致正确)
  + 设备侧回归缺口(imp_stub 全 no-op，硬件连跑留用户，implementer 已在 risk 声明)——均不阻断。无需 loopback。
deliverables:
  - artifacts/T3-reviewer-evidence.md
  - artifacts/T3-reviewer-report.md
verification:
  commands:
    - cmake --build build -j$(nproc)
    - cmake --build build_sim -j$(nproc)
    - grep -n "IMP_Encoder_Query" src/hal/ingenic/IngenicVideo.cpp
    - grep -n "fsMgr\.\(disable\|destroy\)\|IMP_System_UnBind\|IMP_Encoder_DestroyGroup\|IMP_System_Exit\|MultiProcessExit" src/hal/ingenic/IngenicVideo.cpp
    - git diff --stat src/hal/ingenic/IngenicVideo.cpp
  evidence_ref: artifacts/T3-reviewer-evidence.md
state_delta:
  set_task_status:
    T3: done
  add_decision:
    - key: T3-review-passed
      value: "T3 审查通过(success)。独立双平台重编译 exit 0；exit() teardown 顺序满足 imp_system.h:130(UnBind 后于 FS Disable)/131(DestroyGroup 后于 UnBind)；teardown Query 门控已移除改无条件，L1082 运行期 Query 保留；flush ReleaseStream/StopRecvPic 配对无在途帧泄漏；幂等无双重 destroy；SIM 不受影响。"
  add_risk:
    - key: T3-hw-regression-pending-user
      severity: medium
      description: "双平台编译已过，连续 5 次 kill+重启(第3次卡死)回归待 T32 硬件执行(imp_stub 全 no-op，PC 无真实 IMP 驱动)；analyst §8 已给脚本，留用户。"
artifact_path: artifacts/T3-reviewer-report.md
next: ":end"
---

# T3 Reviewer Report

## 审查结论：PASSED / success → next `:end`

无需 loopback implementer。

## 独立验证摘要

| # | 必查项 | 结论 | 证据 |
| --- | --- | --- | --- |
| 1 | 独立重编译 build + build_sim 均 exit 0 | passed | evidence §1（独立复现，非信报告）|
| 2 | exit() 顺序 L130(UnBind 后于 FS Disable) + L131(DestroyGroup 后于 UnBind) | passed | evidence §2（对照 imp_system.h:130/131 原文）|
| 3 | 幂等/无双重 destroy；守卫完整 | passed | evidence §3 |
| 4 | teardown Query 门控移除；L1082 运行期保留 | passed | evidence §4 |
| 5 | flush 配对，无在途帧泄漏到 System_Exit | passed | evidence §5 |
| 6 | SIM 不受影响；正常启动/第1-2次退出无回归 | passed | evidence §6 |
| 7 | 设备侧回归脚本 | analyst §8 已给；硬件验证留用户 | evidence §8 |

## 核心发现

- **顺序合规（根因级修复到位）**：exit() 翻转自 T2 的"先 UnBind 后 destroy FS"为"先 disable FS → flush → UnBind → DestroyGroup → destroy FS → System_Exit"，满足 SDK 两条 attention 约束（imp_system.h:130/131 原文已独立核对）。
- **Query 门控清理彻底**：teardown 路径（析构 897 + exit 兜底 1275）改无条件 UnRegister；唯一保留的 L1082 是 getInfo() 运行期状态查询，非 teardown 判定。
- **无双重 destroy**：析构先 erase 三张 ref-count map，exit() 兜底遍历已空 map no-op；exitCalled_/ref_count/configured_/空指针守卫完整。
- **flush 配对**：析构 ReleaseStream(last_stream_) + StopRecvPic、exit 兜底 StopRecvPic；getFrame 缓存的在途帧被析构兜底释放，System_Exit 不在编码器持帧时调用。

## 不阻断项

- **Nice-to-have**：exit() 兜底 UnBind 重新构造 IMPCell，outputID 硬编码 0。当前与 configure(967-972) 一致正确，未来若 bind outputID 非 0 需同步。建议（非必须）兜底从 map 关联存原 cell。
- **已知缺口**：imp_stub.c 全 no-op，PC 无真实 IMP 驱动状态机，连续重启回归需 T32 硬件执行；analyst §8 已给脚本，implementer 已在 risk(`T3-hw-regression-unverified`) 声明留用户。属 dispatch 约束（禁跑硬件）下的已知项，非代码缺陷。

## 证据指针

- evidence：`artifacts/T3-reviewer-evidence.md`
- 审查源：`src/hal/ingenic/IngenicVideo.cpp:881-902`（析构）、`1189-1302`（exit）、`1034-1076`（getFrame/releaseFrame flush）、`22-133`（ref-count map）、`540-588`（fsMgr）
- SDK：`sdk/include/imp/imp_system.h:129-131`
- upstream：`artifacts/T3-implementer-report.md`、`artifacts/T3-implementer-evidence.md`、`artifacts/T3-analyst-evidence.md`
