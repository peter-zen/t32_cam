# 非Media目录结构重构计划

## 一、总体目标

根据 `doc/review/directory_structure_review.md` 中的推荐方案，对media部分以外的目录进行层次化重组，将分散的目录按照功能层次归类，使目录结构更清晰、更易维护。

## 二、重构原则

- 不修改media/目录结构
- 渐进式重构，分阶段执行
- 每个步骤都可验证、可回滚
- 仅调整目录结构，不修改代码逻辑

## 三、最终目标结构

```
src/
├── app/                    # 应用层
│   └── workmode/          # 工作模式 (从顶层移入)
├── service/                # 服务层
│   ├── http_server/       # HTTP Server
│   └── daemon/            # 守护进程 (从顶层移入)
├── hardware/               # 硬件层
│   ├── mcu/
│   ├── gpio/
│   ├── daynight/
│   ├── power/
│   └── disk/              # 磁盘管理 (从顶层移入)
├── common/                 # 通用工具层
│   ├── utils/             # 通用工具
│   │   ├── base64/        # Base64 (从顶层移入)
│   │   ├── jpeg/          # JPEG处理 (从common移入)
│   │   ├── crc/           # CRC校验 (从common移入)
│   │   ├── serial/        # 串口 (从common移入)
│   │   └── string/        # 字符串转换 (从common移入)
│   ├── time/              # 时间处理
│   │   ├── rtc/           # RTC (从time/移入)
│   │   └── timezone/      # 时区 (从顶层移入)
│   └── misc/              # 其他工具 (从顶层移入)
├── platform/               # 平台相关
│   ├── sdk_stub/          # SDK桩 (从顶层移入)
│   └── tool/              # 平台工具 (从顶层移入)
├── media/                  # 媒体处理层 (保持不变)
├── hal/                    # 硬件抽象层 (保持不变)
├── network/                # 网络层 (保持不变)
├── config/                 # 配置层 (保持不变)
└── logger/                 # 日志系统 (保持不变)
```

**优化后顶层目录数量：从 24 个减少到 9 个（media保持7个子目录）**

---

## 四、详细执行计划

### Phase 1: 创建新的目录结构 (准备阶段)

#### 1.1 调整内容
创建以下新目录：
- `src/common/utils/`
- `src/common/utils/base64/`
- `src/common/utils/jpeg/`
- `src/common/utils/crc/`
- `src/common/utils/serial/`
- `src/common/utils/string/`
- `src/common/time/`
- `src/common/time/rtc/`
- `src/common/time/timezone/`
- `src/app/workmode/`
- `src/service/daemon/`
- `src/hardware/disk/`
- `src/platform/`
- `src/platform/sdk_stub/`
- `src/platform/tool/`

#### 1.2 执行命令
```bash
mkdir -p src/common/utils/{base64,jpeg,crc,serial,string}
mkdir -p src/common/time/{rtc,timezone}
mkdir -p src/app/workmode
mkdir -p src/service/daemon
mkdir -p src/hardware/disk
mkdir -p src/platform/{sdk_stub,tool}
```

#### 1.3 验证
```bash
tree -d -L 3 src/
```

---

### Phase 2: 合并时间相关模块 (低风险)

#### 2.1 调整内容
移动时间相关模块：
- `src/time/RTC.*` → `src/common/time/rtc/`
- `src/time/CMakeLists.txt` → `src/common/time/rtc/CMakeLists.txt`
- `src/timezone/Timezone.*` → `src/common/time/timezone/`
- `src/timezone/CMakeLists.txt` → `src/common/time/timezone/CMakeLists.txt`

#### 2.2 引用检查
```bash
grep -rn "#include.*RTC.h\|Timezone.h" src/ --include="*.cpp" --include="*.h"
```

预期引用位置：
- `src/app/main_app.cpp`
- `src/app/media_app.cpp`
- `src/hardware/mcu/MCU.cpp`
- `src/network/MgmtServClient.cpp`
- `src/network/RemoteCtrlClient.cpp`
- `src/network/StorageServClient.cpp`

