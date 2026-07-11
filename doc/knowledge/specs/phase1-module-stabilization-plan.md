# Phase-1 模块稳定化计划（单功能稳定 → 组合 wm/um）

## 1. 目的

`htc_workmode_app` crash 频发、修复不顺。本计划退回**单功能稳定化**：先把每个功能
抽成干净、可独立测试的单元并在真机验证，确认无误后（Phase-2）再组合成目标态
`wm`（workmode）/`um`（usermode）单进程应用。

本文是后续每个 `/refactor`（或 crash-prone 模块的组合 flow）的 **planner 读源**：
锁定架构决策、交付物、稳定判据、模块边界，避免逐模块反复澄清。

> 本文由 2026-06-21 grill 会话定型。决策一经执行即作为真相源；变更走 ADR。

## 2. crash 根因（治理对象）

crash **不是单功能逻辑 bug**，而是**单进程共享关机的并发 teardown 竞态**：

- `releaseVideoResources → ~VideoRecorder → IMP_System_Exit` 与**仍在每帧触发的 ISP
  硬件中断**（`tisp_day_or_night_s_ctrl → defog`）抢，解引用已释放 buffer → kernel panic。
- 竞态成立的前提是「多功能共享一个关机序列 + 并发」（录影线程 + upload worker +
  event loop + ISP ISR 同时在跑）。

现生产能跑，是靠一排 **kill-switch** 压住 crash：

| kill-switch | 压住的竞态 | 代码位置 |
|---|---|---|
| `HTC_SKIP_ISP_DAYNIGHT_ON_SHUTDOWN=1` | 关机 ISP day/night 切换挑起 defog 刷新 | `workmode_app.cpp` cleanupHook |
| `HTC_SKIP_TEARDOWN_ON_SHUTDOWN=1` | 关机跳 `releaseVideoResources`→跳 `IMP_System_Exit` | `record_task.cpp::stop()` |
| `opts.audio=false` 默认 | `audio.ko` 在 `spk_gpio=-1` 空指针崩 | `record_task.cpp` / `WorkModeRunner.cpp` |
| `HTC_RECORD_NO_THUMBNAIL=1` | CH2 缩略图并发 SDK 干扰主码流→polling 超时空 mp4 | `record_task.cpp` / `WorkModeRunner.cpp` |

“修复不顺” = 想逐个拆掉这些 kill-switch，而它们压的竞态就长在单进程循环里。
生产 T32 仅 64MB（rmem 24M），是所有架构选择的硬约束。详见
`doc/knowledge/bugs/T32-recording-fps-17-investigation.md` 与
`decisions/workmode-usermode-process-split.md`。

## 3. 七项决策（grill 锁定）

| # | 决策点 | 结论 |
|---|---|---|
| 1 | Phase-1 性质 | **提取 + 测试壳**；crash 修复（关机时序硬化）**另立工作流**，二者在 record/snap 上交汇 |
| 2 | 测试层 | **L2 真机二进制优先**（`sdk_stub` 跑不出 crash 类）；L1 sim 缺口顺带补 |
| 3 | 生产架构 | **单进程 + 可重复触发**（不 per-record respawn，控进程数，SDK init 太贵） |
| 4 | 契约分类 | crash-prone(record/snap)=**loop-faithful**；其余=**single-shot** |
| 5 | flow 选型 | **先复用 `/refactor`** 跑非 crash-prone 验 flow；record/snap 的 flow 据表现再定 |
| 6 | Phase-2 拓扑 | **wm/um/共享 三分**；Phase-1 抽出 unit = Phase-2 组成单元（REPLACE，`runCommands` 瀑布退役） |
| 7 | 稳定判据 | **退役 kill-switch + N 轮复现绿**（mp4 非空 + FPS≥阈 + desc 上传成功） |

**贯穿逻辑**：crash 是单进程共享关机并发 teardown（kill-switch 压）→ record/snap 的
「功能稳定」与「crash 修复」在这俩模块上必然交汇（决策 4）；其余模块可干净切分。

## 4. 模块 ↔ 现状 APP 映射（代码事实）

