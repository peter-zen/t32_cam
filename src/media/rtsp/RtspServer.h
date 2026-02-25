#ifndef RTSP_SERVER_H
#define RTSP_SERVER_H

#include <memory>
#include <string>
#include <vector>
#include <queue>
#include <thread>
#include <functional>
#include <mutex>
#include <condition_variable>
#include "IVideo.h"
#include "HalFactory.h"
#include "AudioRecorder.h"
#include "AudioParams.h"

#define RTSP_SENSOR_ID 0
#define RTSP_STREAM_ID 1
#define PPS_SPS_IN_SDP 1
namespace media
{
#define FIFO_MAX_FRAMES 5

typedef struct {
    void *buffer;
    size_t buffer_size;
    size_t used_size;
    uint64_t pts;
} frame_buffer_t;

typedef struct {
    frame_buffer_t frames[FIFO_MAX_FRAMES];
    int head;
    int tail;
    int count;
    std::mutex mutex;
    std::condition_variable not_full;
    std::condition_variable not_empty;
} frame_fifo_t;

class RtspServer {
    public:
        static std::shared_ptr<RtspServer> getInstance();
        static int pullFrame(void **data, size_t *size, uint64_t *timestamp);
        static int releaseFrame(void **data, size_t *size, uint64_t *timestamp);
        static int pullAudioFrame(void **data, size_t *size, uint64_t *timestamp);
        static int releaseAudioFrame(void **data, size_t *size, uint64_t *timestamp);
        static void staticAudioDataCallback(const uint8_t* data, size_t size, uint64_t timestamp, bool isKeyFrame, void* userData);
        static void registerOnsessionClosedCallback(std::function<void(void)> callback);
        bool start();
        bool stop();
        bool isRunning();

    public:
        ~RtspServer();

    private:
        RtspServer();
    RtspServer(const RtspServer &) = default;
    RtspServer &operator=(const RtspServer &) = default;

    private:
        bool initialize();
        void deinitialize();
        bool initVideo();
        bool uninitVideo();
        bool initAudio();
        bool uninitAudio();
        bool sensorFilter(int index);
        bool start_internal();
        bool extractSpsPps(const uint8_t* h264Data, size_t dataSize, std::vector<uint8_t>& sps, std::vector<uint8_t>& pps);
        bool daynight_switch(bool on);
        bool initialized;
        bool pullFrameThreadRun;
        std::shared_ptr<std::thread> pullFrameThread;
        void *rtsp_server;
        bool alreadyGetSpsPps;
    private:
        static std::function<void(void)> onSessionClosedCallback;
        static int onSessionClosed(void **data, size_t *size, uint64_t *timestamp);
        static int onSessionPlay(void **data, size_t *size, uint64_t *timestamp);
        static frame_fifo_t frame_fifo;
        static size_t frame_buffer_size;
        static const size_t DEFAULT_FRAME_BUFFER_SIZE = 1024 * 300;
        std::shared_ptr<hal::IVideo> video_;
        std::shared_ptr<hal::IVideoStream> stream_;
        std::shared_ptr<IAudioRecorder> audioRecorder_;
        struct AudioQueued {
            std::vector<uint8_t> data;
            uint64_t timestamp_ms;
        };
        std::queue<AudioQueued> audioDataQueue_;
        std::mutex audioDataMutex_;
        std::vector<uint8_t> audioWorkBuf_;
        int audioSampleRate_;
        int audioNumPerFrame_;
        bool audioRecording_;
        bool enableAudio_;
        bool streamingEnabled_;
};
}
#endif
