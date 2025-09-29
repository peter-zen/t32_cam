#ifndef VIDEO_RECORDER_H
#define VIDEO_RECORDER_H

#include <memory>
#include <string>
#include <vector>
#include <thread>
#include <functional>
#include "media_common.h"
#define VIDEO_RECORDER_CHN_NUM 0
namespace media
{
class VideoRecorderParams {
	public:
	VideoRecorderParams();
	VideoRecorderParams(const VideoRecorderParams &other)=default;
	~VideoRecorderParams()=default;

	void setVideoSize(int width, int height);
	void getVideoSize(int &width, int &height) const;

    void setFps(int fps);
    int getFps() const;

    void setBitrate(int bitrate);
    int getBitrate() const;

	int getFrameSourceChnNum() const;
	void setFrameSourceChnNum(int nchannels);

	private:
    int sleepTime;
	int nchannels;
	int width;
	int height;
    int fps;
    int bitrate;
};

class VideoRecorder {
    public:
        bool setParams(const VideoRecorderParams &params);
        bool record(const std::string &filename, int duration=0);
		bool record(const std::string &filename, std::function<void(bool)> onRecordDone, int duration=0);
        bool stopRecorder();

        VideoRecorder();
        VideoRecorder(const VideoRecorderParams &params);
        VideoRecorder(const VideoRecorder &) = default;
        VideoRecorder &operator=(const VideoRecorder &) = default;
        ~VideoRecorder();

    private:
        bool initialize();
        void deinitialize();
        bool record(int chnNum, int payloadType, const std::string &filename, int duration=0);
        bool initVideo();
        bool uninitVideo();
        bool sensorFilter(int index);
        ssize_t getNALSize(uint8_t *buf, ssize_t size);
        bool daynight_switch(bool on);
        VideoRecorderParams params;
        bool initialized;
        std::vector<std::thread> threads;
        bool stopRecording;
};
}
#endif