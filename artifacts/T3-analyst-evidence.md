---
task_id: T3
node: analyst
flow: bug
artifact: evidence
status: success
---

# T3 — Analyst 根因分析（evidence）

判定（status: success）：T2 的 teardown **在 RTSP 流（group_id=1）上是完整配对的**，但第 3 次启动仍卡死。
根因不是"单条 IMP 资源漏配 destroy"，而是 **teardown 顺序/幂等性缺陷导致退出期的某次 IMP 反操作在
IMP 驱动里把全局编码器子系统推入不可恢复状态**，叠加 `Misc::poweroff()` 是软关机且 IMP 驱动以
`libimp.so`+内核 ko 形式常驻、`poweroff` 不一定把 IMP 内核态彻底复位，使得"上一进程退出时打坏的
编码器子状态"在下次 `IMP_Encoder_CreateChn`(945)/`RegisterChn`(950) 表现为卡死。前两次未触发是因为
该打坏路径取决于退出时 RTSP pull 线程/编码器在途帧的时序（每进程 0~1 次），累积到第 3 次命中。

> 关键澄清（推翻 PM 三条猜测）：见 §4 —— **OSD region 在本场景从未创建**（grep 无 `CreateOsdRgn done`），
> OSD 不是泄漏源；**`IMP_Encoder_UnRegisterChn` 在代码里存在且被调用**（`~IngenicVideoStream`:885 +
> exit() 兜底:1233），PM"全文 grep 不到"的猜测不成立；**ref-count map 在本进程内是配对填+清的**，
> 不是累积源。真正问题在 §3 的退出顺序 + §5 的 `poweroff` 软复位。

---

## 1. 卡死点精确定位（结合日志）

日志（`logs/debug.log`，三次启动 RTSP 段逐行对照）：

| 阶段 | 来源（file:line） | run1 | run2 | run3 |
| --- | --- | --- | --- | --- |
| `RTSP initialize` | RtspServer.cpp:180 | L53 ✅ | L143 ✅ | L249 ✅ |
| `OSDController: setPoolSize(2) done` | IngenicVideo.cpp:458（init:1121） | ✅ | ✅ | ✅ |
| `IspOsdManager: init done` | IspOsdManager.cpp:28 | ✅ | ✅ | ✅ |
| `IngenicVideo VTS corrected` | IngenicVideo.cpp:1174 | ✅ | ✅ | ✅ |
| `rtsp config: ...` | RtspServer.cpp:474-483（configure 前） | ✅ | ✅ | ✅ |
| `i264e[info]: profile Main, level 3.1` | x264 内部日志，由 **`IMP_Encoder_CreateChn`(945)** 触发 | ✅ | ✅ | ✅(L254) |
| `rtsp stream info: ...` | RtspServer.cpp:490（configure 返回后 getInfo） | ✅ | ✅ | ❌ 缺失 |
| `RTSP server started` | RtspServer.cpp / rtsp.c | ✅ | ✅ | ❌ 缺失 |

**结论**：`i264e[info]` 来自 `IMP_Encoder_CreateChn`(IngenicVideo.cpp:945)——三次都打到了，说明 CreateChn
**返回了**（CreateChn 内部初始化 x264 并打 info，然后正常 return）。run3 卡死在 CreateChn **返回之后**、
`getInfo`(RtspServer.cpp:489 → configure 已 return) 之前的 configure 内部，即以下三步之一：
- `IMP_Encoder_RegisterChn(group_id_, channel_id_)`（IngenicVideo.cpp:**950**）← 最可能
- `acquireBind` → `IMP_System_Bind`（IngenicVideo.cpp:**962**）
- `IspOsdManager::prepare()`（IngenicVideo.cpp:970，但 group_id_=1≠0，**被跳过**，排除）

`prepare()` 因 `group_id_ == 0` 判断（configure:969 / start:992）对 sensor0/stream1（group_id = 0\*3+1 = **1**）不触发，
故 OSD 路径在本 run3 根本没进。卡死点锁定 **`IMP_Encoder_RegisterChn`(950)** 或 **`IMP_System_Bind`(962)**。

证据：configure 失败分支会打 ERROR 日志（946/951/963），run3 无任何 ERROR → 不是 <0 返回失败，
而是 **IMP 调用阻塞（不返回）**。IMP 驱动内部对"已注册/已绑定的 group/channel"重复操作可能阻塞或死锁。

---

