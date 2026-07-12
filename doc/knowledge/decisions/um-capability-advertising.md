# um 能力上报设计决策（product.json `um_*` capability set）

> 关联：[`um-app-spec.md`](../specs/um-app-spec.md)、[`config-ini-to-json-migration.md`](../../design/config-ini-to-json-migration.md)、[`workmode-capability-inventory.md`](../../design/workmode-capability-inventory.md)（后者"capability"指内部 SDK 库盘点，与本文"产品对 APP 上报的能力"是不同概念）。

## 1. 决策主题

um（usermode server）启动后向 APP 上报"本产品在 um 会话期间支持哪些功能"，用 `product.json` 里的 `um_*` 能力集合（presence-set）表达；APP 据此隐藏不可用功能。**相机自报能力，APP 不按 product 型号查表。**

四个能力 token：

- `um_live` — RTSP 实时预览（CH1，1280×720）
- `um_snap` — APP 触发拍照（CH12 JPEG + CH14 缩略图）
- `um_rec` — APP 触发录影（CH0 H264 + CH14 缩略图）
- `um_pb` — APP 浏览/回放历史文件（无 IMP 通道，读 SD/sqlite）

`um_` 前缀是**场景限定符**（"um 与 APP 建连期间"），不是装饰性 namespace —— 它把"APP 交互能力"与"固件自主行为（quickSnap 等）"切开。

## 2. 决策背景

资源受限的 T32（64MB RAM，`CmaTotal=0`，纯 RAM+zram）上，um 今天以 `residentMode<0` **建全 4 路 IMP 常驻通道**（`src/hal/ingenic/IngenicVideo.cpp:1098 buildResidentChannels()`），因为 um 实为**全功能 server**：inline 执行拍/录/回放（`http_api_v1.cpp:699` `takePhoto` / `:992` `startRecord`，**无** wm handoff——workmode 切换路由 `:768` 返 501），建并使用 CH0(录) + CH12(拍) + CH14(缩略图) + CH1(预览) 全部四路。

诉求：um 启动后只保留 CH1 预览、省内存；APP 禁用拍/录/回放按钮。需要一个机制让"um 实际开什么"与"APP 认为能做什么"**不漂移**。

> **事实纠正**：曾以为"um 是瘦预览 server、不拍不录"——**错**。代码证实 um 通过 `http_server → camera_service` 传递性链接 `media_snap`/`media_recorder`（`src/service/camera/CMakeLists.txt`），`um_app.cpp:315` 还预热 `ImageSnap`。um 是全功能 server，建并使用全 4 路通道。

## 3. 决策内容

### 3.1 自报，不按型号查表

否决"APP 读 `BOOT.PModel` 查 model→能力 表"。理由：型号是稳定字符串，但能力随资源/SDK 状态可变（用户诉求是"资源受限"，属当前状态，非永久身份）；model 表内置于 APP，新 SKU 出货 = APP 必须更新否则过期；自报对单产品/多产品都成立，向前兼容免费。相机派生能力集 → 自报 → APP 泛化渲染。

### 3.2 派生、非永久；可恢复

能力不是 T32 的永久身份（"T32 永不能录"是错的形状）。它是**产品级、随 SKU 可变**的真相：新 SKU（更多 RAM）随 product.json 重刷即恢复能力。故能力值在 boot 时从 product.json 派生——不是运行时按内存自省（自省非确定、APP 无法推理），也不是编译期焊死。

### 3.3 单源头（防漂移）

**一个 `capabilities` 字段同时驱动三处**，物理上不可互相矛盾：

1. um 选建哪几路 IMP 通道（lean residentMode）；
2. um 注册/gate 哪些 HTTP 端点；
3. 对 APP 上报的能力集（mDNS TXT + HTTP）。

三处读同一个 product.json 源。这是防"APP UI 与 firmware 现实脱节"的唯一可靠办法。

### 3.4 product.json 只读硬上限；presence-set

- **存放桶**：`product.json`（只读、本机/本 SKU 真相；`ProductConfig` 无 `set()`/`flush()`，`src/config/devconf/ProductConfig.h:25-45`）。`PModel` 改记**具体产品型号**（如 `T01`），平台 `T32` 仍由 service type `_t32cam._tcp` 表达。
- **结构**：本机真相，**不建多型号查表**（否决 device-side model-lookup；同 APP 侧否决理由）。示例：
  ```json
  { "BOOT": { "PModel": "T01", ... },
    "capabilities": "um_live" }
  ```
  （`ProductConfig` 是 section/key 字符串袋，数组以逗号串存、boot 时解析成 set。）
- **形状**：presence-set（在场=支持；缺席=禁）。前向兼容（新 token old APP 忽略）+ 后向兼容（砍 token APP 自动禁）都比 boolean 干净。
- **边界**：token **纯存在性**，不塞参数。预览分辨率走 RTSP SDP、照片尺寸走 `MPic`/stillSize、视频尺寸走 `MVideo`——参数已有家。

