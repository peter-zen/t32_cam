#pragma once

#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <functional>

enum class MediaScannerMode {
    PendingThumbnails,
    FullScan,
};

struct MediaScannerOptions {
    MediaScannerMode mode = MediaScannerMode::PendingThumbnails;
    std::string mediaRootDir;
    std::string pendingThumbDir;
};

/**
 * @brief Scans the storage directory to sync database with file system.
 */
class MediaScanner {
public:
    static MediaScanner& getInstance();

    /**
     * @brief Start scanning in a background thread.
     * @param rootDir The directory to scan (e.g., "/sdcard/DCIM")
     */
    void startScan(const std::string& rootDir);
    void startScan(const MediaScannerOptions& options);

    /**
     * @brief Stop the scanning process.
     */
    void stopScan();

    /**
     * @brief Check if scanning is in progress.
     */
    bool isScanning() const;

private:
    MediaScanner() = default;
    ~MediaScanner();
    MediaScanner(const MediaScanner&) = delete;
    MediaScanner& operator=(const MediaScanner&) = delete;

    void scanLoop(MediaScannerOptions options);
    void scanFullMediaTree(const std::string& rootDir);
    void scanPendingThumbnails(const MediaScannerOptions& options);
    bool processFile(const std::string& filePath);
    void syncDb(const std::vector<std::string>& diskFiles);

    std::atomic<bool> m_running{false};
    std::thread m_thread;
};
