---
contract: dispatch
contract_version: "1"
task_id: T3
node: implementer
flow: bug
upstream:
  - from: analyst
    artifact: artifacts/T3-analyst-report.md
description: |
  修复 T3:重构 IngenicVideo::exit() teardown 顺序以符合 SDK 约束(imp_system.h:130:
  UnBind 必须在 FrameSource Disable 之后),去掉不可靠的 Query 幂等判定,退出前 flush
  编码器在途帧。用户已书面授权改 PIC-owned src/hal/ingenic/IngenicVideo.cpp,不动 Misc::poweroff。
acceptance:
  - exit() 销毁顺序符合 imp_system.h:130:先 disable/destroy FrameSource → UnBind →
    DestroyGroup/Chn → ISP/sensor teardown → IMP_System_Exit / MultiProcessExit
  - 去掉基于 IMP_Encoder_Query(st.registered) 的不可靠幂等判定;~IngenicVideoStream 与
    exit() 兜底职责清晰、无双重 destroy、无遗漏(协调好 g_bind/g_group map 的 erase)
  - 退出前 flush/release 编码器在途帧(IMP_Encoder 在途 GetStream 的 ReleaseStream/FlushStream)
  - 改动幂等、可空(未 init 安全)、SIM 不受影响(IngenicVideo.cpp 不编进 SIM)
  - 双平台编译通过(build/ T32 + build_sim/ SIM)
  - 不 commit / 不 push / 不动 Misc::poweroff
  - 输出 report card + 双平台构建证据
output_contract: report-card@v1
artifact_path: artifacts/T3-implementer-report.md
pointers:
  files:
    - src/hal/ingenic/IngenicVideo.cpp
    - src/hal/ingenic/IngenicVideo.h
    - sdk/include/imp/imp_system.h
    - src/hal/ingenic/IspOsdManager.cpp
  grep:
    - "IngenicVideo::exit|IMP_System_UnBind|fsMgr.destroy|IMP_System_Exit|MultiProcessExit"
    - "IMP_Encoder_Query|IMP_Encoder_UnRegisterChn|IMP_Encoder_DestroyChn|IMP_Encoder_DestroyGroup"
    - "g_bind_ref_count|g_group_ref_count|releaseFrameSource|FlushStream|ReleaseStream"
constraints:
  - 双平台编译通过;IngenicVideo.cpp 仅 T32 编译,但要确保 RtspServer 等共享代码 SIM 仍通过
  - 不 commit / push / rollback;不动 src/common/misc/Misc.cpp 的 poweroff
  - src/hal/** 改动本次已获用户书面授权(仅限本任务 exit() teardown 顺序/幂等/flush)
  - 最小化、贴合既有风格;幂等 + 可空
---

# Implementer 任务 (T3)

先读 `artifacts/T3-analyst-evidence.md`(完整根因 + 修复方向 + file:line),再读
`artifacts/T3-implementer-dispatch.md` 约束。背景:第3次启动卡死,根因是
`IngenicVideo::exit()` teardown 顺序违反 SDK + 幂等兜底脆弱 + 退出未 flush 在途帧,
IMP 内核态子状态被打坏跨重启残留。

## 要改的(用户已授权改 src/hal/ingenic/IngenicVideo.cpp)

### 1. exit() 销毁顺序(核心)
SDK 硬约束:`sdk/include/imp/imp_system.h:130` — **UnBind 必须在 FrameSource Disable 之后**。
当前 `exit()`(1178-1255)顺序错:fallback `IMP_System_UnBind`(~1211) 在 `fsMgr.destroy()`(~1247,才 DisableChn FrameSource)之前。

重排为(参考 SDK 逆序 teardown 惯例):
1. **先 disable/destroy FrameSource**:`fsMgr.destroy()`(内部应 DisableChn→DestroyChn);
   若 fsMgr.destroy 不先 Disable,补 DisableChn。
2. **再 UnBind**:遍历 `g_bind_ref_count` 调 `IMP_System_UnBind`,clear map。
3. **再 DestroyGroup/Chn**:遍历 `g_group_ref_count` → UnRegisterChn → DestroyChn → DestroyGroup,clear map。
4. **ISP/sensor teardown**:DisableTuning → closeISP → sensor disable/del(照现有,注意顺序)。
5. **最后 `IMP_System_Exit`**(及若有 `IMP_Encoder_MultiProcessExit`,按 SDK 配对放对位置)。

### 2. 幂等:去掉不可靠 Query 判定
当前 group 兜底用 `IMP_Encoder_Query(chn,&st)` 判 `st.registered` 才 UnRegisterChn,再无条件
DestroyChn/DestroyGroup。Query 在退出期不可靠,误判会污染 channel 槽位。
- 去掉 Query 判定,改为安全的无条件清理(SDK 一般容忍对未注册资源 UnRegister/Destroy 返回 <0 但无害)。
- **协调双重 destroy**:`~IngenicVideoStream`(880-892)与 exit() 兜底都对 group/bind/channel 操作。
  确认:析构是否从 map erase?若 erase,exit() 兜底 map 已空(no-op)不会双重;若没 erase,exit()
  会再 destroy 一次。要让职责清晰:per-stream(channel/group/bind)由 ~IngenicVideoStream 负责并
  从 map erase;exit() 兜底只覆盖"析构没覆盖的残留"。避免重复 destroy + 遗漏。

### 3. flush 在途帧
退出前(System_Exit 前)对编码器在途帧释放:若 configure/start 路径有 `IMP_Encoder_GetStream`,
退出前对相关 channel `IMP_Encoder_ReleaseStream` / `IMP_Encoder_FlushStream`(以 sdk/include/imp/
头文件实际 API 为准),避免 System_Exit 时编码器还有在途帧导致内核态残留。

## 验证(写入证据)
1. 双平台 build:`cmake --build build -j$(nproc)`(T32)+ `cmake --build build_sim -j$(nproc)`(SIM)。
2. 静态 grep:exit() 顺序变为 FS-destroy→UnBind→Destroy→System_Exit;无 Query 判定;有 flush。
3. 把双平台 build 成功输出(含 "Built target"/"exit 0") + grep 结果 + 关键 diff 摘要写进
   `artifacts/T3-implementer-evidence.md`。**不要**运行 T32 硬件二进制(设备回归留给用户)。

## 交付物(必写)
- `artifacts/T3-implementer-evidence.md`:diff 摘要 + 双平台 build 成功 + grep 证据(含 success/exit 0)。
- `artifacts/T3-implementer-report.md`:report card(report-card@v1,status ∈ success/partial/failed/blocked,
  verification.evidence_ref → evidence,artifact_path = 本 report,deliverables 用文件路径,next 建议 reviewer)。

返回给我:≤300 字中文摘要(exit() 顺序怎么改的、幂等怎么保证、flush 加在哪、双平台 build 是否通过、遗留风险)。
