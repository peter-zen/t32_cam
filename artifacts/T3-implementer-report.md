---
contract: report
contract_version: "1"
task_id: T3
node: implementer
flow: bug
status: success
summary: |
  修复 T3：重排 IngenicVideo::exit() teardown 顺序使其符合 SDK 约束（imp_system.h:130:
  UnBind 必须在 FrameSource Disable 之后；:131: DestroyGroup 必须在 UnBind 之后）。新顺序 =
  fsMgr.disable()(Disable FS) → flush 在途帧(StopRecvPic) → UnBind(g_bind) →
  UnRegisterChn/DestroyChn/DestroyGroup(g_group) → IspOsdManager/ISP → fsMgr.destroy()
  → IMP_System_Exit → sensor/ISP teardown → MultiProcessExit。去掉 exit() 兜底与
  ~IngenicVideoStream 析构里不可靠的 IMP_Encoder_Query(st.registered) 门控，改为无条件
  UnRegister/Destroy（IMP 对已释放 chn 返回 <0 无害）。职责划分：per-stream group/bind/channel
  由 ~IngenicVideoStream 销毁并从 ref-count map erase，exit() 兜底仅清空 map 残留，正常路径
  map 已空 no-op，无双重 destroy。flush 在途帧：~IngenicVideoStream 析构前 ReleaseStream
  (last_stream_) + StopRecvPic；exit() 兜底对残留 group StopRecvPic，保证 IMP_System_Exit
  不在编码器持有未释放 stream 时调用。仅改 PIC-owned src/hal/ingenic/IngenicVideo.cpp
  （+106/-4），不动 Misc::poweroff，SIM 不受影响（IngenicVideo.cpp 不编进 SIM）。双平台 build
  通过（build/ exit 0 + build_sim/ exit 0），未运行 T32 硬件二进制。
deliverables:
  - src/hal/ingenic/IngenicVideo.cpp
  - artifacts/T3-implementer-evidence.md
  - artifacts/T3-implementer-report.md
verification:
  commands:
    - cmake --build build -j$(nproc)
    - cmake --build build_sim -j$(nproc)
    - grep -n "fsMgr.disable()\|StopRecvPic(chn)\|fallback UnBind\|fallback destroy chn/group\|fsMgr.destroy()\|IMP_System_Exit()\|MultiProcessExit" src/hal/ingenic/IngenicVideo.cpp
    - grep -n "IMP_Encoder_Query(chn, &st) >= 0 && st.registered\|if (st.registered)" src/hal/ingenic/IngenicVideo.cpp
    - grep -n "ReleaseStream(channel_id_, &last_stream_)\|IMP_Encoder_StopRecvPic" src/hal/ingenic/IngenicVideo.cpp
    - git diff --stat src/hal/ingenic/IngenicVideo.cpp
  evidence_ref: artifacts/T3-implementer-evidence.md
state_delta:
  set_task_status: {}
  add_decision:
    - key: T3-exit-teardown-order
      value: "IngenicVideo::exit() teardown 顺序 = fsMgr.disable(Disable FS) → flush(StopRecvPic) → UnBind → DestroyGroup/Chn → IspOsdManager/ISP → fsMgr.destroy → IMP_System_Exit → sensor/ISP → MultiProcessExit，满足 imp_system.h:130/131。"
    - key: T3-no-query-gate
      value: "去掉 exit() 兜底与 ~IngenicVideoStream 的 IMP_Encoder_Query(st.registered) 门控，改无条件 UnRegister/Destroy；唯一保留的 Query 在 getInfo() 运行期查询，非 teardown。"
    - key: T3-flush-before-exit
      value: "退出前 flush 在途帧：~IngenicVideoStream 析构 ReleaseStream(last_stream_)+StopRecvPic；exit() 兜底对残留 group StopRecvPic，避免 IMP_System_Exit 在编码器持有未释放 stream 时调用。"
  add_risk:
    - key: T3-hw-regression-unverified
      severity: medium
      description: "双平台编译已过（build/ + build_sim/ exit 0），但第3次启动连续回归（连续 5 次 -m --force-day 不卡、退出日志见 teardown begin/complete）尚未在 T32 硬件验证，留给用户。"
artifact_path: artifacts/T3-implementer-report.md
next: reviewer
---

# T3 Implementer Report

## 改动一句话
重排 `IngenicVideo::exit()` teardown 为 SDK 合规顺序（FS disable → flush → UnBind → DestroyGroup → ISP → System_Exit），去掉不可靠的 Query 幂等门控，退出前 flush 编码器在途帧。仅改 PIC-owned `src/hal/ingenic/IngenicVideo.cpp`。

## 接入点
- `~IngenicVideoStream`（L880-901）：加 flush（ReleaseStream + StopRecvPic）+ `releaseFrameSource`（Disable FS 在 UnBind 前）+ 去掉 `Query(registered)` 门控改无条件 UnRegister。
- `IngenicVideo::exit()`（L1193-1301）：重排为 6 段 SDK 合规顺序。

## 幂等 / 可空 / SIM 安全
- ref-count map（g_bind/g_group/g_fs）正常路径由析构 erase 清空；exit() 兜底空 map no-op，无双重 destroy。
- `exitCalled_` + `g_video_init_ref_count` 守卫；`configured_`/空指针守卫；IMP 对已释放资源返回 <0 无害。
- SIM：`IngenicVideo.cpp` 不编进 SIM（`src/hal/CMakeLists.txt:18-29`），build_sim/ exit 0 印证。

## 验证
- T32 `cmake --build build -j$(nproc)` → exit 0（`Built target htc_main_app`）。
- SIM `cmake --build build_sim -j$(nproc)` → exit 0（`Built target htc_main_app`）。
- 静态 grep：exit() 顺序 = fsMgr.disable → StopRecvPic → UnBind → DestroyGroup → ISP → fsMgr.destroy → IMP_System_Exit；无 `Query(registered)` 门控；有 ReleaseStream/StopRecvPic flush。详见 evidence §3。

## 遗留
- 第3次启动连续回归未在 T32 硬件验证（PC 不能跑 MIPS），留用户在硬件做（analyst §8）。
- 未 git commit / push（未授权）。

详见 `artifacts/T3-implementer-evidence.md`。
