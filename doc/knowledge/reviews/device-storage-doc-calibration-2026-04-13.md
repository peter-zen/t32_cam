# Device / Storage 文档校准记录

日期：2026-04-13

## 1. 本轮目标

把 device / storage 相关 reference 与当前代码状态对齐，建立下一批正式迁移样板。

## 2. 本轮读取范围

代码：
- `src/service/http_server/http_api_v1.cpp`
- `src/hardware/disk/Disk.h`
- `src/hardware/disk/Disk.cpp`
- `src/hardware/mcu/MCU.h`
- `src/hardware/mcu/MCU.cpp`

历史文档：
- `doc/reference/20260324-http-api-reference.md`
- `doc/reference/20260324-http-api-client-quick-reference.md`

## 3. 本轮新增产出

已新增：
- `doc/knowledge/specs/device-and-storage-surface-behavior.md`
- `doc/knowledge/decisions/device-and-storage-http-surface-vs-real-backend-depth.md`
- `doc/knowledge/refs/device-storage-code-entry-and-field-source-notes.md`
- `doc/knowledge/playbooks/device-storage-http-probe.md`

## 4. 本轮校准出的关键结论

### 4.1 HTTP 表层稳定，但后端深度偏浅
当前 device / storage 路由与字段结构已经稳定，但多数值仍然来自构造型 JSON，而不是实时硬件数据。

### 4.2 仓库里并非没有底层能力
当前已能看到：
- `Disk::getInfo()`
- MCU 电池相关读取

所以正确说法不是“没有能力”，而是“能力未完整接入 V1 handler”。

### 4.3 `storage/format` 当前只是 accepted/scheduled 语义
没有看到真实格式化闭环接线，因此不能把它写成已完成真实执行的接口。

### 4.4 reference 文档的字段说明可保留，但成熟度表述必须收紧
字段名和大体结构与当前代码对齐，但不能让示例值被误解为真实采样值。

## 5. 当前推荐文档结构

### 已建立
- `specs/device-and-storage-surface-behavior.md`
- `decisions/device-and-storage-http-surface-vs-real-backend-depth.md`
- `refs/device-storage-code-entry-and-field-source-notes.md`
- `playbooks/device-storage-http-probe.md`

### 后续可补
- `bugs/device-storage-placeholder-data-and-unwired-format-path.md`
- 真机接线后的校准 review 文档

## 6. 结论

device / storage 主题适合作为当前仓库继续迁移的主题。

原因：
- 代码入口清晰
- 历史 reference 已有较稳定字段定义
- 当前最容易发生“字段稳定被误写成真实数据接线完成”的文档漂移

## 7. 下一步建议

优先级建议：
1. 若后续补 event_port 暴露链路，可继续迁移 discovery / event 与 HTTP 的交叉主题
2. 若 device / storage 开始接真实硬件，再补一篇真机校准 review
3. 若格式化链路暴露缺口，再单独补 bug 文档
