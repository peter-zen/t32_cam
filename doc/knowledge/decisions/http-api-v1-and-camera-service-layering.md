# HTTP API V1 与 Camera Service 分层决策

## 1. 决策主题

明确 `t32_cam` 当前 HTTP API 为什么采用如下分层：
- `http_server.c` 负责服务生命周期与基础探针
- `http_api_v1.cpp` 负责 V1 路由与协议包装
- `ICameraService` / `CameraPropertyService` 负责 camera 域业务抽象
- 具体平台实现由 `CameraServiceFactory` 选择 `Sim` 或 `T32`

## 2. 当前分层结构

当前代码已经形成较明确的四层：
1. HTTP Server 层
2. V1 路由/协议层
3. Camera 业务抽象层
4. 平台实现层

这不是偶然结果，而是当前仓库里相对正确的演化方向。

## 3. 为什么不把所有逻辑直接写在 HTTP handler 里

如果把拍照、录像、属性、预览、数据库下载全部直接塞进 HTTP handler，会导致：
- 协议层和业务层强耦合
- simu / T32 差异没法稳定收口
- 后续如果增加 TCP 事件、CLI 入口、非 HTTP 触发源，业务逻辑只能重复实现

当前 `ICameraService` 的存在，就是为了避免 HTTP handler 成为“巨石控制器”。

## 4. 为什么 camera 域要独立于 device/system/storage 来看

当前代码校准后的事实是：
- `device/system/storage` 多数还是最小占位或接收确认接口
- `camera` 域已经接入：
  - `ICameraService`
  - `PhotoJobManager`
  - `CameraPropertyService`
  - `MetadataDao`
  - `TcpEventService`

所以在工程成熟度上，camera 域明显比外围三域更深。

这意味着文档和设计都不该再把四个域写得“同成熟度”。
那是错误表达。

## 5. 当前采用的职责划分

### 5.1 `http_server.c`
职责：
- 启停 CivetWeb
- 注册 `/api/health`、`/healthz`
- 调用 `http_api_register_v1()`
- 提供 default 404 handler

不负责：
- 业务参数解析
- camera 业务决策
- 属性校验

### 5.2 `http_api_v1.cpp`
职责：
- 路由注册
- GET/POST 方法限制
- JSON body / query 参数解析
- HTTP 错误与业务错误包装
- 调用 camera/property/storage 相关服务

不应承担：
- 平台细节
- 直接操作硬件
- 全部业务状态机

### 5.3 `ICameraService`
职责：
- 统一抽象 camera 域能力
- 让上层路由不用直接关心 simu / T32 差异
- 给 photo / video / preview / media list / delete file 提供一致入口

### 5.4 `CameraPropertyService`
职责：
- 集中管理 property schema、默认值、约束、reset 与持久化

这是必要的，因为 property 天然需要：
- 统一 schema
- 统一校验
- 统一错误反馈

如果把它分散在各个 API handler 里，后续必然漂移。

### 5.5 `PhotoJobManager`
职责：
- 把 async photo 从一次性 HTTP 请求中解耦出来
- 为 `job_id`、状态查询、事件推送提供稳定模型

这说明 camera photo 当前已经不是“只有同步 snap 调用”的简单接口了。

## 6. 为什么基础探针不放进 `/api/v1`

当前 `/api/health` 与 `/healthz` 独立于业务版本，是合理的。
原因：
- 它们是基础存活探针，不是业务语义
- 运维与外部探测不应该绑定业务版本升级
- 即使业务路由未来继续升级到 `/api/v2`，健康探针也应保持稳定

因此不建议把 health 再并回 `/api/v1`。

## 7. 当前分层的收益

### 7.1 易于在 simu 上先跑通
由于 `CameraServiceFactory` 会切到 `CameraServiceSim`：
- V1 接口可以在 simu 中先完成大部分验证
- 不需要等真机链路全部闭环后才有 HTTP 可测面

### 7.2 便于保留统一产品语义
- 上层文档可以围绕 `/api/v1/camera/*` 讲产品语义
- 平台差异下沉到 `CameraServiceSim/T32`

### 7.3 便于继续演进
后续无论：
- 补强 T32 真机实现
- 扩展 photo/video 状态模型
- 引入更多事件能力
都不需要先推翻整个 V1 协议层结构

## 8. 当前分层的不足

也必须明确：当前分层虽然方向对，但还没完全成熟。

### 8.1 外围三域仍偏占位
- `device/system/storage` 目前还不够深
- 分层清晰不等于业务闭环完成

### 8.2 T32 实现深度不均衡
- `CameraServiceT32` 中仍有未实现或简化逻辑
- 这会导致“接口结构很完整，但真机语义还不够强”

### 8.3 HTTP 层仍有一定业务感知
`http_api_v1.cpp` 目前仍写了不少组装逻辑、错误码选择与 JSON 结构构造。
这在当前阶段可以接受，但未来如果继续扩大复杂度，可能需要再抽离一层 response assembler / domain adapter。

## 9. 决策结论

当前仓库 HTTP API 的正确演进方向是：
1. 保持 `/api/v1/*` 作为唯一业务主线
2. 保持 health probe 独立于业务版本
3. 继续围绕 `ICameraService + CameraPropertyService + PhotoJobManager` 深化 camera 域
4. 不再恢复 legacy 路由
5. 外围三域后续只做“在现有 V1 结构内补深”，而不是另起第二套风格

## 10. 后续建议

- 下一步可补 `refs/http-api-v1-routes-and-simu-test-entry.md`
- 当 T32 与 simu 的真实差异进一步暴露后，再补一篇 bug/review 文档专门记录平台差异与未实现项
