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
// T29 — RTSP_STREAM_ID is now a RUNTIME decision (see rtspStreamIdForCaps in
// RtspServer.cpp). This macro is kept only as the default (multi-cap / legacy =
// CH1). Pure-preview single-stream ({um_live} without um_rec) consumes CH0 at
// runtime so the RTSP sink matches the FS CH0-Scaler-720p single-stream build
// (design um-capability-advertising §3.6, OOM fix). initVideo calls
// rtspStreamIdForCaps() instead of using this macro directly.
#define RTSP_STREAM_ID 1
#define PPS_SPS_IN_SDP 1
namespace media
{

class RtspServer {
    public:
        static std::shared_ptr<RtspServer> getInstance();
        static int pullFrame(void **data, size_t *size, uint64_t *timestamp);
        static int releaseFrame(void **data, size_t *size, uint64_t *timestamp);
        static int queryVideoDepth(void **data, size_t *size, uint64_t *timestamp);  // *size <- FIFO depth（自适应步速）
        static int pullAudioFrame(void **data, size_t *size, uint64_t *timestamp);
        static int releaseAudioFrame(void **data, size_t *size, uint64_t *timestamp);
        static void registerOnsessionClosedCallback(std::function<void(void)> callback);
        static void registerOnsessionPlayCallback(std::function<void(void)> callback);
        void setPort(int port);
        bool start();
        bool stop();
        bool isRunning();

        // Process-level teardown. Distinct from session-level stop() (which is
        // reused by onSessionClosed/~RtspServer and only halts the RTSP server
        // + media sessions). shutdown() additionally releases the HAL (via
        // deinitialize() -> uninitVideo() -> ~IngenicVideoStream +
        // video_->exit()) so IMP encoder channel/group/bind and ISP/OSD region
        // are torn down before the process is frozen by Misc::poweroff()/while(1).
        // Idempotent and null-safe: safe to call when uninitialized or repeatedly.
        void shutdown();

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
        bool deinitialized_;
        bool pullFrameThreadRun;
        std::shared_ptr<std::thread> pullFrameThread;
        void *rtsp_server;
        bool alreadyGetSpsPps;
    private:
        static std::function<void(void)> onSessionClosedCallback;
        static std::function<void(void)> onSessionPlayCallback;
        static int onSessionClosed(void **data, size_t *size, uint64_t *timestamp);
        static int onSessionPlay(void **data, size_t *size, uint64_t *timestamp);
        std::shared_ptr<hal::IVideo> video_;
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
