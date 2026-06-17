# MCU Service 架构决策

## 1. 背景

`t32_cam` 固件在 2026-06-13 同步的代码中，MCU 模块刚完成 V104 大版本（commit `87068cf update mcu to V104`），新增 200+ 寄存器的读 / 写方法（电池、GPS、信号、温湿度、气压、PIR、Timer、PID/UPID/UPWD、RTC 等）。但 MCU 调用与上层 HTTP 之间存在两层空缺：

1. HTTP 4 个端点是 stub：`GET /api/v1/device/info`、`GET /api/v1/device/sensors`、`POST /api/v1/system/datetime`、`POST /api/v1/system/workmode` 都通过 `IDeviceService` / `ISensorService` / `ISystemService` 的 T32 实现。3 个 T32 实现里 `// TODO: call MCU::getInstance()->…`，目前返写死数据。
2. `CameraParameterRegistry` 的 25 个 STATUS 字段是 placeholder：`MCU_Version`、`Battery1/2`、`EPower`、`Signal_*`、`Sensor_CDS/TEMPS/RHS/APS` 等虽然注册了，但 `placeholderBinding()` 让 `/api/v1/camera/status` 和 `/api/v1/camera/properties/*` 仍返静态值。

`htc_main_app` 自身在 `generateDescInfo()` 和 `syncWithMCU()` 里直接调 `MCU::getInstance()->…` 共 50+ 处，全部在主线程内阻塞 I2C。无 service 抽象、无缓存、无线程，模块耦合在主流程里。

## 2. 评估过的方案

### 方案 A — 保持现状

不抽 service、不启轮询线程。3 个 T32 service stub 直接填实，`main_app` 维持现状。

| 维度 | 评价 |
|---|---|
| 改动面 | 最小 |
| 风险 | 主线程仍被 I2C 阻塞；`device/info` 端点未真接 MCU；CameraParameterRegistry 的 25 个 STATUS 字段仍未激活 |

### 方案 B — `daemon_app` 拥有 MCU + 跨进程 IPC

`htc_daemon_app` 启动一个长期 MCU 轮询线程，主进程通过 unix domain socket / 共享内存向 daemon 拿值。彻底隔离 MCU I2C 跟主进程。

| 维度 | 评价 |
|---|---|
| 隔离度 | 最强 |
| 改动面 | 巨大：要建一条跨进程 IPC；3 个 service interface 要改造成客户端；HTTP 4 个端点都要多一跳；4 个新端点（如果加）都要多一跳 |

### 方案 C — 新增 `service::McuService` 单例（选定）

`htc_main_app` 持有一个 `service::McuService` 进程内单例，封装 `MCU::getInstance()`，在 test mode（`CMD_MOBILE` bit）下启一个后台轮询线程（默认 5s 周期）刷新寄存器到 `McuCache`；work mode 与 SIMU_BUILD 都不启线程，查询接口同步落 `MCU::getInstance()->readXxx()`。3 个 T32 service facade 与 `CameraStatusService` / `CameraPropertyService` 从 `McuService` 拿值。`daemon_app` / `media_app` / `WorkMode` / `MCU.cpp` 全部不动。

| 维度 | 评价 |
|---|---|
| 改动面 | 中：新增 1 个 service 子目录 + 2 个 .cpp/.h 改 facade + 2 个 .cpp 加 mcuBinding 解析 |
| 隔离度 | 中：test mode 缓存；work mode 同步 |
| 与既有 layering-analysis 决策一致性 | 一致（保持 4 个 IDevice/ISensor/IStorage/ISystem service interface） |
| 跟 `media_app` / `daemon_app` 解耦 | 干净：McuService 只被 `htc_main_app` 持有；`media_app` 的 `WorkMode::getWorkingMode()` 保持直接调 `MCU::getInstance()`，不影响 |

## 3. 决策

**采用方案 C**。关键参数：

