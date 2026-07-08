#include "StoragePaths.h"

#include <ctime>
#include <stdio.h>   // snprintf — uClibc <cstdio> 不把 snprintf 放入 std::

namespace storage {

StoragePaths::StoragePaths(std::string root, std::string mediaSubdir)
    : root_(std::move(root)), mediaSubdir_(std::move(mediaSubdir)) {}

const std::string& StoragePaths::root() const { return root_; }

std::string StoragePaths::mediaRoot() const {
    return root_ + "/" + mediaSubdir_;
}

std::string StoragePaths::dataDb() const {
    return root_ + "/data/db";
}

std::string StoragePaths::mediaDb() const {
    return dataDb() + "/media_file.db";
}

std::string StoragePaths::thumbDb() const {
    return dataDb() + "/media_thumb.db";
}

std::string StoragePaths::uploadDir() const {
    return mediaRoot() + "/upload";
}

std::string StoragePaths::previewDir() const {
    return root_ + "/.preview";
}

std::string StoragePaths::makeMediaName(MediaKind kind, std::time_t t, int seq) {
    std::tm tmv;
    localtime_r(&t, &tmv);
    char ts[16];
    std::strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", &tmv);
    const char* prefix = (kind == MediaKind::Image) ? "IMG" : "VID";
    const char* ext    = (kind == MediaKind::Image) ? "jpg" : "mp4";
    char out[40];
    snprintf(out, sizeof(out), "%s_%s_%03d.%s", prefix, ts, seq, ext);
    return std::string(out);
}

}  // namespace storage
