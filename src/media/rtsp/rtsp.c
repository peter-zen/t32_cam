/*
 * A simple RTSP server implementation using libevent [1].
 *
 * To obtain `audio.g711a` and `video.h264`:
 *
 * $ ffmpeg -i http://docs.evostream.com/sample_content/assets/bun33s.mp4 \
 *     -acodec pcm_mulaw -f mulaw -ar 8000 -ac 1 audio.g711a \
 *     -vcodec h264 -x264opts aud=1 video.h264
 *
 * [1] https://libevent.org/
 */
#include "rtsp.h"

#include <smolrtsp.h>
#include <smolrtsp-libevent.h>
#include <elog.h>

// RTSP logging tag for EasyLogger
#define RTSP_LOG_TAG "RTSP"

#include <arpa/inet.h>
#include <netinet/in.h>

#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/event.h>
#include <event2/listener.h>
#include <event2/util.h>

#include <inttypes.h>
#include <pthread.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

// Audio payload types
#define AUDIO_PCMU_PAYLOAD_TYPE  0   // G.711 u-law
#define AUDIO_PCMA_PAYLOAD_TYPE  8   // G.711 A-law
#define AUDIO_L16_PAYLOAD_TYPE   96  // Dynamic PT for L16 (will use 97 to avoid conflict with video)

static bool base64_encode(char *input_data, uint32_t input_data_len, char *output_data, uint32_t *output_data_len);
static bool base64_decode(char *input_data, uint32_t input_data_len, char *output_data, uint32_t *output_data_len);
#define SERVER_PORT SMOLRTSP_DEFAULT_PORT
#define VIDEO_PAYLOAD_TYPE 96 // dynamic PT for video
#define AUDIO_DYN_PAYLOAD_TYPE 97 // dynamic PT for L16 audio
#define MAX_STREAMS 2

typedef struct {
    uint64_t session_id;
    SmolRTSP_RtpTransport *transport;
    struct event *ev;
    SmolRTSP_Droppable ctx;
} Stream;

// RTSP Server structure to hold server resources
struct rtsp_server {
    struct event_base *base;
    struct evconnlistener *listener;
    struct event *sigint_handler;
    struct rtsp_server_param param;
    bool is_running;

    func_t funcs[FUNC_ID_MAX];
};

typedef struct {
    struct event_base *base;
    struct bufferevent *bev;
    struct sockaddr_storage addr;
    size_t addr_len;
    Stream streams[MAX_STREAMS];
    int streams_playing;
    struct rtsp_server *peer;
} Client;

declImpl(SmolRTSP_Controller, Client);

static void listener_cb(
    struct evconnlistener *listener, evutil_socket_t fd, struct sockaddr *sa,
    int socklen, void *ctx);
static void on_event_cb(struct bufferevent *bev, short events, void *ctx);
static void on_sigint_cb(evutil_socket_t sig, short events, void *ctx);

static int setup_transport(
    Client *self, SmolRTSP_Context *ctx, const SmolRTSP_Request *req,
    SmolRTSP_Transport *t);
static int setup_tcp(
    SmolRTSP_Context *ctx, SmolRTSP_Transport *t,
    SmolRTSP_TransportConfig config);
static int setup_udp(
    const struct sockaddr *addr, SmolRTSP_Context *ctx, SmolRTSP_Transport *t,
    SmolRTSP_TransportConfig config);

typedef struct {
    SmolRTSP_RtpTransport *transport;
    size_t i;
    int samples_per_packet;
    int audio_codec;
    int channels;
    int sample_rate;
    struct event *ev;
    struct bufferevent *bev;
    int *streams_playing;
    func_t pull_frame;
    func_t release_frame;
    uint8_t *current_data;
    size_t current_size;
    uint64_t base_timestamp_us;
    uint32_t base_rtp_timestamp;
    bool first_frame;
    uint32_t pace_interval_us;
    uint64_t next_deadline_us;
    int send_count;
    int pull_fail_count;
    int full_log_count;
    uint64_t last_stat_log_us;
} AudioCtx;
static SmolRTSP_Droppable play_audio(
    int sample_rate, int samples_per_packet, int audio_codec, int channels,
    func_t pull_frame, func_t release_frame,
    struct event_base *base, struct bufferevent *bev, SmolRTSP_RtpTransport *t,
    struct event **ev, int *streams_playing);
static void send_audio_packet_cb(evutil_socket_t fd, short events, void *arg);

typedef struct {
    SmolRTSP_NalTransport *transport;
    SmolRTSP_NalStartCodeTester start_code_tester;
    uint32_t timestamp;
    U8Slice99 video;
    int codec;
    uint8_t *nalu_start;
    struct event *ev;
    struct bufferevent *bev;
    int *streams_playing;
    int sample_rate;
    int fps;
    uint64_t au_retry_us;
    bool pace_no_skip;
    func_t pull_frame;
    func_t release_frame;
    bool sps_pps_bypass;
    bool has_extension;
    struct RTPExtenHeader extension;
    uint64_t base_capture_us;
    uint32_t base_rtp_ts;
    bool first_frame;
    bool need_idr;
    uint8_t *current_frame_data;
    size_t current_frame_size;
    uint64_t last_capture_us;
    uint32_t pace_interval_us;
    uint64_t next_deadline_us;
    uint64_t expected_fire_us;
    uint8_t expected_sched_kind;
    int frame_count;
    int pull_fail_count;
    int full_log_count;
    uint64_t last_stat_log_us;
    uint64_t frame_start_us;
    uint32_t frame_nal_count;
    uint32_t frame_cb_count;
    uint64_t frame_done_count;
    uint64_t frame_nal_total;
    uint64_t frame_nal_max;
    uint64_t frame_cb_total;
    uint64_t frame_cb_max;
    uint64_t frame_proc_total_us;
    uint64_t frame_proc_max_us;
    uint64_t cb_exec_total_us;
    uint64_t cb_exec_count;
    uint64_t cb_exec_max_us;
    uint64_t deadline_lag_total_us;
    uint64_t deadline_lag_count;
    uint64_t deadline_lag_max_us;
    uint64_t deadline_lag_over_2ms_count;
    uint64_t pace_lag_total_us;
    uint64_t pace_lag_count;
    uint64_t pace_lag_max_us;
    uint64_t pace_lag_over_2ms_count;
    uint64_t retry_lag_total_us;
    uint64_t retry_lag_count;
    uint64_t retry_lag_max_us;
    uint64_t retry_lag_over_2ms_count;
    uint64_t last_log_frame_done_count;
    uint64_t last_log_frame_nal_total;
    uint64_t last_log_frame_cb_total;
    uint64_t last_log_frame_proc_total_us;
    uint64_t last_log_cb_exec_total_us;
    uint64_t last_log_cb_exec_count;
    uint64_t last_log_deadline_lag_total_us;
    uint64_t last_log_deadline_lag_count;
    uint64_t last_log_deadline_lag_over_2ms_count;
    uint64_t last_log_pace_lag_total_us;
    uint64_t last_log_pace_lag_count;
    uint64_t last_log_pace_lag_over_2ms_count;
    uint64_t last_log_retry_lag_total_us;
    uint64_t last_log_retry_lag_count;
    uint64_t last_log_retry_lag_over_2ms_count;
} VideoCtx;

static SmolRTSP_Droppable play_video(
    int fps, int sample_rate, int codec, func_t pull_frame, func_t release_frame,
    bool sps_pps_bypass, struct event_base *base, struct bufferevent *bev, SmolRTSP_RtpTransport *t,
    struct event **ev, int *streams_playing);
static void send_video_packet_cb(evutil_socket_t fd, short events, void *arg);
static bool send_nalu(VideoCtx *ctx);

static uint64_t monotonic_time_us(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
    }
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
}

static void event_add_after_us(struct event *ev, uint64_t delay_us) {
    if (!ev) {
        return;
    }
    if (delay_us == 0) {
        delay_us = 1;
    }
    struct timeval tv = {
        .tv_sec = (long)(delay_us / 1000000ULL),
        .tv_usec = (suseconds_t)(delay_us % 1000000ULL),
    };
    event_add(ev, &tv);
}

static void schedule_next_deadline(struct event *ev, uint64_t *next_deadline_us, uint32_t interval_us) {
    if (!next_deadline_us) {
        return;
    }
    if (interval_us == 0) {
        interval_us = 1000;
    }

    uint64_t now_us = monotonic_time_us();
    if (*next_deadline_us == 0) {
        *next_deadline_us = now_us + interval_us;
    } else {
        *next_deadline_us += interval_us;
        if (*next_deadline_us <= now_us) {
            uint64_t lag = now_us - *next_deadline_us;
            uint64_t skipped = lag / interval_us + 1;
            *next_deadline_us += skipped * interval_us;
        }
    }

    uint64_t delay_us = (*next_deadline_us > now_us) ? (*next_deadline_us - now_us) : 1;
    event_add_after_us(ev, delay_us);
}

static void schedule_retry_us(struct event *ev, uint64_t retry_us) {
    event_add_after_us(ev, retry_us > 0 ? retry_us : 1000);
}

