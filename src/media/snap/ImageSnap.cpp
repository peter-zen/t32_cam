#include "ImageSnap.h"
#include <mutex>
#include <string.h>
#include <cstdlib>
#include <fstream>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "Common.h"
#include "Logger.h"
#include "Jpeg.h"
#include <vector>
#include <thread>
#include "DayNightSwitch.h"
#include "MetadataDao.h"
#include "HalProvider.h"
#include "IVideo.h"

using namespace media;

ImageSnapParams::ImageSnapParams()
{
    this->nchannels = 1;
    this->width = 1920;
    this->height = 1080;
}

void ImageSnapParams::setImageSize(int width, int height)
{
    this->width = width;
    this->height = height;
}

void ImageSnapParams::getImageSize(int &width, int &height) const
{
    width = this->width;
    height = this->height;
}

int ImageSnapParams::getFrameSourceChnNum() const
{
    return nchannels;
}

void ImageSnapParams::setFrameSourceChnNum(int nchannels)
{
    this->nchannels = nchannels;
}

ImageSnap::ImageSnap()
{
    initialized = initialize();
}

ImageSnap::ImageSnap(const ImageSnapParams &params)
    : params(params)
{
    initialized = initialize();
}

ImageSnap::~ImageSnap()
{
    deinitialize();
}

bool ImageSnap::setParams(const ImageSnapParams& params) {
    this->params = params;
    return true;
}

bool ImageSnap::initialize()
{
    video_ = hal::HalProvider::createVideo();
    if (!video_) {
        Logger::log(LogLevel::ERROR, "initialize: createVideo failed");
        return false;
    }
    if (!video_->init()) {
        Logger::log(LogLevel::ERROR, "initialize: video init failed");
        return false;
    }

    /* Main JPEG stream: CH0 */
    stream_ = video_->createVideoStream();
    if (!stream_) {
        Logger::log(LogLevel::ERROR, "initialize: createVideoStream failed");
        return false;
    }
    int w = 0, h = 0;
    params.getImageSize(w, h);
    hal::VideoStreamConfig cfg;
    memset(&cfg, 0, sizeof(hal::VideoStreamConfig));
    cfg.payload = hal::VideoPayloadType::JPEG;
    cfg.channel.sensor_index = SNAP_SENSOR_ID;
    cfg.channel.stream_index = SNAP_STREAM_ID;
    cfg.width = w;
    cfg.height = h;
    cfg.fps_num = 15;
    cfg.fps_den = 1;
    cfg.quality = 40;
    cfg.rc_mode = hal::VideoRcMode::FIXQP;
    cfg.enable_ivdc = true;
    if (!stream_->configure(cfg)) {
        Logger::log(LogLevel::ERROR, "initialize: stream configure failed");
        return false;
    }

    /* Thumbnail JPEG stream: CH2 (hardware scaler, 320 wide, IVDC) */
    thumbVideo_ = hal::HalProvider::createVideo();
    if (thumbVideo_ && thumbVideo_->init()) {
        thumbStream_ = thumbVideo_->createVideoStream();
    }
    if (thumbStream_) {
        hal::VideoStreamConfig tcfg;
        memset(&tcfg, 0, sizeof(hal::VideoStreamConfig));
        tcfg.payload = hal::VideoPayloadType::JPEG;
        tcfg.channel.sensor_index = SNAP_SENSOR_ID;
        tcfg.channel.stream_index = THUMB_STREAM_ID;
        tcfg.width = 320;
        tcfg.height = (h * 320 + w / 2) / w;  /* maintain aspect ratio */
        tcfg.height = (tcfg.height + 1) & ~1;  /* align to 2 */
        tcfg.fps_num = 15;
        tcfg.fps_den = 1;
        tcfg.quality = 60;
        tcfg.rc_mode = hal::VideoRcMode::FIXQP;
        tcfg.enable_ivdc = true;
        if (!thumbStream_->configure(tcfg)) {
            Logger::log(LogLevel::WARNING, "initialize: thumb stream configure failed (thumbnail disabled)");
            thumbStream_.reset();
        } else {
            Logger::log(LogLevel::INFO, "initialize: thumb stream configured %dx%d",
                        tcfg.width, tcfg.height);
        }
    } else {
        Logger::log(LogLevel::WARNING, "initialize: createVideoStream for thumb failed (thumbnail disabled)");
    }

    return true;
}

