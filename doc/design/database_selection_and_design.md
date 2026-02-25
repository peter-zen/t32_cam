# 数据库选型设计文档

## 1. 背景与需求

### 1.1 背景
工程需要在嵌入式Linux系统（Ingenic T32 MIPS）中引入数据库支持，主要用于：
1. 对拍照录影的文件进行管理
2. 对相机属性的配置进行管理（待评估）

### 1.2 系统环境
- **硬件平台**：Ingenic T32 MIPS
- **操作系统**：嵌入式Linux
- **存储介质**：Flash/SD卡（文件系统：ext4等）
- **资源限制**：内存和存储空间有限，需要轻量级方案

### 1.3 选型候选
- SQLite（推荐）
- FlashDB（备选）

---

## 2. SQLite 适用性评估（更新）

### 2.1 资源占用对比

在嵌入式 Linux 环境下（T32平台），SQLite 与 FlashDB 的资源对比：

| 指标 | FlashDB (KVDB) | SQLite (嵌入式配置) | 评价 |
| :--- | :--- | :--- | :--- |
| **ROM (Flash)** | < 10 KB | 300 KB ~ 600 KB | SQLite 占用较大，但在 16MB+ Flash 设备上完全可接受 |
| **RAM (运行时)** | < 5 KB | 200 KB ~ 2 MB | SQLite 内存占用主要取决于 Page Cache，可配置。**对于 Linux 系统，几百 KB 的开销通常是可以忽略的。** |
| **查询能力** | 仅 Key-Value / 简单遍历 | **强** (SQL, WHERE, ORDER BY, LIMIT) | SQLite 完胜。复杂查询直接由 DB 处理，**反而节省应用层 CPU 和 内存**。 |
| **开发效率** | 需手动实现索引、排序逻辑 | 标准 SQL，极高 | SQLite 维护成本更低 |
| **数据完整性** | 基本校验 | ACID 事务支持 | SQLite 更可靠 |

### 2.2 为什么选择 SQLite？

虽然 FlashDB 极致轻量，但在运行 Linux 的 T32 平台上，**SQLite 是更好的选择**，原因如下：

1.  **查询灵活性**：用户需求包含“属性查询”（如筛选特定日期的视频、按文件大小排序等）。在 FlashDB 中，这需要将所有元数据加载到内存中进行遍历和过滤，随着文件数量增加（如 1000+ 文件），**应用层内存消耗和 CPU 占用会显著上升**。SQLite 将这些工作下沉到数据库引擎，效率更高。
2.  **系统负担可控**：T32 运行 Linux，通常配备 64MB+ 内存。SQLite 的运行时内存可以通过 `PRAGMA cache_size` 严格限制（例如限制在 1MB 以内），不会对系统造成实质压力。
3.  **扩展性**：未来如果需要增加字段或关联查询，SQLite 修改 Schema 或 SQL 语句即可，FlashDB 可能需要重写数据结构解析逻辑。
4.  **生态与维护**：SQLite 是行业标准，排查问题和招聘维护人员都更容易。

### 2.3 SQLite 优化配置建议

为了适应嵌入式环境，建议采用以下配置：
- **Journal Mode**: `WAL` (Write-Ahead Logging) - 提高并发性能，减少写入阻塞。
- **Synchronous**: `NORMAL` - 在保证安全的前提下减少磁盘 I/O。
- **Page Size**: `4096` (匹配文件系统块大小)。
- **Cache Size**: `2000` (约 8MB) 或更小，根据实际内存压力调整。
- **Temp Store**: `MEMORY` - 临时文件存内存。

### 2.4 架构策略：单库 vs 分库 (Split DB)

在处理含大量二进制数据（如缩略图）的场景时，数据库架构直接影响用户体验。以下是方案对比：

