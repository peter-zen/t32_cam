# HTTP API V1 行为规格

## 1. 目的

定义 `t32_cam` 当前 HTTP API V1 的仓库级权威行为描述，覆盖：
- 路由分层与版本策略
- 返回格式与错误语义
- `device / system / storage / camera` 四类域的当前成熟度
- `camera` 域与 `CameraService / PhotoJobManager / CameraPropertyService` 的接线关系
- 已确认事实与仍待验证项

本规格优先以当前代码行为为依据，而不是历史参考手册的静态描述。

## 2. 当前状态

- 状态：已完成一轮代码校准
- 当前主线：`/api/v1/*`
- 基础探针：`/api/health` 与 `/healthz`
- legacy 业务路径：已退役，不应继续作为主线能力描述

## 3. 当前权威代码入口

- `src/service/http_server/http_server.c`
- `src/service/http_server/http_api_v1.cpp`
- `src/service/http_server/PhotoJobManager.h`
- `src/service/http_server/PhotoJobManager.cpp`
- `src/service/camera/ICameraService.h`
- `src/service/camera/CameraPropertyService.h`
- `src/service/camera/CameraServiceFactory.cpp`
- `src/service/camera/impl/CameraServiceSim.cpp`
- `src/service/camera/impl/CameraServiceT32.cpp`

历史参考但不作为唯一依据：
- `doc/reference/20260324-http-api-reference.md`
- `doc/reference/20260324-http-api-client-quick-reference.md`
- `doc/analysis/20260324-http-api-legacy-vs-v1-assessment.md`
- `doc/reference/20260305-http-simu-test-usage.md`

## 4. 版本策略与入口规则

### 4.1 当前业务主线
当前业务接口主线明确是：
- `/api/v1/*`

### 4.2 基础探针不参与版本化
`http_server.c` 当前独立注册：
- `GET /api/health`
- `GET /healthz`

因此它们是基础探针，不属于业务版本层。

### 4.3 default handler 语义
未命中的路径最终会落到默认 `404` handler，而不是被某种 legacy 兼容层吞掉。
因此当前结构上已不支持“继续默认兼容旧业务路由”的说法。

## 5. 通用响应语义

### 5.1 普通 JSON 成功响应
`http_api_v1.cpp` 中当前普通成功响应统一为：
```json
{
  "code": 0,
  "message": "success",
  "data": ...
}
```

### 5.2 协议错误
路径错误、方法错误、JSON 非法、参数缺失等协议错误，当前会：
- 返回 HTTP `4xx`
- 仍给出 JSON 包装

因此客户端不能只看 body，也不能只看 HTTP status，二者都要看。

### 5.3 业务错误
某些业务错误当前仍返回 HTTP `200`，但：
- `code != 0`
- `message` 描述业务失败原因

这意味着：
- “HTTP 200 就代表成功”是错误客户端假设

### 5.4 二进制接口例外
以下接口直接返回文件或图片流，而不是统一 JSON：
- `/api/v1/camera/database/media`
- `/api/v1/camera/database/thumbnail`
- `/api/v1/camera/preview`
- `/api/v1/camera/thumbnail`

## 6. 域划分与成熟度

### 6.1 Device 域
当前路由：
- `GET /api/v1/device/info`
- `GET /api/v1/device/sensors`

代码校准结果：
- 已稳定注册到 V1
- 但当前实现主要是占位/示例数据构造
- 不能把这些字段当成“已全部接真实硬件链路”的事实

因此当前更准确表述是：
- Device 域接口稳定存在
- 语义框架已固定
- 数据深度仍偏占位

### 6.2 System 域
当前路由：
- `POST /api/v1/system/datetime`
- `POST /api/v1/system/workmode`

代码校准结果：
- 当前主要体现“请求接收成功”语义
- 会回包 `accepted=true`
- 但不等同于完整下发链路已全闭环

因此 System 域当前更像：
- 统一化的命令接收接口
- 而不是已完全落地的系统控制闭环

### 6.3 Storage 域
当前路由：
- `GET /api/v1/storage/info`
- `POST /api/v1/storage/format`

代码校准结果：
- `storage/info` 仍是构造型占位数据
- `storage/format` 当前表达的是“已接受 / scheduled”
- 不应写成“真实格式化过程已完整接入并可观测”

### 6.4 Camera 域
这是当前实现深度最高的域，包含：
- photo / burst / timer / status
- video start / stop / status / list
- properties / single property / reset
- presets
- photos / files delete
- database download
- preview / thumbnail