static uint64_t parse_u64_env_or_default(const char *key, uint64_t default_value) {
    if (!key) {
        return default_value;
    }
    const char *val = getenv(key);
    if (!val || !*val) {
        return default_value;
    }
    char *end = NULL;
    unsigned long long parsed = strtoull(val, &end, 10);
    if (end == val || (end && *end != '\0')) {
        return default_value;
    }
    return (uint64_t)parsed;
}

static void video_ctx_record_cb_exec(VideoCtx *ctx, uint64_t cb_begin_us) {
    if (!ctx || cb_begin_us == 0) {
        return;
    }

    const uint64_t now_us = monotonic_time_us();
    const uint64_t cb_cost_us = (now_us >= cb_begin_us) ? (now_us - cb_begin_us) : 0;
    ctx->cb_exec_total_us += cb_cost_us;
    ctx->cb_exec_count++;
    if (cb_cost_us > ctx->cb_exec_max_us) {
        ctx->cb_exec_max_us = cb_cost_us;
    }
}

#define VIDEO_SCHED_UNKNOWN 0
#define VIDEO_SCHED_PACE 1
#define VIDEO_SCHED_RETRY 2

static void video_schedule_next_deadline(VideoCtx *ctx) {
    if (!ctx) {
        return;
    }
    uint32_t interval_us = ctx->pace_interval_us > 0 ? ctx->pace_interval_us : 1000;
    uint64_t now_us = monotonic_time_us();

    if (ctx->next_deadline_us == 0) {
        ctx->next_deadline_us = now_us + interval_us;
    } else {
        ctx->next_deadline_us += interval_us;
        if (ctx->next_deadline_us <= now_us) {
            if (ctx->pace_no_skip) {
                // Catch up quickly when callback is late: do not skip future pacing slots.
                ctx->next_deadline_us = now_us + 1;
            } else {
                uint64_t lag = now_us - ctx->next_deadline_us;
                uint64_t skipped = lag / interval_us + 1;
                ctx->next_deadline_us += skipped * interval_us;
            }
        }
    }

    uint64_t delay_us = (ctx->next_deadline_us > now_us) ? (ctx->next_deadline_us - now_us) : 1;
    event_add_after_us(ctx->ev, delay_us);
    ctx->expected_fire_us = ctx->next_deadline_us;
    ctx->expected_sched_kind = VIDEO_SCHED_PACE;
}

static void video_schedule_retry(VideoCtx *ctx, uint64_t retry_us) {
    if (!ctx) {
        return;
    }
    const uint64_t use_retry_us = retry_us > 0 ? retry_us : 1000;
    ctx->expected_fire_us = monotonic_time_us() + use_retry_us;
    ctx->expected_sched_kind = VIDEO_SCHED_RETRY;
    schedule_retry_us(ctx->ev, use_retry_us);
}

static void listener_cb(
    struct evconnlistener *listener, evutil_socket_t fd, struct sockaddr *sa,
    int socklen, void *arg) {
    (void)listener;
    (void)fd;
    (void)socklen;
    
    struct rtsp_server *server = (struct rtsp_server *)arg;
    struct event_base *base = server->base;

    struct bufferevent *bev;
    if ((bev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE)) ==
        NULL) {
        elog_e(RTSP_LOG_TAG, "bufferevent_socket_new failed");
        event_base_loopbreak(base);
        return;
    }

    Client *client = calloc(1, sizeof *client);
    assert(client);
    client->base = base;
    client->bev = bev;
    client->peer = server;
    memcpy(&client->addr, sa, socklen);
    client->addr_len = socklen;

    SmolRTSP_Controller controller = DYN(Client, SmolRTSP_Controller, client);
    void *ctx = smolrtsp_libevent_ctx(controller);

    bufferevent_setcb(bev, smolrtsp_libevent_cb, NULL, on_event_cb, ctx);
    bufferevent_enable(bev, EV_READ | EV_WRITE);
}

static void on_event_cb(struct bufferevent *bev, short events, void *ctx) {
    // Get Client from context
    SmolRTSP_Controller controller = smolrtsp_libevent_ctx_controller(ctx);
    // Since we know the controller is a Client, we can cast it directly
    Client *client = (Client *)controller.self;
    struct rtsp_server *server = client ? client->peer : NULL;

    if (events & BEV_EVENT_EOF) {
        elog_i(RTSP_LOG_TAG, "Connection closed");
        // Access server if needed
        if (server && server->funcs[FUNC_ID_ON_SESSION_CLOSED]) {
            server->funcs[FUNC_ID_ON_SESSION_CLOSED](NULL, NULL, NULL);
        }
    } else if (events & BEV_EVENT_ERROR) {
        elog_e(RTSP_LOG_TAG, "Connection error");
        // Access server if needed
        if (server && server->funcs[FUNC_ID_ON_SESSION_CLOSED]) {
            server->funcs[FUNC_ID_ON_SESSION_CLOSED](NULL, NULL, NULL);
        }
    }

    bufferevent_free(bev);
    smolrtsp_libevent_ctx_free(ctx);
}

static void on_sigint_cb(evutil_socket_t sig, short events, void *ctx) {
    (void)sig;
    (void)events;

    struct event_base *base = ctx;

    elog_i(RTSP_LOG_TAG, "Caught interrupt signal; exiting cleanly in two seconds");

    struct timeval delay = {2, 0};
    event_base_loopexit(base, &delay);
}

static void Client_drop(VSelf) {
    VSELF(Client);

    for (size_t i = 0; i < MAX_STREAMS; i++) {
        if (self->streams[i].ctx.vptr != NULL) {
            VCALL(self->streams[i].ctx, drop);
        }
    }

    free(self);
}

impl(SmolRTSP_Droppable, Client);

static void
Client_options(VSelf, SmolRTSP_Context *ctx, const SmolRTSP_Request *req) {
    VSELF(Client);

    (void)self;
    (void)req;

    smolrtsp_header(
        ctx, SMOLRTSP_HEADER_PUBLIC, "DESCRIBE, SETUP, TEARDOWN, PLAY");
    smolrtsp_respond_ok(ctx);
}

