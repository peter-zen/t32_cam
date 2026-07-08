#pragma once

#include <ctime>
#include <string>

namespace storage {

enum class MediaKind { Image, Video };

// Media/db/upload path layout. Pure path arithmetic over a storage root +
// media subdirectory — does not read env or know about SIM/HW. Callers resolve
// the root (SIM = simRootPath, HW = /mnt/sdcard | /mnt/huntcam) and pick the
// media subdir ("DCIM" for um, "media" for wm), then read every concrete path
// from here. Centralises the "<root>/data/db", "<root>/DCIM" joins that were
// previously hand-written in each app's StartupConfig fill.
class StoragePaths {
public:
    StoragePaths(std::string root, std::string mediaSubdir);

    const std::string& root() const;

    // <root>/<mediaSubdir>  (e.g. /mnt/sdcard/DCIM, /mnt/huntcam/media)
    std::string mediaRoot() const;

    // <root>/data/db  (the dir DatabaseManager::init() takes)
    std::string dataDb() const;

    // <dataDb>/media_file.db
    std::string mediaDb() const;

    // <dataDb>/media_thumb.db
    std::string thumbDb() const;

    // <mediaRoot>/upload
    std::string uploadDir() const;

    // <root>/.preview  (capturePreviewFrame 临时目录)
    std::string previewDir() const;

    // Unified media filename: <TYPE>_<YYYYMMDD_HHMMSS>_<NNN>.<ext>
    //   TYPE = IMG/VID, ext = jpg/mp4, NNN = seq zero-padded to 3 digits.
    //   seq=0 → "000" (single shot), seq=1 → "001" (first burst frame).
    // Replaces the timestamp-format + prefix + zero-pad logic previously
    // hand-written at 7 call sites (3 independent ts copies, no padding).
    static std::string makeMediaName(MediaKind kind, std::time_t t, int seq);

private:
    std::string root_;
    std::string mediaSubdir_;
};

}  // namespace storage
