#include "MetadataDao.h"
#include "DatabaseManager.h"
#include <elog.h>
#include <sqlite3.h>

#define TAG "DAO"

static MediaItem buildMediaItemFromRow(sqlite3_stmt* stmt) {
    MediaItem item;
    item.filePath = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    item.id = sqlite3_column_int(stmt, 1);
    item.type = sqlite3_column_int(stmt, 2);
    item.timestamp = sqlite3_column_int64(stmt, 3);
    item.fileSize = sqlite3_column_int64(stmt, 4);
    item.duration = sqlite3_column_int(stmt, 5);
    item.width = sqlite3_column_int(stmt, 6);
    item.height = sqlite3_column_int(stmt, 7);
    item.isFavorite = sqlite3_column_int(stmt, 8) != 0;
    item.isLocked = sqlite3_column_int(stmt, 9) != 0;
    return item;
}

bool MetadataDao::addMedia(const MediaItem& item) {
    sqlite3* db = DatabaseManager::getInstance().getMediaDb();
    if (!db) return false;

    const char* sql = "INSERT OR REPLACE INTO media_files (file_path, type, timestamp, file_size, duration, width, height, is_favorite, is_locked) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        elog_e(TAG, "Prepare failed: %s", sqlite3_errmsg(db));
        return false;
    }

    sqlite3_bind_text(stmt, 1, item.filePath.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, item.type);
    sqlite3_bind_int64(stmt, 3, item.timestamp);
    sqlite3_bind_int64(stmt, 4, item.fileSize);
    sqlite3_bind_int(stmt, 5, item.duration);
    sqlite3_bind_int(stmt, 6, item.width);
    sqlite3_bind_int(stmt, 7, item.height);
    sqlite3_bind_int(stmt, 8, item.isFavorite ? 1 : 0);
    sqlite3_bind_int(stmt, 9, item.isLocked ? 1 : 0);

    bool success = (sqlite3_step(stmt) == SQLITE_DONE);
    if (!success) {
        elog_e(TAG, "Insert failed: %s", sqlite3_errmsg(db));
    }
    sqlite3_finalize(stmt);
    return success;
}

bool MetadataDao::saveThumbnail(const std::string& filePath, const std::vector<uint8_t>& data) {
    sqlite3* db = DatabaseManager::getInstance().getThumbDb();
    if (!db) return false;

    const char* sql = "INSERT OR REPLACE INTO thumbnails (file_path, data) VALUES (?, ?);";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        elog_e(TAG, "Thumb Prepare failed: %s", sqlite3_errmsg(db));
        return false;
    }

    sqlite3_bind_text(stmt, 1, filePath.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_blob(stmt, 2, data.data(), data.size(), SQLITE_STATIC);

    bool success = (sqlite3_step(stmt) == SQLITE_DONE);
    if (!success) {
        elog_e(TAG, "Thumb Insert failed: %s", sqlite3_errmsg(db));
    }
    sqlite3_finalize(stmt);
    return success;
}

bool MetadataDao::getMedia(const std::string& filePath, MediaItem& item) {
    sqlite3* db = DatabaseManager::getInstance().getMediaDb();
    if (!db) return false;

    const char* sql = "SELECT id, type, timestamp, file_size, duration, width, height, is_favorite, is_locked FROM media_files WHERE file_path = ?;";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, filePath.c_str(), -1, SQLITE_STATIC);

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        item.filePath = filePath;
        item.id = sqlite3_column_int(stmt, 0);
        item.type = sqlite3_column_int(stmt, 1);
        item.timestamp = sqlite3_column_int64(stmt, 2);
        item.fileSize = sqlite3_column_int64(stmt, 3);
        item.duration = sqlite3_column_int(stmt, 4);
        item.width = sqlite3_column_int(stmt, 5);
        item.height = sqlite3_column_int(stmt, 6);
        item.isFavorite = sqlite3_column_int(stmt, 7) != 0;
        item.isLocked = sqlite3_column_int(stmt, 8) != 0;
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