static void
Client_describe(VSelf, SmolRTSP_Context *ctx, const SmolRTSP_Request *req) {
    VSELF(Client);

    (void)self;
    (void)req;

    struct rtsp_server_param rtsp_param = self->peer->param;
    elog_i(RTSP_LOG_TAG, "DESCRIBE: video=%d sps_len=%zu pps_len=%zu audio=%d asr=%d",
           rtsp_param.video_enable, rtsp_param.video_sps_len, rtsp_param.video_pps_len,
           rtsp_param.audio_enable, rtsp_param.audio_sample_rate);

    char sdp_buf[1024] = {0};
    SmolRTSP_Writer sdp = smolrtsp_string_writer(sdp_buf);
    ssize_t ret = 0;

    // clang-format off
    SMOLRTSP_SDP_DESCRIBE(
        ret, sdp,
        (SMOLRTSP_SDP_VERSION, "0"),
        (SMOLRTSP_SDP_ORIGIN, "SmolRTSP 3855320066 3855320129 IN IP4 0.0.0.0"),
        (SMOLRTSP_SDP_SESSION_NAME, "SmolRTSP example"),
        (SMOLRTSP_SDP_CONNECTION, "IN IP4 0.0.0.0"),
        (SMOLRTSP_SDP_TIME, "0 0"));

    if (rtsp_param.audio_enable) {
        int audio_pt;
        if (rtsp_param.audio_codec == AUDIO_CODEC_PCMU) {
            audio_pt = AUDIO_PCMU_PAYLOAD_TYPE;
            SMOLRTSP_SDP_DESCRIBE(
                ret, sdp,
                (SMOLRTSP_SDP_MEDIA, "audio 0 RTP/AVP %d", audio_pt),
                (SMOLRTSP_SDP_ATTR, "rtpmap:%d PCMU/%d", audio_pt, rtsp_param.audio_sample_rate),
                (SMOLRTSP_SDP_ATTR, "control:audio"));
        } else if (rtsp_param.audio_codec == AUDIO_CODEC_PCMA) {
            audio_pt = AUDIO_PCMA_PAYLOAD_TYPE;
            SMOLRTSP_SDP_DESCRIBE(
                ret, sdp,
                (SMOLRTSP_SDP_MEDIA, "audio 0 RTP/AVP %d", audio_pt),
                (SMOLRTSP_SDP_ATTR, "rtpmap:%d PCMA/%d", audio_pt, rtsp_param.audio_sample_rate),
                (SMOLRTSP_SDP_ATTR, "control:audio"));
        } else {
            // L16 (Linear PCM 16-bit)
            audio_pt = AUDIO_DYN_PAYLOAD_TYPE;
            SMOLRTSP_SDP_DESCRIBE(
                ret, sdp,
                (SMOLRTSP_SDP_MEDIA, "audio 0 RTP/AVP %d", audio_pt),
                (SMOLRTSP_SDP_ATTR, "rtpmap:%d L16/%d/%d", audio_pt, rtsp_param.audio_sample_rate, rtsp_param.audio_channels),
                (SMOLRTSP_SDP_ATTR, "control:audio"));
        }
    }

    if (rtsp_param.video_enable) {
        if (rtsp_param.video_codec == CODEC_H264) {
            if (rtsp_param.video_sps_len && rtsp_param.video_pps_len) {
                char base64_video_sps[100];
                uint32_t base64_video_sps_len;
                char base64_video_pps[100];
                uint32_t base64_video_pps_len;

                base64_encode((char *)rtsp_param.video_sps, rtsp_param.video_sps_len, (char *)base64_video_sps, &base64_video_sps_len);
                base64_encode((char *)rtsp_param.video_pps, rtsp_param.video_pps_len, (char *)base64_video_pps, &base64_video_pps_len);
                elog_d(RTSP_LOG_TAG, "base64_video_sps: %s", base64_video_sps);
                elog_d(RTSP_LOG_TAG, "base64_video_pps: %s", base64_video_pps);

                SMOLRTSP_SDP_DESCRIBE(
                ret, sdp,
                (SMOLRTSP_SDP_MEDIA, "video 0 RTP/AVP %d", VIDEO_PAYLOAD_TYPE),
                (SMOLRTSP_SDP_ATTR, "control:video"),
                (SMOLRTSP_SDP_ATTR, "rtpmap:%d H264/%" PRIu32, VIDEO_PAYLOAD_TYPE, rtsp_param.video_sample_rate),
                (SMOLRTSP_SDP_ATTR, "fmtp:%d packetization-mode=1;sprop-parameter-sets=%s,%s", VIDEO_PAYLOAD_TYPE, base64_video_sps, base64_video_pps),
                (SMOLRTSP_SDP_ATTR, "framerate:%d", rtsp_param.video_fps));

            } else {
                SMOLRTSP_SDP_DESCRIBE(
                ret, sdp,
                (SMOLRTSP_SDP_MEDIA, "video 0 RTP/AVP %d", VIDEO_PAYLOAD_TYPE),
                (SMOLRTSP_SDP_ATTR, "control:video"),
                (SMOLRTSP_SDP_ATTR, "rtpmap:%d H264/%" PRIu32, VIDEO_PAYLOAD_TYPE, rtsp_param.video_sample_rate),
                (SMOLRTSP_SDP_ATTR, "fmtp:%d packetization-mode=1", VIDEO_PAYLOAD_TYPE),
                (SMOLRTSP_SDP_ATTR, "framerate:%d", rtsp_param.video_fps));
            }
        } else  {
            SMOLRTSP_SDP_DESCRIBE(
            ret, sdp,
            (SMOLRTSP_SDP_MEDIA, "video 0 RTP/AVP %d", VIDEO_PAYLOAD_TYPE),
            (SMOLRTSP_SDP_ATTR, "control:video"),
            (SMOLRTSP_SDP_ATTR, "rtpmap:%d H265/%" PRIu32, VIDEO_PAYLOAD_TYPE, rtsp_param.video_sample_rate),
            (SMOLRTSP_SDP_ATTR, "fmtp:%d packetization-mode=1", VIDEO_PAYLOAD_TYPE),
            (SMOLRTSP_SDP_ATTR, "framerate:%d", rtsp_param.video_fps));
        }
    }
    // clang-format on

    assert(ret > 0);

    smolrtsp_header(ctx, SMOLRTSP_HEADER_CONTENT_TYPE, "application/sdp");
    smolrtsp_body(ctx, CharSlice99_from_str(sdp_buf));

    smolrtsp_respond_ok(ctx);
}

static void
Client_setup(VSelf, SmolRTSP_Context *ctx, const SmolRTSP_Request *req) {
    VSELF(Client);

    SmolRTSP_Transport transport;
    if (setup_transport(self, ctx, req, &transport) == -1) {
        return;
    }

    struct rtsp_server_param rtsp_param = self->peer->param;

    const size_t stream_id =
        CharSlice99_primitive_ends_with(
            req->start_line.uri, CharSlice99_from_str("/audio"))
            ? rtsp_param.audio_stream_id
            : rtsp_param.video_stream_id;
    Stream *stream = &self->streams[stream_id];

    const bool aggregate_control_requested = SmolRTSP_HeaderMap_contains_key(
        &req->header_map, SMOLRTSP_HEADER_SESSION);
    if (aggregate_control_requested) {
        uint64_t session_id;
        if (smolrtsp_scanf_header(
                &req->header_map, SMOLRTSP_HEADER_SESSION, "%" SCNu64,
                &session_id) != 1) {
            smolrtsp_respond(
                ctx, SMOLRTSP_STATUS_BAD_REQUEST, "Malformed `Session'");
            return;
        }

        stream->session_id = session_id;
    } else {
        stream->session_id = (uint64_t)rand();
    }
    elog_i(RTSP_LOG_TAG, "SETUP: stream_id=%zu session_id=%" PRIu64, stream_id, stream->session_id);

    if (rtsp_param.audio_stream_id == stream_id) {
        int audio_pt;
        if (rtsp_param.audio_codec == AUDIO_CODEC_PCMU) {
            audio_pt = AUDIO_PCMU_PAYLOAD_TYPE;
        } else if (rtsp_param.audio_codec == AUDIO_CODEC_PCMA) {
            audio_pt = AUDIO_PCMA_PAYLOAD_TYPE;
        } else {
            audio_pt = AUDIO_DYN_PAYLOAD_TYPE;
        }
        stream->transport = SmolRTSP_RtpTransport_new(
            transport, audio_pt, rtsp_param.audio_sample_rate);
        elog_i(RTSP_LOG_TAG, "SETUP: audio transport created pt=%d sr=%d", audio_pt, rtsp_param.audio_sample_rate);
    } else {
        stream->transport = SmolRTSP_RtpTransport_new(
            transport, VIDEO_PAYLOAD_TYPE, rtsp_param.video_sample_rate);
        elog_i(RTSP_LOG_TAG, "SETUP: video transport created pt=%d sr=%d", VIDEO_PAYLOAD_TYPE, rtsp_param.video_sample_rate);
    }

    smolrtsp_header(
        ctx, SMOLRTSP_HEADER_SESSION, "%" PRIu64, stream->session_id);

    smolrtsp_respond_ok(ctx);
}

static void
Client_play(VSelf, SmolRTSP_Context *ctx, const SmolRTSP_Request *req) {
    VSELF(Client);

    uint64_t session_id;
    if (smolrtsp_scanf_header(
            &req->header_map, SMOLRTSP_HEADER_SESSION, "%" SCNu64,
            &session_id) != 1) {
        smolrtsp_respond(
            ctx, SMOLRTSP_STATUS_BAD_REQUEST, "Malformed `Session'");
        return;
    }
    struct rtsp_server_param rtsp_param = self->peer->param;
    elog_i(RTSP_LOG_TAG, "PLAY: session=%" PRIu64, session_id);

    if (self->peer && self->peer->funcs[FUNC_ID_ON_SESSION_PLAY]) {
        self->peer->funcs[FUNC_ID_ON_SESSION_PLAY](NULL, NULL, NULL);
    }

    bool played = false;
    for (size_t i = 0; i < MAX_STREAMS; i++) {
        if (self->streams[i].session_id == session_id) {
            if (rtsp_param.audio_stream_id == i) {
                elog_i(RTSP_LOG_TAG, "PLAY: select audio stream_id=%zu", i);
                self->streams[i].ctx = play_audio(
                    rtsp_param.audio_sample_rate, rtsp_param.audio_samples_per_packet,
                    rtsp_param.audio_codec, rtsp_param.audio_channels,
                    self->peer->funcs[FUNC_ID_PULL_AUDIO_FRAME],
                    self->peer->funcs[FUNC_ID_RELEASE_AUDIO_FRAME],
                    self->base, self->bev, self->streams[i].transport,
                    &self->streams[i].ev, &self->streams_playing);
            } else {
                elog_i(RTSP_LOG_TAG, "PLAY: select video stream_id=%zu", i);
                self->streams[i].ctx = play_video(
                    self->peer->param.video_fps,
                    self->peer->param.video_sample_rate,
                    self->peer->param.video_codec,
                    self->peer->funcs[FUNC_ID_PULL_VIDEO_FRAME],
                    self->peer->funcs[FUNC_ID_RELEASE_VIDEO_FRAME],
                    (self->peer->param.video_sps_len && self->peer->param.video_pps_len),
                    self->base, self->bev, self->streams[i].transport,
                    &self->streams[i].ev, &self->streams_playing);
            }

            played = true;
        }
    }

    if (!played) {
        elog_w(RTSP_LOG_TAG, "PLAY: session not found %" PRIu64, session_id);
        smolrtsp_respond(
            ctx, SMOLRTSP_STATUS_SESSION_NOT_FOUND, "Invalid Session ID");
        return;
    }

    //smolrtsp_header(ctx, SMOLRTSP_HEADER_RANGE, "npt=now-");
    smolrtsp_header(ctx, SMOLRTSP_HEADER_RANGE, "npt=0.000-");
    smolrtsp_header(ctx, SMOLRTSP_HEADER_RTP_INFO, "seq=0;rtptime=0");
    smolrtsp_respond_ok(ctx);
}

