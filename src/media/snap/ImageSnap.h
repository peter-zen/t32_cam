#ifndef IMAGE_SNAP_H
#define IMAGE_SNAP_H

#include <memory>
#include <string>
#include <vector>
#include <thread>
#include <functional>
#include "IVideo.h"
#include "HalProvider.h"

#define SNAP_SENSOR_ID  0
#define SNAP_STREAM_ID  0

namespace media
{
class ImageSnapParams {
	public:
	ImageSnapParams();
	ImageSnapParams(const ImageSnapParams &other)=default;
	~ImageSnapParams()=default;

	void setImageSize(int width, int height);
	void getImageSize(int &width, int &height) const;

	int getFrameSourceChnNum() const;
	void setFrameSourceChnNum(int nchannels);

	private:
    int sleepTime;
	int nchannels;
	int width;
	int height;
};

class ImageSnap {
    public:
        bool setParams(const ImageSnapParams &params);
        bool snap(const std::string &filename);
        bool snap(const std::vector<std::string> &filenames);
		bool snap(const std::string &filename, std::function<void(bool)> onSnapDone);
        bool snap(const std::vector<std::string> &filenames, std::function<void(bool)> onSnapDone);

        ImageSnap();
        ImageSnap(const ImageSnapParams &params);
        ImageSnap(const ImageSnap &) = default;
        ImageSnap &operator=(const ImageSnap &) = default;
        ~ImageSnap();

    private:
        bool initialize();
        void deinitialize();
        bool snap_internal(const std::vector<std::string> &filenames);
        bool daynight_switch(bool on);
        ImageSnapParams params;
        bool initialized;
        std::vector<std::thread> threads;
        std::shared_ptr<hal::IVideo> video_;
        std::shared_ptr<hal::IVideoStream> stream_;
};
}
#endif
