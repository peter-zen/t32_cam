# config.ini → JSON 配置迁移设计

> **状态**：已拍板（2026-07-11 grill），待实施
> **取代**：`res/config.ini` + `res/env.ini` 作为运行时配置载体的角色（文件本身在迁移完成后删除）
> **关联**：[`cps-cs-set1-factory-config.md`](../knowledge/specs/cps-cs-set1-factory-config.md)（factory config 规格）、[`camera-photo-resolution-design.md`](../knowledge/specs/camera-photo-resolution-design.md)（stillSize 协议）、[T32 rootfs 分区布局记忆]（`/config` = jffs2 持久、`/` = 易失 rootfs）
> **范围**：把 `DeviceConfig` 的 ini 后端换成 JSON，配置按「产品/用户/系统」三桶拆分；**不含** quicksnap.json 与 setting.json 之间的字段去重（见 §13）

---

## 1. 背景与动机

现状（grill 前的问题）：

1. **ini 与 JSON 并存**：`DeviceConfig`（`res/config.ini`，ini 格式）与 `Settings`（`res/setting.json`，JSON）是两套配置存储，外加 `quicksnap.json` 运行态缓存、`factory/camera_factory_config.template.json` 出厂模板。格式不统一。
2. **字段重复且已漂移**：bitrate / 电压 / 时区散落在 ini 与 setting.json/quicksnap.json 多处。实测 `res/config.ini` 的 `SYSTEM.BR720P/BR1080P/BR4K = 8/16/32` 仍是老值，而 `Settings.h` 已迁到 `8/4/2`（2026-06-10 mapping table 调整）——副本之间已经不一致，是潜在 bug 源。
3. **section/key 字符串袋、无类型、全程可写**：`DeviceConfig::get("DEVICE","PID")` 这种 stringly-typed 访问，~60 处调用点；且 `set()+flush()` 任意位置可写，产品属性（PID/PModel）和用户配置（WiFi/时区）混在同一个可写袋子里，没有只读边界。
4. **JSON→ini 的反向翻译器在跑**：`CameraFactoryConfigImporter` 把 SD 上的 `camera_factory_config.json` 读进来、翻译成 ini 写回——JSON 本是更自然的载体，却被迫降级成 ini。

本设计把所有运行时配置统一为 JSON，按本质拆成三桶，并建立「产品属性运行时只读」的边界。

## 2. 目标

- **格式统一**：运行时配置全部 JSON；删除 `config.ini` / `env.ini`；`DeviceConfig` 后端从 ini 换成 JSON。
- **三桶分类**：产品属性（只读）/ 用户配置（读写）/ 系统基础设施（读写）分文件存放。
- **单一真相源**：bitrate / 电压 / 时区等重复字段归一到 `setting.json`，删除 ini 副本。
- **只读边界**：产品属性在运行时结构上不可写（只读 class），唯一下写路径是 factory import。
- **消费方最小改动**：三桶中 `system.json` 桶保留 `DeviceConfig` 的 section/key 袋 API，使基础设施类消费方零改动。

## 3. 目标架构

| 桶 | 文件 | 性质 | C++ 载体 | 读写 |
|---|---|---|---|---|
| **产品** | `product.json` | 出厂烧录、设备标识 | `ProductConfig`（新） | 运行时**只读**；仅 factory import 下写 |
| **用户** | `setting.json`（已存在） | 用户在 um 改的配置 | `Settings`（已存在） | 读写 |
| **系统** | `system.json` | 云端/网络/mDNS/策略基础设施 | `DeviceConfig`（瘦身后） | 读写（section/key 袋 API 保留） |

`ProductConfig` 与 `DeviceConfig` 物理上是两个独立 class（只读 vs 可写），合住 `DEVICE/BOOT` vs `SERVER/MDNS/POLICY` 两类 section。`Settings` 不变，仅吸收新字段。

固定路径（取代 env.ini，见 §10）：

