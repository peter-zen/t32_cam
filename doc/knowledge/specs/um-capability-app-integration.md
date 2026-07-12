# um 能力上报 —— APP 端适配指南

> 受众：手机 APP 开发者（iOS/Android/桌面）。本文是相机固件对外暴露的**能力契约**，APP 据此决定 UI 渲染。
> 配套设计决策：[`doc/knowledge/decisions/um-capability-advertising.md`](../decisions/um-capability-advertising.md)（固件侧实现细节，APP 可选读）。
> 版本：随 T28 引入（2026-07）。首产品能力集 = `["um_live"]`（纯直播看景）。

## 1. 一句话

相机现在会**主动告诉 APP 自己支持哪些功能**（实时预览 / 拍照 / 录影 / 历史回放）。APP 不再硬编码"这是 T32 所以能拍照"，而是**读相机上报的能力集**，按"在场=显示，缺席=隐藏"渲染 UI。不同产品型号能力不同，APP 用同一套逻辑适配。

## 2. 能力从哪里读（两条路，任选或都用）

### 路 A：mDNS TXT（发现阶段，连之前就能拿到）

APP 发现相机（mDNS 服务类型 `_t32cam._tcp`）时，TXT 记录里新增一个字段：

```
caps=um_live,um_snap,um_rec,um_pb
```

- 值是逗号分隔的 token 列表（无空格）。
- **用途**：APP 在"连接前"就能据此藏掉不可用的功能按钮，避免"显示→点击→失败→再隐藏"的闪烁。
- 空集/字段缺席 = 相机只支持 `um_live`（fail-safe 瘦身语义，见 §6）。

### 路 B：HTTP（连上后，权威）

```
GET /api/v1/device/capabilities
→ 200 OK
  { "capabilities": ["um_live", "um_snap", "um_rec", "um_pb"] }
```

- 返回 JSON 数组，token 同上。
- **用途**：连上后的权威查询；也方便 APP 不走 mDNS 时（如直连 IP）拿能力。
- 两路**同一份数据源**，不会冲突。

**推荐**：路 A 用于首屏渲染（快、免连），路 B 用于连接后复核 / 不支持 mDNS 的场景。

## 3. 四个能力 token + UI 映射

| token | 含义 | 在场时 APP 该显示 | 缺席时 APP 该隐藏 |
|---|---|---|---|
| `um_live` | RTSP 实时预览（1280×720） | 实时画面入口 / 播放器 | 实时画面（极少缺——基本恒在） |
| `um_snap` | APP 触发拍照 | 拍照按钮、连拍、定时拍、拍照设置 | 拍照相关按钮 + 拍照尺寸设置 |
| `um_rec` | APP 触发录影 | 录影按钮（开始/停止）、录影时长/码率设置 | 录影相关按钮 + 录影尺寸设置 |
| `um_pb` | 浏览/回放历史文件 | 相册/时间线、视频回放、下载、删除 | 历史浏览入口（相册/回放/下载） |

**决策规则**：`has(token) == true` → 显示对应 UI；`false` → 隐藏（连设置项一起藏，不要只灰显——灰显会被点）。

## 4. HTTP 端点按能力分组（APP 调用前先判断）

相机按能力**注册路由**。能力缺席时，对应端点**不注册**——请求会返回 **404**（不是 403、不是 500）。APP 必须把这种 404 当"功能不存在"，别当瞬态错误重试。

| 能力 | 端点（能力在场才存在） | 类型 |
|---|---|---|
| `um_live` | RTSP 流（独立 server，非 HTTP） | 预览 |
| `um_snap` | `POST /api/v1/camera/photo`（含 burst/timer/status）、`GET /api/v1/camera/thumbnail?file_path=` | **写**新媒体（拍照） |
| `um_rec` | `POST /api/v1/camera/video/start`、`POST .../video/stop`、`GET .../video/status` | **写**新媒体（录影） |
| `um_pb` | `GET /api/v1/camera/photos`、`.../video/list`、`.../video/playback?id=`、`.../files/download?id=`、`.../files/delete?id=`、`.../database/media`、`.../database/thumbnail` | **读**已有媒体（浏览/回放） |

**永开**（不受能力管，任何产品都有）：`GET /api/v1/device/info`、`GET .../device/sensors`、`GET .../device/capabilities`（本契约）、`/api/v1/system/*`、`/api/v1/storage/*`、`/api/v1/camera/properties*`、`GET .../camera/status`、`.../camera/presets`、`.../camera/preview`。

> 注意 `/thumbnail`：它是**双语义**路由（拍照后取刚拍的缩略图 + 浏览历史缩略图），固件把它归在 `um_snap` 下（因拍照响应**不自带**缩略图字节）。所以 `um_pb`-only 产品（能浏览但不拍照）纯浏览缩略图请走 `GET /database/thumbnail`（在 `um_pb` 下），不要调 `/thumbnail`。

## 5. 推荐适配流程

### 5.1 发现驱动（走 mDNS）

```
1. 扫 mDNS _t32cam._tcp → 拿到 TXT，解析 caps
2. 按 caps 渲染首屏（um_live 在→显示播放器；um_snap 缺→不显示拍照按钮…）
3. 连上 RTSP / HTTP 后，GET /api/v1/device/capabilities 复核（可选）
```

