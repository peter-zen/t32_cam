# t32_cam 项目概览

## 1. 项目定位

`t32_cam` 是一个面向 Ingenic T32 平台的相机固件/应用仓库，目标是在真机与 PC 仿真两种模式下复用核心代码，覆盖：
- 视频/音频采集与录制
- RTSP 流媒体输出
- HTTP 服务与设备控制接口
- mDNS 发现、事件服务与网络侧能力
- 存储、配置、GPIO/MCU/电源等硬件控制

该项目同时承担：
- 嵌入式真机构建
- PC 仿真验证
- 关键能力的分层重构与接口稳定化

## 2. 当前已识别的构建模式

### 2.1 真机模式
- 默认模式
- 使用 `toolchain.cmake`
- 目标平台为 Ingenic T32 MIPS

典型命令：
```bash
mkdir -p build && cd build
cmake -DCMAKE_TOOLCHAIN_FILE=../toolchain.cmake ..
make -j$(nproc)
```

### 2.2 PC 仿真模式
- 通过 `BUILD_FOR_SIMULATION=ON` 启用
- 使用 `toolchain_sim.cmake`
- 会额外编译 `tests/`

典型命令：
```bash
mkdir -p build_sim && cd build_sim
cmake -DBUILD_FOR_SIMULATION=ON ..
make -j$(nproc)
```

## 3. 主要代码结构

### 3.1 顶层模块
- `src/app/`：主程序入口与应用装配
- `src/media/`：音视频、抓拍、RTSP 能力
- `src/service/`：HTTP、discovery、event、daemon、camera service
- `src/network/`：网络客户端与传输相关实现
- `src/storage/`：数据库与媒体扫描
- `src/hardware/`：GPIO、MCU、电源、磁盘、昼夜切换
- `src/hal/`：硬件抽象层 (Hardware Abstraction Layer, HAL)
- `src/config/`：环境、设备配置
- `src/common/`：时间、工具、公共基础设施
- `tests/`：仿真模式下的测试程序
- `script/`：测试、安装、验证脚本
- `doc/`：历史文档沉淀区
- `doc/knowledge/`：项目知识的权威入口

### 3.2 可执行目标
从 `src/app/CMakeLists.txt` 可见当前主要目标包括：
- `htc_main_app`
- `htc_media_app`
- `htc_daemon_app`
- `snap_test`

## 4. 当前知识治理判断

该仓库此前已有大量 `doc/` 历史文档，但缺少符合当前规范的统一知识入口：
- 原先没有 `doc/knowledge/overview.md`
- 原先没有 `doc/knowledge/working-set.md`
- 原先没有仓库级知识 guide (`doc/knowledge/README.md`)

这会导致后续代理或协作者只能在 `doc/` 中横向搜索，缺少稳定入口。该问题已经在本轮初始化中补齐基础骨架。

## 5. 当前阶段判断

从现有目录与文档分布看，项目处于“持续演进中的存量仓库治理阶段”，而不是空白新项目：
- 核心代码与构建系统已经存在
- PC 仿真、RTSP、HTTP API、mDNS、存储等方向都已有实现与文档
- 文档历史较多，但分类风格不统一，存在旧 `doc/` 体系与新规范并存的问题

因此后续工作的正确做法不是重写所有文档，而是：
1. 先以 `doc/knowledge/` 建立权威入口
2. 再逐步把高价值、仍有效的内容收敛到对应子目录
3. 对历史文档通过 `refs/` 与 `reviews/` 做映射和治理说明

## 6. 约束与注意事项

- `src/hal/**` 按仓库 AGENTS 约定属于 PIC ownership，正常任务下不能直接修改
- 未经明确许可，不执行 Git 提交、回滚、推送等操作
- 项目是双构建形态：真机与仿真都需要考虑
- 对于项目级事实，应以本仓库 `doc/knowledge/` 为准，而不是外部知识库摘要
