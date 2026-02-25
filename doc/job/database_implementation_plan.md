# 数据库功能实施计划

本文档基于 [数据库选型设计文档](../design/database_selection_and_design.md) 制定，旨在规划 SQLite 数据库在 Huntcam 项目中的集成与开发流程。

## 1. 阶段划分

| 阶段 | 目标 | 预计产出 |
| :--- | :--- | :--- |
| **Phase 1: 基础集成** | 将 SQLite 源码集成到工程，确保 T32/PC 双端编译通过。 | SQLite 库文件、更新后的 CMakeLists.txt |
| **Phase 2: 核心封装** | 封装数据库管理类与 DAO 层，屏蔽 SQL 细节。 | `DatabaseManager`, `MetadataDao`, 单元测试用例 |
| **Phase 3: 业务集成** | 将数据库操作接入拍照、录像、回放业务流。 | 具备 DB 写入能力的 `Recorder`，具备 DB 查询能力的 `Playback` |
| **Phase 4: 验证优化** | 验证功能正确性，优化性能与资源占用。 | 性能测试报告、内存泄漏检查报告 |

---

## 2. 详细执行步骤

### Phase 1: 基础集成 (Infrastructure)

1.  **获取源码**
    *   下载 SQLite amalgamation (source code) 版本 (建议 3.45+)。
    *   放置于 `third_party/sqlite/` 目录 (`sqlite3.c`, `sqlite3.h`)。

2.  **构建系统配置**
    *   修改根目录 `CMakeLists.txt` 或 `third_party/CMakeLists.txt`。
    *   定义 `sqlite3` 静态库目标。
    *   添加编译选项：`-DSQLITE_THREADSAFE=1`, `-DSQLITE_TEMP_STORE=2` (内存模式), `-DSQLITE_OMIT_LOAD_EXTENSION` (减小体积)。

3.  **编译验证**
    *   运行 `make` 确保在 PC (Simulation) 和 T32 (Cross-compile) 环境下均能编译通过。

### Phase 2: 核心封装 (Core Implementation)

1.  **DatabaseManager 实现** (`src/storage/`)
    *   实现单例模式。
    *   实现 `open(path)` / `close()`。
    *   配置 SQLite PRAGMA (关键):
        *   `PRAGMA journal_mode = WAL;`
        *   `PRAGMA synchronous = NORMAL;`
        *   `PRAGMA page_size = 4096;`
        *   `PRAGMA cache_size = 2000;`
    *   实现自动建表逻辑 (`CREATE TABLE IF NOT EXISTS ...`)。
    *   管理双库连接：主库 `media_file.db` 和 缩略图库 `media_thumb.db`。

2.  **DAO 层实现**
    *   **MetadataDao**:
        *   `addMedia(MediaItem)`: 插入文件记录。
        *   `queryTimeline(offset, limit)`: 分页查询时间轴。
        *   `deleteMedia(path)`: 事务性删除（同时删除元数据和缩略图）。
        *   `getStatistics()`: 获取文件总数、总大小等。
    *   **ThumbnailDao** (或集成在 MetadataDao):
        *   `saveThumbnail(path, data)`: 写入 BLOB。
        *   `getThumbnail(path)`: 读取 BLOB。

3.  **单元测试 (Unit Test)**
    *   创建 `tests/test_database.cpp`。
    *   验证：插入数据、查询数据、跨库事务（模拟删除）、异常处理。

### Phase 3: 业务集成 (Business Integration)

1.  **录像/拍照后处理**
    *   修改 `MediaRecorder` 或相关业务类。
    *   在文件 `close()` 完成后，生成缩略图。
    *   调用 `MetadataDao::addMedia()` 写入数据库。

2.  **开机扫描 (Scanner)**
    *   实现 `MediaScanner` 类。
    *   功能：遍历 `/sdcard/DCIM`，对比数据库记录。
    *   逻辑：
        *   文件在磁盘但不在 DB -> 插入 DB。
        *   文件在 DB 但不在磁盘 -> 删除 DB 记录。
        *   (优化) 仅在挂载 SD 卡或检测到脏标记时运行。

3.  **APP 交互接口适配**
    *   修改网络服务层 (如 RTSP/HTTP Server)。
    *   新增 API 处理 APP 的列表请求 (JSON response from DB)。
    *   新增 API 处理缩略图请求 (Binary response from DB)。

### Phase 4: 验证与优化 (Verification & Tuning)

1.  **性能基准测试**
    *   **批量插入**：模拟插入 1000 条记录，耗时应 < 1秒 (WAL 模式下)。
    *   **列表加载**：查询 100 条记录，耗时应 < 50ms。
    *   **并发测试**：模拟录像写入的同时，APP 读取列表。

2.  **资源监控**
    *   使用 `top` 或 `pmap` 监控 T32 上的内存占用，确保 DatabaseManager 实例增加的内存 < 2MB。
    *   检查生成的 `media_file.db` 和 `media_thumb.db` 文件大小是否符合预期。

3.  **异常测试**
    *   模拟断电：在写入过程中断电，重启后检查数据库完整性 (`PRAGMA integrity_check`)。
    *   模拟磁盘满：SD 卡满时的写入行为处理。

---

## 3. 进度安排 (暂定)

*   **Day 1**: 完成 Phase 1 (环境集成) 及 Phase 2 的 DatabaseManager 基础框架。
*   **Day 2**: 完成 Phase 2 的 DAO 层实现及单元测试。
*   **Day 3**: 完成 Phase 3 的业务接入 (录像/拍照流程)。
*   **Day 4**: 完成开机扫描逻辑及 T32 真机验证。

## 4. 依赖项
*   SQLite 3.x 源码包。
*   T32 交叉编译工具链 (已就绪)。
*   现有文件系统读写权限。