| | 产品 | 用户 | 系统 |
|---|---|---|---|
| HW | `/config/htc/product.json` | `/config/htc/setting.json` | `/config/htc/system.json` |
| Sim | `./res/product.json` | `./res/setting.json` | `./res/system.json` |

## 4. 三桶 JSON schema

### product.json（DEVICE + BOOT 原样搬迁，只读）

```json
{
  "DEVICE": { "PID": "T152T20250624001", "CSSID": "CKV", "CPWD": "ckvison6688", "SPKVOL": 0 },
  "BOOT":   { "PType": 1, "PModel": "T32", "PName": "CAMERA", "PCompany": "CKVISON",
              "WLED": 0, "MVideo": 1, "MPic": 5, "SMode": 0, "PMac": "" }
}
```

### system.json（SERVER + MDNS + POLICY，可写袋子）

```json
{
  "SERVER": { "MS": "www.aidetcloud.com", "MSPort": 8899,
              "NTP": "www.aidetcloud.com", "NTPPort": 123,
              "FS": "0", "FSPort": 0 },
  "MDNS":   { "Enable": 1, "ServiceType": "_t32cam._tcp",
              "InstanceName": "", "HostName": "",
              "RtspPort": 8554, "CtrlPort": 8080 },
  "POLICY": { "Record": 0, "RemoteWakeup": 0, "FileManage": 0 }
}
```

> MDNS 的 `HostName`/`InstanceName` 留空字符串占位——它们运行时由 `MdnsParams::getDefaultMdnsHostName/InstanceName` 从 PID 派生（`MdnsParams.cpp:39-70`），不是持久化配置值；空表示「用派生默认」。

### setting.json（已存在，新增字段）

在现有 ~95 字段基础上**新增**：

```json
{ "...": "...",
  "timezone": "UTC+8" }
```

电压字段 `lowVol_l/h`、`endVol_l/h` 已在 setting.json 内；本次将其提升为**主源**（见 §7）。

## 5. 配置分类总表（每个 ini section → 桶）

| ini section | keys | → 桶 | 载体 |
|---|---|---|---|
| `DEVICE` | PID, CSSID, CPWD, SPKVOL | 产品 | `ProductConfig` |
| `BOOT` | PType, PModel, PName, PCompany, WLED, MVideo, MPic, SMode, PMac | 产品 | `ProductConfig` |
| `SERVER` | MS, MSPort, NTP, NTPPort, FS, FSPort | 系统 | `DeviceConfig`（system.json） |
| `MDNS` | Enable, ServiceType, InstanceName, HostName, RtspPort, CtrlPort | 系统 | `DeviceConfig`（system.json） |
| `POLICY` | Record, RemoteWakeup, FileManage | 系统 | `DeviceConfig`（system.json） |
| `SYSTEM.UPID/UPWD` | 用户 WiFi SSID / 密码 | 用户 | `Settings` |
| `SYSTEM.BR*` | bitrate（重复） | 用户 | `Settings`（已是主源，删 ini 副本） |
| `SYSTEM.LowVoltage/EndVoltage` | 电压（重复） | 用户 | `Settings`（提升为主源） |
| `NTP.TIMEZONE` | 时区 | 用户 | `Settings`（新增字段） |

## 6. Consumer 迁移映射表

> 以下为**穷举**的 `DeviceConfig::get/set` 调用点（grep 截至 2026-07-11）。按目标桶分组。「改读 ProductConfig」= 把 `DeviceConfig::getInstance()->get(DEVICE/BOOT, ...)` 换成 `ProductConfig::getInstance()->get(...)`（签名相同，token 级替换）；「留 DeviceConfig」= 不动，仅后端换 JSON。

### 6.1 → ProductConfig（DEVICE / BOOT）

