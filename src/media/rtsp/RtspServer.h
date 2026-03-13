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
#include "HalProvider.h"
#include "MediaSession.h"
#include "IAudio.h"

#define RTSP_SENSOR_ID 0
#define RTSP_STREAM_ID 1
#define PPS_SPS_IN_SDP 1
namespace media
{

class RtspServer {
    public:
        static std::shared_ptr<RtspServer> getInstance();
        static int pullFrame(void **data, size_t *size, uint64_t *timestamp);
        static int releaseFrame(void **data, size_t *size, uint64_t *timestamp);
        static int pullAudioFrame(void **data, size_t *size, uint64_t *timestamp);
        static int releaseAudioFrame(void **data, size_t *size, uint64_t *timestamp);
        static void registerOnsessionClosedCallback(std::function<void(void)> callback);
        void setPort(int port);
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
        std::shared_ptr<MediaSession> videoSession_;
        std::shared_ptr<MediaSession> audioSession_;
        int audioSampleRate_;
        int audioNumPerFrame_;
        int port_;
        bool enableAudio_;
        bool streamingEnabled_;
};
}
#endif
