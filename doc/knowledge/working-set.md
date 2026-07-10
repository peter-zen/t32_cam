# t32_cam 当前工作集

## 1. 目的

本文件用于给新会话提供最小充分上下文，不替代详细设计文档。

## 2. 当前关注点

五条 active track 并行：
- **wm-app（最新）**：Phase-1 单功能验证（record/snap/upload 真机绿）完成后，**新建独立 binary `wm`**（`-m 0/1/2`：CAPTURE_ONLY / CAPTURE+UPLOAD / UPLOAD_ONLY）重组这些稳定模块，丢弃 crash 不收敛的 `htc_workmode_app`。架构=任务调度器（pending + Capture/Upload lane + Shutdown task，自管关机）。**时间链（RTC→MCU→NTP + ntpSynced + 关机回写）已用 `time_test` 真机验证通过（7/7 GREEN）**，链代码可 lift 进 wm。规格见 [`specs/wm-app-spec.md`](specs/wm-app-spec.md)，验证见 [`reviews/2026-06-23-time-chain-hw-verification.md`](../reviews/2026-06-23-time-chain-hw-verification.md)。
- **um-app（规划中）**：wm 基本就绪后启动。**关键发现**：um 侧单元（rtsp/http/mdns/daynight）早已是独立 linkable 库，legacy 只在 `runCommands` 做 wiring——故 um=新写编排器组合现成库 + lifecycle，风险 << wm。架构=**server-lifecycle**（非 wm task-scheduler）+ **idle-timeout 自关机**（无客户端 T min→poweroff）。done=binary+L2/sim绿+并存（repoint 延后）。规格见 [`specs/um-app-spec.md`](specs/um-app-spec.md)。
- **quickSnap（最新）**：`htc_media_app` 原地重构 + 改名 → 上电首程序 `quickSnap`，ZL（64MB）唯一拍照者。配置 **γ**（`/config/htc/quicksnap.json` 为 5 字段唯一真相源，**不读 setting.json**）；mode router（SNAP_ONLY→不spawn / SNAP_UPLOAD→`wm -m 2` / UPLOAD_ONLY=heartbeat→`wm -m 3` / TEST_ONLY=user→`um`）；**ZL 1-IMP-per-boot**（wm -m 2/-m 3 不 init IMP）→ wedge 前提不成立；调度走 **fork+execv（无 shell）**。规格见 [`specs/quicksnap-app-spec.md`](specs/quicksnap-app-spec.md)。
- **devtest-automation-loop**：把"串口手敲→人眼读日志→存 log→对照代码"全人工链路自动化成 WSL 上 Claude 驱动的可审计闭环。架构已 grill 定型，正执行 **Phase-0（tracer bullet）**。见 [`decisions/devtest-automation-loop.md`](decisions/devtest-automation-loop.md) + [`todo.md`](todo.md) "DevTest 自动化闭环" track。
- **phase1-module-stabilization**（wm/um 稳定化）：devtest Phase-0 的 tracer bullet 直接服务它（"单进程可重复"判据）。场景测试清单（已有/待实现 + file:line 指针）见 [`specs/scenario-test-manifest.md`](specs/scenario-test-manifest.md)。wm-app 的模块复用依据即来自此。

知识入口骨架（README/overview/working-set/标准子目录）已完成，迁移工作按需推进。

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

### devtest-automation-loop（开发-测试-修复自动闭环）

**状态**：架构已 grill 定型（2026-06-21），**Phase-0 实现完成 + 真机验证通过**（见 [`../../reviews/2026-06-21-devtest-phase0-hw-validation.md`](../../reviews/2026-06-21-devtest-phase0-hw-validation.md)）；挖出两个 IMP 残留 bug（[`bugs/T32-imp-residue-workmode-record-2026-06-21.md`](bugs/T32-imp-residue-workmode-record-2026-06-21.md)）
**架构**：WSL 上 Claude 驱动 → 双平台编译 → NFS 部署 → 常驻串口 broker（`devctl`）跑 app + 抓 `logs/serial.log` → 主机 pytest 确定性判决 → Level-2 人批准修复。8 子决策见 ADR。
**关键约束**：HW 正常退出即 `Misc::poweroff()` 断电（串口无法复活）→ 用 `HTC_TEST_NO_POWEROFF` 标志让 app `_exit(0)` 回 shell；重复跑 IMP 残留必挂 → 依赖 wm/um 稳定化做干净 teardown。
**Phase-0（tracer bullet）**：broker + devctl(run/log/status) + `HTC_TEST_NO_POWEROFF` + `test_wm_repeat.py`（连跑两次 `-wm 0`，断言第 2 轮非 rc=137）+ `noac` 修复 + `/devtest` skill 骨架。
**参考文档**：
- `doc/knowledge/decisions/devtest-automation-loop.md` — 架构 ADR（8 决策 + 循环 + 组件 + 分阶段）
- `doc/knowledge/todo.md` "DevTest 自动化闭环" track — Phase-0 任务 P0-1…P0-7 + done 判据

