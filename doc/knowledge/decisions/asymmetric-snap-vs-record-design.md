# 拍照在 media_app、录影在 main_app 的不对称设计决策

## 1. 决策主题

`t32_cam` 项目中：
- 拍照动作（`quick_snap`）在 `htc_media_app` 中执行
- 录影动作（`processCmdVideoRecord` / `CameraServiceT32::startRecord`）在 `htc_main_app` 中执行

这种"拍照在前置进程、录影在主进程"的不对称分布是有意为之，不是 bug。本 ADR 记录这一设计决策及其理由。

## 2. 决策背景

`t32_cam` 是一款省电型相机，典型使用场景为电池供电、低功耗、上电后**尽快完成首次拍摄**。其启动链为：

```
上电 → htc_media_app (入口) → 检测 working mode → 拍照/拉起主程序
                              ↓
                          htc_main_app (主程序)
```

启动时序特性：
- `htc_media_app` 启动时，**SD 卡可能尚未 mount**（mount 由后续主程序或 udev 事件触发）
- `htc_main_app` 启动时，SD 卡已基本就绪
- 两进程之间存在时间窗，期间系统资源（TF 卡、WiFi、文件系统）逐步 ready

## 3. 决策内容

### 3.1 拍照：放在 `htc_media_app`（SD 卡未 ready 也能拍）

- 省电相机希望**上电后尽快出第一张照片**，不等所有外设 ready
- 单张照片体积小（~2-5MB），可以容忍写到临时位置
- `htc_media_app` 调 `quick_snap()` 拍照到 `/mnt/sdcard/DCIM/<timestamp>/`（早期 mount 点或临时位置）
- 后续 `htc_main_app` 接管时 SD 卡已 mount 完毕，main_app 用 `processCmdSnap()` 把临时位置的照片**搬到正式位置**（`/sdcard/DCIM/<dir>/`）并写 desc
- 整个流程体现"快拍优先"的产品意图

### 3.2 录影：放在 `htc_main_app`（必须等 SD 卡就绪）

- 单段录影体积大（30 秒 1080p H264 ≈ 60MB；10 分钟 ≈ 1.2GB）
- 录影文件必须落 SD 卡，**不能**写到临时位置再搬移（搬移成本高、风险大）
- 录影时机在 `htc_media_app` 阶段不触发，由 `htc_main_app` 的 `CMD_SNAP` 分支根据 `Settings::cameraMode` 派发
- 体现"录影是重量级动作，必须资源就绪"的产品意图

### 3.3 不对称是设计意图，不是 bug

| 维度 | 拍照 (snap) | 录影 (record) |
|------|------------|---------------|
| 执行进程 | `htc_media_app` | `htc_main_app` |
| 文件落点 | 临时位置（早期 mount）| SD 卡正式位置 |
| 后续动作 | `processCmdSnap` 搬移 + 写 desc | 直接在原地 |
| SD 卡就绪要求 | 弱（容忍延迟）| 强（必须就绪）|
| 体积量级 | ~MB | ~百 MB / 段 |

看似"拍照在 media_app、录影在 main_app"违反了"对称设计"原则，但实际是**产品物理约束 + 用户体验**的合理折衷：
- 强行让"拍照也等 SD 卡就绪"会拖累首拍时间
- 强行让"录影也在 media_app 拍"会因 SD 卡未就绪导致录影失败

## 4. 备选方案

### 4.1 方案 A（已弃）：所有拍照录影都在 main_app

- 优点：对称、职责清晰
- 缺点：首拍时间被 SD 卡 mount 时延拖累
- 弃因：违背"省电相机快拍优先"的产品定位

### 4.2 方案 B（已弃）：所有拍照录影都在 media_app

- 优点：实现简单、单进程
- 缺点：media_app 不知道如何处理录影文件落点、desc、DB 写入
- 弃因：media_app 的生命周期不该拖到录影完成（SNAP_ONLY 模式下需要尽快关机省电）

### 4.3 方案 C（采纳）：snap 在 media_app、record 在 main_app

- 优点：每个动作的"前置资源需求"与所处阶段匹配
- 缺点：代码不对称，新人需要看本 ADR 才理解
- 接受代价：通过文档（本文）+ 规格（`specs/main-app-mode-behavior.md`、`specs/media-app-boot-flow.md`）固化知识

## 5. 跨进程的 handoff 契约

`htc_media_app` 与 `htc_main_app` 之间**仅传两个信息**：
- `-wm <0-4>`：working mode 枚举
- `-rtc <0|1>`：RTC 是否正常

**`Settings::cameraMode`（0-5）由两进程各自从 `settings.json` 读**——不通过 CLI 传。这是隐式 handoff，契约见 [`specs/workmode-selection-and-switching.md`](../specs/workmode-selection-and-switching.md) 第 6 节"隐式 handoff 决策"。

`media_app` 用 `Settings::cameraMode` 决定是否 skip `quick_snap`（cameraMode=2 即"仅录影"时跳过拍照）。`main_app` 用 `Settings::cameraMode` 在 `CMD_SNAP` 分支里派发 `processCmdSnap` / `processCmdVideoRecord` / `processCmdConcurrentSnapRecord`。

## 6. 适用范围

本决策**仅约束** boot-time / work mode 的一次性动作：
- `WORKING_MODE_SNAP_ONLY`（0）：media_app 拍 1~N 张 → main_app 搬移 → 关机
- `WORKING_MODE_SNAP_UPLOAD`（1）：media_app 拍 1~N 张 → main_app 搬移 + 录影（按 cameraMode）+ 上传

`WORKING_MODE_TEST_ONLY`（3）、`WORKING_MODE_UVC`（4）、`WORKING_MODE_UPLOAD_ONLY`（2）不走 `CMD_SNAP` 分支，与本决策无关。

## 7. 未来扩展

- 如果未来需要"media_app 阶段也录影"（例如快录 5 秒小视频），需要新增 `quick_record()` 与 `quick_snap()` 对称；本 ADR 不阻碍，但需要评估 SD 卡 mount 时序
- 如果未来需要"main_app 阶段也拍照"（已经实现），属于 `processCmdSnap` + `ImageSnap::snap()` 直接调用，不在本决策范围内
- 详细的 `processCmdSnap` 搬移实现见 [`specs/main-app-mode-behavior.md`](../specs/main-app-mode-behavior.md) 第 3.1 节

## 8. 决策结论

**保留现有不对称设计**。理由：
- 与产品物理约束（省电 + SD 卡 mount 时序）匹配
- snap 的"快"和 record 的"稳"在不同进程分别实现，符合阶段特性
- 通过文档固化知识，不增加代码复杂度
- 任何"强行对称"的方案都会牺牲关键产品特性（首拍速度 或 录影可靠性）

如未来重构需要改变此分布，必须先更新本 ADR 给出新约束，并同步更新 `specs/media-app-boot-flow.md` 与 `specs/main-app-mode-behavior.md`。
