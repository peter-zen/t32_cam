---
task_id: T3
node: implementer
flow: bug
artifact: evidence
status: success
---

# T3 — Implementer 证据（evidence）

修复 `IngenicVideo::exit()` teardown 顺序违反 SDK 约束 + 去掉不可靠 Query 幂等判定 + 退出前 flush
在途帧。仅改 PIC-owned `src/hal/ingenic/IngenicVideo.cpp`（用户已授权），不动 `Misc::poweroff`。

---

## 1. 改动摘要（diff stat）

```
 src/hal/ingenic/IngenicVideo.cpp | 110 +++++++++++++++++++++++++++++++++++++--
 1 file changed, 106 insertions(+), 4 deletions(-)
```

改动点（全部在 `src/hal/ingenic/IngenicVideo.cpp`）：

1. **`#include <vector>`**（L12）显式补全（原代码已隐式用 `std::vector`，经 `IngenicVideo.h` 间接带入；显式化以稳健）。
2. **`~IngenicVideoStream`（L880-901）**：
   - 新增 flush：`if (last_stream_valid_) IMP_Encoder_ReleaseStream(...)` 释放 getFrame 缓存的在途帧，再 `last_stream_valid_=false`。
   - `IMP_Encoder_StopRecvPic(channel_id_)` 停止接收。
   - `releaseFrameSource(group_id_)`（内部 DisableChn + ref-count erase）—— 先 Disable FS 再 UnBind，满足 imp_system.h:130。
   - 去掉 `IMP_Encoder_Query(st.registered)` 门控，改为无条件 `IMP_Encoder_UnRegisterChn`（teardown 后 Query 不可靠；对已释放 chn 调用 UnRegister/Destroy 返回 <0 无害）。
   - 保留 `releaseBind`→`DestroyChn`→`releaseGroup`（DestroyGroup 在 UnBind 之后，满足 imp_system.h:131）。
3. **`IngenicVideo::exit()`（L1193-1301）**：重排 teardown 顺序为 SDK 合规的 6 段（见下 §3 grep 证据）。

---

## 2. 双平台 build 证据（exit 0）

### 2.1 T32 硬件 build（`build/`，`BUILD_FOR_SIMULATION=OFF`）

```
[100%] Linking CXX executable ../../bin/htc_main_app
[100%] Built target htc_media_app
Flattening library symlinks for target deployment
Done flattening symlinks in /home/zengping/project/huntcam/code/t32_cam/build/lib
[100%] Built target htc_main_app
=== build exit: 0 ===
```
命令：`cmake --build build -j$(nproc)` → **exit 0**（`Built target htc_main_app` success）。

### 2.2 PC 模拟 build（`build_sim/`，`BUILD_FOR_SIMULATION=ON`）

```
[100%] Built target http_server
[100%] Built target test_http_server
[100%] Built target htc_main_app
=== build_sim exit: 0 ===
```
命令：`cmake --build build_sim -j$(nproc)` → **exit 0**（`Built target htc_main_app` success）。

> SIM 安全性：`src/hal/CMakeLists.txt:18-29` 中 `IngenicVideo.cpp` 仅在 `NOT BUILD_FOR_SIMULATION` 时编进
> `hal_video`（SIM 用 `simu/SimVideo.cpp`），故本次改动不影响 SIM；SIM build exit 0 印证。

**未运行 T32 硬件二进制**（dispatch 禁止；设备回归留用户）。

---

## 3. 静态 grep 证据

### 3.1 exit() teardown 顺序（行号递增 = 执行顺序）

