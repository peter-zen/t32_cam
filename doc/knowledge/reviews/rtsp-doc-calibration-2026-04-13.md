# RTSP 文档校准记录

日期：2026-04-13

## 1. 本轮目标

把仓库中分散的 RTSP 历史方案/分析文档，收敛为 `doc/knowledge/` 下可供后续会话直接使用的第一批正式文档。

## 2. 本轮读取范围

代码：
- `src/app/main_app.cpp`
- `src/media/rtsp/RtspServer.h`
- `src/media/rtsp/RtspServer.cpp`
- `src/media/rtsp/MediaSession.h`
- `src/media/rtsp/MediaSession.cpp`
- `src/media/fifo/MediaFIFO.h`

历史文档：
- `doc/rtsp_delayed_startup_solution.md`
- `doc/fifo_analysis_and_fix_report.md`
- `doc/solution/20260304-rtsp-simu-hal-linkage-guide.md`
- `doc/analysis/20260326-rtsp-simu-first-frame-delay-root-cause-and-review.md`

## 3. 本轮产出

已新增：
- `doc/knowledge/specs/rtsp-streaming-behavior.md`
- `doc/knowledge/bugs/rtsp-first-frame-delay-mixed-start-code.md`
- `doc/knowledge/playbooks/rtsp-simu-verification.md`

## 4. 本轮校准掉的错误/过时表述

### 4.1 “RTSP 完全通过 pullFrame 首次调用来启动 session”
这个说法已不准确。

根据当前 `RtspServer.cpp`：
- 启动阶段已有 video preopen，用于提取 SPS/PPS
- 正式持续推流由 `onSessionPlay()` 驱动

因此不应继续把旧方案文档中的 `pullFrame 首次调用延迟启动` 当成当前权威事实。

### 4.2 “当前 producer 完全无 pacing，FIFO 满就是必然”
这同样已过时。

根据当前 `MediaSession.cpp`：
- video/audio 都已按参数推导 pacing interval
- 当前实现已不是旧报告中的无限速生产版本

因此旧文档里关于 `3265fps` 的描述，只能视为历史问题阶段的证据，不能直接写入当前规格。

### 4.3 “首画面延迟的根因是 PLAY 时序”
这是错误归因。

已确认根因应写为：
- `rtsp.c` 对 mixed start code 的 H.264 AU 扫描兼容问题

## 5. 当前 RTSP 文档结构建议

### 已建立
- `specs/rtsp-streaming-behavior.md`
- `bugs/rtsp-first-frame-delay-mixed-start-code.md`
- `playbooks/rtsp-simu-verification.md`

### 后续建议补齐
- `decisions/rtsp-session-startup-model.md`
- `refs/rtsp-simu-sample-assets-and-clients.md`

## 6. 结论

RTSP 主题已经适合作为这个仓库第一批正式迁移样例。
原因：
- 历史文档多
- 代码入口清晰
- 已存在典型“旧文档结论和当前代码状态不再一致”的情况
- 很适合建立“规格 / bug / playbook”三分法样板

## 7. 下一步建议

优先级建议：
1. 继续补 `decisions/rtsp-session-startup-model.md`
2. 再选 HTTP API 或 mDNS 做第二批正式迁移样例
3. 当后续真正遇到 RTSP 新问题时，再继续把历史 `doc/analysis/` 中高价值内容吸收进 `bugs/` 或 `refs/`