| 字段 | 读取点 | 写入点 |
|---|---|---|
| PID | `FirmwareUpdate.cpp:97,126,167`、`Broadcast.cpp:93,122,183`、`Manifest.cpp:116`、`StorageServClient.cpp:188`、`MgmtServClient.cpp:174,731,856`、`upload_worker.cpp:193`、`WorkModeRunner.cpp:765`、`upload_task.cpp:265`、`MdnsParams.cpp:51,66,91`、`RemoteCtrlClient.cpp:818` | `ProcessLifecycle.cpp:651` ⚠️（见 §12.1） |
| CSSID / CPWD | `um_app.cpp:253,254`、`WorkModeRunner.cpp:601,602` | — |
| PType | `RemoteCtrlClient.cpp:148,412`、`Manifest.cpp:228`、`MgmtServClient.cpp:934`、`ProcessLifecycle.cpp:459` | — |
| PModel | `RemoteCtrlClient.cpp:821`、`MdnsParams.cpp:90` | — |
| PName | `MgmtServClient.cpp:208`、`MdnsParams.cpp:46` | — |
| MVideo / MPic | `RemoteCtrlClient.cpp:144,145` | — |
| SMode | `MgmtServClient.cpp:172` | — |
| WLED | `DayNightSwitch.cpp:98,168` | — |

### 6.2 → DeviceConfig（SERVER / MDNS / POLICY，留袋子，零改动）

| section.key | 读取点 | 写入点 |
|---|---|---|
| SERVER.MS / MSPort | `RemoteCtrlClient.cpp:379,382`、`WorkModeRunner.cpp:713,715`、`workmode_app.cpp:206,207`、`wm_app.cpp:320,321` | `RemoteCtrlClient.cpp:605,609` |
| SERVER.NTP / NTPPort | `RemoteCtrlClient.cpp:386,389`、`um_app.cpp:223,224`、`WorkModeRunner.cpp:471,472`、`wm_app.cpp:308,309` | `MgmtServClient.cpp:660` |
| SERVER.FS / FSPort | `RemoteCtrlClient.cpp:392,395` | `MgmtServClient.cpp:664,668` |
| MDNS.* | `MdnsParams.cpp:41,61,86,102`、port 经 `getConfiguredPort`（`WorkModeRunner.cpp:87`、`um_app.cpp:75`） | — |
| POLICY.FileManage | `RemoteCtrlClient.cpp:403`、`upload_worker.cpp:275`、`WorkModeRunner.cpp:843` | `RemoteCtrlClient.cpp:621` |
| POLICY.Record / RemoteWakeup | `RemoteCtrlClient.cpp:407,411` | — |

### 6.3 → Settings（SYSTEM 用户字段 / NTP 时区，重复字段归一）

| 字段 | 当前 ini 调用点（改读 `Settings`） | 备注 |
|---|---|---|
| bitrate BR* | `CameraPropertyService.cpp:349,358,366,373`(get) + `389,396,402,407`(set) | §7.1：删 ini 副本 + dual-write + fallback |
| LowVoltage / EndVoltage | `RemoteCtrlClient.cpp:365,368` | §7.2：方向反转，需测 |
| TIMEZONE | `media_app.cpp:196`、`ProcessLifecycle.cpp:364`、`CameraParameterRegistry.cpp:185,380`（binding） | §7.3：Settings 新增 timezone 字段 |

### 6.4 SYSTEM.UPID/UPWD → Settings（用户 WiFi）

| 调用点 | 动作 |
|---|---|
| 读：`RemoteCtrlClient.cpp:359`、`Manifest.cpp:205`、`MgmtServClient.cpp:911`、`WorkModeRunner.cpp:430,431` | 改读 `Settings` |
| 写：`RemoteCtrlClient.cpp:573,577`、`ProcessLifecycle.cpp:660,661` | 改写 `Settings` + `saveToJsonFile` |

### 6.5 Registry 驱动的泛型站点（随 storage 描述符自动跟随）

这些用 `definition.storage.section/key` 间接寻址，**改 `ParameterDefinition.storage` 描述符即自动迁移**，无需逐点改：

- `CameraStatusService.cpp:56,60`（get）
- `CameraPropertyService.cpp:692,696`（get）、`:1263,1265`（set）
- `CameraFactoryConfigImporter.cpp:205,209`（factory import 写）