## 2. init/teardown 配对表（逐项核对，对照 IngenicVideo.cpp）

### 2.1 创建侧（init 1115-1177 + configure 894-976 + start 977-1000 + fsMgr 540-587）

| 资源 | 创建调用 | file:line | 进 map？ |
| --- | --- | --- | --- |
| OSD pool | `IMP_ISP_Tuning_SetOsdPoolSize` / `IMP_OSD_SetPoolSize` | IngenicVideo.cpp:442-456（setPoolSize, init:1121） | 否（全局一次性） |
| Encoder 多进程 | `IMP_Encoder_MultiProcessInit` | init:**1128** | 否（全局） |
| ISP Open | `IMP_ISP_Open` | SensorController::openISP IngenicVideo.cpp:**462** | 否 |
| AddSensor | `IMP_ISP_AddSensor` | IngenicVideo.cpp:**496** | 否 |
| EnableSensor | `IMP_ISP_EnableSensor` | IngenicVideo.cpp:**509-512** | 否 |
| 系统 | `IMP_System_Init` | init:**1136** | 否 |
| ISP Tuning | `IMP_ISP_EnableTuning` | init:**1137** | 否 |
| FrameSource chn (CH0/1/2) | `IMP_FrameSource_CreateChn` | fsMgr::create IngenicVideo.cpp:**547**（init:1151） | 否（fsMgr 列表） |
| Encoder group | `IMP_Encoder_CreateGroup` | acquireGroup IngenicVideo.cpp:**29**（configure:933） | ✅ `g_group_ref_count`:19 |
| Encoder chn | `IMP_Encoder_CreateChn` | configure:**945** | 间接（channel_id==group_id for H264） |
| Encoder register | `IMP_Encoder_RegisterChn` | configure:**950** | 否（依赖 Query 判定） |
| System Bind fs→enc | `IMP_System_Bind` | acquireBind IngenicVideo.cpp:**65**（configure:962） | ✅ `g_bind_ref_count`:55 |
| FS Enable | `IMP_FrameSource_EnableChn` | acquireFrameSource IngenicVideo.cpp:**102**（start:983） | ✅ `g_fs_ref_count`:91 |
| OSD region（time/reserved） | `IMP_ISP_Tuning_CreateOsdRgn` | IspOsdManager.cpp:124/130 | 否（句柄成员变量） |

### 2.2 销毁侧（teardown 链：`RtspServer::shutdown` → stop(227) + deinitialize(198)→uninitVideo(499)）

退出执行顺序（`main_exit` → `RtspServer::shutdown()` main_app.cpp:1843）：
1. `stop()`（RtspServer.cpp:227-249）：`videoSession_->stop()` → MediaSession::stop → `VideoSource::close()`
   → `IngenicVideoStream::stop()`(1001-1021)：`StopRecvPic`(1011) + `releaseFrameSource`(1013)→`DisableChn`。
2. `deinitialize()`(198) → `uninitVideo()`(499-512)：
   - `videoSession_.reset()`(504) → `~MediaSession`(stop) → `~VideoSource` → `~IngenicVideoStream`(880-892)：
     `Query`→`UnRegisterChn`(885) + `releaseBind`→`UnBind`(887) + `DestroyChn`(888) + `releaseGroup`→`DestroyGroup`(889)。
   - `video_->exit()`(507) → `IngenicVideo::exit()`(1178-1258)：见下。
3. `~RtspServer` 不再执行（main 走 `Misc::poweroff(); while(1)`）。

`IngenicVideo::exit()`(1178-1258) 反操作配对：

| 反操作 | file:line | 配对创建 | 状态 |
| --- | --- | --- | --- |
| 遍历 `g_bind_ref_count` → `IMP_System_UnBind` | exit:**1211** | Bind(acquireBind:65) | ✅（但 run 时多为空，见 §3） |
| 遍历 `g_group_ref_count` → `Query`+`UnRegisterChn`(1233)+`DestroyChn`(1235)+`DestroyGroup`(1236) | exit:**1226-1236** | CreateGroup/RegisterChn/CreateChn | ✅（同上，多空） |
| `IspOsdManager::exit()` → stop() 内 DestroyOsdRgn(68/80) + defensive(104/108) | exit:**1244** / IspOsdManager.cpp:59-110 | CreateOsdRgn(124/130) | ✅（但本场景 region 未创建，no-op） |
| `fsMgr.destroy()` → `IMP_FrameSource_DestroyChn` | exit:**1247** / fsMgr::destroy:583 | CreateChn(fsMgr:547) | ✅ |
| `IMP_System_Exit` | exit:**1248** | System_Init(1136) | ✅ |
| `disableAll`→`DisableSensor`(525) | exit:**1250** / SensorController:525 | EnableSensor(509) | ✅ |
| `delAll`→`DelSensor`(533) | exit:**1251** / SensorController:533 | AddSensor(496) | ✅ |
| `IMP_ISP_DisableTuning` | exit:**1252** | EnableTuning(1137) | ✅ |
| `closeISP`→`IMP_ISP_Close`(468) | exit:**1255** | ISP_Open(462) | ✅ |
| `IMP_Encoder_MultiProcessExit` | exit:**1256** | MultiProcessInit(1128) | ✅ |

