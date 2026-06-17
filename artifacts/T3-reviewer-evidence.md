---
task_id: T3
node: reviewer
flow: bug
artifact: evidence
status: success
---

# T3 — Reviewer 独立审查证据（evidence）

独立重编译双平台 + 逐项核对 implementer 改动（仅 `src/hal/ingenic/IngenicVideo.cpp`，+106/-4）。
判定：**passed / success**（无 must-fix；1 个 nice-to-have 小瑕疵 + 设备侧回归缺口，均不阻断 merge）。

---

## 1. 独立重编译双平台（exit 0，非信报告）

### 1.1 T32 硬件 build（`build/`，BUILD_FOR_SIMULATION=OFF + toolchain.cmake）

命令：`cmake --build build -j$(nproc)`

```
[ 96%] Built target htc_daemon_app
[ 96%] Built target snap_test
[ 96%] Built target htc_media_app
[ 99%] Built target camera_service
[100%] Built target http_server
[100%] Built target htc_main_app
=== build exit: 0 ===
```

**exit 0**，`Built target htc_main_app`。独立复现成功。

### 1.2 PC 模拟 build（`build_sim/`，BUILD_FOR_SIMULATION=ON）

命令：`cmake --build build_sim -j$(nproc)`

```
[ 97%] Built target camera_service
[ 98%] Built target test_camera_properties
[ 99%] Built target http_server
[ 99%] Built target test_http_server
[100%] Built target htc_main_app
=== build_sim exit: 0 ===
```

**exit 0**，`Built target htc_main_app` + `test_http_server`/`test_camera_properties`。独立复现成功。

---

## 2. exit() teardown 顺序 SDK 合规（核心必查）

### 2.1 SDK 约束原文（`sdk/include/imp/imp_system.h:129-131`，独立核对）

```
129  * @attention 建议所有的Bind的操作在系统初始化时进行。
130  * @attention 在FrameSource使能后Bind和UnBind操作不能动态调用，需要Disable FrameSource后才可进行UnBind。
131  * @attention DestroyGroup要在UnBind之后才能进行。
```

→ L130：UnBind 必须在 FrameSource Disable **之后**。
→ L131：DestroyGroup 必须在 UnBind **之后**。

### 2.2 exit() 实际顺序（`IngenicVideo.cpp:1189-1301`，行号递增 = 执行顺序）

| 步 | 操作 | 行 | SDK 满足 |
| --- | --- | --- | --- |
| 1 | `fsMgr.disable()`（index=-1 → 遍历 channels_，逐 `IMP_FrameSource_DisableChn`） | 1213 | L130 前置：FS 先 Disable ✓ |
| 2 | flush：遍历 `g_group_ref_count` → `IMP_Encoder_StopRecvPic(chn)` | 1228 | 退出前停接收，配对 start ✓ |
| 3 | UnBind：遍历 `g_bind_ref_count` → `IMP_System_UnBind` → clear | 1253 | L130 ✓（在 FS Disable 后）|
| 4 | destroy：遍历 `g_group_ref_count` → `UnRegisterChn`/`DestroyChn`/`DestroyGroup` → clear | 1275-1277 | L131 ✓（在 UnBind 后）|
| 5 | `ispOsdMgr_->exit()` + reset | 1286 | ISP/OSD teardown |
| 6 | `fsMgr.destroy()`（DestroyChn FS） | 1290 | 配对 fsMgr.create:1162 |
| 7 | `IMP_System_Exit()` | 1291 | 配对 System_Init |
| 8 | sensor disableAll/delAll/DisableTuning/closeISP | 1293-1298 | 配对 init |
| 9 | `IMP_Encoder_MultiProcessExit()` | 1299 | 配对 MultiProcessInit:1128 |

**结论（passed）**：顺序 = Disable FS → flush/StopRecv → UnBind → DestroyGroup/Chn → ISP/OSD → destroy FS → System_Exit → sensor/ISP → MultiProcessExit。
- L130 ✓：UnBind(1253) 在 FS Disable(1213) 之后。
- L131 ✓：DestroyGroup(1277) 在 UnBind(1253) 之后。

