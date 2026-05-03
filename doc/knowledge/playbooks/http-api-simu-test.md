# HTTP API SIMU 测试

## 背景

`htc_main_app -m` 走 `CMD_MOBILE` 流程，包含真机网络前置逻辑（读取 `CSSID/CPWD`、连接 Wi-Fi、DHCP）。
SIMU 场景下直接用 `-m` 测 HTTP 容易因网络前置检查提前退出，不是最稳定的 HTTP 验证入口。

## 推荐测试入口

使用独立 HTTP 测试程序 `test_http_server`，并通过脚本自动验证 API。

- V1 脚本：`script/test_http_api_v1_simu.sh`
- Legacy 退役检查脚本：`script/test_http_api_simu.sh`
- 完整接口手册：`doc/knowledge/specs/http-api-reference.md`
- 客户端精简版：`doc/knowledge/refs/http-api-client-quick-reference.md`

## 使用方式

在工程根目录执行：

```bash
cd /path/to/t32
cmake --build build_sim -j
./script/test_http_api_v1_simu.sh
```

测试通过时会输出：

```text
[SUMMARY] pass=43 fail=0
[SUMMARY] ALL PASS
```

## Camera Properties 测试

```bash
# 构建并运行属性单元测试
cmake --build build_sim --target test_camera_properties -j
cd build_sim && ./tests/test_camera_properties
```

## 手动验证

```bash
# 启动测试 HTTP 服务器
./build_sim/src/service/http_server/test_http_server

# 基础探针
curl http://127.0.0.1:80/api/health

# 属性接口 (CPS grouped model)
curl 'http://127.0.0.1:80/api/v1/camera/properties?group=Camera_Setting&include=schema,value'
curl 'http://127.0.0.1:80/api/v1/camera/properties/item?name=CAM_Mode'

# 设置属性
curl -X POST http://127.0.0.1:80/api/v1/camera/properties/set \
  -H 'Content-Type: application/json' \
  -d '{"name":"CAM_Mode","value":2}'

# Status
curl 'http://127.0.0.1:80/api/v1/camera/status?group=all'
```

## 日志与可选参数

- 默认 server 日志：`build_sim/sdcard/logs/http_test_server.log`
- 可改端口：`HTTP_TEST_PORT=8081 ./script/test_http_api_v1_simu.sh`
- 可改日志路径：`HTTP_TEST_LOG=/tmp/http_test.log ./script/test_http_api_v1_simu.sh`

## 脚本验证内容

V1 主验证脚本 `script/test_http_api_v1_simu.sh` 覆盖：

- 基础探针：`/api/health`, `/healthz`
- Device 域：`/api/v1/device/info`, `/api/v1/device/sensors`
- Storage 域：`/api/v1/storage/info`, `/api/v1/storage/format`
- System 域：`/api/v1/system/datetime`, `/api/v1/system/workmode`
- Camera 属性：`/api/v1/camera/properties`, `/api/v1/camera/properties/reset`
- Camera 拍照：`/api/v1/camera/photo`, `/api/v1/camera/photo/status`
- Camera 录像：`/api/v1/camera/video/start`, `/video/status`, `/video/stop`
- Camera 媒体：`/api/v1/camera/photos`, `/camera/video/list`
- Camera 预览：`/api/v1/camera/preview`, `/camera/thumbnail`
