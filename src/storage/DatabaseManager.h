#pragma once

#include <string>
#include <mutex>
#include <sqlite3.h>

/**
 * @brief Singleton class to manage SQLite database connections.
 * 
 * Handles two database files:
 * 1. Media File DB (media_file.db): Metadata, small size, fast access.
 * 2. Media Thumb DB (media_thumb.db): Thumbnails, large size, async access.
 */
class DatabaseManager {
public:
    static DatabaseManager& getInstance();

    /**
     * @brief Initialize database connections.
     * @param storageDir Directory where DB files are stored (e.g., "/sdcard/data/db")
     * @return true on success, false on failure
     */
    bool init(const std::string& storageDir);

    /**
     * @brief Close all database connections.
     */
    void close();

    /**
     * @brief Get the main metadata database connection.
     * @return sqlite3 pointer or nullptr if not initialized.
     */
    sqlite3* getMediaDb() const;

    /**
     * @brief Get the thumbnail database connection.
     * @return sqlite3 pointer or nullptr if not initialized.
     */
    sqlite3* getThumbDb() const;

    /**
     * @brief Execute a SQL statement on the specified database.
     * @param db Database connection
     * @param sql SQL statement
     * @return true on success
     */
    bool exec(sqlite3* db, const std::string& sql);

private:
    DatabaseManager() = default;
    ~DatabaseManager();
    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;

    bool openDb(const std::string& path, sqlite3** db, bool isThumbDb);
    void configurePragma(sqlite3* db, bool isThumbDb);
    void createTables();
    void migrateMediaSchema();

    sqlite3* m_mediaDb = nullptr;
    sqlite3* m_thumbDb = nullptr;
    std::mutex m_mutex;
    std::string m_storageDir;
    bool m_initialized = false;
};
