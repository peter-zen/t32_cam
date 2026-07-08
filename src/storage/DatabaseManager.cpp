#include "DatabaseManager.h"
#include <elog.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
//#include <filesystem>


#define TAG "DB"

namespace {

bool mediaColumnExists(sqlite3* db, const char* columnName) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "PRAGMA table_info(media_files);", -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    bool found = false;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* name = sqlite3_column_text(stmt, 1);
        if (name && strcmp(reinterpret_cast<const char*>(name), columnName) == 0) {
            found = true;
            break;
        }
    }
    sqlite3_finalize(stmt);
    return found;
}

} // namespace

DatabaseManager& DatabaseManager::getInstance() {
    static DatabaseManager instance;
    return instance;
}

DatabaseManager::~DatabaseManager() {
    close();
}

bool DatabaseManager::init(const std::string& storageDir) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_initialized) {
        return true;
    }

    m_storageDir = storageDir;

    // Ensure directory exists — recursive mkdir(2), no fork-exec shell (OOM-safe)
    if (access(m_storageDir.c_str(), F_OK) != 0) {
        std::string p = m_storageDir;
        for (size_t i = 1; i < p.size(); ++i) {
            if (p[i] == '/') {
                p[i] = '\0';
                mkdir(p.c_str(), 0755);   // ignore EEXIST
                p[i] = '/';
            }
        }
        mkdir(p.c_str(), 0755);
    }

    std::string mediaDbPath = m_storageDir + "/media_file.db";
    std::string thumbDbPath = m_storageDir + "/media_thumb.db";

    elog_i(TAG, "Opening databases in %s", m_storageDir.c_str());

    if (!openDb(mediaDbPath, &m_mediaDb, false)) {
        elog_e(TAG, "Failed to open media DB");
        return false;
    }

    if (!openDb(thumbDbPath, &m_thumbDb, true)) {
        elog_e(TAG, "Failed to open thumb DB");
        // Thumb DB failure might be non-fatal? For now treat as fatal to ensure consistency.
        close();
        return false;
    }

    createTables();

    m_initialized = true;
    return true;
}

void DatabaseManager::close() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_mediaDb) {
        sqlite3_close_v2(m_mediaDb);
        m_mediaDb = nullptr;
    }
    if (m_thumbDb) {
        sqlite3_close_v2(m_thumbDb);
        m_thumbDb = nullptr;
    }
    m_initialized = false;
}

sqlite3* DatabaseManager::getMediaDb() const {
    return m_mediaDb;
}

sqlite3* DatabaseManager::getThumbDb() const {
    return m_thumbDb;
}

bool DatabaseManager::openDb(const std::string& path, sqlite3** db, bool isThumbDb) {
    int rc = sqlite3_open_v2(path.c_str(), db, 
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, 
                             nullptr);
    if (rc != SQLITE_OK) {
        elog_e(TAG, "Can't open database %s: %s", path.c_str(), sqlite3_errmsg(*db));
        return false;
    }

    configurePragma(*db, isThumbDb);
    return true;
}

void DatabaseManager::configurePragma(sqlite3* db, bool isThumbDb) {
    // General optimization
    exec(db, "PRAGMA journal_mode = WAL;");
    exec(db, "PRAGMA synchronous = NORMAL;");
    exec(db, "PRAGMA temp_store = MEMORY;");
    exec(db, "PRAGMA page_size = 4096;");
    
    if (isThumbDb) {
        // Thumbs DB might benefit from larger cache if we read many blobs, 
        // but generally we want to keep memory low.
        exec(db, "PRAGMA cache_size = 2000;"); // ~8MB
    } else {
        // Metadata DB is small, can keep it compact
        exec(db, "PRAGMA cache_size = 2000;"); // ~8MB
    }
    
    // Encoding
    exec(db, "PRAGMA encoding = \"UTF-8\";");
}

bool DatabaseManager::exec(sqlite3* db, const std::string& sql) {
    char* zErrMsg = nullptr;
    int rc = sqlite3_exec(db, sql.c_str(), nullptr, 0, &zErrMsg);
    if (rc != SQLITE_OK) {
        elog_e(TAG, "SQL error: %s, SQL: %s", zErrMsg, sql.c_str());
        sqlite3_free(zErrMsg);
        return false;
    }
    return true;
}