```
$ grep -n "teardown begin|fsMgr.disable()|StopRecvPic(chn)|fallback UnBind|fallback destroy|ispOsdMgr_->exit|fsMgr.destroy()|IMP_System_Exit()|disableAll|delAll|DisableTuning|closeISP|MultiProcessExit|teardown complete" src/hal/ingenic/IngenicVideo.cpp
1211:    Logger::log(LogLevel::INFO, "[HAL] exit: teardown begin (FS disable -> flush -> UnBind -> DestroyGroup -> ISP/System)");
1213:    fsMgr.disable();                                   # 1. Disable FrameSource（满足 L130）
1228:            IMP_Encoder_StopRecvPic(chn);              # 2. flush 在途帧（stop recv）
1252:            Logger::log(LogLevel::INFO, "[HAL] exit: fallback UnBind(group=%d)", grp);   # 3. UnBind（L130 后）
1274:            Logger::log(LogLevel::INFO, "[HAL] exit: fallback destroy chn/group (group=%d, chn=%d)", grp, chn);  # 4. DestroyGroup/Chn（L131 后）
1286:        ispOsdMgr_->exit();                            # 5. ISP/OSD teardown
1290:    if (fsMgr.destroy() < 0) return false;             # 6. DestroyChn FrameSource
1291:    IMP_System_Exit();                                 #    IMP_System_Exit
1293:    if (sensorMgr.disableAll(sensors) < 0) ...         #    sensor teardown
1294:    if (sensorMgr.delAll(sensors) < 0) ...
1295:    if(IMP_ISP_DisableTuning() < 0) ...
1298:    if (sensorMgr.closeISP() < 0) ...
1299:    IMP_Encoder_MultiProcessExit();                    #    MultiProcessExit（与 init:1128 配对）
1300:    Logger::log(LogLevel::INFO, "[HAL] exit: teardown complete");
```

**结论**：顺序 = `fsMgr.disable`(Disable FS) → flush/StopRecvPic → UnBind → DestroyGroup/Chn →
ispOsdMgr.exit → fsMgr.destroy → IMP_System_Exit → sensor/ISP → MultiProcessExit。
满足：
- imp_system.h:**130**：UnBind（L1252）在 FrameSource Disable（L1213）**之后**。✅
- imp_system.h:**131**：DestroyGroup（L1274）在 UnBind（L1252）**之后**。✅

### 3.2 Query(st.registered) 门控已移除（teardown 路径）

```
$ grep -n "IMP_Encoder_Query(chn, &st) >= 0 && st.registered|if (st.registered)" src/hal/ingenic/IngenicVideo.cpp
  (none — good)
```
```
$ grep -n "IMP_Encoder_Query" src/hal/ingenic/IngenicVideo.cpp
889:        // The earlier IMP_Encoder_Query(st.registered) gate is unreliable after   # 注释（析构）
1082:    if (IMP_Encoder_Query(channel_id_, &st) >= 0) {                              # getInfo() 运行期查询，非 teardown
1261:    //    UnRegisterChn — IMP_Encoder_Query(st.registered) is unreliable after   # 注释（exit 兜底）
```
**结论**：teardown 路径（析构 + exit 兜底）的 `Query(registered)` 门控已全部移除；唯一保留的
`IMP_Encoder_Query`（L1082）在 `getInfo()` 内，是运行期状态查询，非 teardown 幂等判定，保留正确。

### 3.3 flush 在途帧存在

```
$ grep -n "ReleaseStream(channel_id_, &last_stream_)|StopRecvPic|flush/StopRecvPic" src/hal/ingenic/IngenicVideo.cpp
892:            IMP_Encoder_ReleaseStream(channel_id_, &last_stream_);   # ~IngenicVideoStream: 释放缓存帧
895:        IMP_Encoder_StopRecvPic(channel_id_);                        # ~IngenicVideoStream: 停接收
1022:        IMP_Encoder_StopRecvPic(channel_id_);                       # stop(): 停接收（原有）
1071:        IMP_Encoder_ReleaseStream(channel_id_, &last_stream_);      # releaseFrame(): 释放（原有）
1227:            Logger::log(LogLevel::INFO, "[HAL] exit: flush/StopRecvPic(chn=%d)", chn);  # exit 兜底 flush
1228:            IMP_Encoder_StopRecvPic(chn);                           # exit 兜底: 对残留 group 停接收
```
**结论**：析构（L892/895）+ exit 兜底（L1227/1228）均 flush 在途帧，确保 `IMP_System_Exit`（L1291）
不在编码器持有未释放 stream 时调用（缺陷 C 修复）。

---

## 4. 幂等 / 可空 / 无双重 destroy 分析

### 4.1 职责划分（解决 analyst 缺陷 B：双重 destroy）

| 资源 | 由谁销毁 | map erase |
| --- | --- | --- |
| per-stream group/bind/channel | `~IngenicVideoStream`（L890-898） | `releaseBind`→erase `g_bind_ref_count`；`releaseGroup`→erase `g_group_ref_count`；`releaseFrameSource`→erase `g_fs_ref_count` |
| 残留 group/bind（析构未覆盖） | `exit()` 兜底（L1228/1252/1274） | clear `g_bind/g_group_ref_count` |

