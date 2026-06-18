#include "ImageSnap.h"
#include <mutex>
#include <string.h>
#include <cstdlib>
#include <cstdio>
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

#ifndef SIMULATION_MODE
#include <imp/imp_framesource.h>
#include <imp/imp_encoder.h>
#include "LargeImageSnap.h"
#include "simd_resize_large.h"
#include "simd_resize_large.c"
#endif

/* CH0 hardware scaler max: 8M (3840x2160) */
static constexpr int HW_SCALER_MAX_W = 3840;
static constexpr int HW_SCALER_MAX_H = 2160;

using namespace media;

ImageSnapParams::ImageSnapParams()
{
    this->nchannels = 1;
    this->width = 1920;
    this->height = 1080;
    this->sensorW = 0;
    this->sensorH = 0;
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

void ImageSnapParams::setSensorNativeSize(int width, int height)
{
    this->sensorW = width;
    this->sensorH = height;
}

void ImageSnapParams::getSensorNativeSize(int &width, int &height) const
{
    width = this->sensorW;
    height = this->sensorH;
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
    int w = 0, h = 0;
    params.getImageSize(w, h);

    /* Determine if target resolution exceeds hardware scaler max (8M) */
    isLargeImage_ = (w > HW_SCALER_MAX_W || h > HW_SCALER_MAX_H);

    /* Large-image path: CH0 cannot be configured at the target resolution â
     * the IMP sensor channel's picWidth must stay at sensor-native (configuring
     * 9216x5184 corrupts the channel and segfaults in SetChnAttr). Configure
     * CH0 at sensor-native; LargeImageSnap upscales to the target in software. */
    int cfgW = w, cfgH = h;
    if (isLargeImage_) {
        int sw = 0, sh = 0;
        params.getSensorNativeSize(sw, sh);
        if (sw > 0 && sh > 0) {
            cfgW = sw;
            cfgH = sh;
        } else {
            Logger::log(LogLevel::WARNING, "initialize: large target %dx%d but sensorNativeSize unset", w, h);
        }
    }
    Logger::log(LogLevel::INFO, "initialize: target=%dx%d ch0cfg=%dx%d isLargeImage=%d",
                w, h, cfgW, cfgH, isLargeImage_ ? 1 : 0);

    video_ = hal::HalProvider::createVideo();
    if (!video_) {
        Logger::log(LogLevel::ERROR, "initialize: createVideo failed");
        return false;
    }
    if (!video_->init()) {
        Logger::log(LogLevel::ERROR, "initialize: video init failed");
        return false;
    }

    /* Main JPEG stream: CH0
     * - ≤ 8M: IVDC enabled (hardware direct path, encoder captures directly)
     * - > 8M: IVDC disabled (need GetFrame for CPU SIMD resize, then InputJpege)
     */
    stream_ = video_->createVideoStream();
    if (!stream_) {
        Logger::log(LogLevel::ERROR, "initialize: createVideoStream failed");
        return false;
    }
    hal::VideoStreamConfig cfg;
    memset(&cfg, 0, sizeof(hal::VideoStreamConfig));
    cfg.payload = hal::VideoPayloadType::JPEG;
    cfg.channel.sensor_index = SNAP_SENSOR_ID;
    cfg.channel.stream_index = SNAP_STREAM_ID;
    cfg.width = cfgW;
    cfg.height = cfgH;
    cfg.fps_num = 15;
    cfg.fps_den = 1;
    cfg.quality = 40;
    cfg.rc_mode = hal::VideoRcMode::FIXQP;
    cfg.enable_ivdc = !isLargeImage_;
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

    /* For ≤ 8M: start encoder stream (IVDC captures directly)
     * For > 8M: skip encoder stream (we use GetFrame + InputJpege instead)
     */
    if (!isLargeImage_) {
        if (!stream_->start()) {
            Logger::log(LogLevel::ERROR, "snap: stream start failed");
            if (onSnapDone) onSnapDone(false);
            return false;
        }
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
            if (isLargeImage_) {
                nonBlockingResult = this->snap_large_internal(filenames);
            } else {
                nonBlockingResult = this->snap_internal(filenames);
            }
            if (thumbStream_) thumbStream_->stop();
            if (!isLargeImage_) stream_->stop();
            if (onSnapDone) {
                onSnapDone(nonBlockingResult);
            }
        });
        return true;
    } else {
        if (isLargeImage_) {
            result = snap_large_internal(filenames);
        } else {
            result = snap_internal(filenames);
        }

        if (thumbStream_) thumbStream_->stop();
        if (!isLargeImage_) stream_->stop();

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

bool ImageSnap::snap_large_internal(const std::vector<std::string> &filenames)
{
#ifndef SIMULATION_MODE
    Logger::log(LogLevel::INFO, "snap_large_internal: capturing %zu images", filenames.size());

    /* Enable FrameSource CH0 to get NV12 frames */
    IMP_FrameSource_EnableChn(SNAP_SENSOR_ID);

    for (auto& filename : filenames) {
        /* Get NV12 frame from CH0 (sensor resolution, no IVDC) */
        IMPFrameInfo *frame = nullptr;
        if (IMP_FrameSource_GetFrame(SNAP_SENSOR_ID, &frame) < 0) {
            Logger::log(LogLevel::ERROR, "snap_large_internal: GetFrame failed");
            IMP_FrameSource_DisableChn(SNAP_SENSOR_ID);
            return false;
        }

        int srcW = frame->width;
        int srcH = frame->height;
        int dstW = 0, dstH = 0;
        params.getImageSize(dstW, dstH);
        const uint8_t* srcData = reinterpret_cast<const uint8_t*>(frame->virAddr);

        Logger::log(LogLevel::INFO, "snap_large_internal: src=%dx%d dst=%dx%d", srcW, srcH, dstW, dstH);

        /* Allocate contiguous NV12 buffer for resized image (required by InputJpege) */
        size_t resizeSize = static_cast<size_t>(dstW) * dstH * 3 / 2;
        uint8_t* resizeBuf = static_cast<uint8_t*>(IMP_Encoder_VbmAlloc(resizeSize, 256));
        if (!resizeBuf) {
            Logger::log(LogLevel::ERROR, "snap_large_internal: VbmAlloc(%zu) failed", resizeSize);
            IMP_FrameSource_ReleaseFrame(SNAP_SENSOR_ID, frame);
            IMP_FrameSource_DisableChn(SNAP_SENSOR_ID);
            return false;
        }
        memset(resizeBuf, 0, resizeSize);

        /* CPU SIMD resize entire frame */
        int ret = opencv_resize_crop_simd(const_cast<uint8_t*>(srcData), srcW, srcH,
                                           resizeBuf, dstW, dstH);
        IMP_FrameSource_ReleaseFrame(SNAP_SENSOR_ID, frame);
        if (ret != 0) {
            Logger::log(LogLevel::ERROR, "snap_large_internal: SIMD resize failed ret=%d", ret);
            IMP_Encoder_VbmFree(resizeBuf);
            IMP_FrameSource_DisableChn(SNAP_SENSOR_ID);
            return false;
        }

        /* Hardware JPEG encode the full frame */
        size_t jpegBufSize = resizeSize;
        uint8_t* jpegBuf = static_cast<uint8_t*>(malloc(jpegBufSize));
        if (!jpegBuf) {
            IMP_Encoder_VbmFree(resizeBuf);
            IMP_FrameSource_DisableChn(SNAP_SENSOR_ID);
            return false;
        }

        int jpegLen = 0;
        ret = IMP_Encoder_InputJpege(resizeBuf, jpegBuf, dstW, dstH, 85, &jpegLen);
        IMP_Encoder_VbmFree(resizeBuf);
        if (ret != 0 || jpegLen <= 0) {
            Logger::log(LogLevel::ERROR, "snap_large_internal: InputJpege failed ret=%d len=%d", ret, jpegLen);
            free(jpegBuf);
            IMP_FrameSource_DisableChn(SNAP_SENSOR_ID);
            return false;
        }

        /* Write JPEG to file */
        FILE* fp = fopen(filename.c_str(), "wb");
        if (!fp) {
            Logger::log(LogLevel::ERROR, "snap_large_internal: open %s failed", filename.c_str());
            free(jpegBuf);
            IMP_FrameSource_DisableChn(SNAP_SENSOR_ID);
            return false;
        }
        fwrite(jpegBuf, 1, jpegLen, fp);
        fclose(fp);
        free(jpegBuf);

        Logger::log(LogLevel::INFO, "snap_large_internal: saved %s (%d bytes)", filename.c_str(), jpegLen);

        /* Save photo metadata to media_file.db */
        {
            MetadataDao dao;
            MediaItem item;
            item.filePath = filename;
            item.type = 1; /* Photo */
            item.timestamp = time(NULL);
            item.fileSize = jpegLen;
            item.width = dstW;
            item.height = dstH;
            if (dao.addMedia(item)) {
                Logger::log(LogLevel::INFO, "snap_large_internal: saved to DB: %s", filename.c_str());
            } else {
                Logger::log(LogLevel::WARNING, "snap_large_internal: addMedia failed for %s", filename.c_str());
            }
        }

        /* Capture thumbnail from CH2 */
        capture_thumbnail();
    }

    IMP_FrameSource_DisableChn(SNAP_SENSOR_ID);
    return true;
#else
    return false;
#endif
}

bool ImageSnap::snapLargeStrip(const std::string &filename, int quality, bool raw) {
#ifndef SIMULATION_MODE
    if (!initialized) {
        Logger::log(LogLevel::ERROR, "snapLargeStrip: not initialized");
        return false;
    }
    int dstW = 0, dstH = 0;
    params.getImageSize(dstW, dstH);

    /* Enable CH0 (sensor native NV12, no IVDC) so LargeImageSnap can GetFrame */
    if (IMP_FrameSource_EnableChn(SNAP_SENSOR_ID) < 0) {
        Logger::log(LogLevel::ERROR, "snapLargeStrip: EnableChn(%d) failed", SNAP_SENSOR_ID);
        return false;
    }
    usleep(2 * 1000 * 1000);  /* let ISP auto-exposure converge before capturing */

    LargeImageSnap largeSnap;
    bool ok = raw ? largeSnap.snapRaw(filename, quality)
                  : largeSnap.snapLarge(filename, dstW, dstH, quality);

    IMP_FrameSource_DisableChn(SNAP_SENSOR_ID);
    Logger::log(LogLevel::INFO, "snapLargeStrip: %s dst=%dx%d q=%d -> %s",
                filename.c_str(), dstW, dstH, quality, ok ? "OK" : "FAIL");
    return ok;
#else
    (void)filename;
    (void)quality;
    (void)raw;
    return false;
#endif
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
