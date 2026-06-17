---
contract: report
contract_version: "1"
task_id: T3
node: analyst
flow: bug
status: success
summary: T2 teardown 已生效(退出日志有 teardown complete)，但第3次启动仍卡在 i264e[info] 之后 configure 内。根因不是单条 IMP 资源漏配 destroy(逐项核对 init/teardown 配对完整，PM 三条猜测全部被代码推翻：OSD 在本场景 group_id=1 从未创建、UnRegisterChn 三处存在、ref-count map 进程内配对)，而是 IngenicVideo::exit() 销毁顺序违反 IMP SDK 约束(imp_system.h:130 先 Disable FS 再 UnBind) + exit() 兜底与 ~IngenicVideoStream 双重 destroy 依赖 IMP_Encoder_Query 判定不可靠 + 退出期编码器在途帧未 FlushStream，叠加 Misc::poweroff() 软关机不彻底复位 IMP 内核态，使退出期打坏的编码器子状态残留到下次 CreateChn/RegisterChn(945/950) 阻塞；前两次因退出时序概率性未命中，第3次必复现。
deliverables:
  - artifacts/T3-analyst-evidence.md
  - artifacts/T3-analyst-report.md
verification:
  commands:
    - grep -n "i264e\|CreateChn\|RegisterChn\|UnRegisterChn\|IMP_System_Bind\|IMP_System_UnBind" src/hal/ingenic/IngenicVideo.cpp
    - grep -n "IMP_System_UnBind\|fsMgr.destroy\|IMP_System_Exit\|MultiProcessExit\|DestroyGroup\|DestroyChn" src/hal/ingenic/IngenicVideo.cpp
    - grep -n "group_id_ == 0\|prepare()\|ensureRegion\|CreateOsdRgn\|DestroyOsdRgn" src/hal/ingenic/IngenicVideo.cpp src/hal/ingenic/IspOsdManager.cpp
    - grep -n "RTSP_SENSOR_ID\|RTSP_STREAM_ID" src/media/rtsp/RtspServer.h
    - sed -n '125,135p;149,180p' sdk/include/imp/imp_system.h
    - grep -n "i264e\|rtsp stream info\|RTSP server started\|CreateOsdRgn done\|exit done" logs/debug.log
  evidence_ref: artifacts/T3-analyst-evidence.md
state_delta:
  set_task_status: {}
  add_decision:
    - key: T3-root-cause
      value: "teardown 配对完整但顺序/幂等性缺陷：IngenicVideo::exit()(IngenicVideo.cpp:1191-1247) 先 IMP_System_UnBind(1211) 后 fsMgr.destroy()(1247)，违反 imp_system.h:130(先 Disable FrameSource 才可 UnBind)；exit() 兜底(1226-1236) 与 ~IngenicVideoStream(880-892) 双重 destroy 依赖 IMP_Encoder_Query 判定 registered 不可靠；退出期编码器在途帧未 FlushStream 即 IMP_System_Exit(1248)。叠加 Misc::poweroff()(Misc.cpp:584-593) 软关机不彻底复位 IMP 内核态，退出期打坏的编码器子状态残留，下次 CreateChn/RegisterChn(945/950) 阻塞 → 第3次必复现。OSD 在 group_id=1 从未创建，非泄漏源。"
    - key: T3-fix-locus
      value: "全部落在 PIC-owned src/hal/ingenic/IngenicVideo.cpp：(1) 修正 exit() 销毁顺序为 disable FS → UnBind → DestroyGroup/Chn → destroy FS → System_Exit；(2) exit() 兜底只对 ~IngenicVideoStream 未覆盖的资源(无 stream 对应的 group) destroy，不无条件 Query；(3) ~IngenicVideoStream/stop 退出前 FlushStream + ReleaseStream 在途帧。需用户书面授权改 src/hal/**。非 PIC 可选缓解：main_app.cpp shutdown 后 poweroff 前延时。"
  add_risk:
    - key: T3-hal-teardown-order
      severity: high
      description: "IngenicVideo::exit() 的 IMP 反操作顺序(先 UnBind 后 disable/destroy FS)违反 imp_system.h:130，且与流析构双重 destroy 依赖 IMP_Encoder_Query 不可靠，退出期可能把 IMP 编码器子系统推入不可恢复脏状态；软关机(poweroff)不彻底复位 IMP 内核态时该脏状态跨重启残留，表现为概率性启动卡死(累积到第3次)。"
    - key: T3-pm-hypotheses-refuted
      severity: low
      description: "PM 三条猜测均不成立(已代码证伪)：(1) 全文无 IMP_Encoder_UnRegisterChn —— 实有 885/964/1233 三处；(2) OSD 双调用泄漏 —— RTSP group_id=1≠0 从未创建 region(日志无 CreateOsdRgn done)；(3) ref-count map 跨进程累积 —— 文件级 static 重启清零且进程内配对。reviewer/implementer 勿据此误改。"