static void
Client_teardown(VSelf, SmolRTSP_Context *ctx, const SmolRTSP_Request *req) {
    VSELF(Client);

    uint64_t session_id;
    if (smolrtsp_scanf_header(
            &req->header_map, SMOLRTSP_HEADER_SESSION, "%" SCNu64,
            &session_id) != 1) {
        smolrtsp_respond(
            ctx, SMOLRTSP_STATUS_BAD_REQUEST, "Malformed `Session'");
        return;
    }

    bool teardowned = false;
    for (size_t i = 0; i < MAX_STREAMS; i++) {
        if (self->streams[i].session_id == session_id) {
            event_del(self->streams[i].ev);
            teardowned = true;
        }
    }

    if (!teardowned) {
        smolrtsp_respond(
            ctx, SMOLRTSP_STATUS_SESSION_NOT_FOUND, "Invalid Session ID");
        return;
    }

    if (self->peer && self->peer->funcs[FUNC_ID_ON_SESSION_CLOSED]) {
        self->peer->funcs[FUNC_ID_ON_SESSION_CLOSED](NULL, NULL, NULL);
    }

    smolrtsp_respond_ok(ctx);
}

static void
Client_unknown(VSelf, SmolRTSP_Context *ctx, const SmolRTSP_Request *req) {
    VSELF(Client);

    (void)self;
    (void)req;

    smolrtsp_respond(ctx, SMOLRTSP_STATUS_METHOD_NOT_ALLOWED, "Unknown method");
}

static SmolRTSP_ControlFlow
Client_before(VSelf, SmolRTSP_Context *ctx, const SmolRTSP_Request *req) {
    VSELF(Client);

    (void)self;
    (void)ctx;

    elog_i(RTSP_LOG_TAG, "%s %s CSeq=%" PRIu32,
           CharSlice99_alloca_c_str(req->start_line.method),
           CharSlice99_alloca_c_str(req->start_line.uri), req->cseq);

    return SmolRTSP_ControlFlow_Continue;
}

static void Client_after(
    VSelf, ssize_t ret, SmolRTSP_Context *ctx, const SmolRTSP_Request *req) {
    VSELF(Client);

    (void)self;
    (void)ctx;
    (void)req;

    if (ret < 0) {
        elog_e(RTSP_LOG_TAG, "Failed to respond");
    }
}

impl(SmolRTSP_Controller, Client);

static int setup_transport(
    Client *self, SmolRTSP_Context *ctx, const SmolRTSP_Request *req,
    SmolRTSP_Transport *t) {
    CharSlice99 transport_val;
    const bool transport_found = SmolRTSP_HeaderMap_find(
        &req->header_map, SMOLRTSP_HEADER_TRANSPORT, &transport_val);
    if (!transport_found) {
        smolrtsp_respond(
            ctx, SMOLRTSP_STATUS_BAD_REQUEST, "`Transport' not present");
        return -1;
    }

    elog_i(RTSP_LOG_TAG, "SETUP: Transport request=%s",
           CharSlice99_alloca_c_str(transport_val));

    SmolRTSP_TransportConfig config;
    if (smolrtsp_parse_transport(&config, transport_val) == -1) {
        smolrtsp_respond(
            ctx, SMOLRTSP_STATUS_BAD_REQUEST, "Malformed `Transport'");
        return -1;
    }

    switch (config.lower) {
    case SmolRTSP_LowerTransport_TCP:
        elog_i(RTSP_LOG_TAG, "SETUP: transport lower=TCP");
        if (setup_tcp(ctx, t, config) == -1) {
            smolrtsp_respond_internal_error(ctx);
            return -1;
        }
        break;
    case SmolRTSP_LowerTransport_UDP:
        elog_i(RTSP_LOG_TAG, "SETUP: transport lower=UDP");
        if (setup_udp((const struct sockaddr *)&self->addr, ctx, t, config) ==
            -1) {
            smolrtsp_respond_internal_error(ctx);
            return -1;
        }
        break;
    }

    return 0;
}

static int setup_tcp(
    SmolRTSP_Context *ctx, SmolRTSP_Transport *t,
    SmolRTSP_TransportConfig config) {
    ifLet(config.interleaved, SmolRTSP_ChannelPair_Some, interleaved) {
        elog_i(RTSP_LOG_TAG, "SETUP: TCP interleaved=%" PRIu8 "-%" PRIu8,
               interleaved->rtp_channel, interleaved->rtcp_channel);
        *t = smolrtsp_transport_tcp(
            SmolRTSP_Context_get_writer(ctx), interleaved->rtp_channel, 512 * 1024);

        smolrtsp_header(
            ctx, SMOLRTSP_HEADER_TRANSPORT,
            "RTP/AVP/TCP;unicast;interleaved=%" PRIu8 "-%" PRIu8,
            interleaved->rtp_channel, interleaved->rtcp_channel);
        return 0;
    }

    smolrtsp_respond(
        ctx, SMOLRTSP_STATUS_BAD_REQUEST, "`interleaved' not found");
    return -1;
}

static int setup_udp(
    const struct sockaddr *addr, SmolRTSP_Context *ctx, SmolRTSP_Transport *t,
    SmolRTSP_TransportConfig config) {

    ifLet(config.client_port, SmolRTSP_PortPair_Some, client_port) {
        elog_i(RTSP_LOG_TAG, "SETUP: UDP client_port=%" PRIu16 "-%" PRIu16,
               client_port->rtp_port, client_port->rtcp_port);
        int fd;
        if ((fd = smolrtsp_dgram_socket(
                 addr->sa_family, smolrtsp_sockaddr_ip(addr),
                 client_port->rtp_port)) == -1) {
            return -1;
        }

        *t = smolrtsp_transport_udp(fd);

        smolrtsp_header(
            ctx, SMOLRTSP_HEADER_TRANSPORT,
            "RTP/AVP/UDP;unicast;client_port=%" PRIu16 "-%" PRIu16,
            client_port->rtp_port, client_port->rtcp_port);
        return 0;
    }

    smolrtsp_respond(
        ctx, SMOLRTSP_STATUS_BAD_REQUEST, "`client_port' not found");
    return -1;
}

static void AudioCtx_drop(VSelf) {
    VSELF(AudioCtx);

    event_free(self->ev);
    VTABLE(SmolRTSP_RtpTransport, SmolRTSP_Droppable).drop(self->transport);
    free(self);
}

impl(SmolRTSP_Droppable, AudioCtx);

static SmolRTSP_Droppable play_audio(
    int sample_rate, int samples_per_packet, int audio_codec, int channels,
    func_t pull_frame, func_t release_frame,
    struct event_base *base, struct bufferevent *bev, SmolRTSP_RtpTransport *t,
    struct event **ev, int *streams_playing) {
    const int safe_sample_rate = sample_rate > 0 ? sample_rate : 8000;
    const int safe_samples_per_packet = samples_per_packet > 0 ? samples_per_packet : 320;
    int pps = safe_sample_rate / safe_samples_per_packet;
    if (pps <= 0) {
        pps = 25;
    }
    uint32_t interval_us = (uint32_t)(1000000 / pps);
    if (interval_us < 2000) {
        interval_us = 2000;
    }

    AudioCtx *ctx = malloc(sizeof *ctx);
    assert(ctx);
    *ctx = (AudioCtx){
        .transport = t,
        .i = 0,
        .ev = NULL,
        .streams_playing = streams_playing,
        .bev = bev,
        .sample_rate = safe_sample_rate,
        .samples_per_packet = safe_samples_per_packet,
        .audio_codec = audio_codec,
        .channels = channels,
        .pull_frame = pull_frame,
        .release_frame = release_frame,
        .current_data = NULL,
        .current_size = 0,
        .base_timestamp_us = 0,
        .base_rtp_timestamp = 0,
        .first_frame = true,
        .pace_interval_us = interval_us,
        .next_deadline_us = 0,
        .send_count = 0,
        .pull_fail_count = 0,
        .full_log_count = 0,
        .last_stat_log_us = 0,
    };

    ctx->ev = event_new(
        base, -1, EV_PERSIST | EV_TIMEOUT, send_audio_packet_cb, (void *)ctx);
    assert(ctx->ev);
    ctx->next_deadline_us = monotonic_time_us() + ctx->pace_interval_us;
    event_add_after_us(ctx->ev, ctx->pace_interval_us);
    *ev = ctx->ev;
    (*streams_playing)++;

    return DYN(AudioCtx, SmolRTSP_Droppable, ctx);
}