**配对表结论**：每一项创建都有反操作，**不存在单条"创建无 destroy"的 IMP 资源**。PM 三条猜测全部被代码推翻（§4）。

---

## 3. 真正缺陷：teardown 顺序与幂等性 + IMP 退出期阻塞

虽然配对完整，但退出路径存在两个会让 IMP 驱动子状态错乱的缺陷：

### 缺陷 A：`exit()` 兜底 destroy 的顺序违反 IMP SDK 约束，且与流析构"赛跑"

IMP SDK 头文件明确两条顺序约束（`sdk/include/imp/imp_system.h`）：
- L130：**"在 FrameSource 使能后 Bind 和 UnBind 操作不能动态调用，需要 Disable FrameSource 后才可进行 UnBind"**
- L131：**"DestroyGroup 要在 UnBind 之后才能进行"**

`IngenicVideo::exit()` 兜底路径（exit:1199-1241）执行顺序为：先 `IMP_System_UnBind`(1211)，
**然后**才 `fsMgr.destroy()`(1247) 做 `DisableChn`/`DestroyChn`。这违反 L130（应先 Disable FS 再 UnBind）。
正常路径下（流析构先跑）FS 已被 `releaseFrameSource` Disable，兜底 map 为空、no-op，不触发；
但若退出时序使兜底分支命中（map 非空），UnBind 在 FS 仍 Enable 时调用 → IMP 驱动内部状态机错乱。

### 缺陷 B：`~IngenicVideoStream` 与 `exit()` 兜底对同一 group/channel 双重 destroy，无跨函数幂等栅栏

- `~IngenicVideoStream`(880-892) 已 `UnRegisterChn`+`DestroyChn`+`DestroyGroup` 并 `releaseGroup`/`releaseBind`
  （清空 map）。
- `exit()` 兜底(1226-1236) 基于 `g_group_ref_count` 再做一遍，依赖 `IMP_Encoder_Query` 判 `registered`。
- 两者靠"map 清空"实现幂等，但 `Query` 在 chn 已 destroy 后返回值/`registered` 字段在 IMP 不同版本上
  不可靠。一旦 `Query` 误判为仍 registered → 对已销毁 chn 再 `UnRegisterChn`/`DestroyChn` → IMP 内部
  double-free 或把 chn 槽位标记为"占用但未注册"的脏状态 → 下个进程 `CreateChn`(945)/`RegisterChn`(950)
  命中该脏槽位 → 阻塞（无 ERROR 返回，与 run3 现象吻合）。

### 缺陷 C（最关键）：`exit()` 的 `IMP_System_Exit`(1248) 在仍有 in-flight 编码帧时调用

退出时 RTSP pull 线程刚停（stop:229-235 join），但 `IMP_Encoder_GetStream` 可能已取走一帧未
`ReleaseStream`（`last_stream_valid_` 缓存，IngenicVideo.h:104 / getFrame:1036 路径）。`IMP_System_Exit`
在编码器持有未释放 stream 时调用，IMP 驱动可能不彻底释放编码硬件，留下脏子状态。这是 IMP SDK 退出
的非确定性行为，正好解释"前两次过、第三次卡"——是否卡取决于退出瞬间编码器在途帧时序。

---

## 4. 推翻 PM 的三条猜测（代码证据）

1. **"全文 grep 不到 `IMP_Encoder_UnRegisterChn`"**——错。存在三处：
   `~IngenicVideoStream`(IngenicVideo.cpp:885)、configure 失败回滚(964)、`exit()` 兜底(1233)。