#### 2.3 需要修改的文件
所有包含以下include的文件：
- `#include "RTC.h"` → `#include "time/rtc/RTC.h"`
- `#include "Timezone.h"` → `#include "time/timezone/Timezone.h"`

#### 2.4 需要修改的CMakeLists.txt
1. `src/common/CMakeLists.txt`
   - 添加子目录：`add_subdirectory(time)`

2. 创建 `src/common/time/CMakeLists.txt`
   ```
   add_subdirectory(rtc)
   add_subdirectory(timezone)
   ```

3. `src/common/time/rtc/CMakeLists.txt` (从time移动)
   - 修改库目标名：`time` → `common_time_rtc`

4. `src/common/time/timezone/CMakeLists.txt` (从timezone移动)
   - 修改库目标名：`timezone` → `common_time_timezone`

5. `src/CMakeLists.txt`
   - 移除：`add_subdirectory(time)`
   - 移除：`add_subdirectory(timezone)`

#### 2.5 影响的app编译
- 所有使用了时间相关模块的app都需要重新链接

#### 2.6 验证步骤
```bash
cd build_sim
cmake ..
make -j$(nproc)
```

---

### Phase 3: 合并工具类模块到 common/utils/ (低风险)

#### 3.1 调整内容
移动工具类模块：
- `src/base64/Base64.*` → `src/common/utils/base64/`
- `src/base64/CMakeLists.txt` → `src/common/utils/base64/CMakeLists.txt`
- `src/common/Jpeg.*` → `src/common/utils/jpeg/`
- `src/common/CRC.*` → `src/common/utils/crc/`
- `src/common/SerialPort.*` → `src/common/utils/serial/`
- `src/common/StringConvert.h` → `src/common/utils/string/StringConvert.h`
- `src/common/AutoRelease.h` → `src/common/utils/AutoRelease.h`

#### 3.2 引用检查
```bash
grep -rn "#include.*Base64\|Jpeg\|CRC\|SerialPort\|StringConvert\|AutoRelease" src/ --include="*.cpp" --include="*.h"
```

预期引用位置：
- `src/network/MgmtServClient.cpp` - Base64.h

#### 3.3 需要修改的文件
所有包含以下include的文件：
- `#include "Base64.h"` → `#include "utils/base64/Base64.h"`
- `#include "Jpeg.h"` → `#include "utils/jpeg/Jpeg.h"`
- `#include "CRC.h"` → `#include "utils/crc/CRC.h"`
- `#include "SerialPort.h"` → `#include "utils/serial/SerialPort.h"`
- `#include "StringConvert.h"` → `#include "utils/string/StringConvert.h"`
- `#include "AutoRelease.h"` → `#include "utils/AutoRelease.h"`

#### 3.4 需要修改的CMakeLists.txt
1. `src/common/CMakeLists.txt`
   - 添加子目录：`add_subdirectory(utils)`
   - 修改：将原来的Jpeg, CRC等文件引用改为从utils目录链接

2. 创建 `src/common/utils/CMakeLists.txt`
   ```
   add_subdirectory(base64)
   add_subdirectory(jpeg)
   add_subdirectory(crc)
   add_subdirectory(serial)
   add_subdirectory(string)
   ```

3. 创建各子目录的CMakeLists.txt：
   - `src/common/utils/base64/CMakeLists.txt`
   - `src/common/utils/jpeg/CMakeLists.txt`
   - `src/common/utils/crc/CMakeLists.txt`
   - `src/common/utils/serial/CMakeLists.txt`
   - `src/common/utils/string/CMakeLists.txt`

4. `src/CMakeLists.txt`
   - 移除：`add_subdirectory(base64)`

#### 3.5 影响的app编译
- 所有使用了工具类模块的app都需要重新链接