static void send_audio_packet_cb(evutil_socket_t fd, short events, void *arg) {
    (void)fd;
    (void)events;

    AudioCtx *ctx = arg;
    
    // Flow Control: Check if transport buffer is full
    if (SmolRTSP_RtpTransport_is_full(ctx->transport)) {
        ctx->full_log_count++;
        if (ctx->full_log_count % 50 == 1) {
            elog_w(RTSP_LOG_TAG, "[AUDIO] Network buffer full, skipping this pacing slot");
        }
        schedule_next_deadline(ctx->ev, &ctx->next_deadline_us, ctx->pace_interval_us);
        return;
    }

    // 使用外部音频源
    if (ctx->pull_frame && ctx->release_frame) {
        uint8_t *audio_data = NULL;
        size_t audio_size = 0;
        uint64_t timestamp = 0;
        
        int pull_result = ctx->pull_frame((void **)&audio_data, &audio_size, &timestamp);
        if (pull_result != 0) {
            ctx->pull_fail_count++;

            ctx->current_data = NULL;
            ctx->current_size = 0;
            
            // 区分不同的错误类型
            if (pull_result == -1) {
                schedule_retry_us(ctx->ev, 5000);
                return;
            } else if (pull_result == -2) {
                if (ctx->pull_fail_count % 100 == 1) {
                    elog_w(RTSP_LOG_TAG, "[AUDIO] stream ended (%d consecutive failures)", ctx->pull_fail_count);
                }
                schedule_retry_us(ctx->ev, 10000);
                return;
            } else {
                schedule_retry_us(ctx->ev, 5000);
                return;
            }
        }
        
        if (ctx->pull_fail_count > 0) {
            if (ctx->pull_fail_count >= 5) {
                elog_i(RTSP_LOG_TAG, "[AUDIO] recovered after %d pull failures", ctx->pull_fail_count);
            }
            ctx->pull_fail_count = 0;
        }

        ctx->current_data = audio_data;
        ctx->current_size = audio_size;

        if (audio_size == 0) {
            // 音频结束
            if (ctx->current_data) {
                ctx->release_frame((void **)&ctx->current_data, &ctx->current_size, NULL);
            }
            event_del(ctx->ev);
            (*ctx->streams_playing)--;
            if (0 == *ctx->streams_playing) {
                bufferevent_trigger_event(ctx->bev, BEV_EVENT_EOF, 0);
            }
            return;
        }
        
        // RTP 时间戳
        uint32_t rtp_timestamp;
        if (ctx->pull_frame && ctx->release_frame) {
            // RTP 时间戳计算：(采集时间(us) * 采样率) / 1000000
            // 为了更好的音视频同步，我们应该从 0 开始或者对齐首帧
            if (ctx->first_frame) {
                ctx->base_timestamp_us = timestamp;
                ctx->base_rtp_timestamp = 0;
                ctx->first_frame = false;
                elog_i(RTSP_LOG_TAG, "[AUDIO] First frame: capture_ts=%" PRIu64 " us, rtp_ts=0", timestamp);
            }
            
            // 计算相对于首帧的 RTP 时间戳
            rtp_timestamp = (uint32_t)((timestamp - ctx->base_timestamp_us) * ctx->sample_rate / 1000000);
        } else {
            rtp_timestamp = ctx->i * ctx->samples_per_packet;
        }

        const SmolRTSP_RtpTimestamp ts = SmolRTSP_RtpTimestamp_Raw(rtp_timestamp);
        const bool marker = (ctx->i == 0); // 第一个包设置 marker
        
        // 对于 L16 格式，需要转换为网络字节序（大端）
        // 注意：需要复制数据，不能修改原始缓冲区
        static uint8_t audio_send_buffer[4096];
        uint8_t *send_data = audio_data;
        
        if (ctx->audio_codec == AUDIO_CODEC_L16) {
            if (audio_size <= sizeof(audio_send_buffer)) {
                memcpy(audio_send_buffer, audio_data, audio_size);
                int16_t *samples = (int16_t *)audio_send_buffer;
                size_t num_samples = audio_size / 2;
                for (size_t j = 0; j < num_samples; j++) {
                    samples[j] = htons(samples[j]);
                }
                send_data = audio_send_buffer;
            }
        }
        
        const U8Slice99 header = U8Slice99_empty(),
                        payload = U8Slice99_new(send_data, audio_size);

        size_t sent_audio_size = audio_size;

        if (SmolRTSP_RtpTransport_send_packet(
                ctx->transport, ts, marker, header, payload) == -1) {
            const int err = errno;
            elog_e(
                RTSP_LOG_TAG,
                "[AUDIO] Failed to send RTP packet errno=%d(%s)",
                err, strerror(err));
        }
        
        ctx->release_frame((void **)&audio_data, &audio_size, NULL);
        ctx->i++;
        ctx->send_count++;
        
        // 每秒输出一次统计
        uint64_t now_us = monotonic_time_us();
        if (ctx->last_stat_log_us == 0) {
            ctx->last_stat_log_us = now_us;
        }
        if (now_us - ctx->last_stat_log_us >= 1000000ULL) {
            int expected_pps = ctx->sample_rate / ctx->samples_per_packet;
            elog_i(RTSP_LOG_TAG, "[AUDIO] sent=%d, ts=%u, size=%zu, expected_pps=%d",
                   ctx->send_count, rtp_timestamp, sent_audio_size, expected_pps);
            ctx->last_stat_log_us = now_us;
        }

        schedule_next_deadline(ctx->ev, &ctx->next_deadline_us, ctx->pace_interval_us);
        return;
    }

    schedule_next_deadline(ctx->ev, &ctx->next_deadline_us, ctx->pace_interval_us);
}

static void VideoCtx_drop(VSelf) {
    VSELF(VideoCtx);

    event_free(self->ev);
    VTABLE(SmolRTSP_NalTransport, SmolRTSP_Droppable).drop(self->transport);
    free(self);
}

impl(SmolRTSP_Droppable, VideoCtx);

static SmolRTSP_Droppable play_video(
    int fps, int sample_rate, int codec, func_t pull_frame, func_t release_frame,
    bool sps_pps_bypass, struct event_base *base, struct bufferevent *bev, SmolRTSP_RtpTransport *t,
    struct event **ev, int *streams_playing) {
    const int safe_fps = fps > 0 ? fps : 15;
    const uint64_t au_retry_us = parse_u64_env_or_default("RTSP_VIDEO_AU_RETRY_US", 1000);
    const bool pace_no_skip = parse_u64_env_or_default("RTSP_VIDEO_PACE_NO_SKIP", 0) > 0;
    uint32_t interval_us = (uint32_t)(1000000 / safe_fps);
    if (interval_us < 2000) {
        interval_us = 2000;
    }
    
    VideoCtx *ctx = malloc(sizeof *ctx);
    assert(ctx);
    {
        *ctx = (VideoCtx){
            .transport = SmolRTSP_NalTransport_new(t),
            .timestamp = 0,
            .codec = codec,
            .nalu_start = NULL,
            .ev = NULL,
            .bev = bev,
            .streams_playing = streams_playing,
            .fps = safe_fps,
            .sample_rate = sample_rate,
            .au_retry_us = au_retry_us,
            .pace_no_skip = pace_no_skip,
            .pull_frame = pull_frame,
            .release_frame = release_frame,
            .sps_pps_bypass = sps_pps_bypass,
            .has_extension = false,
            .first_frame = true,
            .need_idr = true,
            .pace_interval_us = interval_us,
            .next_deadline_us = 0,
            .frame_count = 0,
            .pull_fail_count = 0,
            .full_log_count = 0,
            .last_stat_log_us = 0,
        };
    }

    elog_i(RTSP_LOG_TAG, "[VIDEO] pacing=%u us, au_retry_us=%" PRIu64 ", pace_no_skip=%d",
           interval_us, au_retry_us, pace_no_skip ? 1 : 0);


    ctx->ev = event_new(
        base, -1, EV_PERSIST | EV_TIMEOUT, send_video_packet_cb, (void *)ctx);
    assert(ctx->ev);
    ctx->next_deadline_us = monotonic_time_us() + ctx->pace_interval_us;
    event_add_after_us(ctx->ev, ctx->pace_interval_us);
    *ev = ctx->ev;
    (*streams_playing)++;

    return DYN(VideoCtx, SmolRTSP_Droppable, ctx);
}