| 模块 | 现状代码位置 | 形态 |
|---|---|---|
| 1 拍照(带缩略图) | `media_app.cpp::quick_snap`(ImageSnap，无缩略图) + `WorkModeRunner.cpp::processCmdSnap`/`processCmdConcurrentSnapRecord` + CH2 缩略图(`VideoRecorder::captureThumbnail`/`CameraRecorder`) | 分散 |
| 2 录影(带缩略图/可重复/audio) | `WorkModeRunner.cpp::processCmdVideoRecord` + `record_task.cpp`(RecordTask) + `CameraRecorder`；audio 走 `HTC_RECORD_AUDIO` | 已实现 |
| 3 NTP | `WorkModeRunner.cpp` CMD_NTP(~470) + `app_lifecycle::syncSystemTime` + `RTC::setTime` | inline |
| 4 upload | CMD_UPLOAD(~738) + `upload_worker.cpp`(UploadWorker) + `MgmtServClient`/`StorageServClient` | 已抽 worker |
| 5 RTSP(audio on-off) | CMD_RTSP_SERVER(~702) + `runRtspServerUntilSignal` + `RtspServer`；`--no-audio→HTC_NO_AUDIO`；CH1 | 已实现 |
| 6 缩略图+文件信息DB | `src/storage/`(MetadataDao/DatabaseManager/MediaScanner) + `src/manifest/`；CH2 | 独立库 |
| 7 HTTP控制+属性 | `src/service/http_server/http_api_v1.cpp` + `CameraParameterRegistry` + `CameraServiceT32` + `McuService` | 已实现 |
| 8 mDNS | `MdnsService`(CMD_MOBILE 分支 ~633) | 已实现 |
| 9 HTTP渐进式回放(fMP4) | `http_api_v1.cpp::api_v1_camera_video_playback`(~1068，支持 fmp4/mp4 + playback_token) | 已实现 |
| 10 MCU get/set | `MCU` + `McuService` + `McuCache` | 已实现 |
| 11 Network 切换 | **`net`**(`net_app.cpp --type wifi/eth/usb`) + `Misc::connectWifi/startDHCP` + `UsbDongle` | **已是独立 APP** |

**5 个 APP 真实角色**：
- `htc_media_app` = boot launcher（工作模式判定 + quick_snap + RTC/时间同步 + spawn daemon & main）
- `htc_main_app` = 巨石 CLI，所有 `CMD_*` 载体（多数模块的实际家）
- `htc_daemon_app` = 关机守护（进程监督 + 关机 GPIO 置输入）
- `net` = 已拆好的单功能网络工具（不是“准备拆”）
- `htc_workmode_app` = `-wm` 薄壳（**C3 UNSPAWNED**：`media_app` 仍 spawn `main_app -wm`，未 repoint）+ 新 `EventLoop`(`-wm 0` PIR 录影)

**两处纠正**：`net` 已拆完可用；`htc_workmode_app` 已建但未上线。`snap_test` 是
“单功能测试程序”范本（目前仅覆盖模块 1，且是 48M 可行性工具，非生产 quick_snap 路径）。

## 5. 现有测试覆盖（决定缺口）

- **L1 sim（`tests/`，sdk_stub）已覆盖 5/11**：模块 6(`test_database`)、8(`test_mdns_*`×2)、
  9(`test_minimp4_fragmented_mux`)、10(`test_mcu_service`)、11(`test_net_app_logic`)；
  模块 7 部分(`test_camera_properties`/`test_camera_service`)。
- **L2 真机二进制（`*_test` 可执行）仅 1 个**：`snap_test`（模块 1，非生产路径）。
  `htc_mcu_api_test` 只在注释里，不在本仓库 CMake。
- **缺口**：模块 2/3/4/5/7 既无 L1 sim 也无 L2 二进制。

**关键**：crash 类只在 L2 真机复现，L1 sim 永远跑不出 → L2 是 crash 工作流的唯一仪器层。

## 6. Phase-1 交付物

| 模块 | 契约 | CH映射 | 交付物 | 缺口 |
|---|---|---|---|---|
| 2 record | **loop-faithful** | CH0 主 + CH2 缩略图 | L2 二进制 | 缺 |
| 1 snap | **loop-faithful** | CH0 主 + CH2 缩略图 | L2 二进制（生产 quick_snap 路径） | 缺 |
| 4 upload | single-shot | — | L2 二进制 | worker 已抽，缺二进制 |
| 5 rtsp | single-shot + clean stop | CH1 | L2 二进制 | 缺 |
| 3 ntp | L1 sim | — | `tests/` 单测 | 缺 |
| 7 http 控制+回放 | L1 sim | — | `tests/` 单测 | 部分缺 |
| 6 thumbnail/DB | 随 record/snap L2 + `test_database` | CH2 | — | sim✓ |
| 8 mdns / 10 mcu | 已有 sim | — | — | ✓ |
| 11 network | 已是 `net` | — | — | ✓ |

