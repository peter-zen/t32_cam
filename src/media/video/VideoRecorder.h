#ifndef VIDEO_RECORDER_H
#define VIDEO_RECORDER_H

#include <memory>
#include <string>
#include <vector>
#include <thread>
#include <functional>
#include <pthread.h>
#include <queue>
#include <sys/types.h>
#include <condition_variable>
#include "VideoParams.h"
#include "AudioParams.h"
#include "IAudio.h"
#include "IVideo.h"
#include "HalProvider.h"

#define VIDEO_SENSOR_ID 0
#define VIDEO_STREAM_ID 0
namespace media
{
    
class VideoRecorder {
public:
        struct QueuedAudioSample {
                std::vector<uint8_t> data;
                uint64_t timestamp;
        };
        bool record(const std::string &filename, int duration=0);
	bool record(const std::string &filename, std::function<void(bool)> onRecordDone, int duration=0);
        bool stopRecorder();

        VideoRecorder();
        VideoRecorder(const std::shared_ptr<VideoParams> vidParam, const std::shared_ptr<AudioParams> audParam=nullptr);
        VideoRecorder(const std::shared_ptr<VideoParams> vidParam, const std::shared_ptr<AudioParams> audParam, bool concurrentSnap);
        VideoRecorder(const VideoRecorder &) = default;
        VideoRecorder &operator=(const VideoRecorder &) = default;
        ~VideoRecorder();

        bool captureJpeg(const std::string& filename, int quality);

        // 静态音频数据回调函数
        static void staticAudioDataCallback(const uint8_t* data, size_t size, uint64_t timestamp, bool isKeyFrame, void* userData);

private:
        bool initialize();
        void deinitialize();
        bool record(VideoCodecFormat payloadType, const std::string &filename, int duration=0);
        //video
        bool initVideo();
        bool initJpegStream();
        bool uninitVideo();
        ssize_t getNALSize(uint8_t *buf, ssize_t size);
        //audio
        bool initAudio();
        bool uninitAudio();
        void audioCaptureLoop();
        //others
        bool daynight_switch(bool on);
        std::shared_ptr<VideoParams> vidParam;
        std::shared_ptr<AudioParams> audParam;
        bool initialized;
        std::vector<std::thread> threads;
        bool stopRecording;
        // Audio recording variables
        int audio_track_id;
        bool audioRecording;
        pthread_t audioThreadId;
        std::shared_ptr<hal::IAudio> audio_;
        std::shared_ptr<hal::IAudioStream> audioStream_;
        std::shared_ptr<std::thread> audioThread;
        bool audioThreadRunning;
        std::queue<QueuedAudioSample> audioDataQueue;
        std::mutex audioDataMutex;
        std::condition_variable audioDataCond;
        uint64_t audioTimestamp;
        uint64_t audioCurrentTimestamp;
        int audioSampleRate;
        int audioChannels;
        bool audioIsAac;
        bool audioDsiSet;
        int64_t lastVideoTimestamp;
        std::shared_ptr<hal::IVideo> video_;
        std::shared_ptr<hal::IVideoStream> stream_;
        std::shared_ptr<hal::IVideoStream> jpegStream_;
        bool concurrentSnapEnabled_ = false;
};
}
#endif