static void send_video_packet_cb(evutil_socket_t fd, short events, void *arg) {
    (void)fd;
    (void)events;

    VideoCtx *ctx = arg;
    const uint64_t cb_begin_us = monotonic_time_us();

    if (ctx->expected_fire_us > 0 && cb_begin_us > ctx->expected_fire_us) {
        const uint64_t lag_us = cb_begin_us - ctx->expected_fire_us;
        ctx->deadline_lag_total_us += lag_us;
        ctx->deadline_lag_count++;
        if (lag_us > ctx->deadline_lag_max_us) {
            ctx->deadline_lag_max_us = lag_us;
        }
        if (lag_us > 2000) {
            ctx->deadline_lag_over_2ms_count++;
        }
        if (ctx->expected_sched_kind == VIDEO_SCHED_PACE) {
            ctx->pace_lag_total_us += lag_us;
            ctx->pace_lag_count++;
            if (lag_us > ctx->pace_lag_max_us) {
                ctx->pace_lag_max_us = lag_us;
            }
            if (lag_us > 2000) {
                ctx->pace_lag_over_2ms_count++;
            }
        } else if (ctx->expected_sched_kind == VIDEO_SCHED_RETRY) {
            ctx->retry_lag_total_us += lag_us;
            ctx->retry_lag_count++;
            if (lag_us > ctx->retry_lag_max_us) {
                ctx->retry_lag_max_us = lag_us;
            }
            if (lag_us > 2000) {
                ctx->retry_lag_over_2ms_count++;
            }
        }
    }

    if (!U8Slice99_is_empty(ctx->video)) {
        ctx->frame_cb_count++;
    }

    // Flow Control: Check if transport buffer is full (Backpressure)
    if (SmolRTSP_NalTransport_is_full(ctx->transport)) {
        ctx->full_log_count++;
        if (ctx->full_log_count % 25 == 1) {
            elog_w(RTSP_LOG_TAG, "[VIDEO] Network buffer full (backpressure), skipping frame pull");
        }
        video_schedule_next_deadline(ctx);
        video_ctx_record_cb_exec(ctx, cb_begin_us);
        return;
    }
    
    // 只有当当前帧处理完毕（ctx->video 为空）时才获取新帧
    if (ctx->pull_frame && ctx->release_frame && U8Slice99_is_empty(ctx->video)) {
        uint8_t *video_data;
        size_t video_size;
        uint64_t timestamp = 0;

        int pull_result = ctx->pull_frame((void **)&video_data, &video_size, &timestamp);

        if (ctx->frame_count % 30 == 0) {
            elog_i(RTSP_LOG_TAG, "[VIDEO] Pull result: %d, capture_ts=%" PRIu64, pull_result, timestamp);
        }
        if (pull_result) {
            ctx->pull_fail_count++;
            
            // 区分不同的错误类型
            if (pull_result == -1) {
                video_schedule_retry(ctx, 5000);
                video_ctx_record_cb_exec(ctx, cb_begin_us);
                return;
            } else if (pull_result == -2) {
                if (ctx->pull_fail_count % 100 == 1) {
                    elog_w(RTSP_LOG_TAG, "[VIDEO] stream ended (%d consecutive failures)", ctx->pull_fail_count);
                }
                video_schedule_retry(ctx, 100000);
                video_ctx_record_cb_exec(ctx, cb_begin_us);
                return;
            } else {
                if (ctx->pull_fail_count % 100 == 1) {
                    elog_w(RTSP_LOG_TAG, "[VIDEO] pull_frame failed, count=%d", ctx->pull_fail_count);
                }
                video_schedule_next_deadline(ctx);
                video_ctx_record_cb_exec(ctx, cb_begin_us);
                return;
            }
        }
        
        if (ctx->pull_fail_count > 0) {
            if (ctx->pull_fail_count >= 3) {
                elog_i(RTSP_LOG_TAG, "[VIDEO] recovered after %d pull failures", ctx->pull_fail_count);
            }
            ctx->pull_fail_count = 0;
        }
        
        ctx->frame_count++;
        ctx->frame_start_us = cb_begin_us;
        ctx->frame_nal_count = 0;
        ctx->frame_cb_count = 1;

        ctx->current_frame_data = video_data;
        ctx->current_frame_size = video_size;

        U8Slice99 video = U8Slice99_new(video_data, video_size);

        SmolRTSP_NalStartCodeTester start_code_tester;
        if ((start_code_tester = smolrtsp_determine_start_code(video)) == NULL) {
            elog_e(RTSP_LOG_TAG, "[VIDEO] Invalid video bitstream (missing start code)");
            ctx->release_frame((void **)&ctx->current_frame_data, &ctx->current_frame_size, NULL);
            video_schedule_next_deadline(ctx);
            video_ctx_record_cb_exec(ctx, cb_begin_us);
            return;
        }

        // RTP 时间戳直接使用生产者的timestamp（已单调递增，符合AV Sync设计）
        ctx->timestamp = (uint32_t)(timestamp * 90000 / 1000000);

        if (ctx->first_frame) {
            ctx->base_capture_us = timestamp;
            ctx->base_rtp_ts = ctx->timestamp;
            ctx->first_frame = false;
            elog_i(RTSP_LOG_TAG, "[VIDEO] First frame: capture_ts=%" PRIu64 " us, rtp_ts=%u",
                   timestamp, ctx->timestamp);
        } else {
            // 也可以使用差值计算（可选）
            // ctx->timestamp = ctx->base_rtp_ts + (uint32_t)((timestamp - ctx->base_capture_us) * 90000 / 1000000);
        }

        ctx->video = video;
        ctx->start_code_tester = start_code_tester;
        ctx->nalu_start = NULL;
        ctx->has_extension = false;
        ctx->extension.frameIndex++;
        ctx->extension.frameSize = video_size;
        
        // 每秒输出一次统计
        uint64_t now_us = monotonic_time_us();
        if (ctx->last_stat_log_us == 0) {
            ctx->last_stat_log_us = now_us;
        }
        if (now_us - ctx->last_stat_log_us >= 1000000ULL) {
            const uint64_t frame_delta = ctx->frame_done_count - ctx->last_log_frame_done_count;
            const uint64_t frame_nal_delta = ctx->frame_nal_total - ctx->last_log_frame_nal_total;
            const uint64_t frame_cb_delta = ctx->frame_cb_total - ctx->last_log_frame_cb_total;
            const uint64_t frame_proc_delta_us = ctx->frame_proc_total_us - ctx->last_log_frame_proc_total_us;
            const uint64_t cb_count_delta = ctx->cb_exec_count - ctx->last_log_cb_exec_count;
            const uint64_t cb_cost_delta_us = ctx->cb_exec_total_us - ctx->last_log_cb_exec_total_us;
            const uint64_t lag_count_delta = ctx->deadline_lag_count - ctx->last_log_deadline_lag_count;
            const uint64_t lag_total_delta_us = ctx->deadline_lag_total_us - ctx->last_log_deadline_lag_total_us;
            const uint64_t lag_over_2ms_delta =
                ctx->deadline_lag_over_2ms_count - ctx->last_log_deadline_lag_over_2ms_count;
            const uint64_t pace_lag_count_delta = ctx->pace_lag_count - ctx->last_log_pace_lag_count;
            const uint64_t pace_lag_total_delta_us = ctx->pace_lag_total_us - ctx->last_log_pace_lag_total_us;
            const uint64_t pace_lag_over_2ms_delta =
                ctx->pace_lag_over_2ms_count - ctx->last_log_pace_lag_over_2ms_count;
            const uint64_t retry_lag_count_delta = ctx->retry_lag_count - ctx->last_log_retry_lag_count;
            const uint64_t retry_lag_total_delta_us = ctx->retry_lag_total_us - ctx->last_log_retry_lag_total_us;
            const uint64_t retry_lag_over_2ms_delta =
                ctx->retry_lag_over_2ms_count - ctx->last_log_retry_lag_over_2ms_count;

            const double avg_nal_per_frame =
                (frame_delta > 0) ? ((double)frame_nal_delta / (double)frame_delta) : 0.0;
            const double avg_cb_per_frame =
                (frame_delta > 0) ? ((double)frame_cb_delta / (double)frame_delta) : 0.0;
            const double avg_frame_proc_ms =
                (frame_delta > 0) ? ((double)frame_proc_delta_us / (double)frame_delta / 1000.0) : 0.0;
            const double avg_cb_cost_us =
                (cb_count_delta > 0) ? ((double)cb_cost_delta_us / (double)cb_count_delta) : 0.0;
            const double avg_lag_us =
                (lag_count_delta > 0) ? ((double)lag_total_delta_us / (double)lag_count_delta) : 0.0;
            const double avg_pace_lag_us =
                (pace_lag_count_delta > 0) ? ((double)pace_lag_total_delta_us / (double)pace_lag_count_delta) : 0.0;
            const double avg_retry_lag_us =
                (retry_lag_count_delta > 0) ? ((double)retry_lag_total_delta_us / (double)retry_lag_count_delta) : 0.0;

            elog_i(RTSP_LOG_TAG,
                   "[VIDEO] sent=%d, ts=%u, size=%zu, fps=%d, obs{frame=%" PRIu64 ", nal_avg=%.2f nal_max=%" PRIu64 ", cbpf_avg=%.2f cbpf_max=%" PRIu64 ", frame_ms_avg=%.2f frame_ms_max=%.2f, cb_us_avg=%.2f cb_us_max=%.2f, lag_us_avg=%.2f lag_us_max=%.2f lag2ms=%" PRIu64 ", pace_lag_us_avg=%.2f pace_lag2ms=%" PRIu64 ", retry_lag_us_avg=%.2f retry_lag2ms=%" PRIu64 "}",
                   ctx->frame_count, ctx->timestamp, video_size, ctx->fps,
                   frame_delta,
                   avg_nal_per_frame, ctx->frame_nal_max,
                   avg_cb_per_frame, ctx->frame_cb_max,
                   avg_frame_proc_ms, (double)ctx->frame_proc_max_us / 1000.0,
                   avg_cb_cost_us, (double)ctx->cb_exec_max_us,
                   avg_lag_us, (double)ctx->deadline_lag_max_us,
                   lag_over_2ms_delta,
                   avg_pace_lag_us, pace_lag_over_2ms_delta,
                   avg_retry_lag_us, retry_lag_over_2ms_delta);

            ctx->last_log_frame_done_count = ctx->frame_done_count;
            ctx->last_log_frame_nal_total = ctx->frame_nal_total;
            ctx->last_log_frame_cb_total = ctx->frame_cb_total;
            ctx->last_log_frame_proc_total_us = ctx->frame_proc_total_us;
            ctx->last_log_cb_exec_total_us = ctx->cb_exec_total_us;
            ctx->last_log_cb_exec_count = ctx->cb_exec_count;
            ctx->last_log_deadline_lag_total_us = ctx->deadline_lag_total_us;
            ctx->last_log_deadline_lag_count = ctx->deadline_lag_count;
            ctx->last_log_deadline_lag_over_2ms_count = ctx->deadline_lag_over_2ms_count;
            ctx->last_log_pace_lag_total_us = ctx->pace_lag_total_us;
            ctx->last_log_pace_lag_count = ctx->pace_lag_count;
            ctx->last_log_pace_lag_over_2ms_count = ctx->pace_lag_over_2ms_count;
            ctx->last_log_retry_lag_total_us = ctx->retry_lag_total_us;
            ctx->last_log_retry_lag_count = ctx->retry_lag_count;
            ctx->last_log_retry_lag_over_2ms_count = ctx->retry_lag_over_2ms_count;
            ctx->last_stat_log_us = now_us;
        }
    }
    
 again:
    if (U8Slice99_is_empty(ctx->video)) {
        if (ctx->pull_frame && ctx->release_frame) {
            send_nalu(ctx);
            const uint64_t frame_end_us = monotonic_time_us();
            const uint64_t frame_proc_us =
                (ctx->frame_start_us > 0 && frame_end_us >= ctx->frame_start_us)
                    ? (frame_end_us - ctx->frame_start_us)
                    : 0;
            ctx->frame_done_count++;
            ctx->frame_nal_total += ctx->frame_nal_count;
            ctx->frame_cb_total += ctx->frame_cb_count;
            ctx->frame_proc_total_us += frame_proc_us;
            if (ctx->frame_nal_count > ctx->frame_nal_max) {
                ctx->frame_nal_max = ctx->frame_nal_count;
            }
            if (ctx->frame_cb_count > ctx->frame_cb_max) {
                ctx->frame_cb_max = ctx->frame_cb_count;
            }
            if (frame_proc_us > ctx->frame_proc_max_us) {
                ctx->frame_proc_max_us = frame_proc_us;
            }

            ctx->release_frame((void **)&ctx->current_frame_data, &ctx->current_frame_size, NULL);
            ctx->current_frame_data = NULL;
            ctx->current_frame_size = 0;
            ctx->frame_start_us = 0;
            ctx->frame_nal_count = 0;
            ctx->frame_cb_count = 0;
            video_schedule_next_deadline(ctx);
        } else {
            send_nalu(ctx);
            event_del(ctx->ev);
            (*ctx->streams_playing)--;
            if (0 == *ctx->streams_playing) {
                bufferevent_trigger_event(ctx->bev, BEV_EVENT_EOF, 0);
            }
        }
        video_ctx_record_cb_exec(ctx, cb_begin_us);
        return;
    }

    const size_t start_code_len = ctx->start_code_tester(ctx->video);
    if (0 == start_code_len) {
        ctx->video = U8Slice99_advance(ctx->video, 1);
        goto again;
    }

    bool au_found = false;
    if (NULL != ctx->nalu_start) {
        au_found = send_nalu(ctx);
    }

    ctx->video = U8Slice99_advance(ctx->video, start_code_len);
    ctx->nalu_start = ctx->video.ptr;

    if (!au_found) {
        goto again;
    }

    if (ctx->au_retry_us == 0) {
        goto again;
    }
    // 默认分片发送场景按小延迟调度；可通过 RTSP_VIDEO_AU_RETRY_US 覆盖
    video_schedule_retry(ctx, ctx->au_retry_us);
    video_ctx_record_cb_exec(ctx, cb_begin_us);
}