### 6.6 flush 站点

`um_app.cpp:376`、`workmode_app.cpp:258`、`main_app.cpp:387`、`wm_app.cpp:424`、`ProcessLifecycle.cpp:664`、`CameraPropertyService.cpp:1454`、`CameraFactoryConfigImporter.cpp:237` —— 后端换成 JSON 后，`flush()` 语义不变（原子写见 §8）。

## 7. 重复字段单一真相源（细节）

### 7.1 bitrate（近零风险）

`getConfiguredBucketMbps`（`CameraPropertyService.cpp:344`）已先读 `settings->bitRate_*`，仅当 `settingsValue == 0` 才回退 ini；`writeBitrateForMode` 已 dual-write。→ setting.json 已是事实主源。动作：删 `SYSTEM.BR*` 键、删 `writeBitrateForMode` 的 `deviceConfig->set(BR*)` 四行、删 getter 的 ini fallback 四行。

### 7.2 电压（方向反转，要测）

当前 ini 是主（`RemoteCtrlClient.cpp:365,368` 直读推云端 `SYS_LowVoltage/EndVoltage`），setting.json 有 byte-split 副本 `lowVol_l/h`、`endVol_l/h`。动作：RemoteCtrlClient 改读 `Settings` 的电压字段（注意 byte-split 重组：`(lowVol_h<<8)|lowVol_l`），删 ini 键。**风险**：需确认云端协议 `SYS_LowVoltage` 期望的编码（字符串 "4.2" vs 数）与 byte-split 的换算一致——实施时验证。

### 7.3 时区（Settings 新增字段）

`media_app.cpp:196` boot 阶段读时区，`:203` 紧接着 `Settings::loadFromJsonFile`。因 setting.json 在 `/config`（不依赖 SD），boot 阶段可加载。动作：
1. `Settings` 新增 `std::string timezone`（默认 `"UTC+8"`）+ load/save 接入。
2. `media_app.cpp`：把 `:203` 的 `loadFromJsonFile` 提到 `:196` 之前，时区改读 `Settings::timezone`。
3. `ProcessLifecycle.cpp:364`、`CameraParameterRegistry` binding 同改。
4. 删 `NTP.TIMEZONE` ini 键。

> `quicksnap.json` 的 `timezone` 字段与 boot 阶段时区**无关**（media_app 不从 quicksnap.json 读时区），不在本次范围（§13）。

## 8. DeviceConfig 后端 ini→JSON 映射 + 原子写

- **映射**：section → JSON 顶层 object，key → 叶子。`get("SERVER","MS")` → `root["SERVER"]["MS"]`。`get(int)` 走 `.asInt()`，`get(string)` 走 `.asString()`——bag API 签名不变，消费方零改动。
- **原子写**：`flush()` 改为「写 `system.json.tmp` → `rename()` 到 `system.json`」（同分区 POSIX 原子）。顺带修当前非原子 ofstream 覆写的掉电损坏风险。`setting.json` / `product.json` 同样上原子写。

## 9. factory import 重构 + storage 描述符重定向

**storage 描述符**（`CameraParameterRegistry` / `ParameterDefinition`）：当前 `ParameterStorageKind::DEVICE_CONFIG` + `section` + `key`。拆为：

| 新 kind | 目标 | 写者 |
|---|---|---|
| `PRODUCT` | product.json | 仅 factory import（直写文件 + ProductConfig reload） |
| `SYSTEM` | system.json | DeviceConfig bag（set+flush） |
| `SETTINGS`（已有） | setting.json | Settings（setRegistryPropertyValue） |

`DEVICE_CONFIG` 按字段归属拆成 `PRODUCT`（DEVICE/BOOT）或 `SYSTEM`（无——DEVICE/BOOT 全归产品；SERVER/MDNS/POLICY 本就无 registry binding，直留 bag）。

