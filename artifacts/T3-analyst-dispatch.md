---
contract: dispatch
contract_version: "1"
task_id: T3
node: analyst
flow: bug
upstream:
  - from: pm
    artifact: artifacts/T3-bug-input.md
description: |
  定位 T3 根因:T2 teardown 修复已生效(退出日志有 teardown complete),但第3次启动
  仍卡在 i264e[info] 之后(configure 内)。teardown 清理不彻底,IMP 资源逐步累积泄漏,
  需找出未配对 destroy 的那项。
acceptance:
  - 精确定位第3次卡死点(configure 内 RegisterChn(950)/Bind(962)/CreateOsdRgn 哪一步)
  - 找出 teardown 路径里**未配对 destroy**的 IMP 资源(每进程漏一点、累积到第3次阻塞)
  - 解释"为什么第2次能过、第3次才卡"(累积阈值/资源数量上限)
  - 给出明确修复方向(改哪个文件/函数/IMP 调用对),标注是否落在 src/hal/**(PIC-owned)
  - 输出复现条件 + 设备侧回归验证方法
output_contract: report-card@v1
artifact_path: artifacts/T3-analyst-report.md
pointers:
  files:
    - src/hal/ingenic/IngenicVideo.cpp
    - src/hal/ingenic/IngenicVideo.h
    - src/hal/ingenic/IspOsdManager.cpp
    - src/hal/ingenic/IspOsdManager.h
    - src/media/rtsp/RtspServer.cpp
    - logs/debug.log
  grep:
    - "configure|CreateChn|RegisterChn|UnRegisterChn|DestroyChn|System_Bind|System_UnBind"
    - "IngenicVideo::exit|MultiProcessExit|IMP_System_Exit|fsMgr.destroy"
    - "g_bind_ref_count|g_group_ref_count|g_fs_ref_count|exitCalled_"
    - "CreateOsdRgn|DestroyOsdRgn|IspOsdManager::exit|IspOsdManager::stop"
constraints:
  - 只做静态分析 + 日志 + git 历史;不要运行 T32 硬件二进制
  - src/hal/** 为 PIC-owned;若根因落在该目录,给修复提案(改哪/怎么改),不要改代码
  - 用 file:line 引用,不要大段复制源码
---

# Analyst 任务 (T3)

1. 读 `artifacts/T3-bug-input.md`(三次启动对照 + PM 初步发现)和 `logs/debug.log` 全文。
2. 读 T2 产物理解上次修复范围:`artifacts/T2-analyst-evidence.md`、`artifacts/T2-implementer-evidence.md`(尤其 T2 在 `IngenicVideo::exit()` 加的兜底逻辑)。

## 必查(对照 init/teardown 配对)
3. **完整列出 `IngenicVideo::init()`(约 1115-1177)创建的每一项 IMP 资源**,逐项确认其
   反操作是否在 teardown(`IngenicVideo::exit()`(1178-1244)+ `~IngenicVideoStream`/`stop` +
   `FrameChannelController::destroy`/`fsMgr.destroy`)里存在:
   - encoder: `IMP_Encoder_CreateGroup` ↔ DestroyGroup;`IMP_Encoder_CreateChn` ↔ DestroyChn;
     **`IMP_Encoder_RegisterChn`(950) ↔ `IMP_Encoder_UnRegisterChn`**(grep 不到?这是头号嫌疑);
     `IMP_System_Bind` ↔ `IMP_System_UnBind`。
   - framesource: `IMP_FrameSource_CreateChn`(547)/EnableChn ↔ DestroyChn/DisableChn。
   - ISP/sensor: `IMP_ISP_Open`/`IMP_ISP_EnableSensor`/`IMP_ISP_Tuning_CreateOsdRgn` ↔ Close/Disable/DestroyOsdRgn。
   - 全局: `IMP_System_Init`/`IMP_Encoder_MultiProcessInit` ↔ Exit。
4. **重点验证 T2 的 ref-count map 兜底**:这些 map 是**进程内 static**(重启清零)。
   - 在"本进程"内,configure 时是否把 CreateChn/RegisterChn/Bind 都记进了对应 map?
   - exit 时是否遍历 map 全部反操作?有没有"创建了但没进 map,或进了 map但 exit 没遍历"的资源?
   - 关键:**有没有某项 IMP 资源是"创建了、但 teardown 完全没碰"的**(那才是跨进程累积源)。
5. **OSD 双调用**:`IspOsdManager::stop()`(59)与 `exit()`(90)都 DestroyOsdRgn,"exit done" 打两遍。
   确认 region 是否真被 destroy、有无重复 destroy 副作用、IMP OSD region 是否有数量上限累积。
6. **第3次才卡的机理**:找到"每进程泄漏 N 个、累积到第3次触发"的那项,给出阈值推断
   (如 encoder group/channel 数量上限、OSD region 上限、IMP 全局句柄数)。
7. **确认卡死点**:i264e 之后 configure 内到底卡 RegisterChn(950)、Bind(962) 还是 CreateOsdRgn。
   结合"哪项资源累积"反推。

## 输出
- 根因(一句话)+ 证据链(file:line)。
- 修复方向:补全哪几对 IMP 反操作,改哪个文件/函数。
- **是否触及 src/hal/\*\***?(本次极可能触及——IngenicVideo.cpp / IspOsdManager.cpp)
  若触及,写清"需用户书面授权",并给出最小改动提案。
- 复现条件 + 设备侧回归(连跑 N 次 kill+重启不卡)。

## 交付物(必写)
- `artifacts/T3-analyst-evidence.md`:详细根因(init/teardown 配对表、泄漏项、第3次机理、
  修复方向、复现/回归)。含 "success"/"pass" 字样。
- `artifacts/T3-analyst-report.md`:report card(report-card@v1,status ∈ success/partial/failed/blocked,
  verification.evidence_ref → evidence,artifact_path = 本 report,
  deliverables 用文件路径指针,next 建议 implementer)。

返回给我:≤300 字中文摘要(根因 + 泄漏的具体 IMP 资源 + 修复方向 + 是否触及 src/hal/** + status)。