**无双重 destroy**：正常路径下流析构先跑，把 `g_bind/g_group_ref_count` 里的项 erase 掉；
exit() 兜底遍历到的 map 已空 → no-op（不调任何 IMP destroy）。exit() 兜底仅覆盖"析构没覆盖的残留"
（如 main 经 poweroff/while(1) 跳过单例析构时，map 非空）。

### 4.2 幂等

- `releaseFrameSource`/`releaseBind`/`releaseGroup` 内部按 ref-count `--` + erase，重复调用走 not-found 分支 no-op（L116-120/76-79/40-43 WARNING 返回）。
- exit() 兜底 UnBind/Destroy 对未绑定/已释放资源，IMP 返回 <0 无害（注释 L1250/1265 说明）。
- `exitCalled_` 守卫（exit 首行 L1180）防重复进入；`g_video_init_ref_count` 防 ref>1 时过早 teardown。

### 4.3 可空（未 init 安全）

- exit() 兜底遍历空 map → 不调 IMP；`ispOsdMgr_` 为空指针时跳过（L1285 守卫）；`fsMgr.disable()/destroy()` 对空 channels_ 列表循环不执行。
- `~IngenicVideoStream` 由 `configured_` 守卫（L881），未 configure 直接析构 no-op。

---

## 5. 关键 diff（全文见 `git diff src/hal/ingenic/IngenicVideo.cpp`）

`~IngenicVideoStream`（去 Query、加 flush、补 FS disable 在 UnBind 前）：
```cpp
IngenicVideoStream::~IngenicVideoStream() {
    if (configured_) {
        // ... flush comment ...
        if (last_stream_valid_) {
            IMP_Encoder_ReleaseStream(channel_id_, &last_stream_);
            last_stream_valid_ = false;
        }
        IMP_Encoder_StopRecvPic(channel_id_);
        releaseFrameSource(group_id_);          // Disable FS BEFORE UnBind (L130)
        IMP_Encoder_UnRegisterChn(channel_id_); // 无条件（去 Query gate）
        releaseBind(group_id_, &fs_cell_, &enc_cell_);
        IMP_Encoder_DestroyChn(channel_id_);
        releaseGroup(group_id_);                // DestroyGroup AFTER UnBind (L131)
        configured_ = false;
    }
}
```

`exit()` 重排后骨架（节选）：
```cpp
fsMgr.disable();                                // 1. Disable FS
/* 2. flush: 遍历 g_group_ref_count -> IMP_Encoder_StopRecvPic(chn) */
/* 3. UnBind: 遍历 g_bind_ref_count -> IMP_System_UnBind; clear */
/* 4. Destroy: 遍历 g_group_ref_count -> UnRegisterChn/DestroyChn/DestroyGroup; clear */
ispOsdMgr_->exit();                             // 5. ISP/OSD
fsMgr.destroy();                                // 6. DestroyChn FS
IMP_System_Exit();
sensorMgr.disableAll/delAll/DisableTuning/closeISP;
IMP_Encoder_MultiProcessExit();
```

---

## 6. 约束遵守

- ✅ 仅改 `src/hal/ingenic/IngenicVideo.cpp`（PIC-owned，用户已书面授权）。
- ✅ 不动 `src/common/misc/Misc.cpp` 的 `poweroff`（未触碰）。
- ✅ 不 commit / push（仅工作区改动）。
- ✅ 双平台编译通过（build/ exit 0 + build_sim/ exit 0）。
- ✅ 幂等 + 可空 + SIM 不受影响。
- ✅ 未运行 T32 硬件二进制（设备回归留用户）。

## 7. 遗留 / 设备侧验证（留用户）

- 第3次启动连续回归（analyst §8）：连续 5 次 `htc_main_app -m --force-day` 不卡，日志见
  `[HAL] exit: teardown begin` / `teardown complete`。**需 T32 硬件执行**（PC 无真实 IMP 驱动）。
- 退出顺序日志佐证：`teardown begin (FS disable -> flush -> UnBind -> DestroyGroup -> ISP/System)`。
