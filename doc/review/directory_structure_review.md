# src 目录结构审查与优化方案

## 1. 当前目录结构分析

### 1.1 现状概览

当前 `src/` 目录下有 **24 个顶层目录**，数量较多，结构比较分散。

```
src/
├── app/                    # 应用层
├── base64/                 # Base64编码
├── common/                 # 通用工具
├── config/                 # 配置管理 (3个子目录)
├── daemon/                 # 守护进程
├── disk/                   # 磁盘管理
├── hal/                    # 硬件抽象层 (已部分实现)
│   ├── interface/
│   ├── ingenic/
│   └── sim/
├── hardware/               # 硬件相关 (4个子目录)
│   ├── daynight/
│   ├── gpio/
│   ├── mcu/
│   └── power/
├── logger/                 # 日志系统
├── media/                  # 媒体处理 (7个子目录)
│   ├── audio/
│   ├── base/
│   ├── common/
│   ├── fifo/
│   ├── rtsp/
│   ├── snap/
│   └── video/
├── misc/                   # 杂项
├── network/                # 网络通信 (10个文件)
├── sdk_stub/               # SDK桩代码
├── service/                # 服务层
│   └── http_server/
├── time/                   # RTC时间
├── timezone/               # 时区处理
├── tool/                   # 工具
└── workmode/               # 工作模式
```

### 1.2 与原计划对比

| 维度 | 原计划 (refactor_t32_proposal.md) | 当前实现 | 状态 |
|------|----------------------------------|---------|------|
| hal/ | ✅ 已规划 | ✅ 已实现 | 符合 |
| hardware/ | ✅ 已规划 | ✅ 已实现 | 符合 |
| service/ | ✅ 已规划 | ✅ 部分实现 | 符合 |
| config/ | ✅ 已规划 (重组) | ✅ 已实现 | 符合 |
| network/ | ✅ 已规划 (移除TCP Client) | ✅ 存在 | 待优化 |
| base64/ | ❌ 未规划 | ✅ 存在 | 需归类 |
| daemon/ | ❌ 未规划 | ✅ 存在 | 需归类 |
| disk/ | ❌ 未规划 | ✅ 存在 | 需归类 |
| logger/ | ❌ 未规划 | ✅ 存在 | 需归类 |
| misc/ | ❌ 未规划 | ✅ 存在 | 需归类 |
| sdk_stub/ | ❌ 未规划 | ✅ 存在 | 需归类 |
| time/ | ❌ 未规划 | ✅ 存在 | 需归类 |
| timezone/ | ❌ 未规划 | ✅ 存在 | 需归类 |
| tool/ | ❌ 未规划 | ✅ 存在 | 需归类 |
| workmode/ | ❌ 未规划 | ✅ 存在 | 需归类 |

---

## 2. 问题识别

### 2.1 主要问题

1. **顶层目录过多**
   - 24个目录，过于分散，导航困难
   - 许多目录功能单一，可以合并

2. **功能分类不清晰**
   - `base64/`, `common/` 都是工具类，但分离
   - `time/`, `timezone/` 都是时间相关，但分离
   - `disk/`, `misc/` 没有明确的归类

3. **重复或冗余**
   - `misc/` 目录名称不明确，应该拆分到具体模块
   - `sdk_stub/` 可以归类到 hal 层或 platform 层

4. **层次不够清晰**
   - `logger/` 作为基础设施，应该更突出
   - `daemon/` 作为应用层的一部分，应该在 app/ 下

---

## 3. 推荐方案：按层次重新组织

### 3.1 新的目录结构

```
src/
├── app/                            # 应用层
│   └── workmode/                   # 工作模式 (从顶层移入)
├── service/                        # 服务层
│   ├── http_server/                # HTTP Server
│   └── daemon/                     # 守护进程 (从顶层移入)
├── media/                          # 媒体处理层
│   ├── audio/                      # 音频源
│   ├── video/                      # 视频源
│   ├── rtsp/                       # RTSP 协议
│   ├── snap/                       # 抓拍
│   ├── fifo/                       # 媒体FIFO
│   └── base/                       # 媒体基础接口
├── hardware/                       # 硬件层
│   ├── mcu/                        # MCU通信
│   ├── gpio/                       # GPIO控制
│   ├── daynight/                   # 日夜切换
│   ├── power/                      # 电源管理
│   └── disk/                       # 磁盘管理 (从顶层移入)
├── hal/                            # 硬件抽象层
│   ├── interface/
│   ├── ingenic/
│   └── sim/
├── network/                        # 网络层
├── common/                         # 通用工具层
│   ├── utils/                      # 通用工具
│   │   ├── base64/                 # Base64 (从顶层移入)
│   │   ├── jpeg/                   # JPEG处理
│   │   ├── crc/                    # CRC校验
│   │   ├── serial/                 # 串口
│   │   └── string/                 # 字符串转换
│   ├── time/                       # 时间处理
│   │   ├── rtc/                    # RTC (从time/移入)
│   │   └── timezone/               # 时区 (从顶层移入)
│   └── misc/                       # 其他工具
├── config/                         # 配置层
│   ├── setting/
│   ├── devconf/
│   └── env/
├── platform/                       # 平台相关
│   ├── sdk_stub/                   # SDK桩 (从顶层移入)
│   └── tool/                       # 平台工具 (从顶层移入)
└── logger/                         # 日志系统 (基础设施)
```