void DatabaseManager::createTables() {
    // 1. Media Files Table (media_file.db)
    const char* sqlMedia = R"(
        CREATE TABLE IF NOT EXISTS media_files (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            file_path TEXT NOT NULL UNIQUE,
            type INTEGER NOT NULL,
            timestamp INTEGER NOT NULL,
            file_size INTEGER NOT NULL,
            duration INTEGER DEFAULT 0,
            width INTEGER DEFAULT 0,
            height INTEGER DEFAULT 0,
            is_favorite INTEGER DEFAULT 0,
            is_locked INTEGER DEFAULT 0,
            container_type TEXT DEFAULT '',
            playback_capable INTEGER DEFAULT 0,
            playback_reason TEXT DEFAULT '',
            playback_token TEXT DEFAULT '',
            range_supported INTEGER DEFAULT 0,
            seek_support TEXT DEFAULT '',
            seek_granularity_ms INTEGER DEFAULT 0,
            effective_gop_frames INTEGER DEFAULT 0,
            effective_gop_ms INTEGER DEFAULT 0,
            fragment_index_path TEXT DEFAULT ''
        );
        CREATE INDEX IF NOT EXISTS idx_media_time ON media_files(timestamp DESC);
        CREATE INDEX IF NOT EXISTS idx_media_type ON media_files(type);
        CREATE INDEX IF NOT EXISTS idx_media_type_time ON media_files(type, timestamp DESC);
    )";

    if (!exec(m_mediaDb, sqlMedia)) {
        elog_e(TAG, "Failed to create media_files table");
    }
    migrateMediaSchema();

    // 2. Thumbnails Table (media_thumb.db)
    const char* sqlThumb = R"(
        CREATE TABLE IF NOT EXISTS thumbnails (
            file_path TEXT PRIMARY KEY,
            data BLOB
        );
    )";

    if (!exec(m_thumbDb, sqlThumb)) {
        elog_e(TAG, "Failed to create thumbnails table");
    }
}

void DatabaseManager::migrateMediaSchema() {
    struct ColumnDef {
        const char* name;
        const char* ddl;
    };

    const ColumnDef columns[] = {
        {"container_type", "ALTER TABLE media_files ADD COLUMN container_type TEXT DEFAULT '';"},
        {"playback_capable", "ALTER TABLE media_files ADD COLUMN playback_capable INTEGER DEFAULT 0;"},
        {"playback_reason", "ALTER TABLE media_files ADD COLUMN playback_reason TEXT DEFAULT '';"},
        {"playback_token", "ALTER TABLE media_files ADD COLUMN playback_token TEXT DEFAULT '';"},
        {"range_supported", "ALTER TABLE media_files ADD COLUMN range_supported INTEGER DEFAULT 0;"},
        {"seek_support", "ALTER TABLE media_files ADD COLUMN seek_support TEXT DEFAULT '';"},
        {"seek_granularity_ms", "ALTER TABLE media_files ADD COLUMN seek_granularity_ms INTEGER DEFAULT 0;"},
        {"effective_gop_frames", "ALTER TABLE media_files ADD COLUMN effective_gop_frames INTEGER DEFAULT 0;"},
        {"effective_gop_ms", "ALTER TABLE media_files ADD COLUMN effective_gop_ms INTEGER DEFAULT 0;"},
        {"fragment_index_path", "ALTER TABLE media_files ADD COLUMN fragment_index_path TEXT DEFAULT '';"}
    };

    for (const auto& column : columns) {
        if (!mediaColumnExists(m_mediaDb, column.name) && !exec(m_mediaDb, column.ddl)) {
            elog_e(TAG, "Failed to add media_files column: %s", column.name);
        }
    }

    exec(m_mediaDb, "CREATE INDEX IF NOT EXISTS idx_media_playback ON media_files(type, playback_capable);");
    exec(m_mediaDb, "CREATE INDEX IF NOT EXISTS idx_media_playback_token ON media_files(playback_token);");
    exec(m_mediaDb, "PRAGMA user_version = 2;");
}
