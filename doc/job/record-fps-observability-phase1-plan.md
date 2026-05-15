# 录影 FPS 可观测性增强实施计划（Phase 1）

## 1. 目标

在不修改 `src/hal/**` 的前提下，增强日志可观测性，让现场日志能够直接回答：

- `stream0` / `stream1` 默认规格是什么
- RTSP 预览请求和实际采用的规格是什么
- 录影请求和实际采用的规格是什么
- 录影过程中的编码输出帧统计是否满足 `30fps`
- 录影性能瓶颈更偏向“上游供帧不足”还是“下游写卡/封装拖慢”

## 2. Phase 划分

### Phase 1

范围限定为：

- `doc/job/` 新增实施计划文档
- 应用层 / 媒体层日志增强
- 不修改 `src/hal/**`
- 不修改 sensor 驱动或 ISP 默认 mode

## 3. 修改内容

### 3.1 默认 stream 规格日志

在录影启动侧增加默认 profile 日志，输出：

- 当前 sensor type
- `stream0` 默认分辨率 / fps / enable
- `stream1` 默认分辨率 / fps / enable

说明：

- 该日志用于建立“编译时默认规格”基线
- 该日志不代表运行时最终规格未被重配

### 3.2 RTSP 规格日志

在 RTSP 视频初始化阶段增加：

- `rtsp config`
  - sensor id
  - stream id
  - width / height
  - fps
  - codec
  - rc mode
- `rtsp stream info`
  - output index
  - width / height
  - fps
  - enabled

### 3.3 录影规格日志

在录影启动和真正进入录制循环前增加：

- `record config`
  - file path
  - stream id
  - width / height
  - fps
  - bitrate
  - codec
  - rc mode
  - duration
- `record stream info`
  - pre-start stream info
  - started stream info

### 3.4 保留并利用已有统计日志

保留现有日志：

- `record stats`
- `record wall`
- `record summary`

这些日志已能体现：

- 编码输出帧数
- `observed_fps`
- `wall_fps`
- `avg_write_ms`
- `avg_poll_ms`
- `avg_loop_ms`

## 4. 风险评估

### 风险 1：日志量增加

- 风险等级：低
- 影响：日志量会增加，但频率可控
- 应对：规格日志只在初始化/开始录影时打印一次；过程统计沿用现有 5 秒周期

### 风险 2：默认 profile 与运行时 profile 混淆

- 风险等级：中
- 影响：现场可能把“默认规格”误认为“当前实际规格”
- 应对：日志关键字明确区分：
  - `stream default profile`
  - `rtsp config`
  - `rtsp stream info`
  - `record config`
  - `record stream info`

### 风险 3：无法直接拿到 ISP 出帧计数

- 风险等级：中
- 影响：Phase 1 仍不能严格证明 ISP 实际产帧数
- 应对：先利用编码输出统计缩小范围；若仍不足，再提交 `HAL` 扩展提案

## 5. 验证计划

### 5.1 编译验证

- 真机模式编译通过
- 新增日志代码不引入编译错误

### 5.2 日志验证

启动 RTSP 和录影后，应能在日志中检索到：

- `stream default profile`
- `rtsp config`
- `rtsp stream info`
- `record config`
- `record stream info`
- `record stats`
- `record wall`
- `record summary`

### 5.3 问题定位验证

以 `2K@30` 录影和 `720p@30` RTSP 并存场景为例，检查：

- `record config` 是否为目标规格
- `record stream info` 是否显示目标 fps
- `record summary.observed_fps` 是否接近 30
- `record summary.wall_fps` 是否接近 30
- `avg_write_ms` 是否明显升高

如果：

- `observed_fps` 和 `wall_fps` 都接近 17
  - 更偏向上游供帧不足
- `observed_fps` 接近 30、`wall_fps` 明显低
  - 更偏向封装或写卡拖慢

## 6. 交付物

- `doc/job/record-fps-observability-phase1-plan.md`
- 日志增强代码改动
- 编译结果与诊断结果

## 7. 下一阶段条件

若 Phase 1 完成后仍无法判断 sensor / ISP 是否真实只输出约 `17fps`，则进入 Phase 2：

- 提交 `HAL` 扩展方案
- 在 PIC 确认后增加只读状态采样
- 补充 `IMP_Encoder_Query` / `FrameSource` 只读状态日志
