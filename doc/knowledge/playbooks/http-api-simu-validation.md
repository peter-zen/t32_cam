# HTTP API SIMU 验证操作手册

## 1. 目的

给后续会话和协作者提供一条最小充分、可重复的 HTTP API 仿真验证路径，用于：
- 启动当前推荐的独立 HTTP 测试服务
- 运行 V1 主验证脚本
- 理解 legacy 退役检查脚本的真实用途
- 在失败时按固定顺序排查，而不是凭感觉猜

这是一篇操作手册 (Playbook)，不是协议规格。协议语义请优先看：
- `../specs/http-api-v1-behavior.md`
- `../decisions/http-api-v1-and-camera-service-layering.md`
- `../refs/http-api-v1-routes-and-simu-test-entry.md`

## 2. 适用范围

适用于：
- PC 仿真模式 (`BUILD_FOR_SIMULATION=ON`)
- 目标是验证当前 `/api/v1/*` HTTP 主线
- 希望在不经过 `htc_main_app -m` 网络前置逻辑的情况下，直接验证 HTTP server

不适用于：
- T32 真机闭环验证
- Wi‑Fi 接入、移动配网或完整产品启动链路验证
- 证明 simu 与真机语义完全等价

如果你把这篇 playbook 当成真机完备性证明，那就是错误使用。

## 3. 当前推荐入口

### 3.1 推荐二进制
当前推荐使用独立测试服务：
- `build_sim/src/service/http_server/test_http_server`

代码依据：
- `src/service/http_server/CMakeLists.txt` 中仅在 `BUILD_FOR_SIMULATION` 下编译 `test_http_server`

### 3.2 推荐主脚本
当前推荐主脚本：
- `script/test_http_api_v1_simu.sh`

它会：
- 启动 `test_http_server`
- 等待 `/api/health` 就绪
- 顺序验证 health / v1 device / system / storage / camera 路径
- 验证 preview 与 thumbnail 的 JPEG 返回
- 最终给出 pass/fail 汇总

### 3.3 legacy 脚本的正确定位
- `script/test_http_api_simu.sh`

它当前的正确用途是：
- 验证旧 `/api/*` 业务路由已经退役并返回 `404`

不要把它写成 legacy 正向能力验证脚本。
那样描述是错的。

## 4. 前置条件

### 4.1 基本依赖
脚本当前显式依赖：
- `curl`
- `sed`
- `awk`
- `mktemp`

可选但有帮助：
- `fuser`

若缺少上述命令，脚本会直接失败，或失去自动清理占端口进程的能力。

### 4.2 已完成仿真构建
在仓库根目录执行：
```bash
mkdir -p build_sim && cd build_sim
cmake -DBUILD_FOR_SIMULATION=ON ..
make -j$(nproc)
```

如果只是增量重编：
```bash
cd build_sim
make -j$(nproc)
```

### 4.3 核对服务二进制存在
脚本默认依赖：
```bash
build_sim/src/service/http_server/test_http_server
```

若该文件不存在或不可执行，先不要继续看 HTTP 返回值；问题在构建阶段，不在接口阶段。

## 5. 标准验证流程

### 5.1 一键主验证
在仓库根目录执行：
```bash
./script/test_http_api_v1_simu.sh
```

预期成功输出结尾类似：
```text
[SUMMARY] pass=<N> fail=0
[SUMMARY] ALL PASS
```

### 5.2 自定义端口
默认端口来自脚本：
- `HTTP_TEST_PORT`
- 默认值：`8080`

例如：
```bash
HTTP_TEST_PORT=8081 ./script/test_http_api_v1_simu.sh
```

### 5.3 自定义日志路径
默认日志路径：
```bash
build_sim/sdcard/logs/http_test_server.log
```

可覆盖：
```bash
HTTP_TEST_LOG=/tmp/http_test_server.log ./script/test_http_api_v1_simu.sh
```

### 5.4 运行 legacy 退役检查
```bash
./script/test_http_api_simu.sh
```

预期不是“旧 API 全通过”，而应是：
- 旧业务路由按退役预期返回 `404`

## 6. 当前主脚本覆盖点

根据 `script/test_http_api_v1_simu.sh`，当前覆盖的主路径包括：

### 6.1 基础探针
- `GET /api/health`
- `GET /healthz`

### 6.2 Device 域
- `GET /api/v1/device/info`
- `GET /api/v1/device/sensors`

### 6.3 Storage / System 域
- `GET /api/v1/storage/info`
- `POST /api/v1/system/datetime`
- `POST /api/v1/system/workmode`
- `POST /api/v1/storage/format`

### 6.4 Camera property 域
- `GET /api/v1/camera/properties`
- `GET /api/v1/camera/properties/resolution`
- `POST /api/v1/camera/properties`
- `POST /api/v1/camera/properties/reset`