---

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

### workmode-usermode-split（进程拆分）

**状态**：决策已落定（ADR + spec §14 + reviews note，2026-06-20），代码待 C4 repoint 前执行  
**关键决策**：按运行语义拆进程 —— `htc_workmode_app` 只留 `-wm 0/1/2`（一次性任务），
新建 `htc_usermode_app` 接管 `-wm 3`（CMD_MOBILE）/ `-wm 4`（CMD_RTSP_SERVER）。判定标准
是 `runCommands` 内是否长驻循环，非“与 `-m` 重叠”（`-m`↔`-wm 3`、`-rs`↔`-wm 4` 对称重叠）。  
**参考文档**：
- `doc/knowledge/decisions/workmode-usermode-process-split.md` — 决策全文
- `doc/knowledge/specs/workmode-selection-and-switching.md §14` — 目标态
- `reviews/2026-06-20-workmode-usermode-split.md` — 决策记录

**遗留**：`htc_usermode_app` 未创建；`-m` 子参数兼容、`-wm 3` RGB blink 取舍、CMake link
路线 A/B 见 ADR §7。

---

### phase1-module-stabilization（单功能稳定化 → wm/um）

**状态**：计划已 grill 定型（2026-06-21），待起 `/refactor` 试点
**动机**：`htc_workmode_app` crash 频发；crash 根因=单进程共享关机并发 teardown（kill-switch 压），
非单功能逻辑 bug。退回单功能稳定后再组合。
**关键决策**：提取+测试壳(crash 另立)→L2 真机二进制优先→单进程可重复生产→
crash-prone(record/snap)用 loop-faithful 契约、其余 single-shot→先复用 `/refactor` 验 flow→
wm/um/共享三分(REPLACE，`runCommands` 瀑布退役)→稳定判据=退役 kill-switch+N 轮绿。
**交付**：4 个 L2 二进制(record/snap/upload/rtsp) + 补 2 个 L1 sim(ntp/http)。
**参考文档**：`specs/phase1-module-stabilization-plan.md`（七决策+交付表+模块映射+spec 骨架+执行序列）
**下一步**：起 `/refactor ntp`（最小 inline 抽取 + L1 sim golden）作 flow 试点。

---

### wm-app（新建 wm 程序，重组 Phase-1 稳定模块）

**状态**：规格已 grill 定型；时间链真机 7/7 GREEN；**wm 3 模式矩阵 HW-verified**（`tests/host/test_wm.py`：m2-upload/m0-photo/m0-record/m1-photo/m1-record **5 PASSED**，m0-both/m1-both **xfail**）。**wm 最大风险（IMP 捕获 crash）已排除**。**唯一进行中**：cm==1（photo+record）wedge——根因定位到 `src/hal/ingenic/IngenicVideo.cpp` 的 JPEG→H264 framesource 释放间隙（hal/ 已从 PIC 转交项目维护者）。**【续接 cm==1 修复 → 读 [`reviews/2026-06-24-wm-cm1-handoff.md`](../../reviews/2026-06-24-wm-cm1-handoff.md)】**：4 步计划（hal/ instrument 定位 → same-payload-safe fix → 矩阵回归 → cm==1 聚合 task）。wm 主体完成 + committed（`1df6b1c`）；cm==1 debug 期间的 sharedVideo/oneShot/self-contained-.so 改动待 commit。
**关键决策**：新建独立 binary `wm`（`-m 0/1/2`），与旧 `htc_workmode_app` 并存；内核=任务调度器（pending + Capture/Upload lane + Shutdown task，自管关机，idle-grace G 秒）；触发=可插拔 Trigger（PIR/SimPir/信号）；时间链 wm 自跑（RTC→MCU→NTP + ntpSynced，无 `-rtc` 入参）；关机回写 MCU 仅时间。
**最高风险已收口**：时间链（原 sim-only）已用 `time_test` 真机验证（RTC/MCU/NTP + 链 + 回写均 GREEN）；剩 §11.5 RTC-drift 待决策。
**参考文档**：[`specs/wm-app-spec.md`](specs/wm-app-spec.md)（权威 spec + 决策日志 + 治理规则）、[`../reviews/2026-06-23-time-chain-hw-verification.md`](../reviews/2026-06-23-time-chain-hw-verification.md)（时间链真机验证 + 发现）

---

### um-app（新建 um 程序，组合长驻交互服务）