**优化后目录数量：从 24 个减少到 9 个顶层目录**

### 3.2 变化说明

| 原目录 | 新位置 | 理由 |
|--------|--------|------|
| workmode/ | app/workmode/ | 工作模式是应用层逻辑 |
| daemon/ | service/daemon/ | 守护进程是一种服务 |
| disk/ | hardware/disk/ | 磁盘是硬件资源 |
| base64/ | common/utils/base64/ | 是通用工具 |
| time/ | common/time/rtc/ | 时间处理的子模块 |
| timezone/ | common/time/timezone/ | 时间处理的子模块 |
| misc/ | common/misc/ | 通用工具的一部分 |
| sdk_stub/ | platform/sdk_stub/ | 平台特定的代码 |
| tool/ | platform/tool/ | 平台工具 |

#### 3.1.3 优化后目录数量

从 24 个顶层目录减少到 **9 个顶层目录**，结构更清晰。

---

## 4. 迁移计划

### 4.1 阶段性迁移

为降低风险，建议分阶段迁移：

```
Phase 1: 合并时间相关模块 (低风险)
    ├── time/ + timezone/ → common/time/
    └── 更新 include 路径

Phase 2: 合并工具类模块 (低风险)
    ├── base64/ → common/utils/base64/
    ├── common/ 下的工具 → common/utils/
    └── 更新 include 路径

Phase 3: 调整应用层结构 (中风险)
    ├── workmode/ → app/workmode/
    └── daemon/ → service/daemon/

Phase 4: 调整硬件层结构 (中风险)
    ├── disk/ → hardware/disk/
    └── 更新 include 路径

Phase 5: 调整媒体层结构 (中风险)
    ├── media/rtsp/ → service/rtsp_server/
    └── 更新 include 路径

Phase 6: 调整平台层结构 (低风险)
    ├── sdk_stub/ → platform/sdk_stub/
    └── tool/ → platform/tool/

Phase 7: 清理和验证 (中风险)
    ├── 删除空目录
    ├── 更新 CMakeLists.txt
    └── 编译测试
```

### 4.2 迁移注意事项

1. **保持编译通过**
   - 每次迁移后立即编译测试
   - 更新 CMakeLists.txt 中的路径

2. **更新 include 路径**
   - 批量替换头文件引用
   - 使用相对路径或统一的 include 路径

3. **代码审查**
   - 每个阶段完成后进行代码审查
   - 确认没有遗漏的文件

---

## 5. 替代方案

### 5.1 方案A：保守调整

保持当前大部分结构，仅合并明显相关的目录：

```
src/
├── app/
├── media/
├── hardware/
├── hal/
├── network/
├── service/
├── config/
├── common/              # 合并 base64/, time/, timezone/, misc/
├── platform/            # 合并 sdk_stub/, tool/
└── logger/
```

**优点**：改动最小，风险最低
**缺点**：目录仍然较多，层次不够清晰

### 5.2 方案B：激进重构

完全按照层次重新组织，参考原计划文档：

```
src/
├── app/
├── service/
├── media/
├── hardware/
├── hal/
├── common/
├── config/
└── logger/              # 基础设施层
```

将所有其他模块归类到上述目录下。

**优点**：结构最清晰，层次分明
**缺点**：改动最大，风险最高

---

## 6. 推荐方案与理由

### 6.1 推荐：推荐方案（按层次重新组织）

**理由：**

1. **平衡风险和收益**
   - 分阶段迁移，风险可控
   - 最终结构清晰，易于维护

2. **符合软件工程最佳实践**
   - 按层次组织代码
   - 单一职责原则
   - 高内聚低耦合

3. **保留现有成果**
   - 不改变代码逻辑，仅调整目录
   - 保护已有的开发成果

