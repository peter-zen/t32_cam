# 2026-06-23 — wm 程序规格 grill 记录

> 通过 `/grill-me` 对「新建 wm 程序」做了一轮决策树访谈，定型后落成权威 spec
> [`../doc/knowledge/specs/wm-app-spec.md`](../doc/knowledge/specs/wm-app-spec.md)。本文件仅留校准痕迹。

## 起因

Phase-1 单功能稳定化（record/snap/upload 真机绿、ntp/mcu/http 仿真绿）完成后，要在这些已验证模块上重组一个稳定的「一次性任务」程序。旧 `htc_workmode_app` 因共享关机并发 teardown crash 不收敛，决定**新建独立 binary `wm`**，不复用旧 app。

## grill 定型的关键决策（12 条，详见 spec §12）

1. 新建 `wm`，与旧 app 并存（不破坏 spawn 契约）。
2. `-m 0/1/2` = CAPTURE_ONLY / CAPTURE+UPLOAD / UPLOAD_ONLY。
3. 内核 = 任务调度器（pending + Capture lane + Upload lane + 单独 Shutdown task），task 类型可扩展。
4. 关机自管：三队列全空持续 G 秒 → Shutdown；Upload 超时清除 task；Shutdown 不 flush；MCU override 另流程。
5. 触发 = 可插拔 Trigger 接口（PIR / SimPir / 信号）。
6. cameraMode 0/1/2（不含并发 3）；每 Capture 产出 文件+缩略图DB+元数据DB+desc。
7. 回写 MCU = 仅时间。
8. 时间链 wm 自跑（RTC→MCU→NTP + ntpSynced），无 `-rtc` 入参。
9. m0 离线 + 兜底文件名；不可信则跳过 MCU 回写。
10. 配置：产品走 setting.json，旋钮走 HTC_* env（G=30s / upload=60s / one-shot 默认关）。

## 关键事实校正

- 用户初始前提「manifest 里的程序 T32 都测过了」**部分不成立**：按 manifest 真相源，**ntp/mcu 仅 sim-green、未上 HW**。→ 时间链（依赖 mcu/ntp/rtc）是落在 sim-only 模块上的新编排，标为**最高风险**，需优先单独验证。spec §10/§11 已标注。

## 代码坑（spec §5.3 已记）

旧 workmode 路径 `processCmdSnap` / `processCmdConcurrentSnapRecord` **不写 DB、不存缩略图**。wm Capture task 必须走 `ImageSnap::snap`（photo, `ImageSnap.cpp:393`）/ `VideoRecorder`（video, `VideoRecorder.cpp:981`）这两条会写 DB 的路径，并由调用方 `saveThumbnail`。

## 治理

spec 已写入治理规则：改功能必须同步更新 spec；实现与 spec 冲突先询问是否改 spec。

## 下一步

- 时间链 L1 sim golden + L2 真机冷启验证（最高优先）。
- `src/app/wm/` 骨架 + 调度器 + 三模式 lane 接线。