**`CameraFactoryConfigImporter.importFromSdRoot`**（`CameraFactoryConfigImporter.cpp:174`）改造：
- factory 块循环（`:196-215`）：`DeviceConfig::set` → 按 `storage.kind` 路由；DEVICE/BOOT 字段写 `product.json`（直写文件），结束触发 `ProductConfig` reload。
- properties 块循环（`:218-235`）：已走 `setRegistryPropertyValue`，随描述符自动跟随。
- 末尾 `DeviceConfig::getInstance()->flush()`（`:237`）→ 改为「product.json 写盘 + system.json 不变」。
- **退役 `update_config.ini` 机制**（`ProcessLifecycle.cpp:525-540` 的 SD-drop move）：SD-drop 配网已由 `camera_factory_config.json` import 覆盖，二机制重叠，删之。（若运维另有 override 用途，见 §12.4。）

## 10. env.ini 退役 + 固定路径

`env.ini` 5 个 key 全是固定路径，固化成 constexpr（HW/sim 分支已在 `ProcessLifecycle.cpp:275-282`）：

| env.ini key | 固化 |
|---|---|
| `CONFIG_FILE` | 拆成 product/setting/system.json 三条 constexpr |
| `SETTING_FILE_PATH` | 并入 setting.json 固定路径（冗余） |
| `BROADCAST_FILELIST_PATHNAME` / `BROADCAST_FILE_PATH` / `ISP_FILE_PATH` | SD 音频目录 constexpr（HW `/mnt/sdcard/media/audio/`） |

`EnvManager` **保留为薄 shim**：删 `parsePrimaryEnv(env.ini)` 调用，默认值来自 constexpr；留它支撑 `HTC_WM_CONFIG_FILE` 这类 env-var / 测试注入逃生口。`res/env.ini` 删除，`install.sh:15-16` 停止拷贝 env.ini。

## 11. 实施顺序（依赖链）

> 修正（2026-07-11 Phase-1 落地后）：原方案把「建 system.json 桶」放第 1 步，但那隐含 section 搬迁、会先破坏 DEVICE/BOOT 消费方。真正的零风险入口是**纯格式切换、不搬 section**——DeviceConfig 的文件先整体从 ini 变 json（仍含全部 7 section），后续阶段再逐桶 carve-out。`system.json` 直到 Phase-4 才出现。