4. **便于扩展**
   - 新增模块有明确的归属
   - 减少决策成本

---

## 7. 关键决策点

需要讨论的决策：

1. **`daemon/` 是否移到 `service/daemon/`？**
   - 选项A：保留顶层，因为与 app 平级
   - 选项B：移到 `service/`，因为它是守护服务

2. **`network/` 如何处理？**
   - 选项A：保留顶层
   - 选项B：拆分为 `network/client/` 和 `network/broadcast/`
   - 选项C：逐步移除 TCP Client，只保留必要组件

3. **`media/rtsp/` 是否移到 `service/rtsp_server/`？**
   - 选项A：保留在 `media/rtsp/`，因为与媒体紧密相关
   - 选项B：移到 `service/rtsp_server/`，因为它是服务

4. **是否需要将 `common/` 内部进一步拆分为子目录？**
   - 选项A：保持扁平结构
   - 选项B：按功能拆分 utils/, time/, misc/

---

## 8. 下一步行动

1. **团队讨论**
   - 确认是否采用推荐方案
   - 确认关键决策点

2. **制定详细迁移计划**
   - 每个阶段的具体任务
   - 预计工作量
   - 风险评估

3. **启动 Phase 1**
   - 开始合并时间相关模块
   - 验证编译和功能

---

## 附录：各目录详细分析

### A.1 app/
- **功能**: 应用层入口
- **文件**: main_app.cpp, daemon_app.cpp, media_app.cpp, app.h
- **建议**: 保持

### A.2 base64/
- **功能**: Base64编码解码
- **文件**: Base64.cpp, Base64.h
- **建议**: 移到 common/utils/base64/

### A.3 common/
- **功能**: 通用工具
- **文件**: Common.h, AutoRelease.h, CRC.*, Jpeg.*, SerialPort.*, StringConvert.h
- **建议**: 拆分为 common/utils/ 下的子目录

### A.4 config/
- **功能**: 配置管理
- **子目录**: devconf/, env/, setting/
- **建议**: 保持

### A.5 daemon/
- **功能**: 守护进程
- **文件**: DeamonServer.*, DeamonClient.*, daemon_api.*
- **建议**: 移到 service/daemon/

### A.6 disk/
- **功能**: 磁盘管理
- **文件**: Disk.cpp, Disk.h
- **建议**: 移到 hardware/disk/

### A.7 hal/
- **功能**: 硬件抽象层
- **子目录**: interface/, ingenic/, sim/
- **建议**: 保持

### A.8 hardware/
- **功能**: 硬件相关
- **子目录**: daynight/, gpio/, mcu/, power/
- **建议**: 保持

### A.9 logger/
- **功能**: 日志系统
- **文件**: Logger.*, ElogInit.*
- **建议**: 保持（基础设施）

### A.10 media/
- **功能**: 媒体处理
- **子目录**: audio/, base/, common/, fifo/, rtsp/, snap/, video/
- **建议**:
  - audio/, video/, snap/, fifo/, base/ 保持
  - common/ 下的 sample-common.* 可以合并或重命名
  - rtsp/ 考虑是否移到 service/rtsp_server/

### A.11 misc/
- **功能**: 杂项
- **文件**: Misc.cpp, Misc.h
- **建议**: 移到 common/misc/，如果内容明确则拆分到具体模块

### A.12 network/
- **功能**: 网络通信
- **文件**: 10个文件（Client.h, MgmtServClient.*, Rtmp.*, etc.）
- **建议**:
  - 保留网络通信基础功能
  - 逐步移除不需要的 TCP Client

### A.13 sdk_stub/
- **功能**: SDK桩代码
- **文件**: imp_stub.c, alog_stub.c, system_call_stub.c
- **建议**: 移到 platform/sdk_stub/

### A.14 service/
- **功能**: 服务层
- **子目录**: http_server/
- **建议**: 保持，考虑添加 rtsp_server/

### A.15 time/
- **功能**: RTC时间
- **文件**: RTC.cpp, RTC.h
- **建议**: 移到 common/time/rtc/

### A.16 timezone/
- **功能**: 时区处理
- **文件**: Timezone.cpp, Timezone.h
- **建议**: 移到 common/time/timezone/

### A.17 tool/
- **功能**: 工具
- **文件**: wpa_conn.cpp
- **建议**: 移到 platform/tool/

### A.18 workmode/
- **功能**: 工作模式
- **文件**: WorkMode.cpp, WorkMode.h
- **建议**: 移到 app/workmode/