| 项 | 选定 |
|---|---|
| 进程内拥有者 | `service::McuService`（Meyers 单例） |
| 缓存结构 | `McuCache`：POD `std::atomic<int>` + `std::mutex` 保护 `std::string` |
| 轮询触发 | test mode（`CMD_MOBILE` bit），启动 `std::thread` + `std::condition_variable::wait_for` |
| 轮询周期 | 5000ms 默认（`startPolling(periodMs)` 参数化） |
| 写路径 | `McuService::setDatetime(iso8601)`：strptime → timegm → settimeofday → `MCU::setDatetime` |
| HTTP 范围 | 填 4 个 T32 stub + `CameraParameterRegistry` 换 `mcuBinding()` + `Status/Property` 加 MCU 分支 |
| Work mode 写 | 返 HTTP 501 + `{accepted:false, reason:"work mode is firmware-set via GPIO/MCU firmware, not writable via I2C"}` |
| Sim 行为 | `McuService` 不写 Sim impl；查询走 `MCU::getInstance()->readXxx()`；`MCU_EXIST=1` 时 `I2C_BYPASS` 关闭，I2C 真实失败后 MCU.cpp 返 0 |
| 单测 | sim-only `tests/test_mcu_service.cpp`，5 个 case |

## 4. Out of scope

- 新增 `/api/v1/mcu/*` 端点（独立 future 任务）
- 给 `MCU` 类加回调 / 观察者面
- `MCU.cpp` 内部 stub（`readFirmwareVersion="1.0.0"` 等）填实
- `media_app` / `daemon_app` 任何改动
- `WorkMode::getWorkingMode()` 调用方式（保持直接调 `MCU::getInstance()`）
- I2C 驱动 / 内核模块改动
- `Camera_Mode=6`（CAM_Mode 第 6 种未实现模式）— 独立 feature request

## 5. 风险与缓解

| 风险 | 缓解 |
|---|---|
| Meyers 单例销毁顺序：McuService 析构时可能访问已析构的 `MCU::getInstance()` | `main_app.cpp` 文件作用域加 `static auto& _mcu_keepalive = McuService::getInstance();`，强制 McuService 在 static init 阶段构造、main 退出后析构；`main_exit:` 第一行调 `stopPolling()` join 线程，再走 `Misc::poweroff()` |
| 轮询线程与 HTTP IO 线程并发读 cache | `McuCache` POD 用 `std::atomic<int>`（lock-free）；字符串用 `std::mutex` 保护 |
| `startPolling` 重复调用 | `running_.exchange(true)` 已为原子成功就 early return；`stopPolling` 同样 idempotent |
| sim 下 I2C_BYPASS 关闭导致 700ms+/tick 慢轮询 | 单测改用 `isPolling()` 状态而非 cache 内容；HTTP 端点验证接受 0 值 |
| HTTP 501 改造破坏现有调用 | 仅在 `api_v1_system_workmode` 一处使用，新增 helper `send_http_error_with_data`，其它 handler 不动 |
| `CameraParameterRegistry` 的 `mcuBinding()` 工厂写在匿名 namespace | 移到 `service::` 命名空间，保证 external linkage，让 `CameraStatusService` / `CameraPropertyService` 能链接到 |

## 6. 验证矩阵

| 阶段 | 验证 | 通过条件 |
|---|---|---|
| 编译 | `cmake --build build_sim --target htc_main_app` | 无 error，`libmcu_service.a` 产出 |
| 单测 | `ctest -R mcu_service` | 5 个 case 全过 |
| 集成 | `./build_sim/bin/htc_main_app -m` 跑 10s，Ctrl-C | log 出现 `McuService: startPolling(5000)` / `pollingLoop: entered` / `pollingLoop: exit` / `stopPolling() joined` |
| T32 stub | `curl /api/v1/device/info` + `/device/sensors` + POST `/system/datetime` | 端点 200；sim 下字段值为 0 / 占位但通路正确；`datetime` accepted:true |
| Registry | `curl /api/v1/camera/status?group=Device\|Signal\|Sensor` | 9 个 Signal + 4 个 Sensor + 5 个 Device 字段 `source=real`；6 个 SOR 字段 `source=placeholder` |
| 501 | `curl -X POST -d '{"mode":1}' /api/v1/system/workmode` | HTTP 501；body `{code:501, message, data:{mode, accepted, reason}}` |

## 7. 相关文档

- `doc/api/device-and-system-api.md` — 本轮填实的 4 个端点契约
- `doc/knowledge/refs/mcu-api-and-register-inventory.md` — MCU API 完整清单（132 方法）+ 寄存器映射 + HTTP↔MCU 使用对照 + 未用 API 清单（含 IIC→MCU→McuService→HTTP 调用架构图）
- `doc/knowledge/decisions/service-interface-layering-analysis.md` — 4 service interface 拆分决策
- `doc/knowledge/decisions/asymmetric-snap-vs-record-design.md` — work mode vs test mode 行为差异背景