### 6.5 Camera photo / video 域
- `POST /api/v1/camera/photo`（sync）
- `POST /api/v1/camera/photo`（async）
- `GET /api/v1/camera/photo/status`
- `POST /api/v1/camera/video/start`
- `GET /api/v1/camera/video/status`
- `POST /api/v1/camera/video/stop`
- `GET /api/v1/camera/photos`
- `GET /api/v1/camera/video/list`

### 6.6 二进制媒体接口
- `GET /api/v1/camera/preview?format=jpeg`
- `GET /api/v1/camera/thumbnail?size=160`

## 7. 推荐人工验证流程

当脚本失败或你只想验证单点时，按下面顺序，不要乱跳。

### 7.1 手工启动服务
```bash
./build_sim/src/service/http_server/test_http_server
```

### 7.2 先测健康探针
```bash
curl http://127.0.0.1:8080/api/health
curl http://127.0.0.1:8080/healthz
```

如果这里都不通，后面的 V1 路由验证没有意义。

### 7.3 再测一个最简单的 V1 JSON 接口
```bash
curl http://127.0.0.1:8080/api/v1/device/info
```

### 7.4 再测一个 POST JSON 接口
```bash
curl -X POST http://127.0.0.1:8080/api/v1/system/datetime \
  -H 'Content-Type: application/json' \
  -d '{"datetime":"2026-03-05T09:30:00"}'
```

### 7.5 再测二进制输出接口
```bash
curl -o /tmp/preview.jpg 'http://127.0.0.1:8080/api/v1/camera/preview?format=jpeg'
file /tmp/preview.jpg
```

## 8. 排查顺序

### 8.1 服务起不来
先检查：
- `build_sim/src/service/http_server/test_http_server` 是否存在
- 是否做过 `BUILD_FOR_SIMULATION=ON` 构建
- 日志文件：
  - `build_sim/sdcard/logs/http_test_server.log`
  - 或你通过 `HTTP_TEST_LOG` 指定的路径

### 8.2 端口冲突
脚本会在系统存在 `fuser` 时尝试清理 `${PORT}/tcp`。
如果没有 `fuser`，或端口仍被占用，优先：
```bash
HTTP_TEST_PORT=8081 ./script/test_http_api_v1_simu.sh
```

不要先怀疑协议层；先解决端口冲突。

### 8.3 health 成功但 V1 路由失败
优先判断：
- 是单一路由失败，还是整个 `/api/v1/*` 都失败
- 是 HTTP 层失败，还是返回 `200` 但 `code != 0`

这里必须同时看：
- HTTP status
- JSON body

只看其中一个都不够。

### 8.4 async photo 失败
主脚本当前会：
- 提交 async photo
- 提取 `job_id`
- 轮询 `/api/v1/camera/photo/status`

如果失败，重点看：
- 是否成功提取 `job_id`
- 状态是否长时间不进入 `completed`
- 服务日志中是否出现 photo 相关错误

### 8.5 preview / thumbnail 失败
这两个接口不是统一 JSON，而是二进制 JPEG。
因此正确检查项是：
- HTTP code 是否为 `200`
- `Content-Type` 是否为 `image/jpeg`
- `size_download` 是否大于 0

如果你还按 JSON 接口方式检查，那检查方法本身就是错的。

## 9. 常见错误理解

- 不要把 `htc_main_app -m` 当作最稳的 HTTP 仿真入口
- 不要把 legacy 脚本当作旧接口正向能力证明
- 不要把 script 覆盖通过，写成“真机功能全部完成”
- 不要把 preview / thumbnail 按 JSON 接口理解
- 不要只看 HTTP 200 就认定业务成功

## 10. 当前局限

这篇 playbook 只覆盖：
- 当前 simu 下的最小充分验证路径
- 脚本已覆盖的主路径
- 常见失败的优先排查顺序

它不覆盖：
- T32 真机稳定性结论
- 全量异常路径
- 长时间 soak test
- 高并发压测
- 真机与 simu 的差异归因

这些如果后续需要，应单独补：
- `bugs/http-api-known-gaps-t32-vs-simu.md`
- 或专门的真机联调 / 回归验证文档

## 11. 推荐阅读顺序

进入 HTTP API 主题时，建议按下面顺序：
1. `../specs/http-api-v1-behavior.md`
2. `../decisions/http-api-v1-and-camera-service-layering.md`
3. `../refs/http-api-v1-routes-and-simu-test-entry.md`
4. 本文
5. 再读具体代码：
   - `src/service/http_server/http_api_v1.cpp`
   - `src/service/http_server/PhotoJobManager.cpp`
   - `src/service/camera/impl/CameraServiceSim.cpp`
