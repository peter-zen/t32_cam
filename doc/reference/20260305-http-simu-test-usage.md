# t32_yb HTTP SIMU 测试使用说明

## 背景

在 `t32_yb` 中，`htc_main_app -m` 走的是 `CMD_MOBILE` 流程，包含真机网络前置逻辑（读取 `CSSID/CPWD`、连接 Wi-Fi、DHCP）。  
因此在 SIMU 场景下，直接用 `-m` 测 HTTP 容易因网络前置检查提前退出，不是最稳定的 HTTP 验证入口。

## 推荐测试入口

使用独立 HTTP 测试程序 `test_http_server`，并通过脚本自动验证 API。

- 脚本路径：`script/test_http_api_simu.sh`
- 服务二进制：`build_sim/src/service/http_server/test_http_server`

## 使用方式

在工程根目录执行：

```bash
cd /home/zengping/project/huntcam/code/t32_yb
cmake --build build_sim -j
./script/test_http_api_simu.sh
```

测试通过时会输出：

```text
[SUMMARY] pass=16 fail=0
[SUMMARY] ALL PASS
```

## 脚本验证内容

- `GET /api/health`
- `GET /api/device/info`
- `GET /api/sensor/data`
- `GET /api/params`
- `GET /api/storage/info`
- `GET /api/record/status`
- `GET /api/snapshot`
- `POST /api/params/set`
- `POST /api/params/reset`
- `POST /api/system/datetime`
- `POST /api/system/workmode`
- `POST /api/storage/format`
- `POST /api/record/start`
- `POST /api/record/stop`

## 日志与可选参数

- 默认 server 日志：`build_sim/sdcard/logs/http_test_server.log`
- 可改端口：`HTTP_TEST_PORT=8081 ./script/test_http_api_simu.sh`
- 可改日志路径：`HTTP_TEST_LOG=/tmp/http_test.log ./script/test_http_api_simu.sh`

## 如果你仍想手动验证

```bash
./build_sim/src/service/http_server/test_http_server
curl http://127.0.0.1:80/api/health
```