static bool send_h264_nalu(VideoCtx *ctx) {
    const SmolRTSP_NalUnit nalu = {
        .header = SmolRTSP_NalHeader_H264(
            SmolRTSP_H264NalHeader_parse(ctx->nalu_start[0])),
        .payload = U8Slice99_from_ptrdiff(ctx->nalu_start + 1, ctx->video.ptr),
    };

    bool au_found = false;

    int unit_type = SmolRTSP_NalHeader_unit_type(nalu.header);
    //printf("%s-%d, unit_type=%d\n", __func__, __LINE__, unit_type);
    if (ctx->sps_pps_bypass && (unit_type == SMOLRTSP_H264_NAL_UNIT_SPS || unit_type == SMOLRTSP_H264_NAL_UNIT_PPS)) {
        return au_found;
    }

    if (ctx->need_idr) {
        if (unit_type == SMOLRTSP_H264_NAL_UNIT_CODED_SLICE_IDR) {
            ctx->need_idr = false;
        } else if (unit_type == SMOLRTSP_H264_NAL_UNIT_AUD || unit_type == SMOLRTSP_H264_NAL_UNIT_SPS || unit_type == SMOLRTSP_H264_NAL_UNIT_PPS) {
            // allow AUD/SPS/PPS before IDR
        } else {
            // drop non-IDR slices until first IDR
            return au_found;
        }
    }
    if (unit_type == SMOLRTSP_H264_NAL_UNIT_AUD || 
        unit_type == SMOLRTSP_H264_NAL_UNIT_CODED_SLICE_NON_IDR ||
        unit_type == SMOLRTSP_H264_NAL_UNIT_CODED_SLICE_IDR
    ) {
        au_found = true;
    }
    //printf("timestamp: %d-%d-%d\n", ctx->timestamp, SmolRTSP_RtpTimestamp_Raw(ctx->timestamp), SmolRTSP_RtpTimestamp_SysClockUs(ctx->timestamp));
    if (ctx->has_extension) {
        if (SmolRTSP_NalTransport_send_packet_ext(
                ctx->transport, SmolRTSP_RtpTimestamp_Raw(ctx->timestamp), nalu, (uint8_t *)&ctx->extension, sizeof(ctx->extension)) ==
            -1) {
            const int err = errno;
            elog_e(
                RTSP_LOG_TAG,
                "[VIDEO] Failed to send RTP/NAL(ext) errno=%d(%s)",
                err, strerror(err));
        }
    } else {
        if (SmolRTSP_NalTransport_send_packet(
                ctx->transport, SmolRTSP_RtpTimestamp_Raw(ctx->timestamp), nalu) ==
            -1) {
            const int err = errno;
            elog_e(
                RTSP_LOG_TAG,
                "[VIDEO] Failed to send RTP/NAL errno=%d(%s)",
                err, strerror(err));
        }
    }
    

    return au_found;
}

static bool send_h265_nalu(VideoCtx *ctx) {
    
    const SmolRTSP_NalUnit nalu = {
        .header = SmolRTSP_NalHeader_H265(
            SmolRTSP_H265NalHeader_parse(ctx->nalu_start)),
        .payload = U8Slice99_from_ptrdiff(ctx->nalu_start + 1, ctx->video.ptr),
    };

    bool au_found = false;
  
    if (SmolRTSP_NalHeader_unit_type(nalu.header) ==
        SMOLRTSP_H265_NAL_UNIT_AUD_NUT) {
        // ctx->timestamp += ctx->sample_rate / ctx->fps;
        au_found = true;
    }
    
    if (SmolRTSP_NalTransport_send_packet(
            ctx->transport, SmolRTSP_RtpTimestamp_Raw(ctx->timestamp), nalu) ==
        -1) {
        const int err = errno;
        elog_e(
            RTSP_LOG_TAG,
            "[VIDEO] Failed to send RTP/NAL errno=%d(%s)",
            err, strerror(err));
    }
    
    return au_found;
}

static bool send_nalu(VideoCtx *ctx) {
    if (ctx && ctx->frame_start_us > 0) {
        ctx->frame_nal_count++;
    }
    if (ctx->codec == CODEC_H264) {
        return send_h264_nalu(ctx);
    } else if (ctx->codec == CODEC_H265) {
        return send_h265_nalu(ctx);
    } else {
        return false;
    }
}
 void* create_server(const struct rtsp_server_param *param)
{
    if (!param) {
        elog_e(RTSP_LOG_TAG, "Invalid parameter");
        return NULL;
    }
    
    elog_i(RTSP_LOG_TAG, "Creating RTSP server: audio_enable=%d codec=%d sample_rate=%d",
           param->audio_enable, param->audio_codec, param->audio_sample_rate);
    
    if (param->audio_enable) {
        elog_i(RTSP_LOG_TAG, "Audio params: codec=%d rate=%d channels=%d samples_per_packet=%d",
               param->audio_codec, param->audio_sample_rate,
               param->audio_channels, param->audio_samples_per_packet);
    }

    struct rtsp_server *server = (struct rtsp_server *)malloc(sizeof(struct rtsp_server));
    if (!server) {
        elog_e(RTSP_LOG_TAG, "Failed to allocate memory for server");
        return NULL;
    }
    memset(server, 0, sizeof(struct rtsp_server));
    server->param = *param;
    srand(time(NULL));
    // Create event base
    server->base = event_base_new();
    if (!server->base) {
        elog_e(RTSP_LOG_TAG, "event_base_new failed");
        free(server);
        return NULL;
    }

    // Set up server address
    struct sockaddr_in sin = {
        .sin_family = AF_INET,
        .sin_port = htons(server->param.port),
    };

    // Create listener
    server->listener = evconnlistener_new_bind(
        server->base, listener_cb, (void *)server,
        LEV_OPT_REUSEABLE | LEV_OPT_CLOSE_ON_FREE, -1,
        (struct sockaddr *)&sin, sizeof sin);
    if (!server->listener) {
        elog_e(RTSP_LOG_TAG, "evconnlistener_new_bind failed");
        event_base_free(server->base);
        free(server);
        return NULL;
    }

    server->is_running = false;
    
    return server;
}

