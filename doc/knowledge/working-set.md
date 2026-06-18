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

## 7. 当前活跃任务

### photo-video-concurrent（同步拍录）

**状态**：Sample 级验证已完成，结论已记录  
**关键结论**：
- 并发拍照最佳路径：**CH2 硬件放大到 8M（3840×2160）**，录影 30fps 不受影响
- 并发拍照分辨率上限：**8M**（CH2 Encoder 不支持软件缩放；CH0 软件缩放会拖垮录影到 0.5fps）
- GC4653 需要 **VTS=1680 workaround**（已 push `4e79f75`）

**参考文档**：
- `doc/knowledge/specs/photo-video-concurrent-implementation-plan.md` — 实现计划 & 验证结果
- `doc/knowledge/reviews/photo-video-concurrent-sample-calibration-2026-05-20.md` — 本轮验证校准记录
- `doc/knowledge/bugs/T32-recording-fps-17-investigation.md` — GC4653 FPS 根因分析

**遗留问题**：
- >8M 并发拍照无可行路径（需暂停录影后走 `LargeImageSnap`）
- `LargeImageSnap` 在并发录影时的 CPU 负载影响未实测

---

### mcu-service-integration（MCU Service 集成）

**状态**：实现已完成，sim 下 4 个端点 + 18 个 STATUS 字段全部走 McuService；T32 端待真机联调  
**关键决策**：
- 新增 `service::McuService`（Meyers 单例，进程内），`htc_main_app` 唯一持有
- test mode（`CMD_MOBILE`）下启 5s 轮询线程写到 `McuCache`；work mode 与 SIMU_BUILD 同步直通
- `CameraParameterRegistry` 加 `mcuBinding()` 工厂 + `ParameterStorageKind::MCU` 枚举值；18 个 STATUS 字段绑定到 MCU
- `POST /api/v1/system/workmode` 改返 **HTTP 501**（work mode firmware-only，无 I2C 写路径）
- 新单测 `tests/test_mcu_service.cpp`，sim build 跑 5 个 case

**参考文档**：
- `doc/knowledge/decisions/mcu-service-architecture.md` — 架构决策
- `doc/api/device-and-system-api.md` — 4 个端点契约
- `doc/knowledge/refs/mcu-api-and-register-inventory.md` — **MCU API 完整清单 + 寄存器映射 + HTTP 使用对照 + 未用 API 清单**（132 方法逐项，23 个经 HTTP / 45 个总用 / 88 个未用）

**遗留问题**（follow-up）：
- `MCU::readFirmwareVersion()` 等仍是 stub（返 "1.0.0"），需 MCU 固件侧配合
- 6 个 `SOR_*` 传感器（AL/UVL/NOISE/CO/CO2/O2）暂不接
- `Location_LON/LAT/ELE` / `SPower` / `Device_MAC/IP/IMEI/NO` / `Event_Total/Event_NUFQ` 仍 placeholder
- 新 `/api/v1/mcu/*` 端点（独立 future 任务）
- 6th CAM_Mode 仍未实现

---

### workmode-sdk-extraction（工作模式 SDK 抽离）

**状态**：Phase A 设计文档已完成（T8），Phase B/C 待启动  
**参考文档**：`doc/design/workmode-sdk-architecture.md`（方向 + 路线图），`doc/design/workmode-capability-inventory.md`（能力清单 + 缺口）

---

## 8. 可能的下一步

按优先级建议：
1. 产品代码集成：将 CH2 硬件 8M 并发路径集成到 `VideoRecorder` + `CameraServiceT32`
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