| 维度 | **单库方案** (Metadata + Blob) | **分库方案** (Metadata + Thumbnail DB) |
| :--- | :--- | :--- |
| **元数据体积** | 随文件数线性增长，若含缩略图则体积巨大 (1000图 ≈ 10MB)。 | **极小**。仅存文本信息，1000条记录仅 ~100KB。 |
| **列表加载速度** | 首次连接需下载 10MB 文件，耗时约 2-5秒，用户有感知。 | 首次连接仅下载 100KB，**< 0.1秒，真正的秒开**。 |
| **内存/Cache效率** | SQLite Page Cache 容易被大块 BLOB 数据挤占，查询元数据可能频繁触发 I/O。 | 元数据 DB 极小，几乎可全驻留 Page Cache，查询极快，不受缩略图影响。 |
| **灵活性** | 必须全量下载。 | **按需加载**。APP 可先展示列表，后台静默下载缩略图库，或仅下载屏幕可见区域的缩略图。 |
| **一致性维护** | 事务天然保证（单库 ACID）。 | 需应用层保证（删文件时需同时操作两个 DB）。但通过封装 DAO 层很容易实现。 |

**结论**：本项目采用 **分库方案**。
- 主库 (`media_file.db`)：仅存元数据，保证极速列表加载。
- 缩略图库 (`media_thumb.db`)：存缩略图，异步或按需加载。

---

## 3. 数据库架构设计

### 3.1 整体架构

```
┌─────────────────────────────────────────────────────┐
│                   应用层                              │
│  (拍照、录像、配置管理、回放列表)                      │
└──────────────────┬──────────────────────────────────┘
                   │ SQL (CRUD)
                   ▼
┌─────────────────────────────────────────────────────┐
│              存储管理层 (src/storage/)               │
│  ┌──────────────┐  ┌──────────────┐  ┌────────────┐ │
│  │  Database    │  │  Metadata    │  │  Config    │ │
│  │   Manager    │  │   Manager    │  │  Manager   │ │
│  └──────────────┘  └──────────────┘  └────────────┘ │
│          │ (SQLite C API)                           │
└──────────┼──────────────────────────────────────────┘
           ▼
┌──────────────────────┐
│       SQLite3        │ (third_party/sqlite)
└──────────┬───────────┘
           ▼
┌──────────────────────┐
│      文件系统         │ (/sdcard/data/db/media_file.db)
└──────────────────────┘
```

### 3.2 目录结构设计

```
third_party/
└── sqlite/                      # SQLite 源码 (amalgamation)
    ├── sqlite3.c
    └── sqlite3.h

src/
├── storage/
│   ├── DatabaseManager.h        # 单例，管理 DB 连接 (media_file.db)
│   ├── ThumbnailManager.h       # 独立管理缩略图 DB (media_thumb.db)
│   ├── MetadataDao.h            # 数据访问对象 (DAO) - 媒体文件
│   ├── MetadataDao.cpp
│   ├── ConfigDao.h              # 数据访问对象 (DAO) - 配置
│   └── ConfigDao.cpp
```

---

## 4. 数据表设计 (Schema)

基于分库策略，我们将数据分散到两个数据库文件中。

### 4.1 媒体文件表 (`media_file.db`)

**关联键选择：`file_path` vs `id`**
- **选择**：`file_path`
- **理由**：
    1.  **解耦与健壮性**：`media_thumb.db` 可能作为缓存被独立清理或重建。如果使用 `id`，一旦主库重建（ID重置），缩略图库将彻底失效。使用物理路径作为关联键，即使主库丢失，重新扫描文件后，缩略图库依然有效。
    2.  **直观性**：在单独调试 `media_thumb.db` 时，可以直接看到是哪个文件的缩略图，无需联表查询。
    3.  **空间权衡**：虽然路径字符串比整数 ID 占用更多空间，但在分库场景下，解耦带来的系统稳定性价值远大于微小的空间损耗。

**主数据库 (`media_file.db`) - 表结构**：

