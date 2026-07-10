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
#include <sys/stat.h>
#include "Common.h"
#include "Logger.h"
#include "Jpeg.h"
#include <vector>
#include <thread>
#include <chrono>
#include "DayNightSwitch.h"
#include "MetadataDao.h"
#include "HalProvider.h"
#include "IVideo.h"

#ifndef SIMULATION_MODE
#include <imp/imp_framesource.h>
#include <imp/imp_encoder.h>
#include "LargeImageSnap.h"
#endif


using namespace media;

ImageSnapParams::ImageSnapParams()
{
    this->nchannels = 1;
    this->width = 1920;
    this->height = 1080;
    this->sensorW = 0;
    this->sensorH = 0;
    this->enableThumbnail = true;
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

void ImageSnapParams::setThumbnailEnabled(bool enabled)
{
    this->enableThumbnail = enabled;
}

bool ImageSnapParams::isThumbnailEnabled() const
{
    return this->enableThumbnail;
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

    /* CH0 (framesource) CANNOT be configured above sensor-native: doing so
     * corrupts the sensor channel (segfault in SetChnAttr, then in
     * IMP_Encoder_PollingStream on the first frame). Confirmed 2026-06-22:
     * snap at 3840x2160 (> sensor 2560x1440) -> polling segfault; at 2560x1440
     * (sensor-native) -> OK. So ANY target exceeding sensor-native must capture
     * at sensor-native and upscale in software (the LargeImageSnap path) — not
     * just >8M. Targets <= sensor-native use the HW encoder path (down/native).
     * (≤8M HW UP-scale above sensor is not supported by this JPEG IVDC path.) */
    int sw = 0, sh = 0;
    params.getSensorNativeSize(sw, sh);
    if (sw <= 0 || sh <= 0) { sw = 2560; sh = 1440; }  /* gc4653 default (T32) */
    isLargeImage_ = (w > sw || h > sh);

    int cfgW = w, cfgH = h;
    if (isLargeImage_) {
        cfgW = sw;
        cfgH = sh;
    }
    Logger::log(LogLevel::INFO, "initialize: target=%dx%d sensor=%dx%d ch0cfg=%dx%d isLargeImage=%d",
                w, h, sw, sh, cfgW, cfgH, isLargeImage_ ? 1 : 0);

    video_ = hal::HalProvider::sharedVideo();   // 进程级单例（Slice 1b：与 VideoRecorder/preview 共享），已 init；不再 createVideo/init
    if (!video_) {
        Logger::log(LogLevel::ERROR, "initialize: sharedVideo null");
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

    /* Thumbnail JPEG stream: CH2 (hardware scaler, 320 wide, IVDC) — skipped
     * entirely when the caller disabled thumbnail capture (frees the CH2 sensor
     * channel, not just the per-photo capture). */
    if (params.isThumbnailEnabled()) {
        thumbVideo_ = hal::HalProvider::sharedVideo();   // 同一进程级单例（已 init）
        if (thumbVideo_) {
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
    } else {
        Logger::log(LogLevel::INFO, "initialize: thumbnail disabled by config (CH2 not opened)");
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
        // 不调 video_/thumbVideo_->exit()：它们是进程级 sharedVideo() 单例（与
        // VideoRecorder 共享），进程内永不 IMP_System_Exit（否则 cm==1 photo→record 的
        // exit→re-Init 会 kernel wedge）。channel 级释放由 stream_/thumbStream_ 析构
        //（~IngenicVideoStream → DestroyChn）完成；单例 ref 在 ~ImageSnap 自然减一。
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
                if (filenames.size() > 1) {
                    nonBlockingResult = this->snap_large_burst_internal(filenames);
                } else {
                    nonBlockingResult = this->snap_large_internal(filenames);
                }
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
            /* >8M: single → strip encode in one go; burst → temp-buffer flow
             * (capture N NV12 to sdcard first, then sequential strip encode). */
            if (filenames.size() > 1) {
                result = snap_large_burst_internal(filenames);
            } else {
                result = snap_large_internal(filenames);
            }
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

    // AE ready wait (opt-in): wait for auto-exposure convergence before
    // capturing the first frame. Uses ae_converged fast-path and ae_mean-vs-target
    // settling (6 consecutive in-tolerance frames, 3s timeout).
    // Default off — only quickSnap enables this; snap_test/wm are unaffected.
    if (params.isAEReadyWait()) {
        const int AE_TIMEOUT_MS  = 3000;
        const int AE_POLL_MS     = 50;
        const int AE_SETTLE_NEED = 6;
        const int AE_MEAN_TOL    = 20;

        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(AE_TIMEOUT_MS);
        int settle = 0;
        int waited = 0;
        bool aeReady = false;

        Logger::log(LogLevel::INFO, "AE wait: starting (timeout=%dms settle=%d tol=%d)",
                    AE_TIMEOUT_MS, AE_SETTLE_NEED, AE_MEAN_TOL);

        while (std::chrono::steady_clock::now() < deadline) {
            // Drain one frame to advance the ISP pipeline
            hal::VideoEncodedFrame drainFrame{};
            if (stream_->polling(AE_POLL_MS) && stream_->getFrame(drainFrame)) {
                stream_->releaseFrame(drainFrame);
            }

            if (!stream_->getInfo(info)) {
                Logger::log(LogLevel::WARNING, "AE wait: getInfo failed @%dms", waited);
                waited += AE_POLL_MS;
                continue;
            }

            // Fast-path: ISP reports AE stable
            if (info.ae_converged) {
                aeReady = true;
                Logger::log(LogLevel::INFO, "AE wait: stable fast-path @%dms", waited);
                break;
            }

            int diff = (info.ae_target == 0) ? 0 : abs((int)info.ae_mean - (int)info.ae_target);
            if (diff < AE_MEAN_TOL) {
                settle++;
            } else {
                settle = 0;
            }

            Logger::log(LogLevel::INFO, "AE: stable=%d mean=%u target=%u diff=%d settled=%d/%d @%dms",
                        info.ae_converged ? 1 : 0, info.ae_mean, info.ae_target, diff, settle, AE_SETTLE_NEED, waited);

            if (settle >= AE_SETTLE_NEED) {
                aeReady = true;
                break;
            }

            waited += AE_POLL_MS;
        }

        if (!aeReady) {
            Logger::log(LogLevel::WARNING, "AE not converged after %dms, capture anyway", waited);
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
        if (fflush(fp) != 0) {
            Logger::log(LogLevel::ERROR, "snap(int): fflush %s failed", filename.c_str());
            fclose(fp);
            return false;
        }
        if (::fileno(fp) >= 0 && ::fsync(::fileno(fp)) != 0) {
            Logger::log(LogLevel::ERROR, "snap(int): fsync %s failed", filename.c_str());
            fclose(fp);
            return false;
        }
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
    Logger::log(LogLevel::INFO, "snap_large_internal: capturing %zu images (strip path)",
                filenames.size());

    int dstW = 0, dstH = 0;
    params.getImageSize(dstW, dstH);

    /* Enable FrameSource CH0 (sensor-native NV12, no IVDC); LargeImageSnap
     * GetFrameEx's from it and strip-stitches to dstWxdstH in a memory-safe
     * per-strip working set (~8MB, vs ~143MB for the dead full-frame path at
     * 48M which cannot fit the 32MB board). One enable covers the whole burst. */
    if (IMP_FrameSource_EnableChn(SNAP_SENSOR_ID) < 0) {
        Logger::log(LogLevel::ERROR, "snap_large_internal: EnableChn(%d) failed", SNAP_SENSOR_ID);
        return false;
    }
    usleep(2 * 1000 * 1000);  /* let ISP auto-exposure converge before capturing */

    LargeImageSnap largeSnap;
    for (auto& filename : filenames) {
        bool ok = largeSnap.snapLarge(filename, dstW, dstH, 85);
        if (!ok) {
            Logger::log(LogLevel::ERROR, "snap_large_internal: snapLarge failed for %s",
                        filename.c_str());
            IMP_FrameSource_DisableChn(SNAP_SENSOR_ID);
            return false;
        }

        struct stat st;
        long fileSize = (::stat(filename.c_str(), &st) == 0) ? (long)st.st_size : -1;
        Logger::log(LogLevel::INFO, "snap_large_internal: saved %s (%ld bytes)",
                    filename.c_str(), fileSize);

        /* Save photo metadata to media_file.db */
        {
            MetadataDao dao;
            MediaItem item;
            item.filePath = filename;
            item.type = 1; /* Photo */
            item.timestamp = time(NULL);
            item.fileSize = fileSize;
            item.width = dstW;
            item.height = dstH;
            if (dao.addMedia(item)) {
                Logger::log(LogLevel::INFO, "snap_large_internal: saved to DB: %s", filename.c_str());
            } else {
                Logger::log(LogLevel::WARNING, "snap_large_internal: addMedia failed for %s",
                            filename.c_str());
            }
        }

        /* Capture thumbnail from CH2 (when enabled — thumbStream_ is null otherwise) */
        capture_thumbnail();
    }

    IMP_FrameSource_DisableChn(SNAP_SENSOR_ID);
    return true;
#else
    return false;
#endif
}


bool ImageSnap::snap_large_burst_internal(const std::vector<std::string> &filenames)
{
#ifndef SIMULATION_MODE
    const int N = static_cast<int>(filenames.size());
    int dstW = 0, dstH = 0;
    params.getImageSize(dstW, dstH);
    const char* tmpDir = "/mnt/sdcard/.snap_tmp";
    ::mkdir(tmpDir, 0777);  /* ignore EEXIST */

    Logger::log(LogLevel::INFO, "snap_large_burst: %d images, dst=%dx%d, temp=%s",
                N, dstW, dstH, tmpDir);

    /* Phase 1 — capture N sensor-native NV12 frames to temp (fast, at sensor
     * rate). Software scale-up can't keep up with a burst, so decouple capture
     * from encode. */
    if (IMP_FrameSource_EnableChn(SNAP_SENSOR_ID) < 0) {
        Logger::log(LogLevel::ERROR, "snap_large_burst: EnableChn(%d) failed", SNAP_SENSOR_ID);
        return false;
    }
    usleep(2 * 1000 * 1000);  /* AE converge */

    int srcW = 0, srcH = 0;
    std::vector<std::string> tmpPaths(N);
    bool captureOk = true;
    for (int i = 0; i < N; i++) {
        IMPFrameInfo *frame = nullptr;
        if (IMP_FrameSource_GetFrameEx(SNAP_SENSOR_ID, &frame) < 0) {
            Logger::log(LogLevel::ERROR, "snap_large_burst: GetFrame %d failed", i);
            captureOk = false;
            break;
        }
        if (i == 0) {
            srcW = frame->width;
            srcH = frame->height;
        }
        size_t nv12Size = static_cast<size_t>(frame->width) * frame->height * 3 / 2;
        char tmpPath[160];
        snprintf(tmpPath, sizeof(tmpPath), "%s/frame_%d.nv12", tmpDir, i);
        FILE* fp = fopen(tmpPath, "wb");
        if (!fp) {
            Logger::log(LogLevel::ERROR, "snap_large_burst: open %s failed", tmpPath);
            IMP_FrameSource_ReleaseFrameEx(SNAP_SENSOR_ID, frame);
            captureOk = false;
            break;
        }
        size_t wr = fwrite(reinterpret_cast<const uint8_t*>(frame->virAddr), 1, nv12Size, fp);
        if (wr == nv12Size) {
            if (fflush(fp) != 0 || (::fileno(fp) >= 0 && ::fsync(::fileno(fp)) != 0)) {
                Logger::log(LogLevel::ERROR, "snap_large_burst: sync %s failed", tmpPath);
                wr = 0;
            }
        }
        fclose(fp);
        IMP_FrameSource_ReleaseFrameEx(SNAP_SENSOR_ID, frame);
        if (wr != nv12Size) {
            Logger::log(LogLevel::ERROR, "snap_large_burst: short write %s (%zu/%zu)",
                        tmpPath, wr, nv12Size);
            captureOk = false;
            break;
        }
        tmpPaths[i] = tmpPath;
        /* Thumbnail for the first frame (CH2, same sensor) — thumbData_ holds
         * one; the caller persists it. */
        if (i == 0) {
            capture_thumbnail();
        }
        Logger::log(LogLevel::INFO, "snap_large_burst: captured frame %d -> %s (%zu bytes)",
                    i, tmpPath, nv12Size);
    }
    IMP_FrameSource_DisableChn(SNAP_SENSOR_ID);

    if (!captureOk || srcW == 0 || srcH == 0) {
        Logger::log(LogLevel::ERROR, "snap_large_burst: capture phase failed; cleaning temps");
        for (auto& p : tmpPaths) {
            if (!p.empty()) ::unlink(p.c_str());
        }
        return false;
    }

    /* Phase 2 — sequentially strip-scale+JPEG-encode each staged NV12 file.
     * Read strip-by-strip from the temp file (snapLargeFromFile) so the full
     * ~5.5MB NV12 frame is NEVER loaded into RAM — only one strip's source rows
     * at a time. That is what keeps the burst within the 32MB board's budget
     * (a full-frame vector tipped it into OOM on memory-tight boots). */
    LargeImageSnap largeSnap;
    bool allOk = true;
    for (int i = 0; i < N; i++) {
        bool ok = largeSnap.snapLargeFromFile(filenames[i], dstW, dstH, 85,
                                              tmpPaths[i], srcW, srcH);
        ::unlink(tmpPaths[i].c_str());  /* temp NV12 consumed */
        if (!ok) {
            Logger::log(LogLevel::ERROR, "snap_large_burst: encode failed for %s",
                        filenames[i].c_str());
            allOk = false;
            continue;
        }

        struct stat st;
        long fileSize = (::stat(filenames[i].c_str(), &st) == 0) ? (long)st.st_size : -1;
        Logger::log(LogLevel::INFO, "snap_large_burst: saved %s (%ld bytes)",
                    filenames[i].c_str(), fileSize);

        MetadataDao dao;
        MediaItem item;
        item.filePath = filenames[i];
        item.type = 1; /* Photo */
        item.timestamp = time(NULL);
        item.fileSize = fileSize;
        item.width = dstW;
        item.height = dstH;
        if (!dao.addMedia(item)) {
            Logger::log(LogLevel::WARNING, "snap_large_burst: addMedia failed for %s",
                        filenames[i].c_str());
        }
    }
    ::rmdir(tmpDir);  /* succeeds only if empty (all temps unlinked) */
    Logger::log(LogLevel::INFO, "snap_large_burst: done, allOk=%d", allOk ? 1 : 0);
    return allOk;
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
