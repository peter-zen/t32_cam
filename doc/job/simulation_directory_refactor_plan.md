# 模拟环境目录重构实施计划

## 1. 概述
本计划旨在执行 `doc/design/simulation_environment_directory_structure.md` 中定义的目录重构，分离测试资源与运行时数据，规范化模拟环境路径。

## 2. 影响范围分析

通过搜索 `sim_sdcard` 关键字，确认以下文件和模块将受到影响：

### 2.1 源码 (Source Code)
*   `src/app/main_app.cpp`: 可能包含初始化路径的逻辑。
*   `src/hal/sim/hal_framesource_sim.c`: 包含了硬编码的视频源文件路径 (`sim_sdcard/video/test.h264`)。
*   `src/media/rtsp/test_rtsp_file.cpp`, `test_rtsp_av.cpp`, `test_rtsp_av_simple.cpp`: 测试用例中硬编码的路径。
*   `src/app/app.h`: 可能定义了全局路径常量。

### 2.2 配置文件 (Config Files)
*   `res/env.ini`: 定义了 `BROADCAST_FILELIST_PATHNAME`, `BROADCAST_FILE_PATH`, `ISP_FILE_PATH` 等路径，当前指向 `./sim_sdcard/...`。
*   `sim_sdcard/rtsp_config.ini`: 需要移动并更新引用。

### 2.3 脚本与文档 (Scripts & Docs)
*   `test_av_full.sh`, `test_new_media.sh`, `test_client.sh`, `test_rtsp_fix.sh`: 启动脚本中可能创建或引用了旧目录。
*   `verify_config.sh`, `RTSP_CONFIG_CHECKLIST.txt` 等文档或辅助脚本。

## 3. 实施步骤

### Step 1: 建立新目录结构 (Assets)
*   [ ] 创建 `tests/assets` 目录及其子目录 (`video`, `audio`, `image`, `configs`)。
*   [ ] 将 `sim_sdcard` 下的有效测试资源迁移至 `tests/assets`：
    *   `sim_sdcard/video/test.h264` -> `tests/assets/video/test.h264`
    *   `sim_sdcard/rtsp_config.ini` -> `tests/assets/configs/rtsp_config.ini` (作为模板)
*   [ ] 确认 `res/` 目录下的配置文件 (`config.ini`, `setting.json`) 保持不变，作为默认配置源。

### Step 2: 修改代码路径 (Code Refactoring)
*   [ ] **HAL 层**: 修改 `src/hal/sim/hal_framesource_sim.c`
    *   将 `DEFAULT_H264_PATH` 从 `"sim_sdcard/video/test.h264"` 修改为 `"../../tests/assets/video/test.h264"` (假设 CWD 为 `build_sim`)。
*   [ ] **RTSP 测试用例**: 修改 `src/media/rtsp/test_*.cpp`
    *   更新其中的文件读取路径指向 `../../tests/assets/...`。
*   [ ] **配置文件**: 修改 `res/env.ini`
    *   将所有指向 `./sim_sdcard/...` 的输出路径修改为 `./sdcard/...`。
    *   例如: `BROADCAST_FILE_PATH=./sdcard/media/audio/`。

### Step 3: 更新构建与运行环境 (Build & Run)
*   [ ] **构建脚本**: 修改 `CMakeLists.txt` (如果其中有自动拷贝逻辑) 或创建新的环境初始化脚本 `init_sim_env.sh`。
    *   脚本功能：在 `build_sim/` 下自动创建 `sdcard/{log,data/db,media,configs}` 目录结构。
    *   从 `res/` 复制配置文件到 `build_sim/sdcard/configs/`。
*   [ ] **测试脚本**: 更新 `test_av_full.sh` 等脚本
    *   确保在运行程序前调用环境初始化逻辑。
    *   移除对旧 `sim_sdcard` 的引用。

### Step 4: 清理与验证
*   [ ] 删除项目根目录下的 `sim_sdcard` 目录。
*   [ ] 运行 `test_av_full.sh` 或手动运行 `t32_sim_app`，验证：
    *   程序能正确读取 `tests/assets` 下的视频源。
    *   程序能在 `build_sim/sdcard/log` 下生成日志。
    *   程序能在 `build_sim/sdcard/data/db` 下生成数据库。

## 4. 验证标准
1.  **编译通过**: 所有修改后的代码在 PC 模拟环境下编译无误。
2.  **运行正常**: 启动模拟程序，无"File not found"相关错误日志。
3.  **目录干净**: 根目录下无 `sim_sdcard`，所有运行时产生的文件均被限制在 `build_sim/sdcard` 内。
