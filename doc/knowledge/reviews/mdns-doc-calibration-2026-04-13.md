# mDNS 文档校准记录

日期：2026-04-13

## 1. 本轮目标

把 mDNS / DNS-SD 相关历史 spec / solution / roadmap / analysis 文档，与当前代码状态对齐，建立第三个正式迁移样板。

## 2. 本轮读取范围

代码：
- `src/app/main_app.cpp`
- `src/service/discovery/MdnsService.h`
- `src/service/discovery/MdnsService.cpp`
- `src/service/discovery/MdnsTxtRecord.h`
- `src/service/discovery/MdnsTxtRecord.cpp`
- `src/service/discovery/CMakeLists.txt`
- `third_party/CMakeLists.txt`
- `third_party/tinysvcmdns/CMakeLists.txt`
- `tests/test_mdns_txt_record.cpp`
- `tests/test_mdns_model_normalization.cpp`

历史文档：
- `doc/spec/mdns-device-discovery-spec.md`
- `doc/solution/20260313-t32-mdns-integration-plan.md`
- `doc/roadmap/20260313-t32-mdns-integration-roadmap.md`
- `doc/analysis/20260313-t32-mdns-simu-verification.md`

## 3. 本轮新增产出

已新增：
- `doc/knowledge/specs/mdns-device-discovery-behavior.md`
- `doc/knowledge/decisions/mdns-cmd-mobile-lifecycle-model.md`
- `doc/knowledge/refs/mdns-code-entry-and-config-keys.md`
- `doc/knowledge/playbooks/mdns-simu-and-bonjour-verification.md`

## 4. 本轮校准出的关键结论

### 4.1 mDNS 已不是“待集成方案”
从当前 CMake 与源码可确认：
- `tinysvcmdns` 已进入构建
- `discovery_service` 已建立
- `main_app.cpp` 已在 `CMD_MOBILE` 路径实际调用 `MdnsService::start()`

因此旧 roadmap / solution 中很多“将要接入”的表述，当前已经过时。

### 4.2 当前主入口是 `CMD_MOBILE`
当前 mDNS 生命周期和：
- HTTP server
- TCP event server
- RTSP server

一起由 `CMD_MOBILE` 编排。

因此不应再把它写成独立 daemon 模型或与主模式无关的常驻服务。

### 4.3 Simu 下跳过真实 Wi‑Fi / DHCP 已落地
历史方案中提出的这点，当前代码已经通过 `#ifdef BUILD_FOR_SIMULATION` 落地。
所以这里应从“设计建议”升级为“当前事实”。

### 4.4 TXT Record 已有集中生成与单测
当前：
- TXT 生成逻辑集中在 `MdnsTxtRecord`
- `model` 归一化在 `MdnsService` 中有独立函数
- 两者都已有针对性测试

因此不应继续把 TXT 描述成散落式拼接逻辑。

### 4.5 外部发现闭环仍不能写满
`doc/analysis/20260313-t32-mdns-simu-verification.md` 明确指出：
- 端口与运行旁证已确认
- 但外部 Bonjour 浏览工具和真机手机端发现尚未全部实测

所以不能把 mDNS 写成“所有发现链路都已完备验证”。

## 5. 当前推荐文档结构

### 已建立
- `specs/mdns-device-discovery-behavior.md`
- `decisions/mdns-cmd-mobile-lifecycle-model.md`
- `refs/mdns-code-entry-and-config-keys.md`
- `playbooks/mdns-simu-and-bonjour-verification.md`

### 后续可补
- `bugs/mdns-network-change-and-reregistration-gaps.md`
- 真机侧发现与联调 review 文档

## 6. 结论

mDNS 已适合作为该仓库第三个正式迁移主题。

原因：
- 历史文档密集
- 当前代码入口明确
- 方案文档与已落地代码之间存在时间差
- 非常适合通过 spec / decision / refs / playbook 结构收敛成当前权威入口

## 7. 下一步建议

优先级建议：
1. 如果后续进入真机联调，补一篇真机发现结果 review
2. 如果网络漂移问题开始暴露，补一篇 bug 文档记录重注册缺口
3. 下一主题可转向 event service / TCP event 或工作模式切换链路
