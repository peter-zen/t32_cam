# HTTP API V1 路由与 SIMU 测试入口参考

## 1. 目的

给后续会话提供一个最小充分入口，用于快速回答：
- 当前 HTTP API 该从哪里看
- 应该跑哪个二进制与脚本
- 哪些文档是 reference，哪些才是当前知识入口

## 2. 当前代码入口

### 2.1 服务启动
- `src/service/http_server/http_server.c`

职责：
- 启停 CivetWeb
- 注册 `/api/health` 与 `/healthz`
- 注册 V1 路由

### 2.2 V1 路由
- `src/service/http_server/http_api_v1.cpp`

这里是当前最核心的 HTTP 业务入口。
如果用户问“某个路径到底有没有、怎么返回、支持什么参数”，默认先看这个文件。

### 2.3 Camera 抽象与平台实现
- `src/service/camera/ICameraService.h`
- `src/service/camera/CameraServiceFactory.cpp`
- `src/service/camera/impl/CameraServiceSim.cpp`
- `src/service/camera/impl/CameraServiceT32.cpp`
- `src/service/camera/CameraPropertyService.h`

### 2.4 异步拍照模型
- `src/service/http_server/PhotoJobManager.h`
- `src/service/http_server/PhotoJobManager.cpp`

## 3. 当前最有价值的历史参考文档

如果必须读旧 `doc/`，优先：
- `doc/reference/20260324-http-api-reference.md`
- `doc/reference/20260324-http-api-client-quick-reference.md`
- `doc/analysis/20260324-http-api-legacy-vs-v1-assessment.md`
- `doc/reference/20260305-http-simu-test-usage.md`

注意：
- 这些是历史参考，不再是仓库内当前权威入口
- 当前应以 `doc/knowledge/` 下的新文档为准

## 4. 当前推荐的 SIMU 测试入口

根据 `doc/reference/20260305-http-simu-test-usage.md`，在 simu 场景下更稳定的入口是：
- 独立 HTTP 测试程序 `test_http_server`
- 而不是直接依赖 `htc_main_app -m`

原因：
- `-m` 走 `CMD_MOBILE`，包含网络前置逻辑
- 在 simu 场景下，这不是最稳的 HTTP 验证入口

## 5. 当前常用测试脚本

### 5.1 V1 主验证
- `script/test_http_api_v1_simu.sh`

这是当前最重要的主验证脚本。

### 5.2 legacy 退役验证
- `script/test_http_api_simu.sh`

当前它的意义不是“测试 legacy 可用”，而是：
- 验证 legacy 业务路由已经退役并返回 `404`

不要误读这个脚本的用途。

## 6. 常见人工验证方式

### 6.1 手工启动测试服务
历史文档给出的手工入口：
```bash
./build_sim/src/service/http_server/test_http_server
```

### 6.2 手工探针检查
```bash
curl http://127.0.0.1:80/api/health
curl http://127.0.0.1:80/healthz
```

### 6.3 主脚本验证
```bash
./script/test_http_api_v1_simu.sh
```

## 7. 后续进入本主题时的默认阅读顺序

建议默认按下面顺序：
1. `doc/knowledge/specs/http-api-v1-behavior.md`
2. `doc/knowledge/decisions/http-api-v1-and-camera-service-layering.md`
3. 本文
4. 再去读：
   - `src/service/http_server/http_api_v1.cpp`
   - `src/service/camera/ICameraService.h`
   - 某个具体平台实现文件

## 8. 当前不应再重复的错误理解

- 不要再把 legacy `/api/*` 当作当前双主线之一
- 不要把 `test_http_api_simu.sh` 当作 legacy 正向功能测试
- 不要把 simu 下的可用度直接等同于 T32 真机已完全实现
- 不要把 reference 文档当作仓库内当前唯一权威入口
