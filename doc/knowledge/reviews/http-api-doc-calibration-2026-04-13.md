# HTTP API 文档校准记录

日期：2026-04-13

## 1. 本轮目标

把 HTTP API / camera service 相关的历史 reference / analysis 文档，与当前代码状态对齐，建立第二批正式迁移样板。

## 2. 本轮读取范围

代码：
- `src/service/http_server/http_server.c`
- `src/service/http_server/http_api_v1.cpp`
- `src/service/http_server/PhotoJobManager.h`
- `src/service/http_server/PhotoJobManager.cpp`
- `src/service/camera/ICameraService.h`
- `src/service/camera/CameraPropertyService.h`
- `src/service/camera/CameraServiceFactory.cpp`
- `src/service/camera/impl/CameraServiceSim.cpp`
- `src/service/camera/impl/CameraServiceT32.cpp`

历史文档：
- `doc/analysis/20260324-http-api-legacy-vs-v1-assessment.md`
- `doc/reference/20260324-http-api-reference.md`
- `doc/reference/20260324-http-api-client-quick-reference.md`
- `doc/reference/20260305-http-simu-test-usage.md`

## 3. 本轮新增产出

已新增：
- `doc/knowledge/specs/http-api-v1-behavior.md`
- `doc/knowledge/decisions/http-api-v1-and-camera-service-layering.md`
- `doc/knowledge/refs/http-api-v1-routes-and-simu-test-entry.md`

## 4. 本轮校准出的关键结论

### 4.1 V1 是唯一业务主线
这点在历史分析文档里已经有结论，但本轮通过代码再次确认：
- 当前业务主线就是 `/api/v1/*`
- `/api/health` 与 `/healthz` 是独立基础探针
- 不应再把 legacy 业务路径描述成并行主线

### 4.2 `device/system/storage` 的成熟度不能写得过满
历史参考手册列出了完整路径，但当前代码显示：
- 这三个域多数还是占位响应或请求接收接口
- 不能写成“都已完整接真实业务”

### 4.3 camera 域成熟度明显更高
camera 域已经接入：
- `ICameraService`
- `PhotoJobManager`
- `CameraPropertyService`
- `MetadataDao`
- preview / thumbnail / database download 等能力

因此后续如果做产品/平台分层，camera 域应优先成为正式治理中心。

### 4.4 Sim 与 T32 不能机械等价
代码校准显示：
- `CameraServiceSim` 已实现较多真实可运行行为
- `CameraServiceT32` 仍有未实现或简化路径，例如 burst photo / 部分状态接口

所以不能把 simu 测试全通过，直接表述成真机完全等价已完成。

## 5. 当前推荐文档结构

### 已建立
- `specs/http-api-v1-behavior.md`
- `decisions/http-api-v1-and-camera-service-layering.md`
- `refs/http-api-v1-routes-and-simu-test-entry.md`

### 后续可补
- `bugs/http-api-known-gaps-t32-vs-simu.md`
- `playbooks/http-api-simu-validation.md`

## 6. 结论

HTTP API / camera service 已适合作为这个仓库第二批正式迁移主题。

原因：
- 历史文档齐全
- 当前代码入口清晰
- 旧 reference 与当前实现成熟度之间存在明显“容易被写过满”的风险
- 非常适合建立 spec / decision / refs 三分法样板

## 7. 下一步建议

优先级建议：
1. 若要继续补齐第二批样板，可再补 `playbooks/http-api-simu-validation.md`
2. 若要切第三个主题，建议选择 mDNS
3. 若后续进入真机联调阶段，再专门补一篇 T32 与 simu 差异的 bug/review 文档