### 3.5 scope：仅 um；自主路径正交

`um_*` **只管 um 这个 server**。开机生命周期——quickSnap 总被调用（内部按模式决定拍不拍）→ 按 boot 模式启 wm 或 um——是产品给定、跟 `um_*` 正交。quick_snap/wm 是各自 boot、各自建通道、读 `cameraMode`（`Settings.h:60`）/`workingMode`（GPIO），**不看 `um_*`**。故 `um_snap` 缺席只禁 APP 交互拍照，quickSnap 开机自拍照样跑（用户原意）。

### 3.6 capability → IMP 通道映射（单源头落地点）

| capability | um 建哪路 IMP | HTTP 端点 | 缺席→ |
|---|---|---|---|
**关键背景（官方 Ingenic 指南，2026-07-12 据此修订本节）**：T32 内核 cmdline `nrvbs=N` 在 **boot 时为 CH0（主通道）预分配 N 帧连续物理内存**，固定不可回收。故单流场景**必须用 CH0**——否则内核为 CH0 预留的内存白白浪费；用 CH0 + Scaler 降到低分辨率 + 低 `nrVBs` 是 64MB 下最省内存路径。**否决"跳过 CH0、单开 CH1"**（T28 初版曾这么定）：真机实测 um 走 CH1 时，CH0 的 2560×1440 raw ring + IMP 池（≈11×8MB virtual + 24MB `/dev/rmem`）仍被分配且空闲，~148s 触发 OOM。通道分配因此**按 capability 集合分支**（非单一硬编码表）：

| capability 集 | CH0 (main) | CH1 (sub) | CH2 | 说明 |
|---|---|---|---|---|
| `["um_live"]`（**首产品，纯预览**） | **RTSP 720p**（sensor 2560×1440 → **Scaler 1280×720**，`nrVBs=2`） | 不建 | 不建 | 官方单流路径 |
| `["um_live","um_rec"]`（预览+录影） | 录影 全分辨率 2560×1440 | RTSP 720p（Scaler） | — | 经典主/子码流 |
| `["um_live","um_snap"]`（预览+拍照） | RTSP 720p（Scaler）+ 拍照 JPEG 共享 group0 | 不建 | 不建 | CH0 一肩挑 |

HTTP 端点 gate（与通道分配无关，仅按 capability 注册路由 → 缺席 404）：

| capability | HTTP 端点 | 缺席→ |
|---|---|---|
| `um_snap` | `POST /api/v1/camera/photo`（**写**新媒体=拍照） | 不注册、404 |
| `um_rec` | `POST video/start`、`video/stop`（**写**新媒体=录影） | 不注册、404 |
| `um_pb` | `GET photos`、`video/list`、`video/playback`、`files/download`、`files/delete`、`database/media`、`database/thumbnail`、`thumbnail?file_path=`（均**读**已有=浏览/回放） | 不注册、404 |

> **端点分类规则**：写新媒体=`um_snap`/`um_rec`；读已有=`um_pb`。`/thumbnail?file_path=` 归 `um_pb`（读已有缩略图）；若 `POST /photo` snap 响应不自带 thumb、APP 须事后 GET /thumbnail，则 `/thumbnail` 改归 `um_snap`（实现验证 `PhotoResult` 是否含 thumb）。

**两个配套内存杠杆**：
1. **应用侧**（T29）：纯预览用 **CH0 + Scaler→720p**（不建 CH1/CH2，`nrVBs=2`），`RTSP_STREAM_ID` 1→0。砍掉 CH0 全分辨率 raw ring（11×8MB 元凶）。`fillFsAttrForOutput` CH0 的 `scaler.outwidth/outheight` 2560×1440→1280×720（仅纯预览分支；`um_rec` 仍全分辨率）。
2. **内核侧**：cmdline `nrvbs` 须同步——查 `/proc/cmdline`，若仍按 CH0 全分辨率预留需调低（720p、N 小）。应用侧改完砍 user-space 8MB，内核预留（rmem 池 + nrvbs）需 cmdline 配合才完全回收。

**内存账（修订）**：T28 初版推断"省 2-4MB"偏小——真机 `maps` 显示纯预览走 CH1 时 CH0 全分辨率 raw ring + IMP 池 ≈ **11×8MB(~88MB virtual) + 24MB rmem** 被分配，64MB 撑不住→OOM。改走 CH0-Scaler-720p 单流：CH0 输出 720p（raw ~1.35MB/帧）、不建 CH1/CH2、CH0 不再全分辨率 → user-space 大幅瘦身。准确省量以设备复测为准（重跑 `tools/mem_profile_um.sh`，期待 11×8MB 块消失、MemFree >20MB、无 OOM）。

### 3.7 缺省策略：缺席 → fail-safe 瘦身

