# T3 — Bug Report (raw + PM 初读)

## 用户描述
测试 `htc_main_app -m` 到第三次,第三次启动还是没能正常启动完成就卡住了。
三次启动日志在 `logs/debug.log`(完整 254 行,已附在下方要点里)。

## 现象(三次启动对照)

| 启动 | 日志行 | 结果 | 退出 teardown |
| --- | --- | --- | --- |
| 第1次 | L1–91 | ✅ 正常,`RTSP server started on port 8554`(L73) | `RtspServer::shutdown: teardown complete`(L84)+ `IspOsdManager: exit done` ×2 |
| 第2次 | L92–184 | ✅ **正常**(到 L163 `RTSP server started on port 8554`)—— T2 修复有效,不再卡 | `teardown complete`(L177)+ `exit done` ×2 |
| 第3次 | L198–254 | ❌ **卡死**,停在 `i264e[info]: profile Main, level 3.1`(L254);**缺** `rtsp stream info` 和 `RTSP server started` | (未退出) |

每次启动间用 `^C`(SIGINT, signal 2)退出(见 L74/L167)。

## 关键证据

1. **T2 修复已生效**:每次退出都有 `RtspServer::shutdown: process-level teardown` → `IspOsdManager: exit done` → `RtspServer::shutdown: teardown complete` → `Power off From Main function`。teardown 路径**确实执行了**。
2. **但仍累积到第3次卡**。卡死点与 T2 修复前**完全相同**:`i264e[info]` 之后、`rtsp stream info` 之前 = `configure()` 内。
3. **核心矛盾**:teardown 执行了,却没把 IMP 资源清干净 → 有资源**逐步累积**,前两次未达阈值,第3次 configure 在 `IMP_Encoder_RegisterChn`/`IMP_System_Bind`(或 CreateOsdRgn)阻塞。
4. `IspOsdManager: exit done` 每次退出打**两遍**(L82-83 / L175-176)—— OSD region 退出被调两次,泄漏嫌疑。

## PM 初步代码发现(方向,需 analyst 独立验证/推翻)

- `IngenicVideoStream::configure()`(IngenicVideo.cpp:894-976):`IMP_Encoder_CreateChn`(945)→ `IMP_Encoder_RegisterChn`(950)→ `acquireBind`/`IMP_System_Bind`(962)→ OSD `ensureRegion`(CreateOsdRgn)。
- 析构/停止只做一半:`~IngenicVideoStream`(line 888)仅 `IMP_Encoder_DestroyChn`;`stop()`(1001-1021)仅 `releaseFrameSource`(DisableChn)+ `IspOsdManager::stop()`。
- **全文 grep 不到 `IMP_Encoder_UnRegisterChn`** —— RegisterChn(950) 的注册关系疑似从不反注册。
- T2 的兜底在 `IngenicVideo::exit()`(1178-1244),基于**进程内 static map**(`g_bind_ref_count`/`g_group_ref_count`/`g_fs_ref_count`,line 19/55/91)。这些 map **进程重启清零**,所以第二个进程里兜底只覆盖"本进程这次创建"的资源。要确认 map 填充与 exit 遍历 destroy 是否完整、对称。
- `FrameChannelController::init`(540)`IMP_FrameSource_CreateChn`(547),destroy(583)`DestroyChn`;`IngenicVideo::exit()` 是否真的 `fsMgr.destroy()` 全部 FrameSource channel?
- OSD:`IspOsdManager::stop()`(59)与 `exit()`(90)**都** DestroyOsdRgn → 双调用;`ensureRegion`(115)`CreateOsdRgn`(124/130)。IMP OSD region 是否有数量上限、是否每次泄漏一个。

## 完整日志
见 `logs/debug.log`(本仓根目录)。要点摘录见上;analyst 自行读取全文。
