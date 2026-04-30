# t32_cam 知识初始化评审记录

日期：2026-04-13

## 1. 本轮目的

按当前项目文档规范，为 `t32_cam` 仓库建立项目级权威知识入口，避免后续会话继续直接在历史 `doc/` 目录中无序检索。

## 2. 观察结果

### 2.1 已有情况
仓库原本具备：
- 根 `README.md`，但内容只有极简构建提示
- `AGENTS.md`，包含构建、代码风格、HAL ownership、Git 操作限制等高价值协作规则
- 大量历史 `doc/` 文档，分散在：
  - `analysis/`
  - `design/`
  - `solution/`
  - `job/`
  - `reference/`
  - `review/`
  - 以及若干顶层方案/报告文件

### 2.2 缺失情况
仓库原本缺少：
- `doc/knowledge/README.md`
- `doc/knowledge/overview.md`
- `doc/knowledge/working-set.md`
- 标准知识子目录骨架

这意味着项目文档虽多，但没有统一入口与明确归档规则。

## 3. 本轮创建内容

已创建：
- `doc/knowledge/README.md`
- `doc/knowledge/overview.md`
- `doc/knowledge/working-set.md`
- `doc/knowledge/todo.md`
- `doc/knowledge/reviews/knowledge-bootstrap-2026-04-13.md`
- 子目录骨架：
  - `specs/`
  - `decisions/`
  - `bugs/`
  - `playbooks/`
  - `refs/`
  - `inbox/`
  - `reviews/`

## 4. 历史文档初步映射建议

### 4.1 更像 `bugs/` / `reviews/` 的历史文档
- `doc/analysis/*.md`
- 部分顶层验证/分析报告，如：
  - `doc/producer_stability_verification_report.md`
  - `doc/video_timestamp_fix_report.md`
  - `doc/fifo_analysis_and_fix_report.md`
  - `doc/av_sync_flow_analysis_report.md`

### 4.2 更像 `specs/` / `decisions/` 的历史文档
- `doc/design/*.md`
- `doc/solution/*.md`
- `doc/spec/*.md`
- 部分顶层架构/设计/提案类文档，如：
  - `doc/av_sync_design.md`
  - `doc/audio_video_source_architecture_analysis.md`
  - `doc/refactor_t32_proposal.md`
  - `doc/video_source_architecture_t32.md`

### 4.3 更像 `playbooks/` / `refs/` 的历史文档
- `doc/reference/*.md`
- `doc/PC_BUILD_GUIDE.md`
- 一些 job/实施计划文档可视情况拆分：
  - 可复用流程进入 `playbooks/`
  - 一次性计划保留在历史区或只在 review 中提及

## 5. 当前治理结论

结论很明确：
- 这个仓库不是“没有文档”，而是“文档很多但缺少规范化入口”。
- 因此本轮正确动作不是重写历史，而是先建立 `doc/knowledge/` 权威入口与分类规则。
- 后续每当某个主题再次进入活跃开发，应围绕该主题进行增量迁移，而不是全量搬家。

## 6. 后续建议

### 高优先级
1. 以后新增的项目知识默认写入 `doc/knowledge/`
2. 选择一个当前活跃主题，做第一批正式迁移样例
3. 在后续主题迁移时补充更具体的 superseded / redirect 说明

### 中优先级
1. 将 `doc/review/` 的治理类内容逐步吸收进 `doc/knowledge/reviews/`
2. 将可复用测试/联调步骤从 `doc/reference/` 和 `script/` 中提炼到 `doc/knowledge/playbooks/`

### 低优先级
1. 再决定是否对旧 `doc/` 做大规模重命名
2. 再决定是否将部分旧文档只保留为历史档案

## 7. 读取建议

后续会话进入本项目时，默认先读：
1. `doc/knowledge/overview.md`
2. `doc/knowledge/working-set.md`
3. 本评审记录

只有在主题明确后，再继续读取对应的旧 `doc/` 专题文档与源码。