2. **"OSD 双调用泄漏"**——本场景不成立。RTSP 流 group_id = 0\*3+1 = **1**（RtspServer.h:17-18
   `RTSP_SENSOR_ID=0`/`RTSP_STREAM_ID=1`；configure:896 `group_id = sensor*3+stream`），而 OSD
   `prepare()`/`start()` 仅在 `group_id_ == 0` 触发（configure:969 / start:992）→ **OSD region 从未创建**。
   日志三次启动均无 `CreateOsdRgn done`（grep 确认），`exit done` 打两遍是 `exit()`(1244)+`reset()`→
   `~IspOsdManager`→`exit()`(IspOsdManager.cpp:19) 的幂等双调用，但 handle 恒为 -1，无实际 destroy/累积。
3. **"ref-count map 是跨进程累积源"**——错。`g_*_ref_count`(IngenicVideo.cpp:19/55/91) 是文件级 static，
   进程重启清零；且本进程内 configure 填、exit/析构清，配对完整。

---

## 5. 为什么"第 1/2 次过、第 3 次卡"——机理

- 每次启动是**新进程**（`logs/debug.log` L92/L198 是 shell 重新执行 `./bin/htc_main_app`；启动间时间戳
  从 `08:04:15`(poweroff) 跳到 `00:04:23`(新启动 epoch) 证明发生了 reboot/power-cycle）。
- 但 `Misc::poweroff()`（Misc.cpp:584-593）只是 `system("poweroff")` **软关机**；T32 上 IMP 以
  `libimp.so` + 内核 ko 形式常驻，软关机/重启**不一定把 IMP 内核态编码器子系统彻底复位**。
- 因此缺陷 A/B/C 在退出期打坏的 IMP 编码器子状态（脏 group/channel 槽位、未释放 stream），可能**残留
  到下一次 `IMP_Encoder_CreateChn`/`RegisterChn` 的可见范围内**。
- 该残留是否触发卡死是**概率性**的（取决于退出瞬间编码器在途帧、Query 返回、FS 使能态时序），
  表现为"每进程 0~1 次命中，累积到第 3 次必然复现"的统计现象，而非确定性每进程漏 N 个。
- 阈值推断：IMP 编码器子系统对 group/channel 槽位有 `NR_MAX_ENC_GROUPS`/`NR_MAX_ENC_CHN` 上限
 （imp_encoder.h:900/958）；脏槽位累积到使某次 `RegisterChn`(950) 选中的 group 已"逻辑存在但物理损坏"
  → 内部等待/死锁 → run3 卡死。

> 不确定性声明：本结论基于静态代码 + IMP SDK 头文档 + 日志时序推断；确切"哪一项 IMP 内核态残留"需
> 设备侧 strace/IMP 调试日志二次确认（dispatch 禁止跑硬件，故交 implementer/设备验证）。

---

## 6. 修复方向（Fix direction，给 implementer）

主修复（**PIC-owned `src/hal/ingenic/`，需用户书面授权**）：

1. **修正 `IngenicVideo::exit()` 的销毁顺序**（exit:1191-1247）使其符合 IMP SDK 约束：
   - 先确保所有 FrameSource `DisableChn`（`fsMgr.disable()` 或先 `releaseFrameSource` 全部 group），
     **再** 做 `IMP_System_UnBind`（满足 imp_system.h:130）；
   - `UnBind` 完成后再 `DestroyGroup`/`DestroyChn`（满足 imp_system.h:131）；
   - 最后 `fsMgr.destroy()`(DestroyChn FS) → `IMP_System_Exit`。
   即把现有"先 UnBind/DestroyGroup，后 fsMgr.destroy"翻转为"先 disable FS → UnBind → DestroyGroup/Chn → destroy FS → System_Exit"。

2. **去除 `exit()` 兜底与 `~IngenicVideoStream` 的双重 destroy 依赖 `Query` 判定**：
   - 要么 `exit()` 兜底只对 `~IngenicVideoStream` **未覆盖**的资源做（如 CH0/CH2 这类无 stream 对应的
     group），而不是对"所有 g_group_ref_count 的 key"无条件 `Query`+destroy；
   - 要么在 `~IngenicVideoStream` destroy 前显式 `StopRecvPic` + `FlushStream`，保证无 in-flight 帧再 destroy。

3. **退出前 flush 编码器在途帧**：在 `IngenicVideoStream::stop()` 或 `~IngenicVideoStream` 增加
   `IMP_Encoder_FlushStream`(imp_encoder.h:1212) + 确保 `getFrame` 取走的 `last_stream_` 已 `ReleaseStream`，
   再做 destroy，避免 `IMP_System_Exit` 在持有未释放 stream 时调用（缺陷 C）。