#### 3.6 验证步骤
```bash
cd build_sim
cmake ..
make -j$(nproc)
```

---

### Phase 4: 调整应用层结构 (中风险)

#### 4.1 调整内容
移动工作模式模块：
- `src/workmode/WorkMode.*` → `src/app/workmode/`
- `src/workmode/CMakeLists.txt` → `src/app/workmode/CMakeLists.txt`

#### 4.2 引用检查
```bash
grep -rn "#include.*WorkMode" src/ --include="*.cpp" --include="*.h"
```

预期引用位置：
- `src/app/workmode/WorkMode.cpp`
- `src/app/media_app.cpp`
- `src/app/main_app.cpp`

#### 4.3 需要修改的文件
所有包含以下include的文件：
- `#include "WorkMode.h"` → `#include "workmode/WorkMode.h"` (在app目录内的文件可以改为相对路径)

具体修改：
- `src/app/media_app.cpp`: `#include "workmode/WorkMode.h"`
- `src/app/main_app.cpp`: `#include "workmode/WorkMode.h"`
- `src/app/workmode/WorkMode.cpp`: `#include "WorkMode.h"` (保持不变，同目录)

#### 4.4 需要修改的CMakeLists.txt
1. `src/app/CMakeLists.txt`
   - 添加子目录：`add_subdirectory(workmode)`
   - 修改：将workmode库的引用改为从workmode子目录链接

2. `src/app/workmode/CMakeLists.txt`
   - 保持内容基本不变，只需调整库目标名和依赖

3. `src/CMakeLists.txt`
   - 移除：`add_subdirectory(workmode)`

#### 4.5 影响的app编译
- `main_app`, `media_app` 等应用需要重新编译

#### 4.6 验证步骤
```bash
cd build_sim
cmake ..
make -j$(nproc)
./bin/htc_main_app -v  # 验证基本功能
```

---

### Phase 5: 调整服务层结构 (中风险)

#### 5.1 调整内容
移动守护进程模块：
- `src/daemon/*.*` → `src/service/daemon/`
- `src/daemon/CMakeLists.txt` → `src/service/daemon/CMakeLists.txt`
- `src/daemon/README.md` → `src/service/daemon/README.md`

#### 5.2 引用检查
```bash
grep -rn "#include.*Deamon\|Deamon" src/ --include="*.cpp" --include="*.h"
```

预期引用位置：
- `src/daemon/daemon_api.cpp`
- `src/daemon/DeamonServer.cpp`
- `src/daemon/DeamonClient.cpp`
- `src/daemon/example.cpp`

#### 5.3 需要修改的文件
移动后不需要修改include路径（因为所有文件都在同一个目录下移动）

#### 5.4 需要修改的CMakeLists.txt
1. `src/service/CMakeLists.txt`
   - 添加子目录：`add_subdirectory(daemon)`

2. `src/service/daemon/CMakeLists.txt`
   - 保持内容基本不变

3. `src/CMakeLists.txt`
   - 移除：`add_subdirectory(daemon)`

#### 5.5 影响的app编译
- 使用守护进程的app需要重新编译

#### 5.6 验证步骤
```bash
cd build_sim
cmake ..
make -j$(nproc)
```

---

### Phase 6: 调整硬件层结构 (中风险)

#### 6.1 调整内容
移动磁盘管理模块：
- `src/disk/Disk.*` → `src/hardware/disk/`
- `src/disk/CMakeLists.txt` → `src/hardware/disk/CMakeLists.txt`

#### 6.2 引用检查
```bash
grep -rn "#include.*Disk" src/ --include="*.cpp" --include="*.h"
```

预期引用位置：
- `src/app/main_app.cpp`
- `src/network/MgmtServClient.cpp`
- `src/network/RemoteCtrlClient.cpp`
- `src/hardware/disk/Disk.cpp`

#### 6.3 需要修改的文件
所有包含以下include的文件：
- `#include "Disk.h"` → `#include "disk/Disk.h"`

