# WorkMode 文档校准记录

日期：2026-04-13

## 1. 本轮目标

把 workmode 相关历史 reference 与当前代码状态对齐，建立下一批正式迁移样板。

## 2. 本轮读取范围

代码：
- `src/app/workmode/WorkMode.h`
- `src/app/workmode/WorkMode.cpp`
- `src/app/workmode/CMakeLists.txt`
- `src/app/main_app.cpp`
- `src/app/media_app.cpp`
- `src/service/http_server/http_api_v1.cpp`

历史文档：
- `doc/reference/20260324-http-api-reference.md`
- `doc/reference/20260324-http-api-client-quick-reference.md`
- `doc/job/directory_reorganization_plan.md`

## 3. 本轮新增产出

已新增：
- `doc/knowledge/specs/workmode-selection-and-switching.md`
- `doc/knowledge/decisions/workmode-vs-cmd-mobile-layering.md`
- `doc/knowledge/refs/workmode-code-entry-and-mode-mapping.md`
- `doc/knowledge/playbooks/workmode-startup-and-http-probe.md`

## 4. 本轮校准出的关键结论

### 4.1 `WorkMode` 是启动期模式来源，不是运行时切换中心
当前代码里 `WorkMode` 只有读取能力，没有成体系的写入/切换能力。
所以不能把它描述成统一运行时模式管理器。

### 4.2 `CMD_MOBILE` 与 `WorkMode` 不是同一层概念
当前 `main_app.cpp` 明确是：
- `WORKING_MODE_TEST_ONLY` -> `CMD_MOBILE`

这说明 `CMD_MOBILE` 是命令路径，而不是某个 workmode 枚举值本身。

### 4.3 HTTP `/api/v1/system/workmode` 当前只做到 accepted 语义
当前 handler：
- 做参数校验
- 打日志
- 返回 `accepted=true`

但没有代码证明真实模式切换已经发生。

### 4.4 历史 reference 的接口说明需要收敛口径
旧 reference 把它描述成“请求切换工作模式”。
这句话作为协议层说明可以保留，但如果进一步暗示“后端已完成真实切换闭环”，那就写过头了。

## 5. 当前推荐文档结构

### 已建立
- `specs/workmode-selection-and-switching.md`
- `decisions/workmode-vs-cmd-mobile-layering.md`
- `refs/workmode-code-entry-and-mode-mapping.md`
- `playbooks/workmode-startup-and-http-probe.md`

### 后续可补
- `bugs/workmode-http-accepted-but-no-runtime-switch.md`
- 真机工作模式切换联调 review 文档

## 6. 结论

workmode 主题适合作为当前仓库下一批正式迁移主题。

原因：
- 代码入口明确
- 历史文档已有协议描述
- “产品意图”和“当前真实闭环”之间存在明显差距
- 很适合通过 spec / decision / refs / playbook 结构把口径校正过来

## 7. 下一步建议

优先级建议：
1. 如果后续开始补真实切换链路，补一篇 bug / decision 文档记录设计收敛
2. 如果真机有 MCU 写回路径，再补一篇真机校准 review
3. 下一主题可转向 device / storage 状态接线或 event_port 暴露链路
