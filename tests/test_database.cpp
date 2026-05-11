#include "DatabaseManager.h"
#include "MetadataDao.h"
#include "MediaScanner.h"
#include "StringConvert.h"
#include <iostream>
#include <vector>
#include <fstream>
#include <cstdlib>
//#include <filesystem>
#include <unistd.h>
#include <sqlite3.h>

#define TEST_DB_DIR "test_db_dir"
#define TEST_MEDIA_DIR "test_media_dir"

void cleanup() {
    std::string cmd = "rm -rf " + std::string(TEST_DB_DIR);
    (void)system(cmd.c_str());
    cmd = "rm -rf " + std::string(TEST_MEDIA_DIR);
    (void)system(cmd.c_str());
}

static void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << std::endl;
        exit(1);
    }
}

void test_init() {
    std::cout << "[Test] Database Init..." << std::endl;
    cleanup();
    require(DatabaseManager::getInstance().init(TEST_DB_DIR) == true, "database init failed");
    require(DatabaseManager::getInstance().getMediaDb() != nullptr, "media db is null");
    require(DatabaseManager::getInstance().getThumbDb() != nullptr, "thumbnail db is null");
    std::cout << "[PASS]" << std::endl;
}

static void exec_sql(sqlite3* db, const char* sql) {
    char* err = nullptr;
    int rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        std::cerr << "[FAIL] SQL error: " << (err ? err : "") << std::endl;
        sqlite3_free(err);
        exit(1);
    }
}