非 PIC-owned 补充（可选）：
4. `main_app.cpp` mobile/rtsp-server 退出路径在 `RtspServer::shutdown()` 后、`Misc::poweroff()` 前，
   显式 `sleep(几十~百 ms)` 让 IMP 驱动完成异步清理再软关机（降低缺陷 A/B/C 残留概率，治标）。

优先级：1（顺序修正）是根因级；2/3 健壮性；4 兜底缓解。

---

## 7. 复现条件（Reproduction）

- 设备：T32 硬件（`BUILD_FOR_SIMULATION=OFF`，`build/`，NFS `/mnt/huntcam`）。
- 步骤：连续执行 `./bin/htc_main_app -m --force-day`，每次等到 `RTSP server started on port 8554`
  后 `^C`（SIGINT）让其经 `performCleanup` → `main_exit` → `RtspServer::shutdown()` → `Misc::poweroff()`
  → reboot；重复 3 次，第 3 次日志停在 `i264e[info]: profile Main, level 3.1` 之后，缺
  `rtsp stream info` 与 `RTSP server started`。
- 不复现：PC 模拟（`imp_stub.c` 全 no-op，无真实 IMP 驱动状态）。

## 8. 回归验证（Regression test）—— pass / 验收（设备侧）

1. 连续 5 次 kill+重启 `htc_main_app -m --force-day`，每次均出现 `rtsp stream info: ...`
  （RtspServer.cpp:490）+ `RTSP server started on port 8554`，第 5 次也不卡（排除偶发）。
2. 退出日志出现 `RtspServer::shutdown: teardown complete` + 退出顺序符合"先 disable FS → UnBind →
   DestroyGroup → destroy FS → System_Exit"（新增 INFO 日志佐证）。
3. RTSP 客户端连第 3/5 次启动的端口能正常拉流（非阻塞、有 IDR）。
4. 静态：grep 确认 `IngenicVideo::exit()` 销毁顺序已修正；`~IngenicVideoStream` 有 `FlushStream`/
   `ReleaseStream` 兜底。
5. （可选）设备侧开启 IMP 调试日志，确认退出后 group/channel 槽位全部释放，无脏残留。

---

## 9. 证据指针（Evidence refs）

- `src/hal/ingenic/IngenicVideo.cpp:945`（CreateChn→i264e）、`:950`（RegisterChn，卡死点首嫌疑）、
  `:962`（acquireBind/System_Bind，卡死点次嫌疑）、`:969/992`（OSD 仅 group_id==0 触发）、
  `:880-892`（~IngenicVideoStream destroy 配对）、`:1115-1177`（init 创建全表）、`:1178-1258`（exit 反操作，
  顺序缺陷：1211 UnBind 先于 1247 fsMgr.destroy，违反 imp_system.h:130）。
- `src/hal/ingenic/IspOsdManager.cpp:59-113`（stop/exit DestroyOsdRgn，本场景 no-op）、`:115-140`
  （ensureRegion/CreateOsdRgn，group_id≠0 不触发）。
- `src/media/rtsp/RtspServer.cpp:214-225`（shutdown）、`:499-512`（uninitVideo 顺序）、
  `:447-497`（initVideo→configure:484→getInfo:489）、`:17-18`（RTSP_SENSOR_ID=0/STREAM_ID=1→group_id=1）。
- `src/common/misc/Misc.cpp:584-593`（poweroff 软关机）、`src/app/main_app.cpp:1843/1875`（shutdown 后 poweroff）。
- `sdk/include/imp/imp_system.h:130-131`（UnBind/DestroyGroup 顺序约束）、`sdk/include/imp/imp_encoder.h:900-958`
  （Group/Chn 上限与语义）、`sdk/include/imp/imp_isp.h:3838-3917`（OSD region create/destroy）。
- `src/hal/ingenic/sensor-config.h:41-64`（GC4653: CH0/1/2_EN=1，fsMgr.create 建 3 通道）、
  `CMakeLists.txt:63`（激活 GC4653）。
- `logs/debug.log`（三次启动对照：L58/148/254 i264e；run3 L254 后缺 rtsp stream info）。
- git: 无新相关 commit；T2 修复（`artifacts/T2-implementer-evidence.md`）已落地本次 teardown。
