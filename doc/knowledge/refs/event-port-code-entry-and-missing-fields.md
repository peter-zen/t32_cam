# event_port 代码入口与缺失字段参考

## 1. 目的

给后续会话提供最小充分入口，用于快速回答：
- event port 当前从哪里来
- 哪些地方本该暴露但还没暴露
- 该读哪些文件确认缺口

## 2. 当前最重要代码入口

### 2.1 event server 端口定义
- `src/service/event/TcpEventService.h`

关键常量：
- `kDefaultTcpEventPort = 5000`

### 2.2 启动接线
- `src/app/main_app.cpp`

关键位置：
- `CMD_MOBILE` 中 `TcpEventService::start(kDefaultTcpEventPort)`

### 2.3 HTTP 表层
- `src/service/http_server/http_api_v1.cpp`

关键位置：
- `build_device_info_json()`

### 2.4 mDNS TXT 表层
- `src/service/discovery/MdnsTxtRecord.h`
- `src/service/discovery/MdnsTxtRecord.cpp`
- `src/app/main_app.cpp` 中 `buildMdnsParams(...)`

## 3. 当前缺失字段

### 3.1 HTTP 缺失
`device/info` 当前没有：
- `event_port`

### 3.2 mDNS 缺失
当前 TXT Record 没有：
- `event_port`

`MdnsTxtPayload` 结构里也没有对应字段。

## 4. 当前最容易误读的点

- 不要把 event server 存在误写成客户端已可发现
- 不要把默认端口 `5000` 当成已公开协议承诺
- 不要把设计文档里的建议写成当前实现事实

## 5. 推荐阅读顺序

进入该主题时建议按下面顺序：
1. `doc/knowledge/specs/event-port-exposure-gap.md`
2. `doc/knowledge/decisions/event-port-http-vs-mdns-exposure-strategy.md`
3. 本文
4. 再看：
   - `src/service/event/TcpEventService.h`
   - `src/app/main_app.cpp`
   - `src/service/http_server/http_api_v1.cpp`
   - `src/service/discovery/MdnsTxtRecord.cpp`