// Thread function to run event loop
static void *event_loop_thread(void *arg)
{
    struct rtsp_server *rtsp_server = (struct rtsp_server *)arg;

    // Start event loop
    event_base_dispatch(rtsp_server->base);

    // Event loop exited
    rtsp_server->is_running = false;
    
    return NULL;
}

int start_server(void *server)
{
    if (!server) {
        elog_e(RTSP_LOG_TAG, "Invalid server pointer");
        return EXIT_FAILURE;
    }

    struct rtsp_server *rtsp_server = (struct rtsp_server *)server;

    if (rtsp_server->is_running) {
        elog_w(RTSP_LOG_TAG, "Server is already running on port %d", rtsp_server->param.port);
        return EXIT_SUCCESS;
    }

    elog_i(RTSP_LOG_TAG, "Server started on port %d", rtsp_server->param.port);
    rtsp_server->is_running = true;

    // Create thread to run event loop
    pthread_t thread_id;
    if (pthread_create(&thread_id, NULL, event_loop_thread, rtsp_server) != 0) {
        elog_e(RTSP_LOG_TAG, "pthread_create failed");
        rtsp_server->is_running = false;
        return EXIT_FAILURE;
    }

    // Detach the thread so we don't need to join it
    pthread_detach(thread_id);

    return EXIT_SUCCESS;
}

int stop_server(void *server)
{
    if (!server) {
        elog_e(RTSP_LOG_TAG, "Invalid server pointer");
        return EXIT_FAILURE;
    }

    struct rtsp_server *rtsp_server = (struct rtsp_server *)server;

    if (!rtsp_server->is_running) {
        elog_w(RTSP_LOG_TAG, "Server is not running");
        return EXIT_SUCCESS;
    }

    // Stop the event loop
    event_base_loopbreak(rtsp_server->base);
    rtsp_server->is_running = false;
    elog_i(RTSP_LOG_TAG, "Server stopped");
    return EXIT_SUCCESS;
}

int destroy_server(void *server)
{
    if (!server) {
        elog_e(RTSP_LOG_TAG, "Invalid server pointer");
        return EXIT_FAILURE;
    }

    struct rtsp_server *rtsp_server = (struct rtsp_server *)server;

    // Free resources
    if (rtsp_server->sigint_handler) {
        event_free(rtsp_server->sigint_handler);
    }

    if (rtsp_server->listener) {
        evconnlistener_free(rtsp_server->listener);
    }

    if (rtsp_server->base) {
        event_base_free(rtsp_server->base);
    }

    free(server);
    elog_i(RTSP_LOG_TAG, "Server destroyed");
    return EXIT_SUCCESS;
}

int register_function(void *server, func_id_t id, func_t func)
{
    if (!server) {
        elog_e(RTSP_LOG_TAG, "Invalid server pointer");
        return EXIT_FAILURE;
    }

    struct rtsp_server *rtsp_server = (struct rtsp_server *)server;
    rtsp_server->funcs[id] = func;
    return EXIT_SUCCESS;
}

int is_server_running(void *server)
{
    if (!server) {
        elog_e(RTSP_LOG_TAG, "Invalid server pointer");
        return 0;
    }

    struct rtsp_server *rtsp_server = (struct rtsp_server *)server;
    return rtsp_server->is_running? 1:0;
}

int set_server_param(void *server, int param_id, void *param, size_t size)
{
    if (!server) {
        elog_e(RTSP_LOG_TAG, "Invalid server pointer");
        return EXIT_FAILURE;
    }

    struct rtsp_server *rtsp_server = (struct rtsp_server *)server;
    switch (param_id) {
        case RTSP_SERVER_PARAM_VIDEO_SPS:
            for (int i = 0; i < size; i++) {
                rtsp_server->param.video_sps[i] = ((uint8_t *)param)[i];
            }
            rtsp_server->param.video_sps_len = size;
            break;
        case RTSP_SERVER_PARAM_VIDEO_PPS:
            for (int i = 0; i < size; i++) {
                rtsp_server->param.video_pps[i] = ((uint8_t *)param)[i];
            }
            rtsp_server->param.video_pps_len = size;
            break;
        default:
            elog_e(RTSP_LOG_TAG, "Invalid param id");
            break;
    }

    return EXIT_SUCCESS;
}

static char base64_table[] = { 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q',
			       'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h',
			       'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y',
			       'z', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '+', '/', '\0' };

/**
 * @brief Base64 encoding
 * @param pInData -[in] Source string
 * @param input_data_len -[in] Length of the source string
 * @param pOutData -[out] Encoded string
 * @param pOutLen -[out] Length of the encoded string
 * @return true - Success; false - Failure
 */
static bool base64_encode(char *input_data, uint32_t input_data_len, char *output_data, uint32_t *output_data_len)
{
	if (NULL == input_data || 0 == input_data_len) {
		return false;
	}

	uint32_t i = 0;
	uint32_t j = 0;
	uint32_t temp = 0;
	// Convert in groups of 3 bytes
	for (i = 0; i < input_data_len; i += 3) {
		// Get the first 6 bits
		temp = (*(input_data + i) >> 2) & 0x3F;
		*(output_data + j++) = base64_table[temp];

		// Get the first two bits of the second 6 bits
		temp = (*(input_data + i) << 4) & 0x30;
		// Special handling if there is only one character
		if (input_data_len <= (i + 1)) {
			*(output_data + j++) = base64_table[temp];
			*(output_data + j++) = '=';
			*(output_data + j++) = '=';
			break;
		}
		// Get the last four bits of the second 6 bits
		temp |= (*(input_data + i + 1) >> 4) & 0x0F;
		*(output_data + j++) = base64_table[temp];

		// Get the first four bits of the third 6 bits
		temp = (*(input_data + i + 1) << 2) & 0x3C;
		if (input_data_len <= (i + 2)) {
			*(output_data + j++) = base64_table[temp];
			*(output_data + j++) = '=';
			break;
		}
		// Get the last two bits of the third 6 bits
		temp |= (*(input_data + i + 2) >> 6) & 0x03;
		*(output_data + j++) = base64_table[temp];

		// Get the fourth 6 bits
		temp = *(input_data + i + 2) & 0x3F;
		*(output_data + j++) = base64_table[temp];
	}
	*(output_data + j) = '\0';
	// Length of encoded data
	*output_data_len = input_data_len * 8 / 6;
	return true;
}

/**
 * @brief Base64 decoding
 * @param pInData -[in] Source string
 * @param input_data_len -[in] Length of the source string
 * @param pOutData -[out] Decoded string
 * @param pOutLen -[out] Length of the decoded string
 * @return true - Success; false - Failure
 */
static bool base64_decode(char *input_data, uint32_t input_data_len, char *output_data, uint32_t *output_data_len)
{
	if (NULL == input_data || 0 == input_data_len || input_data_len % 4 != 0) {
		return false;
	}

	uint32_t i = 0;
	uint32_t j = 0;
	uint32_t k = 0;
	char temp[4] = "";
	// Convert in groups of 4 bytes
	for (i = 0; i < input_data_len; i += 4) {
		// Find the corresponding value in the encoding index table
		for (j = 0; j < 64; j++) {
			if (*(input_data + i) == base64_table[j]) {
				temp[0] = j;
			}
		}
		for (j = 0; j < 64; j++) {
			if (*(input_data + i + 1) == base64_table[j]) {
				temp[1] = j;
			}
		}
		for (j = 0; j < 64; j++) {
			if (*(input_data + i + 2) == base64_table[j]) {
				temp[2] = j;
			}
		}
		for (j = 0; j < 64; j++) {
			if (*(input_data + i + 3) == base64_table[j]) {
				temp[3] = j;
			}
		}

		// Combine the first 6 bits and the first two bits of the second 6 bits into
		// an 8-bit byte
		*(output_data + k++) = ((temp[0] << 2) & 0xFC) | ((temp[1] >> 4) & 0x03);
		if (*(input_data + i + 2) == '=') {
			break;
		}
		// Combine the first four bits of the second 6 bits and the first four bits
		// of the third 6 bits into an 8-bit byte
		*(output_data + k++) = ((temp[1] << 4) & 0xF0) | ((temp[2] >> 2) & 0x0F);
		if (*(input_data + i + 3) == '=') {
			break;
		}
		// Combine the last two bits of the third 6 bits and the fourth 6 bits into
		// an 8-bit byte
		*(output_data + k++) = ((temp[2] << 6) & 0xF0) | (temp[3] & 0x3F);
	}

	*output_data_len = k;

	return true;
}