1. **✅ Phase-1（已落地，T23 done）— 纯格式切换、行为等价**：`DeviceConfig::parse/flush` 换 jsoncpp（`config_data` 仍是 `map<section,map<key,string>>` string 袋，get/set 零改动；叶子 `.asString()`）；`res/config.ini → res/config.json`（全部 7 section 1:1 翻译，**D1 引号保留**：`PCompany`/`PName` 存字面双引号）；`res/config.sim.ini → res/config.sim.json`；4 处 `CONFIG_FILE` 默认值改指 `.json`（app.h / ProcessLifecycle sim / wm_app huntcam / env.ini 值）；`wm_app.cpp` `readIniString→readJsonString`（R3 连带）；devconf CMakeLists 链 jsoncpp；golden `tests/test_device_config.cpp`(U1-U8)；flush 顺带上原子写（.tmp+rename）。→ 验证：golden 改前(ini)+改后(json)双绿、build_sim + build_t32@200 双 exit 0、sim smoke main_app/media_app 不崩。旧 `.ini` 保留不删（回滚路径）。详见 `artifacts/T23-*-report.md`。
2. **✅ Phase-2（已落地，T24 done）— 抽出 ProductConfig**：新建只读 class `src/config/devconf/ProductConfig.{cpp,h}`（**无 set/flush**，结构级只读；唯一写者 = factory import 直写文件 + `reload()`）；`res/product.json`/`product.sim.json` 装 `BOOT.*` + `DEVICE{CSSID,CPWD,SPKVOL}`；DeviceConfig 的 `config.json` carve-out（BOOT 删、DEVICE 只剩 PID）；新增 `PRODUCT_FILE` env（4 处注入，对称 CONFIG_FILE）；**18 个非 PID** DEVICE/BOOT 读取点迁 ProductConfig（实测，含 ProcessLifecycle:466 PType、MdnsParams 方案 A）；registry 13 binding retarget `PRODUCT` kind（PID 2 binding 留 DEVICE_CONFIG）；factory import 按-kind 路由 + `writeProductDelta` read-modify-write + reload；golden `test_product_config.cpp`(P1-P6+EQ) + `test_device_config` fixture 更新。→ 验证：golden 双绿、build_sim + build_t32@200 双 exit 0、回归套件 12 套零新增（test_camera_properties 仍只剩 T23-H5 pre-existing FAIL）。一次 loopback（tester 抓到 test_camera_properties fixture 漏设 PRODUCT_FILE，implementer 修 fixture 复测绿）。**§12.1 已决议**：PID 留 DeviceConfig（MCU 权威源），详见下。
3. **✅ Phase-3（已落地，T25 done）— setting.json 吸收用户字段**：`Settings` 新增 `timezone`(UTC+8)/`upid`/`upwd`/`lowVoltage`/`endVoltage` 字段；timezone 3 reader 改 Settings（media_app 调整 boot 顺序先 load 后读 + UTC+ 归一化）；bitrate 删 4 ini-fallback + 4 dual-write 死代码 + 4 BR 键；电压走**保守 string 路径**（`lowVoltage`/`endVoltage` 原样透传，零云端编码风险，§12.3 化解）；UPID/UPWR 4 reader + 2 writer 改 Settings（**含 syncWithMCU 关机写 Settings+saveToJsonFile**）；registry 6 binding → settingsBinding；Settings dispatch read(:773-777)+write(:1420-1435) 双分支全加；config.json 删 SYSTEM+NTP 段（只剩 DEVICE:PID + SERVER/MDNS/POLICY）；T23-H5 fixture ini→json（stillSize 2K 独立 bug 不动）。→ 验证：3 golden(test_device_config/test_product_config/test_settings) 双绿、build_sim+build_t32@200 双 exit 0、12 套回归零新引入、5 等价命题独立验证（无值漂移、syncWithMCU 持久化对、dispatch 完整）。**决议**：CSSID/CPWR 保持只读 ProductConfig（无实际写者）；D1 去引号**排除出 Phase-3**（MgmtServClient:208 依赖字面引号，需云端确认）。详见 `artifacts/T25-*-report.md`。
4. **✅ Phase-4（已落地，T26 done）— 清场 + 三桶成型**：新建 `src/config/Paths.h`（6 namespace-scope constexpr，C++14 兼容——首次 inline constexpr 在 T32 gcc5.4 挂、改 namespace-scope）；rename `res/config.json→system.json` + `config.sim.json→system.sim.json`，CONFIG_FILE 默认全改指 system.json；退役 `env.ini`（9 文件，EnvManager 默认值来自 Paths.h constexpr、保留 parsePrimaryEnv 薄 shim 无调用方）；退役 `update_config.ini`（7 删除点：app.h macro / ProcessLifecycle move 块 / wm_app lean skip / Importer kIniFileName+兼容分支，用户确认无外部 dropper）；删 `res/config.ini`/`config.sim.ini`/`env.ini`；`install.sh` 改 cp system.json+product.json+setting.json（去 config.ini/env.ini）；R4 EXDEV fallback（DeviceConfig::flush rename 失败→copyFile+unlink，fail-closed）。分 Step-A(rename+constexpr+退役机制, 可独立验证) → Step-B(删文件+install.sh)。→ 验证：3 golden + 新 `test_t26_phase4_paths`(R1-R5 加载真实 res/system.json+product.json) 全绿、build_sim+build_t32@200 双 exit 0、20 套回归零新引入、dangling 引用 grep 清 0、install.sh 产线链审计过。详见 `artifacts/T26-*-report.md`。

---

