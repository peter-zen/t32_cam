#include "DatabaseManager.h"
#include "MetadataDao.h"
#include <iostream>
#include <vector>
#include <cassert>
//#include <filesystem>
#include <unistd.h>

#define TEST_DB_DIR "test_db_dir"

void cleanup() {
    std::string cmd = "rm -rf " + std::string(TEST_DB_DIR);
    system(cmd.c_str());
}

void test_init() {
    std::cout << "[Test] Database Init..." << std::endl;
    cleanup();
    bool ret = DatabaseManager::getInstance().init(TEST_DB_DIR);
    assert(ret == true);
    assert(DatabaseManager::getInstance().getMediaDb() != nullptr);
    assert(DatabaseManager::getInstance().getThumbDb() != nullptr);
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
    
    bool ret = dao.addMedia(item);
    assert(ret == true);
    
    // 2. Get
    MediaItem loaded;
    ret = dao.getMedia(item.filePath, loaded);
    assert(ret == true);
    assert(loaded.fileSize == item.fileSize);
    assert(loaded.width == 1920);
    
    // 3. Count
    int count = dao.getCount();
    assert(count == 1);
    
    // 4. Delete
    ret = dao.deleteMedia(item.filePath);
    assert(ret == true);
    count = dao.getCount();
    assert(count == 0);
    
    std::cout << "[PASS]" << std::endl;
}

void test_thumbnail() {
    std::cout << "[Test] Thumbnail Operations..." << std::endl;
    MetadataDao dao;
    std::string path = "/sdcard/DCIM/thumb.jpg";
    
    std::vector<uint8_t> data = {0x1, 0x2, 0x3, 0x4};
    bool ret = dao.saveThumbnail(path, data);
    assert(ret == true);
    
    std::vector<uint8_t> loadedData;
    ret = dao.getThumbnail(path, loadedData);
    assert(ret == true);
    assert(loadedData.size() == 4);
    assert(loadedData[3] == 0x4);
    
    // Delete media should ideally delete thumbnail too (but our simple impl requires explicit delete or trigger)
    // In MetadataDao::deleteMedia implementation, it deletes both.
    ret = dao.deleteMedia(path);
    assert(ret == true);
    
    std::vector<uint8_t> checkData;
    ret = dao.getThumbnail(path, checkData);
    assert(ret == false); // Should be gone
    
    std::cout << "[PASS]" << std::endl;
}

void test_timeline() {
    std::cout << "[Test] Timeline Pagination..." << std::endl;
    MetadataDao dao;
    
    for (int i = 0; i < 20; i++) {
        MediaItem item;
        item.filePath = "/sdcard/file_" + std::to_string(i) + ".jpg";
        item.type = 1;
        item.timestamp = 1000 + i; // ascending time
        item.fileSize = 100;
        dao.addMedia(item);
    }
    
    // Get latest 5 (descending)
    auto list = dao.getTimeline(0, 5);
    assert(list.size() == 5);
    assert(list[0].timestamp == 1019); // newest first
    
    // Get next 5
    list = dao.getTimeline(5, 5);
    assert(list.size() == 5);
    assert(list[0].timestamp == 1014);
    
    std::cout << "[PASS]" << std::endl;
}

int main() {
    test_init();
    test_crud();
    test_thumbnail();
    test_timeline();
    
    cleanup();
    std::cout << "All Tests Passed!" << std::endl;
    return 0;
}