**状态**：规格已 grill 定型（2026-06-28）；**L1 sim smoke + Slice 2a 完成**——`um_idle` 纯核心（8 用例）+ `src/app/um_app.cpp` 编排器 + `um` CMake target（双平台）。idle-timeout→poweroff 通路 sim 跑通（`--no-http --no-rtsp`，1s timeout → self-SIGTERM → cleanupHook → lc.shutdown → `rc=0`；mDNS 起着仍 timeout 证 spec §4）；T32 MIPS 编译通过。**Slice 2b 完成**——`UmIdleCore` 加 mutex（线程安全：RTSP/HTTP 回调在 libevent/civetweb 线程、tick 在 main；L1 回归 8 绿逻辑不变）+ `RtspServer` 对称加 `registerOnsessionPlayCallback`（引擎单 active-session，PLAY/CLOSED 1:1）+ `um_app` 接 play/close → `onRtspConnect/Disconnect` 续命；双平台编译 + sim 不回归 `rc=0`。**Slice 2c 完成**——`http_server` `begin_request`（CivetWeb universal per-request hook，`return 0` 不破坏路由）+ `__sync` 原子 counter + `http_server_request_seq()` accessor + `um_app` idle-wait poll change-detection → `onHttpRequest`（不混 C/C++ 时钟域）。**双活跃信号 RTSP+HTTP 齐备**，um binary 功能完整（sim 侧 done：L1 8 绿 + 双平台编译 + sim 通路不回归）。待 **L2 真机联调**
**关键发现**：um 侧单元（rtsp/http/mdns/daynight）早已是独立 linkable 库，legacy 只在 `WorkModeRunner.cpp::runCommands` CMD_MOBILE/RTSP 分支做 wiring——故 um **不是"拆模块"而是"组合现成库 + 写 lifecycle"**，风险/工作量 << wm。**关键简化**：um 不重做 IMP boot——`RtspServer::getInstance()` 构造即懒初始化整个 sensor→encoder→streaming 栈，故 `um_app.cpp` = 镜像 `wm_app.cpp` main 骨架 + CMD_MOBILE bring-up/teardown 顺序 + `event_loop` idle-wait。
**关键决策**：架构=**server-lifecycle**（bring-up/idle-wait/teardown，**非** wm task-scheduler，复用 wm 原语）；bring-up=rtsp+http+mdns+day/night（默认全开，`--no-*` flag 覆盖 `-wm4`）；**无模式只 flag**；关机=**idle-timeout 自关机**（无 RTSP 客户端 + 无 HTTP 请求 持续 T → `Power::requestShutdown()` → poweroff）；MCU on-demand 读（STATUS）；时间链复用 wm；done=binary+L2/sim绿+并存（repoint 延后，联合 C4）。
**硬约束**：1-IMP-per-boot → um 与 wm 不能同 boot 共存 → um 是 whole-boot 进程。
**下一步**：~~L1 + Slice 2a/2b/2c~~ ✅（um binary 功能完整：server-lifecycle + idle-timeout + RTSP/HTTP 续命双信号）→ **L2 真机联调**（默认 RTSP IMP 起落 + RTSP 客户端预览续命 + HTTP 请求续命 + idle-poweroff 干净 teardown；re-point B3/B4 idle-timeout case）。可选后续：`--no-audio`/`--record-stream1` flag、HTTP port env 覆盖（sim 80 bind 限制）、`media_app` repoint spawn `um`（联合 wm C4）。
**参考文档**：[`specs/um-app-spec.md`](specs/um-app-spec.md)（权威 spec + 决策日志 + 治理规则）、[`scenario-test-manifest.md`](specs/scenario-test-manifest.md)（B3/B4 验证状态）

---

### quickSnap-app（重构 media_app 为 boot 首程序）

**状态**：规格已 grill 定型（2026-07-09），**待实现**（代码未落地）
**关键决策**：B 原地重构 `htc_media_app`→`quickSnap`（不新建并行二进制）；配置架构 **γ**（`/config/htc/quicksnap.json` 5 字段唯一真相源，quickSnap 只读它不读 setting.json；force_upload 消费后 read-modify-write 回 0、stillSize 内存钳 ≤8M 不写回）；拍照 ≤8M HW scaler（不走 strip）；IMP 释放 = ImageSnap + 末尾 `HalProvider::resetSharedVideo()`（强制 IMP_System_Exit）；调度 **fork+execv 禁 system_call**（释放后才 fork）；handoff 改 `wm -m <2|3>`/`um`（**无 -wm/-rtc**，wm 自跑时间链）。**ZL 1-IMP-per-boot**（wm -m 2/-m 3 不 init IMP）→ cm==1 wedge 定性存疑、非 quickSnap 风险。
**外部依赖/待办**：① wm 新增 `-m 3`（heartbeat 档）；② γ 闭环迁移（wm cameraMode 读点迁 quicksnap.json + Settings 序列化摘 4 字段影子 + um 承担 quicksnap.json 写入）；③ `PC(8)` 一脚两用澄清（POWER_HOLD_PIN 与 mode-check-pin_1 同 pin）；④ `/tmp` tmpfs 上限 + burst 上限实测；⑤ `QUICK_SNAP_DIR` 改名 `/tmp/media/`（wm 同步）；⑥ `htc_media_app` 退役时点。
**参考文档**：[`specs/quicksnap-app-spec.md`](specs/quicksnap-app-spec.md)（权威 spec + 13 决策日志 + 治理规则）

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
