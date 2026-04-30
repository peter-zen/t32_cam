# t32_cam 当前工作集

## 1. 目的

本文件用于给新会话提供最小充分上下文，不替代详细设计文档。

## 2. 当前关注点

当前已确认的首要工作不是改代码，而是先把项目知识入口按规范建立起来。已完成的初始化包括：
- 建立 `doc/knowledge/README.md`
- 建立 `doc/knowledge/overview.md`
- 建立 `doc/knowledge/working-set.md`
- 建立标准子目录骨架
- 建立一份历史文档迁移/映射评审记录

## 3. 新会话默认先读

1. `doc/knowledge/overview.md`
2. `doc/knowledge/working-set.md`
3. `doc/knowledge/reviews/knowledge-bootstrap-2026-04-13.md`
4. 再按任务需要读取下列源码或文档：
   - `CMakeLists.txt`
   - `src/CMakeLists.txt`
   - `src/app/CMakeLists.txt`
   - `src/media/CMakeLists.txt`
   - `AGENTS.md`

## 4. 当前已知项目结构要点

- 顶层 CMake 已区分真机与仿真模式
- `src/media/` 已拆成 `snap / video / audio / fifo / base / rtsp`
- `tests/` 只在仿真模式构建
- 历史 `doc/` 下存在大量 analysis/design/solution/job/reference/review 文档，说明项目过去已有较多分析与方案沉淀

## 5. 当前文档治理策略

短期内不要做这几件事：
- 不要一次性大迁移整个 `doc/`
- 不要把历史文档机械复制到 `doc/knowledge/`
- 不要在没有映射说明的情况下删除旧文档

应优先做：
- 以后新增项目知识默认写入 `doc/knowledge/`
- 当某个主题再次被实际使用时，再把对应历史文档提炼/迁入 `specs`、`decisions`、`bugs`、`playbooks`、`refs`
- 每做一轮迁移，都在 `reviews/` 中留下校准记录

## 6. 当前迁移进展

当前已经完成三批可复用样板：
1. RTSP 主题
2. HTTP API / camera service 主题
3. mDNS device discovery 主题

其中 mDNS 主题当前已具备：
- `specs/mdns-device-discovery-behavior.md`
- `decisions/mdns-cmd-mobile-lifecycle-model.md`
- `refs/mdns-code-entry-and-config-keys.md`
- `playbooks/mdns-simu-and-bonjour-verification.md`
- `reviews/mdns-doc-calibration-2026-04-13.md`

## 7. 可能的下一步

按优先级建议：
1. 进入真机校准阶段，优先验证：
   - device / storage 真实接线
   - workmode 真实切换闭环
   - mDNS / event discovery 实测
2. 为历史 `doc/` 建立更细的迁移策略，至少覆盖：
   - `doc/analysis/`
   - `doc/design/`
   - `doc/solution/`
   - `doc/reference/`
   - `doc/review/`
3. 对已迁移主题补充更细的 bug / review / 真机联调文档

## 8. 非目标

本文件不维护：
- 详细架构说明
- 完整历史文档清单
- 逐文件代码导读

这些内容应分别进入对应正式目录或专题文档。
