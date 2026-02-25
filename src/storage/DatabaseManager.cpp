#include "DatabaseManager.h"
#include <elog.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
//#include <filesystem>


#define TAG "DB"

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

    // Ensure directory exists
    if (access(m_storageDir.c_str(), F_OK) != 0) {
        // Simple mkdir -p implementation or use std::filesystem if available (C++17)
        // Since we are on embedded linux, system() is a quick hack, but filesystem is better if supported.
        // The project seems to use CMake, check C++ standard. 
        // Assuming C++17 or fallback to mkdir command.
        std::string cmd = "mkdir -p " + m_storageDir;
        system(cmd.c_str());
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
            is_locked INTEGER DEFAULT 0
        );
        CREATE INDEX IF NOT EXISTS idx_media_time ON media_files(timestamp DESC);
        CREATE INDEX IF NOT EXISTS idx_media_type ON media_files(type);
        CREATE INDEX IF NOT EXISTS idx_media_type_time ON media_files(type, timestamp DESC);
    )";

    if (!exec(m_mediaDb, sqlMedia)) {
        elog_e(TAG, "Failed to create media_files table");
    }

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