具体修改：
- `src/app/main_app.cpp`: `#include "disk/Disk.h"`
- `src/network/MgmtServClient.cpp`: `#include "disk/Disk.h"`
- `src/network/RemoteCtrlClient.cpp`: `#include "disk/Disk.h"`

#### 6.4 需要修改的CMakeLists.txt
1. `src/hardware/CMakeLists.txt`
   - 添加子目录：`add_subdirectory(disk)`

2. `src/hardware/disk/CMakeLists.txt`
   - 保持内容基本不变

3. `src/CMakeLists.txt`
   - 移除：`add_subdirectory(disk)`

#### 6.5 影响的app编译
- 所有使用了磁盘功能的app需要重新编译

#### 6.6 验证步骤
```bash
cd build_sim
cmake ..
make -j$(nproc)
```

---

### Phase 7: 调整平台层结构 (低风险)

#### 7.1 调整内容
移动平台相关模块：
- `src/sdk_stub/*.*` → `src/platform/sdk_stub/`
- `src/sdk_stub/CMakeLists.txt` → `src/platform/sdk_stub/CMakeLists.txt`
- `src/tool/wpa_conn.cpp` → `src/platform/tool/wpa_conn.cpp`
- 创建 `src/tool/CMakeLists.txt` → `src/platform/tool/CMakeLists.txt`

#### 7.2 引用检查
```bash
grep -rn "#include.*wpa_conn" src/ --include="*.cpp" --include="*.h"
```

预期引用位置：
- 可能无引用或引用较少

#### 7.3 需要修改的文件
如果有引用，需要修改include路径

#### 7.4 需要修改的CMakeLists.txt
1. 创建 `src/platform/CMakeLists.txt`
   ```
   add_subdirectory(sdk_stub)
   add_subdirectory(tool)
   ```

2. `src/platform/sdk_stub/CMakeLists.txt`
   - 保持内容基本不变

3. `src/platform/tool/CMakeLists.txt`
   - 从tool/CMakeLists.txt移动过来

4. `src/CMakeLists.txt`
   - 移除：`add_subdirectory(sdk_stub)`
   - 移除：`add_subdirectory(tool)`
   - 添加：`add_subdirectory(platform)`

#### 7.5 影响的app编译
- SDK stub和工具影响较小

#### 7.6 验证步骤
```bash
cd build_sim
cmake ..
make -j$(nproc)
```

---

### Phase 8: 移动 misc 到 common/ (低风险)

#### 8.1 调整内容
移动杂项模块：
- `src/misc/Misc.*` → `src/common/misc/`
- `src/misc/CMakeLists.txt` → `src/common/misc/CMakeLists.txt`

#### 8.2 引用检查
```bash
grep -rn "#include.*Misc" src/ --include="*.cpp" --include="*.h"
```

预期引用位置：
- 可能较少

#### 8.3 需要修改的文件
所有包含以下include的文件：
- `#include "Misc.h"` → `#include "misc/Misc.h"`

#### 8.4 需要修改的CMakeLists.txt
1. `src/common/CMakeLists.txt`
   - 添加子目录：`add_subdirectory(misc)`

2. `src/common/misc/CMakeLists.txt`
   - 保持内容基本不变

3. `src/CMakeLists.txt`
   - 移除：`add_subdirectory(misc)`

#### 8.5 影响的app编译
- 影响较小

#### 8.6 验证步骤
```bash
cd build_sim
cmake ..
make -j$(nproc)
```

---

### Phase 9: 清理和验证

#### 9.1 清理空目录
```bash
# 删除已移动的空目录
rmdir src/time src/timezone src/base64 src/workmode src/daemon src/disk src/sdk_stub src/tool src/misc
```

#### 9.2 更新 include 路径配置
如果使用了统一的include路径配置，需要更新：
- 修改CMakeLists.txt中的include_directories
- 确保所有头文件都能被正确找到

