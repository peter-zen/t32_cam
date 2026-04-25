#pragma once

#include <string>
#include <vector>
#include <cstdint>

/**
 * @brief Represents a media file record.
 */
struct MediaItem {
    int id = 0;
    std::string filePath;
    int type = 0; // 1:Photo, 2:Video
    int64_t timestamp = 0;
    int64_t fileSize = 0;
    int duration = 0;
    int width = 0;
    int height = 0;
    bool isFavorite = false;
    bool isLocked = false;
    std::string containerType;
    bool playbackCapable = false;
    std::string playbackReason;
    std::string playbackToken;
    bool rangeSupported = false;
    std::string seekSupport;
    int seekGranularityMs = 0;
    int effectiveGopFrames = 0;
    int effectiveGopMs = 0;
    std::string fragmentIndexPath;
};

/**
 * @brief Data Access Object for Media Metadata.
 */
class MetadataDao {
public:
    MetadataDao() = default;
    ~MetadataDao() = default;

    /**
     * @brief Add a new media record.
     * @param item The media item to add.
     * @return true on success.
     */
    bool addMedia(const MediaItem& item);

    /**
     * @brief Add a thumbnail for a file.
     * @param filePath The file path (must match MediaItem.filePath).
     * @param data The thumbnail binary data.
     * @return true on success.
     */
    bool saveThumbnail(const std::string& filePath, const std::vector<uint8_t>& data);

    /**
     * @brief Get a specific media item by path.
     * @param filePath The file path.
     * @param[out] item The result item.
     * @return true if found.
     */
    bool getMedia(const std::string& filePath, MediaItem& item);

    /**
     * @brief Get a specific media item by database id.
     * @param id Media row id.
     * @param[out] item The result item.
     * @return true if found.
     */
    bool getMediaById(int id, MediaItem& item);

    /**
     * @brief Get a specific media item by opaque playback token.
     * @param token Playback token.
     * @param[out] item The result item.
     * @return true if found.
     */
    bool getMediaByPlaybackToken(const std::string& token, MediaItem& item);

    /**
     * @brief Delete a media record (and its thumbnail).
     * @param filePath The file path to delete.
     * @return true on success.
     */
    bool deleteMedia(const std::string& filePath);

    /**
     * @brief Get timeline list (paginated).
     * @param offset Offset.
     * @param limit Limit.
     * @return List of MediaItems.
     */
    std::vector<MediaItem> getTimeline(int offset, int limit);

    /**
     * @brief Get timeline list filtered by media type.
     * @param mediaType 1 for photo, 2 for video.
     * @param offset Offset.
     * @param limit Limit.
     * @return List of MediaItems.
     */
    std::vector<MediaItem> getTimelineByType(int mediaType, int offset, int limit);

    /**
     * @brief Get thumbnail data.
     * @param filePath The file path.
     * @param[out] data The thumbnail binary data.
     * @return true if found.
     */
    bool getThumbnail(const std::string& filePath, std::vector<uint8_t>& data);

    /**
     * @brief Get total count of media files.
     * @return count, or -1 on error.
     */
    int getCount();

    /**
     * @brief Get total count for a specific media type.
     * @param mediaType 1 for photo, 2 for video.
     * @return count, or -1 on error.
     */
    int getCountByType(int mediaType);
};