### 5.2 直连驱动（不走 mDNS，如手动输 IP）

```
1. 连 HTTP → GET /api/v1/device/capabilities → 拿 caps 数组
2. 按数组渲染 UI
3. （如需预览）连 RTSP
```

### 5.3 伪代码

```swift
// 解析能力集（mDNS TXT 或 HTTP JSON 都转成同一 Set）
let caps: Set<String> = /* 从 "caps=um_live,um_snap" split，或从 JSON ["um_live","um_snap"] */

showLiveView       = caps.contains("um_live")
showSnapButton     = caps.contains("um_snap")
showRecordButton   = caps.contains("um_rec")
showHistoryAlbum   = caps.contains("um_pb")

// 拍照设置项也跟着 um_snap 走
showPhotoSizePicker = caps.contains("um_snap")
showVideoSizePicker = caps.contains("um_rec")
```

```kotlin
// 网络层：把 404 当"功能不可用"
when (resp.code) {
    200 -> /* 正常 */
    404 -> /* 该能力在本机缺席——别重试，直接禁用对应 UI（理论上 UI 已据 caps 藏掉，这里是兜底） */
    else -> /* 真错误，正常处理 */
}
```

## 6. 边界 / fail-safe 语义

- **字段缺席 / 空串 / `caps=""`**：相机退化为只 `um_live`（fail-safe 朝瘦走）。APP 应按"只预览"渲染。
- **未知 token**（如将来固件加 `um_xxx`，老 APP 不认）：**忽略即可**（前向兼容）。不要因为出现陌生 token 报错。
- **`um_live` 缺席**：极少见（基本恒在）。若真缺席，意味着这台机连预览都没有——APP 应提示"不支持实时预览"。

## 7. 向后兼容（关键）

| 场景 | 行为 | APP 该怎么做 |
|---|---|---|
| **新 APP + 新固件（T28+）** | mDNS 有 `caps`，HTTP 有 `/capabilities` | 按能力渲染（正常路径） |
| **老 APP + 新固件** | 老 APP 不认 caps、显示所有按钮；点到缺席端点 → **404** | （老 APP 侧）把 404 当"功能不可用"提示，别崩溃/别无限重试 |
| **新 APP + 老固件（T28 前）** | mDNS **无** `caps` 字段；`GET /capabilities` → **404** | **回退**：若 `/capabilities` 返 404 且 TXT 无 `caps`，视为"老固件、功能全开"，按全功能渲染（预扫 T28 前的行为） |

**判断老固件的规则**：`/capabilities` 返 404（端点不存在）= 老固件 → 走 legacy 全功能路径。不要把 404 当"这台机啥都不支持"。

## 8. 禁忌（别这么干）

- ❌ **不要按相机型号查表**（"PModel==T32 就能拍照"）。型号与能力的映射会随 SKU 变、会过期。一律读能力集。
- ❌ **不要假设端点永远存在**。能力缺席 → 端点 404。调之前先查 caps，或对 404 有兜底。
- ❌ **不要用 `model` TXT 字段决定功能**。`model` 是身份标识（给人看的），`caps` 才是能力。两者独立。
- ❌ **不要往 token 里塞参数**（分辨率/尺寸）。预览分辨率走 RTSP SDP（连上 `DESCRIBE` 拿），照片尺寸走 `/camera/properties` 的 `Photo_Size_MAX`，视频走 `Video_Size_MAX`。token 只管"有没有"。

## 9. 首产品落地（`["um_live"]`）

第一个产品的能力集 = 只有 `um_live`。APP 连上后应渲染成：

- ✅ 实时预览（RTSP 1280×720）。
- ✅ 设备信息 / 系统设置 / 存储 / 摄像头属性（永开端点）。
- ❌ 不显示拍照按钮、录影按钮、历史相册。
- 设备自己在开机时拍的图（quickSnap）由固件自主上传/管理，APP **看不到**（无 `um_pb`）。

> 若产品定位改为"直播 + 相册"，固件侧 `product.json` 加 `um_pb` token 即可（零内存代价），APP 无需改代码——因为 APP 是按 caps 渲染的。

## 10. 自测清单（APP 侧）

- [ ] 发现阶段解析 mDNS TXT `caps`，按它渲染首屏（无闪烁）。
- [ ] 连上后 `GET /api/v1/device/capabilities`，结果与 TXT 一致。
- [ ] `["um_live"]` fixture：只显示预览，无拍照/录影/相册按钮。
- [ ] 全开 fixture：四个功能按钮都在，各自端点 200。
- [ ] 点缺席端点 → 收到 404 → UI 优雅提示，不崩不重试。
- [ ] 老固件（`/capabilities` 404）→ 回退全功能渲染。
- [ ] 未知 token（`um_xxx`）→ 忽略，不报错。

---

**契约-owner**：固件侧 `src/config/devconf/ProductConfig`（caps 解析）+ `src/service/http_server/http_api_v1.cpp`（端点 gate + `/capabilities`）+ `src/service/discovery/MdnsTxtRecord`（TXT caps）。契约变更须同步更新本文 + [`um-capability-advertising.md`](../decisions/um-capability-advertising.md) §3.6。