```sql
CREATE TABLE IF NOT EXISTS media_files (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    
    -- 核心唯一标识 (兼作关联键)
    file_path TEXT NOT NULL UNIQUE,    -- 绝对路径
    
    -- 类型与时间
    type INTEGER NOT NULL,             -- 1:Photo, 2:Video
    timestamp INTEGER NOT NULL,        -- 创建时间戳 (UTC)
    
    -- 文件属性
    file_size INTEGER NOT NULL,        -- 字节数
    duration INTEGER DEFAULT 0,        -- 视频时长(秒)，照片为0
    width INTEGER DEFAULT 0,           -- 宽
    height INTEGER DEFAULT 0,          -- 高
    
    -- 状态
    is_favorite INTEGER DEFAULT 0,     -- 收藏标记
    is_locked INTEGER DEFAULT 0        -- 锁定标记
);

-- 索引
CREATE INDEX IF NOT EXISTS idx_media_time ON media_files(timestamp DESC);
CREATE INDEX IF NOT EXISTS idx_media_type ON media_files(type);
-- 组合索引：加速"按类型查时间轴" (如只看视频回放)
CREATE INDEX IF NOT EXISTS idx_media_type_time ON media_files(type, timestamp DESC);
```

### 4.2 缩略图表 (`media_thumb.db`)

独立存储，APP 可选择后台下载或按需查询。

**缩略图数据库 (`media_thumb.db`) - 表结构**：

```sql
CREATE TABLE IF NOT EXISTS thumbnails (
    file_path TEXT PRIMARY KEY,        -- 关联键，与 media_files.file_path 一致
    data BLOB                          -- 缩略图二进制数据
);
```

**APP 交互模式优化**：
1.  **连接阶段**：APP 连接 WiFi。
2.  **元数据同步**：APP 下载 `media_file.db`（极快，<1秒）。
3.  **列表展示**：APP 立即展示文件列表（此时显示默认图标）。
4.  **缩略图同步（策略可选）**：
    *   **策略 A (推荐)**：后台静默下载 `media_thumb.db`。
    *   **策略 B (按需)**：APP 仅根据当前屏幕可见的 `file_path`，通过 HTTP API 向相机请求对应的缩略图数据（相机查 `media_thumb.db` 返回）。

---

### 4.2 配置表 (`system_config`)

使用 Key-Value 结构存储配置，灵活且易扩展。

```sql
CREATE TABLE IF NOT EXISTS system_config (
    key TEXT PRIMARY KEY,
    value TEXT,
    type TEXT DEFAULT 'string',   -- 'int', 'bool', 'string', 'json'
    updated_at INTEGER
);
```

**示例数据**：
| key | value | type |
| :--- | :--- | :--- |
| `camera.resolution` | `1080p` | `string` |
| `system.volume` | `80` | `int` |
| `video.loop_record` | `1` | `bool` |

### 4.3 日志表 (`operation_log`)

```sql
CREATE TABLE IF NOT EXISTS operation_log (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp INTEGER NOT NULL,
    level INTEGER,       -- 1:INFO, 2:WARN, 3:ERROR
    module TEXT,
    message TEXT
);

-- 定期清理旧日志的索引
CREATE INDEX IF NOT EXISTS idx_log_time ON operation_log(timestamp);
```

---

## 5. 接口设计 (C++ API)

### 5.1 DatabaseManager

负责打开/关闭数据库，执行原生 SQL（如果需要）。

```cpp
class DatabaseManager {
public:
    static DatabaseManager& getInstance();
    bool open(const std::string& path);
    sqlite3* getDb(); // 获取原始句柄供 DAO 使用
    // ...
};
```

### 5.2 MetadataDao (Data Access Object)

封装具体的业务查询逻辑。

```cpp
struct MediaItem {
    int id;
    std::string filePath;
    std::string type;
    int64_t timestamp;
    int duration;
    // ...
};

class MetadataDao {
public:
    // 增
    bool addMedia(const MediaItem& item);
    
    // 删
    bool deleteMedia(const std::string& filePath);
    bool deleteOldest(int count); // 循环录像覆盖逻辑
    
    // 查
    std::vector<MediaItem> getTimeline(int offset, int limit);
    std::vector<MediaItem> getByDate(const std::string& date);
    int getTotalCount();
};
```

---

## 6. 开发计划

1.  **集成 SQLite**: 下载 `sqlite3.c` / `sqlite3.h` 到 `third_party/sqlite`。
2.  **构建系统**: 更新 `CMakeLists.txt` 编译 SQLite。
3.  **封装层实现**: 实现 `DatabaseManager` 和 DAO 层。
4.  **业务对接**: 拍照/录像完成后调用 `addMedia`。
