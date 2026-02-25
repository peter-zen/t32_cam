# 模拟环境目录结构设计 (Simulation Environment Directory Structure)

## 1. 背景与问题
当前项目根目录下的 `sim_sdcard` 目录职责不清：
1.  **混淆了输入与输出**：既包含模拟硬件所需的源文件（如 `test.h264`），又包含了运行时产生的日志 (`log/`)。
2.  **命名不准确**：`sim_sdcard` 暗示它是模拟的 SD 卡（通常是读写的），但其中的视频源文件实际上是只读的测试素材。
3.  **路径不规范**：模拟环境下的文件操作路径与真机 (`/sdcard/...`) 差异较大，增加了代码中的条件编译负担。

## 2. 设计目标
1.  **分离“测试资源”与“运行时数据”**：
    *   测试资源 (Assets)：只读，Git 版本控制管理。
    *   运行时数据 (Runtime Data)：读写，Git 忽略，每次运行可能变化。
2.  **统一模拟路径**：在 `build_sim` 目录下构建一个模拟真机文件系统结构的 `sdcard` 目录，使模拟环境下的路径逻辑尽可能接近真机。

## 3. 目录结构方案

### 3.1 项目源码结构 (Source Tree)

新增 `tests/assets` 目录，用于存放所有模拟硬件所需的只读素材。

```
project_root/
├── tests/
│   └── assets/                  <-- [新建] 替代原 sim_sdcard 的"输入"功能
│       ├── video/
│       │   ├── test.h264        <-- 模拟视频流源文件
│       │   └── test.mp4
│       ├── audio/
│       │   └── test.pcm
│       ├── image/
│       │   └── test.jpg
│       └── configs/             <-- 默认配置文件模板
│           └── rtsp_config.ini
├── res/                         <-- 现有的资源目录 (保持不变，存放出厂默认配置)
│   ├── config.ini
│   └── setting.json
└── ...
```

### 3.2 编译输出结构 (Build Tree)

在 `build_sim` 目录下创建 `sdcard` 目录，完全模拟真机的 `/sdcard` 挂载点。

```
project_root/
├── build_sim/
│   ├── bin/
│   │   └── t32_sim_app          <-- 可执行程序
│   └── sdcard/                  <-- [新建] 模拟真机的 /sdcard 分区
│       ├── log/                 <-- 存放运行时日志 (e.g., app.log)
│       ├── data/
│       │   └── db/              <-- 存放 SQLite 数据库 (media_file.db)
│       ├── media/               <-- 存放生成的媒体文件
│       │   ├── video/
│       │   ├── photo/
│       │   └── thumb/
│       └── configs/             <-- 运行时配置文件 (从 res 复制)
```

## 4. 路径映射规则

| 资源类型 | 真机路径 (Target) | 模拟环境代码路径 (Sim) | 备注 |
| :--- | :--- | :--- | :--- |
| **日志** | `/sdcard/log/` | `./sdcard/log/` | 相对路径，基于 CWD (build_sim) |
| **数据库** | `/sdcard/data/db/` | `./sdcard/data/db/` | 同上 |
| **照片/录像** | `/sdcard/media/...` | `./sdcard/media/...` | 同上 |
| **模拟视频源** | N/A (Sensor产生) | `../../tests/assets/video/test.h264` | HAL 层模拟代码读取 |
| **配置文件** | `/sdcard/configs/` | `./sdcard/configs/` | 启动脚本负责初始化 |

## 5. 实施步骤

### Step 1: 整理测试资源
1.  创建 `tests/assets` 目录结构。
2.  将 `sim_sdcard/video/test.h264` 等文件移动到 `tests/assets/video/`。
3.  删除项目根目录下的 `sim_sdcard`。

### Step 2: 更新模拟层代码
1.  修改 `src/hal/sim/hal_framesource_sim.c`：
    *   将默认读取路径从 `sim_sdcard/video/test.h264` 改为 `../../tests/assets/video/test.h264` (假设从 `build_sim` 运行) 或者通过环境变量/配置文件配置绝对路径。

### Step 3: 更新构建与启动脚本
1.  修改 `CMakeLists.txt` 或测试脚本 (如 `test_av_full.sh`)：
    *   在编译或运行前，确保 `build_sim/sdcard` 及其子目录 (`log`, `data/db`, `media`) 已创建。
    *   如果需要，将默认配置文件从 `res/` 复制到 `build_sim/sdcard/configs/`。

### Step 4: 更新配置文件
1.  修改 `res/env.ini` (如果是模拟环境专用) 或相关配置代码，将日志和文件存储路径指向 `./sdcard/...`。

## 6. 预期效果
*   **清晰的职责分离**：源码目录不再被运行时产生的垃圾文件污染。
*   **更真实的模拟**：`build_sim/sdcard` 的结构与真机一致，方便调试文件管理相关功能。
*   **Git 友好**：`tests/assets` 纳入版本控制，`build_sim` 保持被忽略状态。