void test_media_schema_migration() {
    std::cout << "[Test] Media Schema Migration..." << std::endl;
    DatabaseManager::getInstance().close();
    cleanup();
    (void)system(("mkdir -p " + std::string(TEST_DB_DIR)).c_str());

    sqlite3* db = nullptr;
    require(sqlite3_open((std::string(TEST_DB_DIR) + "/media_file.db").c_str(), &db) == SQLITE_OK,
            "failed to create old media db");
    exec_sql(db, R"(
        CREATE TABLE media_files (
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
        INSERT INTO media_files
            (file_path, type, timestamp, file_size, duration, width, height, is_favorite, is_locked)
        VALUES
            ('/sdcard/DCIM/old.mp4', 2, 1700000000, 4096, 12, 1280, 720, 0, 0);
    )");
    sqlite3_close(db);

    require(DatabaseManager::getInstance().init(TEST_DB_DIR) == true, "database init after migration failed");

    MetadataDao dao;
    MediaItem oldItem;
    require(dao.getMedia("/sdcard/DCIM/old.mp4", oldItem) == true, "old media row missing after migration");
    require(oldItem.fileSize == 4096, "old media file size not preserved");
    require(oldItem.playbackCapable == false, "old media should default to playback_capable=false");
    require(oldItem.containerType.empty(), "old media container type should default empty");

    MediaItem playbackItem;
    playbackItem.filePath = "/sdcard/DCIM/new.mp4";
    playbackItem.type = 2;
    playbackItem.timestamp = 1700000010;
    playbackItem.fileSize = 8192;
    playbackItem.duration = 20;
    playbackItem.width = 1920;
    playbackItem.height = 1080;
    playbackItem.containerType = "fmp4";
    playbackItem.playbackCapable = true;
    playbackItem.rangeSupported = true;
    playbackItem.seekSupport = "keyframe";
    playbackItem.seekGranularityMs = 2000;
    playbackItem.effectiveGopFrames = 60;
    playbackItem.effectiveGopMs = 2000;
    require(dao.addMedia(playbackItem) == true, "add playback-capable media failed");

    MediaItem loaded;
    require(dao.getMedia("/sdcard/DCIM/new.mp4", loaded) == true, "get playback-capable media failed");
    require(dao.getMediaById(loaded.id, loaded) == true, "getMediaById failed");
    require(loaded.containerType == "fmp4", "container type mismatch");
    require(loaded.playbackCapable == true, "playback capability mismatch");
    require(loaded.rangeSupported == true, "range support mismatch");
    require(loaded.seekSupport == "keyframe", "seek support mismatch");
    require(loaded.effectiveGopFrames == 60, "effective GOP frames mismatch");

    DatabaseManager::getInstance().close();
    cleanup();
    std::cout << "[PASS]" << std::endl;
}

void test_crud() {
    std::cout << "[Test] CRUD Operations..." << std::endl;
    MetadataDao dao;
    
    // 1. Add
    MediaItem item;
    item.filePath = "/sdcard/DCIM/test.mp4";
    item.type = 2; // Video
    item.timestamp = 1700000000;
    item.fileSize = 1024 * 1024;
    item.duration = 60;
    item.width = 1920;
    item.height = 1080;
    
    require(dao.addMedia(item) == true, "addMedia failed");
    
    // 2. Get
    MediaItem loaded;
    require(dao.getMedia(item.filePath, loaded) == true, "getMedia failed");
    require(loaded.fileSize == item.fileSize, "loaded file size mismatch");
    require(loaded.width == 1920, "loaded width mismatch");
    
    // 3. Count
    require(dao.getCount() == 1, "media count mismatch after add");
    
    // 4. Delete
    require(dao.deleteMedia(item.filePath) == true, "deleteMedia failed");
    require(dao.getCount() == 0, "media count mismatch after delete");
    
    std::cout << "[PASS]" << std::endl;
}

void test_thumbnail() {
    std::cout << "[Test] Thumbnail Operations..." << std::endl;
    MetadataDao dao;
    std::string path = "/sdcard/DCIM/thumb.jpg";
    
    std::vector<uint8_t> data = {0x1, 0x2, 0x3, 0x4};
    require(dao.saveThumbnail(path, data) == true, "saveThumbnail failed");
    
    std::vector<uint8_t> loadedData;
    require(dao.getThumbnail(path, loadedData) == true, "getThumbnail failed");
    require(loadedData.size() == 4, "thumbnail size mismatch");
    require(loadedData[3] == 0x4, "thumbnail content mismatch");
    
    // Delete media should ideally delete thumbnail too (but our simple impl requires explicit delete or trigger)
    // In MetadataDao::deleteMedia implementation, it deletes both.
    require(dao.deleteMedia(path) == true, "deleteMedia for thumbnail path failed");
    
    std::vector<uint8_t> checkData;
    require(dao.getThumbnail(path, checkData) == false, "thumbnail still exists after delete");
    
    std::cout << "[PASS]" << std::endl;
}

void test_timeline() {
    std::cout << "[Test] Timeline Pagination..." << std::endl;
    MetadataDao dao;
    
    for (int i = 0; i < 20; i++) {
        MediaItem item;
        item.filePath = "/sdcard/file_" + to_string_custom(i) + ".jpg";
        item.type = 1;
        item.timestamp = 1000 + i; // ascending time
        item.fileSize = 100;
        dao.addMedia(item);
    }
    
    // Get latest 5 (descending)
    auto list = dao.getTimeline(0, 5);
    require(list.size() == 5, "timeline first page size mismatch");
    require(list[0].timestamp == 1019, "timeline first page order mismatch");
    
    // Get next 5
    list = dao.getTimeline(5, 5);
    require(list.size() == 5, "timeline second page size mismatch");
    require(list[0].timestamp == 1014, "timeline second page order mismatch");
    
    std::cout << "[PASS]" << std::endl;
}

void test_timeline_by_type() {
    std::cout << "[Test] Timeline By Type..." << std::endl;
    MetadataDao dao;

    for (int i = 0; i < 6; i++) {
        MediaItem item;
        item.filePath = "/sdcard/video_" + to_string_custom(i) + ".mp4";
        item.type = 2;
        item.timestamp = 2000 + i;
        item.fileSize = 200;
        item.duration = 10 + i;
        dao.addMedia(item);
    }

    auto photos = dao.getTimelineByType(1, 0, 3);
    require(photos.size() == 3, "photo timeline size mismatch");
    require(photos[0].type == 1, "photo timeline type mismatch");
    require(photos[0].timestamp == 1019, "photo timeline order mismatch");

    auto videos = dao.getTimelineByType(2, 0, 2);
    require(videos.size() == 2, "video timeline size mismatch");
    require(videos[0].type == 2, "video timeline type mismatch");
    require(videos[0].timestamp == 2005, "video timeline order mismatch");

    require(dao.getCountByType(1) == 20, "photo count mismatch");
    require(dao.getCountByType(2) == 6, "video count mismatch");

    std::cout << "[PASS]" << std::endl;
}

static void wait_scanner_idle() {
    for (int i = 0; i < 100 && MediaScanner::getInstance().isScanning(); ++i) {
        usleep(10000);
    }
    require(MediaScanner::getInstance().isScanning() == false, "scanner did not finish");
}

static void write_binary_file(const std::string& path, const std::vector<uint8_t>& data) {
    std::ofstream file(path, std::ios::binary);
    require(file.is_open(), "failed to open test file");
    file.write(reinterpret_cast<const char*>(data.data()), data.size());
    require(file.good(), "failed to write test file");
}

void test_pending_thumbnail_scan() {
    std::cout << "[Test] Pending Thumbnail Scanner..." << std::endl;
    (void)system(("mkdir -p " + std::string(TEST_MEDIA_DIR) + "/DCIM " +
                  std::string(TEST_MEDIA_DIR) + "/thumb_pending").c_str());

    const std::string mediaPath = std::string(TEST_MEDIA_DIR) + "/DCIM/IMG_TEST.jpg";
    const std::string thumbPath = std::string(TEST_MEDIA_DIR) + "/thumb_pending/IMG_TEST.jpg.thumb.jpg";
    const std::string orphanThumbPath = std::string(TEST_MEDIA_DIR) + "/thumb_pending/IMG_MISSING.jpg.thumb.jpg";

    write_binary_file(mediaPath, {0xff, 0xd8, 0xff, 0xd9});
    write_binary_file(thumbPath, {0x10, 0x20, 0x30, 0x40});
    write_binary_file(orphanThumbPath, {0x50, 0x60});

    MediaScannerOptions options;
    options.mode = MediaScannerMode::PendingThumbnails;
    options.mediaRootDir = std::string(TEST_MEDIA_DIR) + "/DCIM";
    options.pendingThumbDir = std::string(TEST_MEDIA_DIR) + "/thumb_pending";
    MediaScanner::getInstance().startScan(options);
    wait_scanner_idle();

    MetadataDao dao;
    MediaItem item;
    require(dao.getMedia(mediaPath, item) == true, "media record was not synced");
    require(item.type == 1, "media type is not photo");

    std::vector<uint8_t> thumbData;
    require(dao.getThumbnail(mediaPath, thumbData) == true, "thumbnail was not synced");
    require(thumbData.size() == 4, "thumbnail size mismatch");
    require(thumbData[0] == 0x10, "thumbnail content mismatch");
    require(access(thumbPath.c_str(), F_OK) != 0, "synced pending thumbnail was not removed");
    require(access(orphanThumbPath.c_str(), F_OK) != 0, "orphan pending thumbnail was not removed");

    std::cout << "[PASS]" << std::endl;
}

int main() {
    test_media_schema_migration();
    test_init();
    test_crud();
    test_thumbnail();
    test_timeline();
    test_timeline_by_type();
    test_pending_thumbnail_scan();
    
    cleanup();
    std::cout << "All Tests Passed!" << std::endl;
    return 0;
}