**实物 ≈ 4 个 L2 二进制（record/snap/upload/rtsp）+ 补 2 个 L1 sim（ntp/http）。**

## 7. Phase-2 拓扑（抽取目标）

```
media_app (supervisor)
  ├─ spawn daemon_app          (关机守护，现状)
  ├─ spawn wm  (单进程)         snap / record / upload / ntp   ← 一次性+可重复触发
  ├─ spawn um  (单进程，长驻)   rtsp / http控制+回放 / mdns     ← 等连接/信号
  └─ 前置 net           network（wm/um 启动前 spawn 一次拿 IP）

共享库（wm/um 各链/各实例）：mcu(McuService) / thumbnail+DB(storage) / manifest
```

- **Phase-1 抽出的干净 unit = Phase-2 wm/um 的组成单元**（`/refactor` 的 REPLACE 语义：
  抽出的 unit 成为唯一源，`app_workmode::runCommands` 大瀑布在抽取过程中**逐步退役**，
  不是“测试壳调一份、生产另留一份”的重复）。
- 与 `decisions/workmode-usermode-process-split.md` 的关系：Phase-2 即**用测过的干净单元
  实现 ADR 的目标态**（wm=`-wm 0/1/2`，um=`-wm 3/4`）。本计划是 ADR 的执行机制。

## 8. 每模块 spec 骨架（planner 模板）

每模块进 flow 前，先落 `doc/knowledge/specs/<module>-spec.md`，含 8 节：

1. **功能边界**：做什么 / 不做什么（避免 scope 蔓延）
2. **现状代码位置**：file:line 指针
3. **契约类型**：loop-faithful / single-shot / L1 sim
4. **CH 通道映射**（snap/record 专属）：CH0 主码流 / CH1 RTSP / CH2 缩略图
5. **稳定判据**：crash-prone = kill-switch 退役 + N 轮绿；其余 = 功能正确性清单
6. **抽取目标 API**：wm/um 将 compose 的干净 unit 接口签名
7. **测试程序**：L2 二进制名 + CLI/env 旋钮（N、interval、audio、thumb、bitrate）；或 L1 sim 用例名
8. **kill-switch 清单**（crash-prone 专属）：逐个退役计划

## 9. 执行序列（依赖序）

1. **/refactor 试点（验 flow）**：先 ntp（最小 inline 抽取 + L1 sim golden），再 rtsp
   （L2、真抽 RtspServer 臂）。跑 2–3 个判断 `/refactor` 够不够用。
2. **定 record/snap 的 flow**：据 1 的表现，选「`/bug`→`/refactor` 两段」或新建
   「stabilize」组合 flow（extract→测试二进制→repro+fix→verify）。
3. **crash-prone 闭环（record/snap）**：loop-faithful 二进制 + 逐个退役 kill-switch +
   N 轮绿（= 决策 1 的 crash 工作流在此交汇）。
4. **Phase-2 组合**：wm{snap,record,upload,ntp} + um{rtsp,http,mdns} 单进程；删 `runCommands`
   瀑布；C4 repoint（`media_app` 改 spawn wm/um，见 ADR §7.4）。
5. **改名 wm/um，收尾。**

## 10. 留待 planner 节点定（可逆/低风险，不在本计划锁）

- 具体稳定判据数值：N（轮数）、FPS 阈值、mp4 非空校验粒度。
- 分支策略：Phase-1 落点（当前 `merge_develop_simu`）。
- C4 repoint 时机：workmode_app/wm 何时替换 `main_app -wm` 成为 live 路径。
- wm/um 改名时机。
- upload/rtsp 是否也建 loop 变体（upload 在生产里与 record 并发，其并发性由 record 的
  loop-faithful 二进制覆盖；rtsp 的 clean-stop 是否需独立判据）。

## 11. 相关文档

- `decisions/workmode-usermode-process-split.md` — workmode/usermode 进程拆分 ADR（§7.5 标注本计划为执行机制）
- `specs/workmode-selection-and-switching.md §14` — 进程归属目标态
- `bugs/T32-recording-fps-17-investigation.md` — record FPS/crash 根因
- `reviews/photo-video-concurrent-sample-calibration-2026-05-20.md` — 并发拍录验证
