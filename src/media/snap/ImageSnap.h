#ifndef IMAGE_SNAP_H
#define IMAGE_SNAP_H

#include <memory>
#include <string>
#include <vector>
#include <thread>
#include <functional>
#include "IVideo.h"
#include "HalProvider.h"

#include <cstdint>

#define SNAP_SENSOR_ID  0
#define SNAP_STREAM_ID  0
#define THUMB_STREAM_ID 2  /* CH2: hardware scaler for thumbnail */

namespace media
{
class ImageSnapParams {
	public:
	ImageSnapParams();
	ImageSnapParams(const ImageSnapParams &other)=default;
	~ImageSnapParams()=default;

	void setImageSize(int width, int height);
	void getImageSize(int &width, int &height) const;

	// Sensor-native resolution to configure CH0 on the large-image (>8M) path:
	// the IMP sensor channel cannot be set above sensor resolution, so CH0
	// outputs sensor-native NV12 and LargeImageSnap upscales in software.
	void setSensorNativeSize(int width, int height);
	void getSensorNativeSize(int &width, int &height) const;

	int getFrameSourceChnNum() const;
	void setFrameSourceChnNum(int nchannels);

	// Thumbnail capture (CH2 hardware scaler) is on by default for back-compat.
	// Disable to skip the CH2 stream entirely (frees the sensor channel + the
	// per-photo thumbnail capture), e.g. for thumbnail-less burst capture.
	void setThumbnailEnabled(bool enabled);
	bool isThumbnailEnabled() const;

	// AE ready wait (opt-in, default off): wait for auto-exposure convergence
	// before capturing the first frame. Uses ae_converged fast-path and
	// ae_mean-vs-target settling (6 consecutive in-tolerance frames, 3s timeout).
	void setAEReadyWait(bool v) { aeReadyWait_ = v; }
	bool isAEReadyWait() const { return aeReadyWait_; }

	private:
    int sleepTime;
	int nchannels;
	int width;
	int height;
	int sensorW;
	int sensorH;
	bool enableThumbnail;
	bool aeReadyWait_ = false;
};

class ImageSnap {
    public:
        bool setParams(const ImageSnapParams &params);
        bool snap(const std::string &filename);
        bool snap(const std::vector<std::string> &filenames);
		bool snap(const std::string &filename, std::function<void(bool)> onSnapDone);
        bool snap(const std::vector<std::string> &filenames, std::function<void(bool)> onSnapDone);

        // Capture a large (>8M) JPEG via strip stitching (LargeImageSnap). The
        // target size is taken from ImageSnapParams (set >3840 or >2160).
        // Hardware only — returns false in simulation builds.
        bool snapLargeStrip(const std::string &filename, int quality, bool raw = false);

        // Get the thumbnail JPEG data captured during the last snap()
        const std::vector<uint8_t>& getThumbnailData() const { return thumbData_; }
        bool hasThumbnail() const { return !thumbData_.empty(); }
        void clearThumbnail() { thumbData_.clear(); }

        ImageSnap();
        ImageSnap(const ImageSnapParams &params);
        ImageSnap(const ImageSnap &) = default;
        ImageSnap &operator=(const ImageSnap &) = default;
        ~ImageSnap();

    private:
        bool initialize();
        void deinitialize();
        bool snap_internal(const std::vector<std::string> &filenames);
        bool snap_large_internal(const std::vector<std::string> &filenames);
        // >8M burst: capture N sensor NV12 frames to a sdcard temp dir first
        // (software scale-up can't keep up with burst rate), then sequentially
        // strip-scale+JPEG-encode each to its target path.
        bool snap_large_burst_internal(const std::vector<std::string> &filenames);
        bool capture_thumbnail();
        bool daynight_switch(bool on);
        ImageSnapParams params;
        bool initialized;
        bool isLargeImage_;  /* true when target > 8M: CH0 no IVDC, GetFrame+SIMD */
        std::vector<std::thread> threads;
        std::shared_ptr<hal::IVideo> video_;
        std::shared_ptr<hal::IVideoStream> stream_;
        std::shared_ptr<hal::IVideo> thumbVideo_;
        std::shared_ptr<hal::IVideoStream> thumbStream_;
        std::vector<uint8_t> thumbData_;
};
}
#endif
