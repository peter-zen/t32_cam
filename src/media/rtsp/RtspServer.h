#ifndef RTSP_SERVER_H
#define RTSP_SERVER_H

#include <memory>
#include <string>
#include <vector>
#include <thread>
#include <functional>
#include <mutex>
#include <condition_variable>
#include "media_common.h"

#define RTSP_SENSOR_CHN_NUM  1
#define PPS_SPS_IN_SDP 1
namespace media
{
#define FIFO_MAX_FRAMES 5

typedef struct {
    void *buffer;
    size_t buffer_size;
    size_t used_size;
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
        static int pullFrame(void **data, size_t *size);
        static int releaseFrame(void **data, size_t *size);
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
        bool sensorFilter(int index);
        bool start(int chnNum, int payloadType);
        bool extractSpsPps(const uint8_t* h264Data, size_t dataSize, std::vector<uint8_t>& sps, std::vector<uint8_t>& pps);
        bool initialized;
        bool pullFrameThreadRun;
        std::shared_ptr<std::thread> pullFrameThread;
        void *rtsp_server;
        bool alreadyGetSpsPps;
    private:
        static std::function<void(void)> onSessionClosedCallback;
        static int onSessionClosed(void **data, size_t *size);
        static frame_fifo_t frame_fifo;
        static size_t frame_buffer_size;
        static const size_t DEFAULT_FRAME_BUFFER_SIZE = 1024 * 300;
};
}
#endif