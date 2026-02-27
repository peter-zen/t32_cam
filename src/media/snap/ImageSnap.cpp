#include "ImageSnap.h"
#include <mutex>
#include <string.h>
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
        if (stream_) {
            stream_->stop();
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

    bool result = true;
    if (onSnapDone) {
        this->threads.emplace_back([this, filenames, onSnapDone]() {
            bool nonBlockingResult = true;
            if (!this->snap_internal(filenames)) {
                nonBlockingResult = false;
            }
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
#if 0
        // Save to DB
        MediaItem item;
        item.filePath = filename;
        item.type = 1; // Photo
        item.timestamp = time(NULL);
        item.fileSize = fileSize;
        item.width = info.width;
        item.height = info.height;
        
        MetadataDao dao;
        if (dao.addMedia(item)) {
            Logger::log(LogLevel::INFO, "Saved photo to DB: %s", filename.c_str());
            
            // Generate and save thumbnail
            std::vector<char> thumbData = Jpeg::extractThumbnail(filename);
            if (!thumbData.empty()) {
                 std::vector<uint8_t> uData(thumbData.begin(), thumbData.end());
                 dao.saveThumbnail(filename, uData);
                 Logger::log(LogLevel::INFO, "Saved thumbnail to DB");
            } else {
                 // Fallback: use the original image if small enough? Or skip.
                 // For now, let's assume extractThumbnail works or we skip.
                 Logger::log(LogLevel::WARNING, "Failed to extract thumbnail");

            }
        } else {
            Logger::log(LogLevel::ERROR, "Failed to save photo to DB: %s", filename.c_str());
        }
#endif
    }
    return true;
}

bool ImageSnap::daynight_switch(bool on)
{
    auto daynight_controller = DayNightSwitch::getInstance();
    if (!daynight_controller) {
        Logger::log(LogLevel::ERROR, "daynight_switch: controller null");
        return false;
    }
    daynight_controller->setCdsPins(CDS_SENSOR_PIN);
    daynight_controller->setIRLedPins(IR_LED_PIN);
    daynight_controller->setIRCutPins(IR_CUT_ENABLE_PIN, IR_CUT_CTRL_PIN);
    if (on) {
        auto daynight_state = daynight_controller->getDayNightState();
        daynight_controller->controlISP(daynight_state);
        daynight_controller->controlIRCut(daynight_state);
        daynight_controller->controlIRLed(daynight_state);
    } else {
        daynight_controller->controlISP(DayNightState::DAY);
        daynight_controller->controlIRCut(DayNightState::DAY);
        daynight_controller->controlIRLed(DayNightState::DAY);
    }
    return true;
}