独立 grep 印证 `fsMgr.disable()`(1213) 与 `fsMgr.destroy()`(1290) 分处 System_Exit(1291) 前后两侧，翻转自 T2 的"先 UnBind 后 destroy FS"。

---

## 3. 幂等 / 无双重 destroy

### 3.1 职责划分（解决 analyst 缺陷 B）

| 资源 | 正常路径销毁者 | map erase | exit() 兜底 |
| --- | --- | --- | --- |
| per-stream group | `~IngenicVideoStream` releaseGroup → `DestroyGroup` + erase | g_group_ref_count.erase | map 已空 → no-op |
| per-stream bind | `~IngenicVideoStream` releaseBind → `UnBind` + erase | g_bind_ref_count.erase | map 已空 → no-op |
| per-stream fs | `~IngenicVideoStream` releaseFrameSource → `DisableChn` + erase | g_fs_ref_count.erase | （fsMgr.disable 覆盖） |
| per-stream chn | `~IngenicVideoStream` UnRegisterChn + DestroyChn | （channel_id==group_id for H264）| 兜底无条件 UnRegister/Destroy |

**无双重 destroy（passed）**：`~IngenicVideoStream`(897-900) 先跑、先 erase 三张 map；exit() 兜底(1220/1237/1268)遍历到的 map **已空** → 循环体不执行、不调任何 IMP destroy。exit() 兜底仅覆盖"析构未覆盖的残留"（如 main 走 poweroff/while(1) 跳过单例析构、map 非空时）。

### 3.2 幂等栅栏

- `exitCalled_`(1190 首行 `if (exitCalled_) return true`) 防 exit 重复进入。
- `g_video_init_ref_count.fetch_sub(1) > 1`(1192) 防 ref>1 时过早 teardown。
- releaseGroup/releaseBind/releaseFrameSource 内部 ref-count `--` + erase；not-found 分支 WARNING 返回，不调 IMP（38-43/74-79/114-121 确认）。
- `~IngenicVideo`(1120-1122) `if (!exitCalled_) exit()` 防析构与显式 exit 双跑。

### 3.3 可空（未 init 安全）

- exit() 兜底遍历空 map → 循环 0 次，不调 IMP。
- `ispOsdMgr_` 空指针守卫(1285 `if (ispOsdMgr_)`)。
- `~IngenicVideoStream` 由 `configured_`(882) 守卫，未 configure 直接析构 no-op。

---

## 4. Query 门控移除判定（必查项 #4）

```
$ grep -n "IMP_Encoder_Query" src/hal/ingenic/IngenicVideo.cpp
889:        // The earlier IMP_Encoder_Query(st.registered) gate is unreliable after   (注释，析构)
1082:    if (IMP_Encoder_Query(channel_id_, &st) >= 0) {                                (getInfo 运行期)
1261:    //    UnRegisterChn — IMP_Encoder_Query(st.registered) is unreliable after   (注释，exit 兜底)
```

**结论（passed）**：
- teardown 路径（析构 881-902 + exit 兜底 1264-1283）的 `Query(st.registered)` **幂等门控已全部移除**，改为无条件 `IMP_Encoder_UnRegisterChn`(析构 897 / exit 兜底 1275)。IMP 对已释放 chn 返回 <0 无害。
- 唯一保留的 `IMP_Encoder_Query`(1082) 在 `getInfo()` 内，是**运行期状态查询**（`info.enabled = st.registered`），非 teardown 幂等判定，保留正确、合理。

---

## 5. flush 配对（缺陷 C）

| 路径 | 调用 | 行 | 配对 |
| --- | --- | --- | --- |
| getFrame | `IMP_Encoder_GetStream` 缓存 `last_stream_` + `last_stream_valid_=true` | 1047/1065-1066 | 缓存在途帧 |
| releaseFrame | `ReleaseStream(last_stream_)` + `last_stream_valid_=false` | 1071-1072 | 正常释放 |
| 析构兜底 | `if (last_stream_valid_) ReleaseStream` + `StopRecvPic` | 891-895 | 退出前 flush 在途帧 ✓ |
| start | `StartRecvPic` | 998 | 配对 |
| stop | `StopRecvPic` | 1022 | 配对 |
| exit 兜底 | 遍历 `g_group_ref_count` → `StopRecvPic(chn)` | 1228 | 残留 group flush ✓ |

