---
contract: dispatch
contract_version: "1"
task_id: T2
node: analyst
flow: bug
upstream:
  - from: pm
    artifact: artifacts/T2-bug-report.md
description: |
  定位 T2 根因:第二次启动 `htc_main_app -m --force-day` 时,日志停在
  `i264e[info]: profile Main, level 3.1`(x264 编码器初始化)之后,
  没有出现第一次启动里有的 "RTSP 监听端口" 日志。第一次启动正常。
  需对照代码确定卡死/失败的精确位置与根因(重点怀疑:第一次进程退出时
  HAL/编码器/ISP 硬件资源未正确 teardown,导致第二次 init 被阻塞或失败)。
acceptance:
  - 定位到 RtspServer.cpp:457(rtsp config 日志)之后、RTSP 监听端口日志之前的确切代码位置
  - 解释为何"第二次启动"才复现(对比第一次),给出硬件/资源/全局状态层面的根因
  - 给出明确的修复方向(改哪个文件/函数),并标注是否落在 src/hal/** PIC-owned 区域(若是,只能出提案不动手)
  - 输出可复现条件与回归验证方法
output_contract: report-card@v1
artifact_path: artifacts/T2-analyst-report.md
pointers:
  files:
    - src/media/rtsp/RtspServer.cpp
    - src/media/rtsp/RtspServer.h
    - src/media/rtsp/rtsp.h
    - src/hal/ingenic/IngenicVideo.h
    - src/hal/ingenic/IngenicVideo.cpp
    - src/hal/ingenic/IspOsdManager.h
    - src/hal/ingenic/IspOsdManager.cpp
  grep:
    - "RTSP initialize"
    - "rtsp config"
    - "VTS corrected"
constraints:
  - src/hal/** 为 PIC-owned;若根因落在该目录,只出修复提案,不改代码
  - 仅做静态分析 + 已有日志;不要在本机尝试运行 T32 硬件二进制
---

# Analyst 任务 (T2)

1. 读 `artifacts/T2-bug-report.md` 获取完整现象与日志。
2. 沿 `RtspServer.cpp` 初始化路径,定位 line 457(`rtsp config` 日志)之后到
   "RTSP server 监听端口" 日志之间的所有步骤(create encoder / codec / create_server /
   start listening)。确定 `i264e[info]` 之后下一步是什么、为什么不再产出日志。
3. 追问"为什么第二次才复现":检查进程退出/信号处理/析构链路是否释放了
   Ingenic 硬件(视频编码、ISP、OSD region、码流通道)。重点关注:
   - HAL provider(IngenicVideo/IngenicAudio)的 init/deinit 配对
   - 全局单例(IspOsdManager::s_instance、RtspServer::getInstance call_once)
   - 近期改动 commit(a205cb7 async-signal-safe signal handling + on-demand MCU reads,
     6209f88 RTSP audio, 3d854ab log demote)是否改变了 teardown 行为
4. 输出根因 + 修复方向 + 是否触及 `src/hal/**`。

## 交付物(必写文件)

- **report card**: `artifacts/T2-analyst-report.md`(frontmatter 按 report-card@v1,
  `contract: report`,`status` ∈ success/partial/failed/blocked,
  `verification.evidence_ref` 指向一个非空文件,`artifact_path` = 本 report)
- **evidence**: `artifacts/T2-analyst-evidence.md`(详细根因分析:代码引用 `file:line`、
  第一次 vs 第二次对比、根因、修复方向、复现/验证方法;内含 "pass"/"success" 字样)
- `deliverables` 用文件路径指针(artifacts/*.md),不要内联大段代码。

report card 的 `state_delta.add_decision` 可记录根因结论;`next` 建议 implementer。