`capabilities` 字段缺席（旧 product.json / sim / 漏刷）→ um **只 `um_live`** + 大声 log。理由：64MB T32 上"建全 4 路"是危险方向（用户正想逃离），兜底必须朝**瘦**走，不朝全走。量产 golden test 卡"product.json 必含 `capabilities`，缺席 CI 红"，让缺席在量产永不发生。dev/sim 模板填全开以免开发被惊到。

### 3.8 上报通道：mDNS TXT + HTTP，都从 product.json 派生

- **mDNS TXT**：APP 发现阶段（连前）读到 → 进 APP 直接藏按钮，不闪。加字段 `caps=um_live,um_pb`（保留 `um_` 前缀）。TXT 是 LAN 明文，但 caps 不敏感。`MdnsTxtRecord`（`src/service/discovery/MdnsTxtRecord.h:13-22`）现有 model/sn/fw_ver/端口/status，加 `caps`。
- **HTTP `GET /api/v1/device/capabilities`** → `{"capabilities":["um_live",...]}`。连上后权威详情 + 未来扩展位。今天**无**此端点（route table `http_api_v1.cpp:1722-1759` 无 capabilities/feature/support 字样）。

两处同一源，零漂移。

### 3.9 叶子决策

- **未知 token**（写错 / 将来新 token 老 firmware 不认）→ 静默忽略 + log（前向兼容）。
- **被 gate 的端点** → **不注册路由** → 直接 404（语义"本产品没这功能"，比 403 干净）。
- **控制类 HTTP**（settings / device-info / daynight / MCU 状态）**不受 cap 集合管**，um 永远开；cap 集合只 gate 这 4 个媒体功能。

## 4. 备选方案（均否决）

| 方案 | 否决理由 |
|---|---|
| APP 按 `PModel` 查 model→能力 表 | model 稳定但能力可变；表随新 SKU 过期；APP 必须更新 |
| 能力焊为 T32 永久身份 | "资源受限"是状态非身份；新 SKU 无法恢复 |
| 运行时按内存自省派生 | 非确定；APP 无法推理"今天能不能录" |
| 三处（通道/HTTP/上报）各管各 | 必漂移；可变 = 会改 = 漏改某处 |
| device-side 多型号查表 | 把 model-lookup 从 APP 搬到设备，同样过期 |
| 往 token 塞分辨率/尺寸 | 参数已有家（SDP/MPic/MVideo）；scope creep |

## 5. 首产品落地

**第一个产品的 um cap 集 = `["um_live"]`**（用户原话"禁用拍照、录影、回放浏览"；greenfield，无已部署单元，此即所有未来产品的模板）。

> **`um_pb` 已确认 OFF**（2026-07-12 grill 收敛）：首产品 `["um_live"]`，APP 纯直播盒子、看不到 quickSnap 的图（由 wm 自主上传/管理）。`um_pb` 不占 IMP 通道，将来想加为零成本——只需 product.json 加 token。

## 6. 需要显式批准的改动（`hal/` guardrail）

落地需动 `src/hal/**`（按项目 guardrail，hal/ 改动必须显式提出 + 批准）：

1. **`buildResidentChannels`（`IngenicVideo.cpp:1098`）由 capability 集合选通道**——现在只看 `residentMode` 一个 int（`HalProvider.cpp:53-55` 默认 -1 全建）。需让它接受 cap 集合，或加映射 cap→residentMode。
2. **`um_app.cpp:315` 的 `prewarm` 加 `um_snap` 门控**——现在只看 `!flags.noHttp`，无条件建拍照链。
3. HTTP 端点按集合注册/gate（非 hal，但同属单源头改造）。

## 7. 适用范围

仅约束 **um server 进程**的 APP 交互面。不约束：

- 自主路径（quickSnap 开机自拍、wm 任务）——由 `cameraMode`（`Settings.h:60`，值见 `CameraParameterRegistry.cpp:212-217`）+ `workingMode`（GPIO）管；
- 控制类 HTTP（settings/device-info/daynight/MCU）——永远开。

## 8. 未来扩展

- 新增 `um_*` token（如 `um_audio`）：加进集合即可，老 APP 忽略。
- 非 um 场景前缀（如未来 `wm_*`）：`um_` 已确立场景限定符模式，照抄。
- token 加参数：目前不需要（SDP/MPic/MVideo 覆盖）；真要加时升级 HTTP `/capabilities` 返回结构化对象，TXT 仍只带 presence。

## 9. 决策结论

**采纳**：product.json `um_*` capability presence-set，相机自报（mDNS TXT + HTTP），单源头驱动 IMP 通道构建 + HTTP gate + 上报；缺席 fail-safe 瘦身；scope 仅 um。首产品 `["um_live"]`。

**否决**：model 查表、永久身份、运行时自省、分离 gate、token 塞参数。

**落地前置**：显式批准 `hal/` 改动（§6）。