**✅ 全迁移收尾（T23+T24+T25+T26 全 done，2026-07-11）**：三桶成型——`system.json`(SERVER/MDNS/POLICY/DEVICE:PID, DeviceConfig 可写袋) + `product.json`(BOOT+DEVICE-static, ProductConfig 只读) + `setting.json`(用户字段, Settings)。PID 在 system.json（§12.1 MCU 镜像，每次关机 syncWithMCU 刷新）。ini 格式从运行时配置全面退役。每阶段走完整 refactor flow（planner→implementer→tester→reviewer + C1/C2），共 4 阶段、1 次 loopback（T24 test fixture）。Final follow-up 清单见各阶段 §12.x。

## 12. 待确认 / 风险

### 12.1 ✅ 已决议（Phase-2 落地）：PID 留 DeviceConfig

查证 `syncWithMCU()`（`ProcessLifecycle.cpp:644`）在**每次关机**运行，从 MCU 读 PID/UPID/UPWD 镜像写进 config——**PID 是 MCU 权威源、每次关机刷新的运行时可写字段**，非一次性出厂烧录。决议：**PID 留 `DeviceConfig`**（可写 MCU 镜像），其 15 读 + 1 写（syncWithMCU）一律不迁；`ProductConfig` 只装真正静态出厂标识（BOOT.* + DEVICE\{CSSID,CPWD,SPKVOL\}）。`DEVICE` section 因此跨两文件：DeviceConfig 的 config.json 保留 `DEVICE:{PID}`，product.json 放 `DEVICE:{CSSID,CPWD,SPKVOL}+BOOT`。`syncWithMCU` 的 UPID/UPWD 写仍走 DeviceConfig（Phase-3 才迁 Settings）。无其它 DEVICE/BOOT 非出厂运行时写。

### 12.2 ProductConfig 形态：只读袋子 vs 严格 typed getter

本设计取**只读 JSON-backed 袋子**（`ProductConfig::get(section,key)`，签名同 DeviceConfig，无 set/flush）——为让 §6.1 的 ~20 个调用点 token 级替换、契合「消费方最小改动」。若想要严格 typed（`ProductConfig::pid()` / `.pmodel()`）的编译期安全，那是额外的 ~20 处机械改名，建议作为本迁移落地后的独立加固项。

### 12.3 电压云端协议编码

§7.2：反转后确认 `SYS_LowVoltage/EndVoltage` 的云端期望（字符串 "4.2" vs 数值 vs byte-split 重组）与 `Settings` 字段换算一致，避免云端显示异常。

### 12.4 update_config.ini 是否真无运维用途

§9 退役 `update_config.ini`。若产线/云端另有「下发 override 配置」走这条路径，则不退役、改为 `update_config.json` 路由到对应桶。**实施前与产线/云端侧确认**。

### 12.5 config.sim.ini

`res/config.sim.ini`（仅 `DEVICE.PID`）随 `config.ini` 一并退役；sim 的 PID 进 `./res/product.json`。

### 12.6 Phase-1 落地新增（T23，low，已登记 follow-up）

- **T23-H5（→ Phase-3 顺带修）**：`tests/test_camera_properties.cpp:30,45-69` 仍写 **ini 格式** config fixture，Phase-1 后 DeviceConfig 解析不了 ini。当前被该测试一个无关的 pre-existing FAIL（line 126 stillSize 2K 断言）掩盖——一旦那个 unrelated bug 修，ini-fixture 会造成新 FAIL。Phase-1 不阻塞（测试本就红、HEAD 同样复现）；Phase-3 做 setting.json 吸收时一并把它的 fixture 改 json。
- **T23-R4（→ Phase-4）**：`DeviceConfig::flush` 原子写用 `rename()`，跨分区（EXDEV）会失败且当前**静默返 false 无 fallback**。HW 主路径 `/config/htc` 单分区不受影响；SD override 跨分区场景才暴露。Phase-4 清场时补 fallback（如 copy+unlink 兜底）。

### 12.7 Phase-2 落地新增（T24，需 Phase-3 决策）