其中真正值得当当前主线能力描述的，是 camera 域，而不是外围占位域。

## 7. Camera 域核心模型

### 7.1 CameraService 是统一业务抽象
`ICameraService` 当前定义了：
- 拍照
- 连拍
- 定时拍照
- 预览帧抓取
- 录像 start/stop/status
- 属性访问
- 媒体列表 / 数据库路径 / 文件删除
- factory reset

因此 HTTP V1 camera 域并不是直接散落调用底层模块，而是围绕 `ICameraService` 组织。

### 7.2 平台选择
`CameraServiceFactory::create()` 当前根据编译模式选择：
- `SIMULATION_MODE` -> `CameraServiceSim`
- 非仿真 -> `CameraServiceT32`

这意味着：
- camera HTTP API 的产品语义是统一的
- 具体行为深度会受 simu / T32 实现差异影响

### 7.3 Photo 异步模型
`POST /api/v1/camera/photo` 支持：
- `response_mode = sync`
- `response_mode = async`

当为 async 时：
- 由 `PhotoJobManager` 接收任务
- 生成 `job_id`
- 可通过 `/api/v1/camera/photo/status?job_id=...` 查询
- 还会返回事件信息，指向：
  - `camera.photo.completed`
  - `camera.photo.failed`

这说明当前拍照接口不是单一“同步阻塞调用”，而是已具备任务化模型。

### 7.4 Property 模型
camera properties 当前不是无约束 key-value 杂写，而是由 `CameraPropertyService` 统一管理：
- schema
- valid options
- min/max/step
- default value
- reset
- 批量设置与单属性设置

因此当前 camera property 域的正确描述是：
- 已有集中式属性治理
- 已支持约束校验和部分错误明细返回

不要再把它写成“简单透传配置接口”。

### 7.5 Preview / Thumbnail
`preview` 与 `thumbnail` 当前支持直接返回 JPEG；`preview` 还支持 `mjpeg`。
因此：
- 这是当前 V1 中少数明显面向实时媒体读取的 HTTP 能力
- 不应与 JSON 配置类接口混为一谈

## 8. 当前平台差异与成熟度差异

### 8.1 Sim 实现
`CameraServiceSim.cpp` 已接入较多真实行为：
- `takePhoto()` 会生成文件
- `capturePreviewFrame()` 会抓图或 fallback 到 sample image
- `startRecord()` / `stopRecord()` 已接入 `VideoRecorder`
- timer photo 有线程驱动逻辑

因此 simu 下很多 V1 路径不仅是占位，而是可以实际工作。

### 8.2 T32 实现
`CameraServiceT32.cpp` 中部分能力仍较弱或未完全闭环，例如：
- `startBurstPhoto()` 直接返回未实现
- `getPhotoStatus()` / `getRecordStatus()` 仍偏简化
- 某些流程更像最低可用接线，而不是完整状态机

所以不能把 simu 下的“可用度”机械外推为 T32 真机全部等价成立。

## 9. 当前明确结论

### 9.1 可以明确写成事实的
- `/api/v1/*` 是当前唯一业务主线
- `/api/health` 与 `/healthz` 是基础探针
- HTTP 层已不再维护 legacy 业务路由主线
- camera 域是当前实现最深的 V1 域
- photo 已具备 sync/async 双模式
- property 已接入集中式 schema / validation / reset
- preview/thumbnail/database 下载属于二进制接口，不走统一 JSON 包装

### 9.2 不能写得过满的
- `device/system/storage` 已全部接真实业务链路
- T32 与 simu 在所有 camera 能力上完全等价
- video status / burst photo / system/workmode 等都已达到最终产品态

这些说法都会过度承诺。

## 10. 仍待验证项

- 真机 `CameraServiceT32` 下录像状态与定时任务状态是否足够稳定
- `burst photo` 的真机实现何时补全
- `device/system/storage` 从占位数据升级为真实接线后的字段稳定性
- `test_http_api_v1_simu.sh` 覆盖之外的更多异常路径

## 11. 推荐后续拆分

后续如果继续治理，建议进一步拆成：
- `decisions/http-api-v1-and-camera-service-layering.md`
- `refs/http-api-v1-routes-and-simu-test-entry.md`
- `bugs/http-api-known-gaps-t32-vs-simu.md`

当前这篇规格先承担“统一入口”作用，避免继续在 reference / analysis / job 文档之间来回跳。