**结论（passed）**：退出路径 `StopRecvPic` + `ReleaseStream(last_stream_)` 配对完整；`IMP_System_Exit`(1291) 不会在编码器持有未释放 stream 时调用。析构兜底(891-895) 覆盖 getFrame 已取但未 releaseFrame 的缓存帧。

---

## 6. SIM 不受影响 + 无回归

- `src/hal/CMakeLists.txt:18-29`：`IngenicVideo.cpp` 仅在 `NOT BUILD_FOR_SIMULATION` 时编入 `hal_video`（SIM 用 `simu/SimVideo.cpp`）。
- 本改动只触及 `IngenicVideo.cpp`，SIM `hal_video` 链 `sdk_stub`(imp_stub.c 全 no-op)，RtspServer 等共享代码未因本次改动改。
- build_sim exit 0（§1.2）印证：SIM `htc_main_app` + `test_http_server` + `test_camera_properties` 全过。
- 对第 1/2 次正常 teardown 无回归：新顺序（先 disable FS → UnBind → DestroyGroup）是 SDK 推荐顺序，比 T2 的违反顺序更严格，正常路径反而更稳。

diff stat 独立核对：
```
src/hal/ingenic/IngenicVideo.cpp | 110 +++++++++++++++++++++++++++++++++++++--
1 file changed, 106 insertions(+), 4 deletions(-)
```
与 implementer 报告一致。

> 注：`git diff --name-only` 显示工作区还有 main_app.cpp/RtspServer.{cpp,h}/IspOsdManager.cpp/CMakeLists.txt 等改动，属 T1/T2 遗留（dispatch 明确本次范围仅审 IngenicVideo.cpp），不在 T3 审查范围。

---

## 7. Nice-to-have 小瑕疵（不阻断）

exit() 兜底 UnBind(1242-1251) 重新构造 `fs_cell/enc_cell`，`outputID` 硬编码 0。依赖 configure 时存的也是 `outputID=0`（IngenicVideo.cpp:967-972 确认一致）。当前正确，但若未来 bind outputID 非 0，兜底 UnBind 的 cell 会与原 bind 不匹配。建议（非必须）：兜底从 g_bind_ref_count 关联存原 IMPCell，或在 map 存 outputID。

---

## 8. 缺测试 / 设备侧回归（已知约束，非代码缺陷）

- 本仓无 IMP 真实驱动单测（`imp_stub.c` 全 no-op，SIM 不可覆盖 IMP 驱动状态机）。
- analyst §8 已给设备侧回归脚本：T32 上连跑 5 次 `htc_main_app -m --force-day`，每次 ^C 经 `main_exit → RtspServer::shutdown → Misc::poweroff`，验证第 5 次仍出现 `rtsp stream info` + `RTSP server started on port 8554`，退出日志见新增 `[HAL] exit: teardown begin` / `teardown complete`。
- implementer 已在 report state_delta 的 risk(`T3-hw-regression-unverified`，severity medium) 声明硬件回归留用户。属 dispatch 约束（禁跑硬件）下的已知缺口，非本次代码缺陷。

---

## 9. 逐项验收清单

| # | 必查项 | 结论 |
| --- | --- | --- |
| 1 | 独立重编译 build + build_sim 均 exit 0 | passed（§1，独立复现）|
| 2 | exit() 顺序 L130(UnBind 后于 FS Disable) + L131(DestroyGroup 后于 UnBind) | passed（§2）|
| 3 | 幂等/无双重 destroy；exitCalled_/configured_/空指针守卫 | passed（§3）|
| 4 | teardown Query 门控移除；L1082 运行期保留合理 | passed（§4）|
| 5 | flush ReleaseStream/StopRecvPic/GetStream 退出配对，无在途帧泄漏 | passed（§5）|
| 6 | SIM 不受影响；正常启动/第1-2次退出无回归 | passed（§6）|
| 7 | 设备侧回归脚本 | analyst §8 已给；硬件验证留用户（已知约束）|

**总判定：passed / success → next `:end`**。无需 loopback implementer。