void ImageSnap::deinitialize()
{
    if (initialized) {
        daynight_switch(false);
        for (auto& thread : threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        threads.clear();
        if (thumbStream_) {
            thumbStream_->stop();
        }
        if (stream_) {
            stream_->stop();
        }
        if (thumbVideo_) {
            thumbVideo_->exit();
        }
        if (video_) {
            video_->exit();
        }
    }
}

bool ImageSnap::snap(const std::string &filename)
{
    std::vector<std::string> filenames;
    filenames.push_back(filename);
    return snap(filenames);
}

bool ImageSnap::snap(const std::vector<std::string> &filenames)
{
    return snap(filenames, nullptr);
}

bool ImageSnap::snap(const std::string &filename, std::function<void(bool)> onSnapDone)
{
    std::vector<std::string> filenames;
    filenames.push_back(filename);
    return snap(filenames, onSnapDone);
}

bool ImageSnap::snap(const std::vector<std::string> &filenames, std::function<void(bool)> onSnapDone)
{
    Logger::log(LogLevel::INFO, "snap(batch,cb): count=%zu cb=%d", filenames.size(), onSnapDone ? 1 : 0);
    if (!initialized) {
        Logger::log(LogLevel::ERROR, "snap: ImageSnap is not initialized");
        if (onSnapDone) {
            onSnapDone(false);
        }
        return false;
    }
    
    if (!stream_) {
        Logger::log(LogLevel::ERROR, "snap: stream_ is null");
        if (onSnapDone) onSnapDone(false);
        return false;
    }
    hal::VideoStreamInfo preInfo{};
    if (stream_) {
        if (!stream_->getInfo(preInfo)) {
            Logger::log(LogLevel::WARNING, "snap: pre-start info query failed");
        }
    }
    
    daynight_switch(true);

    if (!stream_->start()) {
        Logger::log(LogLevel::ERROR, "snap: stream start failed");
        if (onSnapDone) onSnapDone(false);
        return false;
    }

    /* Start thumbnail stream (CH2) if available */
    if (thumbStream_) {
        if (!thumbStream_->start()) {
            Logger::log(LogLevel::WARNING, "snap: thumb stream start failed (thumbnail disabled)");
            thumbStream_.reset();
        }
    }

    bool result = true;
    if (onSnapDone) {
        this->threads.emplace_back([this, filenames, onSnapDone]() {
            bool nonBlockingResult = true;
            if (!this->snap_internal(filenames)) {
                nonBlockingResult = false;
            }
            if (thumbStream_) thumbStream_->stop();
            stream_->stop();
            if (onSnapDone) {
                onSnapDone(nonBlockingResult);
            }
        });
        return true;
    } else {
        if (!snap_internal(filenames)) {
            result = false;
        }

        if (thumbStream_) thumbStream_->stop();
        stream_->stop();

        if (onSnapDone) {
            onSnapDone(result);
        }
        return result;
    }
}

bool ImageSnap::snap_internal(const std::vector<std::string> &filenames)
{
    hal::VideoStreamInfo info{};
    if (stream_) {
        if (stream_->getInfo(info)) {
            
        } else {
            Logger::log(LogLevel::WARNING, "stream info: query failed");
        }
    }
    for (auto& filename : filenames) {
        
        FILE* fp = fopen(filename.c_str(), "wb");
        if (fp == nullptr) {
            Logger::log(LogLevel::ERROR, "snap(int): open %s failed", filename.c_str());
            return false;
        }
        
        if (!stream_->polling(1000)) {
            Logger::log(LogLevel::ERROR, "snap(int): polling JPEG timeout");
            fclose(fp);
            return false;
        }
        
        hal::VideoEncodedFrame frame;
        if (!stream_->getFrame(frame)) {
            Logger::log(LogLevel::ERROR, "snap(int): getFrame failed");
            fclose(fp);
            return false;
        }
        for (int i = 0; i < frame.piece_count; ++i) {
            size_t written = fwrite(frame.pieces[i].data, 1, frame.pieces[i].size, fp);
            if (written != frame.pieces[i].size) {
                Logger::log(LogLevel::ERROR, "snap(int): write failed written=%zu expected=%zu", written, frame.pieces[i].size);
                fclose(fp);
                return false;
            }
        }

        stream_->releaseFrame(frame);

        long fileSize = ftell(fp);
        fclose(fp);
        Logger::log(LogLevel::INFO, "snap(int): saved %s (%ld bytes)", filename.c_str(), fileSize);

        /* Save photo metadata to media_file.db */
        {
            MetadataDao dao;
            MediaItem item;
            item.filePath = filename;
            item.type = 1; /* Photo */
            item.timestamp = time(NULL);
            item.fileSize = fileSize;
            item.width = info.width;
            item.height = info.height;
            if (dao.addMedia(item)) {
                Logger::log(LogLevel::INFO, "snap(int): saved to DB: %s", filename.c_str());
            } else {
                Logger::log(LogLevel::WARNING, "snap(int): addMedia failed for %s", filename.c_str());
            }
        }

        /* Capture thumbnail from CH2 (same sensor frame, hardware scaled) */
        capture_thumbnail();
    }
    return true;
}

bool ImageSnap::capture_thumbnail()
{
    thumbData_.clear();
    if (!thumbStream_) {
        return false;
    }

    if (!thumbStream_->polling(1000)) {
        Logger::log(LogLevel::WARNING, "capture_thumbnail: polling timeout");
        return false;
    }

    hal::VideoEncodedFrame frame;
    if (!thumbStream_->getFrame(frame)) {
        Logger::log(LogLevel::WARNING, "capture_thumbnail: getFrame failed");
        return false;
    }

    /* Collect all pieces into thumbData_ */
    size_t totalSize = 0;
    for (int i = 0; i < frame.piece_count; ++i) {
        totalSize += frame.pieces[i].size;
    }
    thumbData_.reserve(totalSize);
    for (int i = 0; i < frame.piece_count; ++i) {
        const auto* p = static_cast<const uint8_t*>(frame.pieces[i].data);
        thumbData_.insert(thumbData_.end(), p, p + frame.pieces[i].size);
    }

    thumbStream_->releaseFrame(frame);
    Logger::log(LogLevel::INFO, "capture_thumbnail: captured %zu bytes", thumbData_.size());
    return true;
}

bool ImageSnap::daynight_switch(bool /*on*/)
{
    /* Photo capture should not change the current day/night mode.
     * The ISP is already in the correct mode set by the application startup.
     * Changing it here would affect RTSP preview and other streams globally.
     */
    return true;
}
