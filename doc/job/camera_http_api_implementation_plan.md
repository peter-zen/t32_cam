# 相机 HTTP API 实现计划

## 1. 概述
本计划旨在落实 `doc/design/camera_http_api_design.md` 中设计的 HTTP API 接口，特别是新增的数据库文件传输接口。

## 2. 准备工作
- [ ] **环境检查**: 确认 CivetWeb HTTP Server 已集成到项目中。
- [ ] **数据库依赖**: 确认 SQLite3 库已正确编译并链接。
- [ ] **存储模块**: 确认 `StorageManager` 或相关模块已就绪，能够提供 `media_file.db` 和 `media_thumb.db` 的路径和访问权限。

## 3. 实施阶段

### Phase 0: 业务层抽象 (Service Abstraction) (预计 1 天)
构建 ICameraService 接口层，隔离 HTTP API 与底层硬件逻辑。

- [ ] **接口定义**:
    - 创建 `src/service/camera/ICameraService.h`，定义拍照、录像、属性配置、DB路径获取等纯虚接口。
    - 定义相关的数据结构 (PhotoResult, RecordStatus 等)。
- [ ] **模拟实现 (Sim)**:
    - 创建 `src/service/camera/impl/CameraServiceSim.cpp`。
    - 实现文件模拟 (拷贝 assets 目录下的 sample 文件)。
    - 实现 DB 模拟 (确保 MetadataDao 在 PC 端可用)。
- [ ] **真机实现 (T32)**:
    - 创建 `src/service/camera/impl/CameraServiceT32.cpp`。
    - 封装现有的 `ImageSnap` 和 `VideoRecorder` 调用。
- [ ] **工厂封装**:
    - 实现 `CameraServiceFactory`，根据编译宏 `PLATFORM_T32` 切换实现。

### Phase 1: 基础框架与控制 API (预计 1-2 天)
建立 API 路由框架，实现最核心的相机控制功能。

- [ ] **框架搭建**:
    - 初始化 CivetWeb Server。
    - 注册 API 路由处理函数。
    - 实现统一的 JSON 响应封装函数。
    - 实现错误处理中间件。
- [ ] **属性管理**:
    - `GET /api/v1/camera/properties` (获取所有属性)
    - `POST /api/v1/camera/properties` (批量设置)
- [ ] **录像控制**:
    - `POST /api/v1/camera/video/start`
    - `POST /api/v1/camera/video/stop`
    - `GET /api/v1/camera/video/status`
- [ ] **拍照控制**:
    - `POST /api/v1/camera/photo` (单张)

### Phase 2: 高级功能与预设 (预计 1-2 天)
实现更复杂的拍摄模式和配置管理。

- [ ] **高级拍照**:
    - `POST /api/v1/camera/photo/burst` (连拍)
    - `POST /api/v1/camera/photo/timer` (定时)
    - `GET /api/v1/camera/photo/status`
- [ ] **精细属性控制**:
    - `GET /api/v1/camera/properties/{name}`
    - `POST /api/v1/camera/properties/{name}`
- [ ] **预设管理**:
    - `GET /api/v1/camera/presets`
    - `POST /api/v1/camera/presets/{id}`

### Phase 3: 文件管理与数据库同步 (预计 1-2 天)
重点实现基于数据库的文件管理和新增的数据库同步接口。

- [ ] **数据库同步接口 (核心新增)**:
    - `GET /api/v1/camera/database/media`: 读取 `media_file.db` 并以 `application/x-sqlite3` 格式返回。
    - `GET /api/v1/camera/database/thumbnail`: 读取 `media_thumb.db` 并以 `application/x-sqlite3` 格式返回。
    - **注意**: 需处理文件读取并发锁，确保读取时数据库未被写入锁定。
- [ ] **文件列表 (传统 API)**:
    - `GET /api/v1/camera/video/list` (基于 DB 查询)
    - `GET /api/v1/camera/photos` (基于 DB 查询)
- [ ] **文件操作**:
    - `POST /api/v1/camera/files/delete` (需同步更新 DB)
- [ ] **预览与缩略图**:
    - `GET /api/v1/camera/preview`
    - `GET /api/v1/camera/thumbnail`

## 4. 测试与验证

### 4.1 单元测试
- 对 JSON 解析/生成模块进行测试。
- 对 URL 路由分发逻辑进行测试。

### 4.2 集成测试
编写 Shell 脚本或 Python 脚本，使用 `curl` 模拟客户端请求：
1.  **控制流测试**: 拍照 -> 查状态 -> 查列表。
2.  **DB 同步测试**:
    - 调用 `GET /api/v1/camera/database/media` 下载 DB。
    - 使用 `sqlite3` 命令行工具验证下载的 DB 文件完整性。
3.  **并发测试**: 在录像/拍照过程中请求 DB 下载，验证系统稳定性。

## 5. 风险评估
- **数据库锁冲突**: 下载 DB 文件时，若后台正在写入元数据，可能导致读取失败或文件损坏。需要使用 WAL 模式或在应用层做读写互斥。
- **性能影响**: 传输大文件（如缩略图 DB）时可能会占用较高 CPU/网络带宽，影响实时预览或录像。需要进行压力测试。
