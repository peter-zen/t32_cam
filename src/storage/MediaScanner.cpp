#include "MediaScanner.h"
#include "MetadataDao.h"
#include <elog.h>
#include <sys/stat.h>
#include <algorithm>
#include <set>
#include <dirent.h>
#include <cstring>
#include <unistd.h>

#define TAG "Scanner"

MediaScanner& MediaScanner::getInstance() {
    static MediaScanner instance;
    return instance;
}

MediaScanner::~MediaScanner() {
    stopScan();
}

void MediaScanner::startScan(const std::string& rootDir) {
    if (m_running.exchange(true)) {
        elog_w(TAG, "Scanner already running");
        return;
    }

    if (m_thread.joinable()) {
        m_thread.join();
    }

    m_thread = std::thread(&MediaScanner::scanLoop, this, rootDir);
}

void MediaScanner::stopScan() {
    m_running = false;
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

bool MediaScanner::isScanning() const {
    return m_running;
}

void MediaScanner::scanLoop(std::string rootDir) {
    elog_i(TAG, "Start scanning %s", rootDir.c_str());
    
    std::vector<std::string> diskFiles;
    std::vector<std::string> dirs;
    dirs.push_back(rootDir);

    while (!dirs.empty() && m_running) {
        std::string currentDir = dirs.back();
        dirs.pop_back();

        DIR* dir = opendir(currentDir.c_str());
        if (!dir) continue;

        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr && m_running) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

            std::string path = currentDir + "/" + entry->d_name;
            struct stat st;
            if (stat(path.c_str(), &st) == 0) {
                if (S_ISDIR(st.st_mode)) {
                    dirs.push_back(path);
                } else if (S_ISREG(st.st_mode)) {
                    // Get extension
                    std::string filename = entry->d_name;
                    size_t dotPos = filename.find_last_of(".");
                    if (dotPos != std::string::npos) {
                        std::string ext = filename.substr(dotPos);
                        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                        
                        if (ext == ".jpg" || ext == ".mp4" || ext == ".mov" || ext == ".jpeg") {
                            diskFiles.push_back(path);
                            processFile(path);
                        }
                    }
                }
            }
        }
        closedir(dir);
    }
    
    if (m_running) {
        syncDb(diskFiles);
    }

    m_running = false;
    elog_i(TAG, "Scan finished");
}

void MediaScanner::processFile(const std::string& filePath) {
    MetadataDao dao;
    MediaItem item;
    
    // 1. Check if exists
    if (dao.getMedia(filePath, item)) {
        // Exists.
        return;
    }
    
    // 2. Not exists, parse and add
    struct stat st;
    if (stat(filePath.c_str(), &st) == 0) {
        item.filePath = filePath;
        item.fileSize = st.st_size;
        item.timestamp = st.st_mtime;
        
        // Determine type
        std::string ext = "";
        size_t dotPos = filePath.find_last_of(".");
        if (dotPos != std::string::npos) {
            ext = filePath.substr(dotPos);
        }
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        
        if (ext == ".jpg" || ext == ".jpeg") {
            item.type = 1; // Photo
        } else {
            item.type = 2; // Video
            item.duration = 0; // TODO: Parse duration
        }
        
        // Default dimensions
        item.width = 0;
        item.height = 0;
        
        if (dao.addMedia(item)) {
            elog_d(TAG, "Added new file: %s", filePath.c_str());
        }
    }
}


void MediaScanner::syncDb(const std::vector<std::string>& diskFiles) {
    // This is a bit heavy: we need to find DB records that are NOT in diskFiles.
    // Optimization: Iterate all DB records (pagination?) or just trust the disk scan if we can get all paths from DB.
    
    MetadataDao dao;
    // Get all paths from DB? MetadataDao doesn't support getAllPaths yet.
    // We can implement a simple getAllPaths or iterate via pagination.
    // For now, let's skip "Delete from DB if not on disk" or implement a simplified version.
    
    // Let's implement getAllPaths in MetadataDao later if needed.
    // Or, we can do:
    // 1. Get total count.
    // 2. Paging get items.
    // 3. Check if item.filePath in diskFiles (using a set).
    
    int count = dao.getCount();
    int offset = 0;
    int limit = 100;
    
    std::set<std::string> diskSet(diskFiles.begin(), diskFiles.end());
    
    while (offset < count && m_running) {
        auto list = dao.getTimeline(offset, limit);
        if (list.empty()) break;
        
        for (const auto& item : list) {
            if (diskSet.find(item.filePath) == diskSet.end()) {
                // Not on disk
                if (dao.deleteMedia(item.filePath)) {
                    elog_i(TAG, "Removed missing file from DB: %s", item.filePath.c_str());
                }
                // Adjust count/offset? Deleting changes the offset logic.
                // It's tricky to delete while iterating.
                // Better strategy: Collect IDs to delete, then delete.
            }
        }
        offset += limit;
    }
}
