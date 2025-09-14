#ifndef MEDIA_RTSP_H
#define MEDIA_RTSP_H

#ifdef __cplusplus
extern "C" {
#endif
#include <unistd.h>
#include <stdint.h>
typedef enum {
    FUNC_ID_PULL_VIDEO_FRAME = 0,
    FUNC_ID_PULL_AUDIO_FRAME,
    FUNC_ID_RELEASE_VIDEO_FRAME,
    FUNC_ID_ON_SESSION_CLOSED,
    FUNC_ID_MAX
}func_id_t;

enum {
    CODEC_H264 = 0,
    CODEC_H265,
    CODEC_MAX,
};
enum {
    RTSP_SERVER_PARAM_VIDEO_SPS = 0,
    RTSP_SERVER_PARAM_VIDEO_PPS,
};

struct RTPExtenHeader
{
    uint8_t  creationTimeMsecHigh;
    uint8_t  creationTimeMsecLow;
    uint8_t  ehl1;
    uint8_t  ehl0;
    uint32_t creationTimeSec;
    uint32_t frameIndex;
    uint32_t frameSize;
};

struct rtsp_server_param {
    int port;
    int video_enable;
    int video_codec;
    int video_fps;
    int video_sample_rate;
    int video_stream_id;
    uint8_t video_sps[100];
    size_t video_sps_len;
    uint8_t video_pps[100];
    size_t video_pps_len;

    int audio_enable;
    int audio_sample_rate;
    int audio_stream_id;
    int audio_samples_per_packet;
};

void* create_server(const struct rtsp_server_param *param);
int set_server_param(void *server, int param_id, void *param, size_t size);
int start_server(void *server);
int stop_server(void *server);
int destroy_server(void *server);
int is_server_running(void *server);
typedef int (*func_t)(void **data, size_t *size);
int register_function(void *server, func_id_t id, func_t func);


#ifdef __cplusplus
}
#endif

#endif //MEDIA_RTSP_H