artifact_path: artifacts/T3-analyst-report.md
next: implementer
---

# T3 Analyst Report — 给 implementer

## 根因（一句话）
T2 teardown 在 RTSP 流上配对完整，但 `IngenicVideo::exit()` 的 **IMP 反操作顺序违反 SDK 约束** +
**exit() 兜底与流析构双重 destroy 依赖 `IMP_Encoder_Query` 不可靠** + **退出期编码器在途帧未 flush**，
叠加 `Misc::poweroff()` 软关机不彻底复位 IMP 内核态，使退出期打坏的编码器子状态跨重启残留，
下次 `configure()` 的 `IMP_Encoder_CreateChn`(945)/`RegisterChn`(950) 阻塞；第 3 次必复现。

## 卡死点
- `i264e[info]` 由 `IngenicVideo.cpp:945 IMP_Encoder_CreateChn` 触发，三次都打到 → CreateChn 已返回。
- run3 卡在 CreateChn 之后：`RegisterChn`(950) 首嫌疑 / `IMP_System_Bind`(962) 次嫌疑；`prepare()`(970) 因
  `group_id_=1≠0` 被跳过(排除 OSD)。
- 证据：configure 失败会打 ERROR(946/951/963)，run3 无 ERROR → IMP 调用阻塞不返回；`rtsp stream info`
  (RtspServer.cpp:490) 缺失 = configure 未返回。

## 推翻 PM 猜测（勿据此误改）
1. `IMP_Encoder_UnRegisterChn` 存在三处：IngenicVideo.cpp:885 / 964 / 1233（非"grep 不到"）。
2. OSD region 在 RTSP(group_id = 0*3+1 = 1) 从未创建（日志三次均无 `CreateOsdRgn done`）；
   `exit done` 打两遍是 exit()+~IspOsdManager 幂等双调用，handle 恒 -1，无累积。
3. ref-count map 是文件级 static，进程重启清零，进程内 configure 填/exit 清配对，非累积源。

## 真正缺陷
- **缺陷 A**：`IngenicVideo::exit()`(1211 `UnBind` 先于 1247 `fsMgr.destroy`) 违反
  imp_system.h:130「FrameSource 使能后不能 UnBind，需先 Disable FS」。兜底分支命中时 UnBind 在 FS 仍
  Enable 下调用 → IMP 状态机错乱。
- **缺陷 B**：`exit()` 兜底(1226-1236) 与 `~IngenicVideoStream`(880-892) 对同一 group/chn 双重 destroy，
  靠 `IMP_Encoder_Query` 判 `registered` 幂等；Query 对已 destroy chn 的返回在不同 IMP 版本不可靠，
  误判 → 对已销毁 chn 再 UnRegisterChn/DestroyChn → 脏槽位。
- **缺陷 C**：退出期 `getFrame` 取走的 `last_stream_` 可能未 ReleaseStream，`IMP_System_Exit`(1248) 在
  编码器持有未释放 stream 时调用 → 编码硬件未彻底释放。

## 修复方向（PIC-owned，需用户书面授权）
1. 修正 `IngenicVideo::exit()` 销毁顺序为：**disable FS → UnBind → DestroyGroup/DestroyChn → destroy FS →
   IMP_System_Exit**（满足 imp_system.h:130/131）。
2. `exit()` 兜底只 destroy `~IngenicVideoStream` 未覆盖的资源（无 stream 对应的 group），不无条件 `Query`+
   destroy 全部 `g_group_ref_count` key。
3. `~IngenicVideoStream`/`stop()` 退出前 `IMP_Encoder_FlushStream` + 确保在途 `last_stream_` 已
   `ReleaseStream`，再做 destroy。
- 非可选缓解：`main_app.cpp` `RtspServer::shutdown()` 后、`Misc::poweroff()` 前延时（治标）。

## 触及 src/hal/**
- **是，主要修复全部落在 `src/hal/ingenic/IngenicVideo.cpp`（PIC-owned，需用户书面授权）**。
- 非必需可选缓解落 `src/app/main_app.cpp`（非 PIC）。

## 复现 / 回归
- 复现：T32 硬件，连续 3 次 `htc_main_app -m --force-day`，每次 SIGINT 退出→reboot；第 3 次停 i264e[info]。
- 回归 pass：连续 5 次 kill+重启每次均出现 `rtsp stream info` + `RTSP server started`；退出日志销毁顺序符合
  「先 disable FS → UnBind → DestroyGroup → destroy FS → System_Exit」；RTSP 客户端第 3/5 次能拉流。

详见 `artifacts/T3-analyst-evidence.md`。