- **T24-R-cssid-cpwr-writability（MEDIUM，→ Phase-3 决策）**：CSSID/CPWR（出厂 WiFi AP 凭证）原是 `Network_Setting` property + `READ_WRITE` + DEVICE_CONFIG 存储，云端可经 registry API 写。Phase-2 迁到 ProductConfig（只读）后，`isWritableRegistryStorage` 不含 PRODUCT → `writeRegistryValue` 落穿返错，**API 契约从「可写」变「只读」**。这与设计 §3（产品属性只读）一致，但是迁移的**隐性副作用**、未显式决议。Phase-3 需确认：CSSID/CPWR 出厂后是否真的不该再被云端改？若该保持可写，则它们应归 DeviceConfig 可写袋而非 ProductConfig。
- **T24-R-sticky-singleton（low，→ Phase-3）**：`ProductConfig` 用 `std::once_flag` 单例（同 DeviceConfig/Settings 模式），进程内粘性 → 无法在同进程里测「load 失败→空袋→default」回退。load-failure 单测因此缺失。Phase-3 若要补，需给 ProductConfig 加测试用的 reset/inject 钩子（或接受该回退靠代码审计保证）。

### 12.8 Phase-3 落地新增（T25，low，已登记 follow-up）

- **T24-R-cssid-cpwr 决议（RESOLVED）**：查证 CSSID/CPWR 为 `addFactory` factory-classified（`CameraParameterRegistry.cpp:177`）、运行时只读（仅 um_app/WorkModeRunner 读做 AP 配网引用）、factory import 之外**无任何写者**、无 cloud/HTTP 写路径。"registry 可写性丢失"是纯 schema 层面、无实际调用方 → **保持只读 ProductConfig 正确，不回退**。残留 low risk：云端若 push `Network_Setting.CSSID` set 命令现静默失败（原先会写）——R_cloud_cssid_push，需云端侧确认无此 push。
- **T25-R-voltage-string-path（§12.3 RESOLVED）**：电压不用 byte-split lowVol_l/h 重组（编码无法确认），改走 `Settings` 新增 string 字段 `lowVoltage`/`endVoltage`（默认 "0.0"）原样透传 → **零云端编码风险**。byte-split `lowVol_l/h`/`endVol_l/h` 现成 orphan（无消费方），作为 pre-existing 保留不删，登记 lowVol-orphan-cleanup。
- **T25-R-d1-quote-strip-excluded**：D1 去引号（product.json 的 PCompany/PName 去字面引号）**排除在迁移外**——`MgmtServClient.cpp:208` 直送 PName 到云端不剥引号 = 依赖字面引号；去引号改云端报文格式，需云端侧确认。独立后续。
- **T25-G2/G3 coverage gap（low）**：registry write-dispatch roundtrip（G2）、timezone `+8`→`UTC+8` 归一化（G3）、voltage "0.0"/空边界（G4）未直接单测——字段 roundtrip 已在 Settings 层（S3/S4/S6）锁定、分支已逐行审计，accept as follow-up。
- **§12.4 update_config.ini 代码查证**：repo 内**无任何代码写** update_config.ini（仅 `ProcessLifecycle.cpp:536/546` boot move 读+搬、`CameraFactoryConfigImporter.h:23` 兼容性检查）。它是**外部丢 SD 的 override 文件**。退役 read 侧对本 repo 安全，但外部 dropper（不在 repo）会失效——Phase-4 前需用户确认外部是否有此 dropper、是否迁到 `camera_factory_config.json`。

## 13. 不在范围内

- **quicksnap.json 与 setting.json 的字段去重**：`quicksnap.json`（cameraMode/force_upload/burstNumber/stillSize/timezone）是 `quick_snap.cpp` 自维护的 snap 相关缓存，与 setting.json 有重叠，但与本次「ini→JSON 配置迁移」解耦。是否归一并入 setting.json 是独立小清理。
- **factory 模板字段名规整**：`camera_factory_config.template.json` 现有字段名（如 `RWakeup _SET` 带空格、`M_Server` 等）与 ini/新 JSON 的命名不完全一致，importer 现靠 registry 翻译。字段名统一是独立的 schema 治理项。