bool MetadataDao::deleteMedia(const std::string& filePath) {
    // Note: We need to delete from both DBs.
    // Ideally this should be robust.
    
    sqlite3* mediaDb = DatabaseManager::getInstance().getMediaDb();
    sqlite3* thumbDb = DatabaseManager::getInstance().getThumbDb();
    
    bool success = true;

    // 1. Delete Metadata
    if (mediaDb) {
        const char* sql = "DELETE FROM media_files WHERE file_path = ?;";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(mediaDb, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, filePath.c_str(), -1, SQLITE_STATIC);
            if (sqlite3_step(stmt) != SQLITE_DONE) {
                elog_e(TAG, "Delete media failed: %s", sqlite3_errmsg(mediaDb));
                success = false;
            }
            sqlite3_finalize(stmt);
        } else {
            success = false;
        }
    }

    // 2. Delete Thumbnail (Independent)
    if (thumbDb) {
        const char* sql = "DELETE FROM thumbnails WHERE file_path = ?;";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(thumbDb, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, filePath.c_str(), -1, SQLITE_STATIC);
            sqlite3_step(stmt); // Ignore result, maybe it didn't exist
            sqlite3_finalize(stmt);
        }
    }

    return success;
}

std::vector<MediaItem> MetadataDao::getTimeline(int offset, int limit) {
    std::vector<MediaItem> list;
    sqlite3* db = DatabaseManager::getInstance().getMediaDb();
    if (!db) return list;

    const char* sql = "SELECT file_path, id, type, timestamp, file_size, duration, width, height, is_favorite, is_locked FROM media_files ORDER BY timestamp DESC LIMIT ? OFFSET ?;";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return list;
    }

    sqlite3_bind_int(stmt, 1, limit);
    sqlite3_bind_int(stmt, 2, offset);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        list.push_back(buildMediaItemFromRow(stmt));
    }
    sqlite3_finalize(stmt);
    return list;
}

std::vector<MediaItem> MetadataDao::getTimelineByType(int mediaType, int offset, int limit) {
    std::vector<MediaItem> list;
    sqlite3* db = DatabaseManager::getInstance().getMediaDb();
    if (!db) return list;

    const char* sql =
        "SELECT file_path, id, type, timestamp, file_size, duration, width, height, is_favorite, is_locked "
        "FROM media_files WHERE type = ? ORDER BY timestamp DESC LIMIT ? OFFSET ?;";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return list;
    }

    sqlite3_bind_int(stmt, 1, mediaType);
    sqlite3_bind_int(stmt, 2, limit);
    sqlite3_bind_int(stmt, 3, offset);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        list.push_back(buildMediaItemFromRow(stmt));
    }
    sqlite3_finalize(stmt);
    return list;
}

bool MetadataDao::getThumbnail(const std::string& filePath, std::vector<uint8_t>& data) {
    sqlite3* db = DatabaseManager::getInstance().getThumbDb();
    if (!db) return false;

    const char* sql = "SELECT data FROM thumbnails WHERE file_path = ?;";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, filePath.c_str(), -1, SQLITE_STATIC);

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const void* blob = sqlite3_column_blob(stmt, 0);
        int bytes = sqlite3_column_bytes(stmt, 0);
        if (blob && bytes > 0) {
            data.assign(static_cast<const uint8_t*>(blob), static_cast<const uint8_t*>(blob) + bytes);
            found = true;
        }
    }
    sqlite3_finalize(stmt);
    return found;
}

int MetadataDao::getCount() {
    sqlite3* db = DatabaseManager::getInstance().getMediaDb();
    if (!db) return -1;
    
    const char* sql = "SELECT COUNT(*) FROM media_files;";
    sqlite3_stmt* stmt;
    int count = 0;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    return count;
}

int MetadataDao::getCountByType(int mediaType) {
    sqlite3* db = DatabaseManager::getInstance().getMediaDb();
    if (!db) return -1;

    const char* sql = "SELECT COUNT(*) FROM media_files WHERE type = ?;";
    sqlite3_stmt* stmt;
    int count = 0;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, mediaType);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    return count;
}
