#include "MediaScanner.h"
#include "MetadataDao.h"
#include <elog.h>
#include <sys/stat.h>
#include <algorithm>
#include <fstream>
#include <set>
#include <dirent.h>
#include <cstring>
#include <cstdio>
#include <unistd.h>

#define TAG "Scanner"

namespace {

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), ::tolower);
    return value;
}

std::string joinPath(const std::string& base, const std::string& name) {
    if (base.empty()) {
        return name;
    }
    if (base.back() == '/') {
        return base + name;
    }
    return base + "/" + name;
}

std::string getFilename(const std::string& path) {
    const size_t pos = path.find_last_of('/');
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

std::string getExtension(const std::string& path) {
    const std::string name = getFilename(path);
    const size_t pos = name.find_last_of('.');
    if (pos == std::string::npos) {
        return "";
    }
    return toLower(name.substr(pos));
}

bool endsWith(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool fileExists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool dirExists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool isMediaExtension(const std::string& ext) {
    return ext == ".jpg" || ext == ".jpeg" || ext == ".mp4" || ext == ".mov";
}

bool isThumbExtension(const std::string& ext) {
    return ext == ".jpg" || ext == ".jpeg";
}

bool readBinaryFile(const std::string& path, std::vector<uint8_t>& data) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    file.seekg(0, std::ios::end);
    std::streamsize size = file.tellg();
    if (size <= 0) {
        return false;
    }

    file.seekg(0, std::ios::beg);
    data.resize(static_cast<size_t>(size));
    return file.read(reinterpret_cast<char*>(data.data()), size).good();
}

bool buildMediaItem(const std::string& filePath, MediaItem& item) {
    struct stat st;
    if (stat(filePath.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
        return false;
    }

    const std::string ext = getExtension(filePath);
    if (!isMediaExtension(ext)) {
        return false;
    }

    item = MediaItem();
    item.filePath = filePath;
    item.fileSize = st.st_size;
    item.timestamp = st.st_mtime;
    item.type = (ext == ".jpg" || ext == ".jpeg") ? 1 : 2;
    item.duration = 0;
    item.width = 0;
    item.height = 0;
    return true;
}

bool parsePendingThumbName(const std::string& thumbName, std::string& mediaName) {
    const std::string lowerName = toLower(thumbName);
    if (lowerName.empty() || lowerName[0] == '.' || endsWith(lowerName, ".tmp")) {
        return false;
    }

    const char* suffixes[] = {".thumb.jpg", ".thumb.jpeg"};
    for (const char* suffix : suffixes) {
        const std::string suffixValue(suffix);
        if (endsWith(lowerName, suffixValue)) {
            mediaName = thumbName.substr(0, thumbName.size() - suffixValue.size());
            return !mediaName.empty();
        }
    }

    return false;
}

} // namespace

MediaScanner& MediaScanner::getInstance() {
    static MediaScanner instance;
    return instance;
}

MediaScanner::~MediaScanner() {
    stopScan();
}

void MediaScanner::startScan(const std::string& rootDir) {
    MediaScannerOptions options;
    options.mode = MediaScannerMode::FullScan;
    options.mediaRootDir = rootDir;
    startScan(options);
}

void MediaScanner::startScan(const MediaScannerOptions& options) {
    if (m_running.exchange(true)) {
        elog_w(TAG, "Scanner already running");
        return;
    }

    if (m_thread.joinable()) {
        m_thread.join();
    }

    m_thread = std::thread(&MediaScanner::scanLoop, this, options);
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

void MediaScanner::scanLoop(MediaScannerOptions options) {
    const char* modeName = options.mode == MediaScannerMode::FullScan ? "full" : "pending_thumb";
    elog_i(TAG, "Start scanning mode=%s media=%s pending_thumb=%s",
           modeName,
           options.mediaRootDir.c_str(),
           options.pendingThumbDir.c_str());

    if (options.mode == MediaScannerMode::FullScan) {
        scanFullMediaTree(options.mediaRootDir);
    } else {
        scanPendingThumbnails(options);
    }

    m_running = false;
    elog_i(TAG, "Scan finished mode=%s", modeName);
}

void MediaScanner::scanFullMediaTree(const std::string& rootDir) {
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
                        std::string ext = toLower(filename.substr(dotPos));

                        if (isMediaExtension(ext)) {
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
}

void MediaScanner::scanPendingThumbnails(const MediaScannerOptions& options) {
    if (options.mediaRootDir.empty() || options.pendingThumbDir.empty()) {
        elog_w(TAG, "Pending thumbnail scan skipped: empty media or pending directory");
        return;
    }
    if (!dirExists(options.pendingThumbDir)) {
        elog_i(TAG, "Pending thumbnail directory not found: %s", options.pendingThumbDir.c_str());
        return;
    }

    DIR* dir = opendir(options.pendingThumbDir.c_str());
    if (!dir) {
        elog_w(TAG, "Failed to open pending thumbnail directory: %s", options.pendingThumbDir.c_str());
        return;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr && m_running) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        const std::string thumbName = entry->d_name;
        const std::string thumbPath = joinPath(options.pendingThumbDir, thumbName);
        if (!fileExists(thumbPath) || !isThumbExtension(getExtension(thumbPath))) {
            continue;
        }

        std::string mediaName;
        if (!parsePendingThumbName(thumbName, mediaName)) {
            elog_w(TAG, "Remove invalid pending thumbnail: %s", thumbPath.c_str());
            if (remove(thumbPath.c_str()) != 0) {
                elog_w(TAG, "Failed to remove invalid pending thumbnail: %s", thumbPath.c_str());
            }
            continue;
        }

        const std::string mediaPath = joinPath(options.mediaRootDir, mediaName);
        if (!fileExists(mediaPath)) {
            elog_w(TAG, "Remove orphan pending thumbnail: thumb=%s media=%s",
                   thumbPath.c_str(), mediaPath.c_str());
            if (remove(thumbPath.c_str()) != 0) {
                elog_w(TAG, "Failed to remove orphan pending thumbnail: %s", thumbPath.c_str());
            }
            continue;
        }

        if (!processFile(mediaPath)) {
            elog_w(TAG, "Failed to sync media for pending thumbnail: %s", mediaPath.c_str());
            continue;
        }

        std::vector<uint8_t> thumbData;
        if (!readBinaryFile(thumbPath, thumbData)) {
            elog_w(TAG, "Failed to read pending thumbnail: %s", thumbPath.c_str());
            continue;
        }

        MetadataDao dao;
        if (!dao.saveThumbnail(mediaPath, thumbData)) {
            elog_w(TAG, "Failed to save pending thumbnail to DB: %s", thumbPath.c_str());
            continue;
        }

        if (remove(thumbPath.c_str()) != 0) {
            elog_w(TAG, "Synced thumbnail but failed to remove pending file: %s", thumbPath.c_str());
        } else {
            elog_i(TAG, "Synced pending thumbnail: media=%s thumb=%s",
                   mediaPath.c_str(), thumbPath.c_str());
        }
    }

    closedir(dir);
}

bool MediaScanner::processFile(const std::string& filePath) {
    MetadataDao dao;
    MediaItem item;
    
    // 1. Check if exists
    if (dao.getMedia(filePath, item)) {
        // Exists.
        return true;
    }
    
    // 2. Not exists, parse and add
    if (!buildMediaItem(filePath, item)) {
        return false;
    }

    if (dao.addMedia(item)) {
        elog_d(TAG, "Added new file: %s", filePath.c_str());
        return true;
    }

    return false;
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