#### 9.3 全量编译测试
```bash
cd build_sim
rm -rf *
cmake -DBUILD_FOR_SIMULATION=ON ..
make -j$(nproc)
```

#### 9.4 功能验证
```bash
# 运行主应用
./bin/htc_main_app -v

# 运行daemon测试（如果有）
./bin/daemon_example

# 运行RTSP测试
./bin/test_rtsp_live
```

---

## 五、时间估算

| Phase | 预计时间 | 风险等级 | 说明 |
|-------|----------|----------|------|
| Phase 1: 创建目录 | 15分钟 | 低 | 准备阶段 |
| Phase 2: 合并时间模块 | 45分钟 | 低 | 6个引用点 |
| Phase 3: 合并工具类 | 60分钟 | 低 | 多个模块 |
| Phase 4: 调整应用层 | 30分钟 | 中 | 2个引用点 |
| Phase 5: 调整服务层 | 30分钟 | 中 | 守护进程 |
| Phase 6: 调整硬件层 | 30分钟 | 中 | 3个引用点 |
| Phase 7: 调整平台层 | 30分钟 | 低 | SDK stub |
| Phase 8: 移动misc | 20分钟 | 低 | 杂项 |
| Phase 9: 清理和验证 | 45分钟 | 中 | 全量测试 |
| **总计** | **约5小时** | - | 不包括问题排查 |

---

## 六、风险控制

### 6.1 每个Phase完成后
- 编译验证：必须编译通过
- 运行验证：运行相关测试程序
- 记录验证结果

### 6.2 回滚机制
- 每个Phase都是独立的，可以单独回滚
- 使用git记录每个Phase的变更
- 如果某Phase失败，回滚到上一Phase

### 6.3 问题处理
- 如果某Phase失败，立即停止
- 分析失败原因（编译错误、链接错误、运行时错误）
- 修复后重新验证
- 确保不会影响已完成的Phase

---

## 七、执行顺序

```
Phase 1 → Phase 2 → Phase 3 → Phase 4 → Phase 5 → Phase 6 → Phase 7 → Phase 8 → Phase 9
```

每个Phase完成后：
1. 编译验证
2. 向用户汇报
3. 用户确认后进行下一Phase

---

## 八、依赖关系

| Phase | 依赖 |
|-------|------|
| Phase 1 | 无 |
| Phase 2 | Phase 1 |
| Phase 3 | Phase 2 |
| Phase 4 | 无（独立） |
| Phase 5 | 无（独立） |
| Phase 6 | 无（独立） |
| Phase 7 | 无（独立） |
| Phase 8 | Phase 3 |
| Phase 9 | 所有之前Phase |

---

## 九、关键文件映射

### Include路径映射表

| 原include | 新include |
|-----------|-----------|
| `#include "RTC.h"` | `#include "time/rtc/RTC.h"` |
| `#include "Timezone.h"` | `#include "time/timezone/Timezone.h"` |
| `#include "Base64.h"` | `#include "utils/base64/Base64.h"` |
| `#include "Disk.h"` | `#include "disk/Disk.h"` |
| `#include "WorkMode.h"` | `#include "workmode/WorkMode.h"` (app内) |
| `#include "Misc.h"` | `#include "misc/Misc.h"` |

### 库名称映射表（如果需要修改）

| 原库名 | 新库名 |
|--------|--------|
| `time` | `common_time_rtc` |
| `timezone` | `common_time_timezone` |
| `base64` | `common_utils_base64` |
| `disk` | `hardware_disk` |
| `workmode` | `app_workmode` |
| `misc` | `common_misc` |
| `sdk_stub` | `platform_sdk_stub` |
| `tool` | `platform_tool` |

---

## 十、备注

- 所有修改不涉及media/目录
- 需要确保CMakeLists.txt的include路径正确
- 建议在每个Phase后提交git（用户批准后）
- 遵循AGENTS.md中的代码规范
- 最终顶层目录从24个减少到9个
