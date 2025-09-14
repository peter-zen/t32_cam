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

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <time.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>  // For gettimeofday
#include <pthread.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/event.h>
#include <event2/listener.h>
#include <event2/util.h>

// G.711 A-Law, 8k sample rate, mono channel.
#include "audio.g711a.h"

// H.264 video with AUDs, 25 FPS.
#include "video.h264.h"

static bool base64_encode(char *input_data, uint32_t input_data_len, char *output_data, uint32_t *output_data_len);
static bool base64_decode(char *input_data, uint32_t input_data_len, char *output_data, uint32_t *output_data_len);
#define SERVER_PORT SMOLRTSP_DEFAULT_PORT
#define AUDIO_PCMU_PAYLOAD_TYPE  0
#define VIDEO_PAYLOAD_TYPE 96 // dynamic PT
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
    struct event *ev;
    struct bufferevent *bev;
    int *streams_playing;
    int sample_rate;
    int samples_per_packet;
} AudioCtx;

static SmolRTSP_Droppable play_audio(
    int sample_rate, int samples_per_packet,
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
    func_t pull_frame;
    func_t release_frame;
    bool sps_pps_bypass;
    bool has_extension;
    struct RTPExtenHeader extension;
} VideoCtx;

static SmolRTSP_Droppable play_video(
    int fps, int sample_rate, int codec, func_t pull_frame, func_t release_frame,
    bool sps_pps_bypass, struct event_base *base, struct bufferevent *bev, SmolRTSP_RtpTransport *t,
    struct event **ev, int *streams_playing);
static void send_video_packet_cb(evutil_socket_t fd, short events, void *arg);
static bool send_nalu(VideoCtx *ctx);

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
        printf("bufferevent_socket_new failed.\n");
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
        puts("Connection closed.");
        // Access server if needed
        if (server && server->funcs[FUNC_ID_ON_SESSION_CLOSED]) {
            server->funcs[FUNC_ID_ON_SESSION_CLOSED](NULL, NULL);
        }
    } else if (events & BEV_EVENT_ERROR) {
        perror("Got an error on the connection");
        // Access server if needed
        if (server && server->funcs[FUNC_ID_ON_SESSION_CLOSED]) {
            server->funcs[FUNC_ID_ON_SESSION_CLOSED](NULL, NULL);
        }
    }

    bufferevent_free(bev);
    smolrtsp_libevent_ctx_free(ctx);
}

static void on_sigint_cb(evutil_socket_t sig, short events, void *ctx) {
    (void)sig;
    (void)events;

    struct event_base *base = ctx;

    puts("Caught an interrupt signal; exiting cleanly in two seconds.");

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
        SMOLRTSP_SDP_DESCRIBE(
            ret, sdp,
            (SMOLRTSP_SDP_MEDIA, "audio 0 RTP/AVP %d", AUDIO_PCMU_PAYLOAD_TYPE),
            (SMOLRTSP_SDP_ATTR, "control:audio"));
    }

    if (rtsp_param.video_enable) {
        if (rtsp_param.video_codec == CODEC_H264) {
            if (rtsp_param.video_sps_len && rtsp_param.video_pps_len) {
                char base64_video_sps[100];
                size_t base64_video_sps_len;
                char base64_video_pps[100];
                size_t base64_video_pps_len;

                base64_encode((char *)rtsp_param.video_sps, rtsp_param.video_sps_len, (char *)base64_video_sps, &base64_video_sps_len);
                base64_encode((char *)rtsp_param.video_pps, rtsp_param.video_pps_len, (char *)base64_video_pps, &base64_video_pps_len);
                printf("base64_video_sps: %s\n", base64_video_sps);
                printf("base64_video_pps: %s\n", base64_video_pps);

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

    if (rtsp_param.audio_stream_id == stream_id) {
        stream->transport = SmolRTSP_RtpTransport_new(
            transport, AUDIO_PCMU_PAYLOAD_TYPE, rtsp_param.audio_sample_rate);
    } else {
        stream->transport = SmolRTSP_RtpTransport_new(
            transport, VIDEO_PAYLOAD_TYPE, rtsp_param.video_sample_rate);
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

    bool played = false;
    for (size_t i = 0; i < MAX_STREAMS; i++) {
        if (self->streams[i].session_id == session_id) {
            if (rtsp_param.audio_stream_id == i) {
                self->streams[i].ctx = play_audio(
                    rtsp_param.audio_sample_rate, rtsp_param.audio_samples_per_packet,
                    self->base, self->bev, self->streams[i].transport,
                    &self->streams[i].ev, &self->streams_playing);
            } else {
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

    printf(
        "%s %s CSeq=%" PRIu32 ".\n",
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
        perror("Failed to respond");
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

    SmolRTSP_TransportConfig config;
    if (smolrtsp_parse_transport(&config, transport_val) == -1) {
        smolrtsp_respond(
            ctx, SMOLRTSP_STATUS_BAD_REQUEST, "Malformed `Transport'");
        return -1;
    }

    switch (config.lower) {
    case SmolRTSP_LowerTransport_TCP:
        if (setup_tcp(ctx, t, config) == -1) {
            smolrtsp_respond_internal_error(ctx);
            return -1;
        }
        break;
    case SmolRTSP_LowerTransport_UDP:
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
        *t = smolrtsp_transport_tcp(
            SmolRTSP_Context_get_writer(ctx), interleaved->rtp_channel, 0);

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
    int sample_rate, int samples_per_packet,
    struct event_base *base, struct bufferevent *bev, SmolRTSP_RtpTransport *t,
    struct event **ev, int *streams_playing) {
    AudioCtx *ctx = malloc(sizeof *ctx);
    assert(ctx);
    *ctx = (AudioCtx){
        .transport = t,
        .i = 0,
        .ev = NULL,
        .streams_playing = streams_playing,
        .bev = bev,
        .sample_rate = sample_rate,
        .samples_per_packet = samples_per_packet,
    };

    ctx->ev = event_new(
        base, -1, EV_PERSIST | EV_TIMEOUT, send_audio_packet_cb, (void *)ctx);
    assert(ctx->ev);

    event_add(
        ctx->ev, &(const struct timeval){
                     .tv_sec = 0,
                     .tv_usec = (1e6 / (sample_rate / samples_per_packet)),
                 });
    *ev = ctx->ev;
    (*streams_playing)++;

    return DYN(AudioCtx, SmolRTSP_Droppable, ctx);
}

static void send_audio_packet_cb(evutil_socket_t fd, short events, void *arg) {
    (void)fd;
    (void)events;

    AudioCtx *ctx = arg;
    
    if (ctx->i * ctx->samples_per_packet >= ___media_audio_g711a_len) {
        event_del(ctx->ev);
        (*ctx->streams_playing)--;
        if (0 == *ctx->streams_playing) {
            bufferevent_trigger_event(ctx->bev, BEV_EVENT_EOF, 0);
        }
        return;
    }

    const SmolRTSP_RtpTimestamp ts =
        SmolRTSP_RtpTimestamp_Raw(ctx->i * ctx->samples_per_packet);
    const bool marker = false;
    const size_t samples_count =
        ___media_audio_g711a_len <
                ctx->i * ctx->samples_per_packet + ctx->samples_per_packet
            ? ___media_audio_g711a_len % ctx->samples_per_packet
            : ctx->samples_per_packet;
    const U8Slice99 header = U8Slice99_empty(),
                    payload = U8Slice99_new(
                        ___media_audio_g711a +
                            ctx->i * ctx->samples_per_packet,
                        samples_count);

    if (SmolRTSP_RtpTransport_send_packet(
            ctx->transport, ts, marker, header, payload) == -1) {
        perror("Failed to send RTP/PCMU");
    }

    ctx->i++;
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
    
    VideoCtx *ctx = malloc(sizeof *ctx);
    assert(ctx);
    if (!pull_frame && !release_frame) {
        U8Slice99 video = Slice99_typed_from_array(___media_video_h264);
        SmolRTSP_NalStartCodeTester start_code_tester;
        
        if ((start_code_tester = smolrtsp_determine_start_code(video)) == NULL) {
            printf("%s:Invalid video file.\n", __func__);
            abort();
        }
        *ctx = (VideoCtx){
            .transport = SmolRTSP_NalTransport_new(t),
            .start_code_tester = start_code_tester,
            .timestamp = 0,
            .video = video,
            .codec = codec,
            .nalu_start = NULL,
            .ev = NULL,
            .bev = bev,
            .streams_playing = streams_playing,
            .fps = fps,
            .sample_rate = sample_rate,
            .pull_frame = pull_frame,
            .release_frame = release_frame,
            .sps_pps_bypass = sps_pps_bypass,
            .has_extension = false,
        };
    } else {
        *ctx = (VideoCtx){
            .transport = SmolRTSP_NalTransport_new(t),
            .timestamp = 0,
            .codec = codec,
            .nalu_start = NULL,
            .ev = NULL,
            .bev = bev,
            .streams_playing = streams_playing,
            .fps = fps,
            .sample_rate = sample_rate,
            .pull_frame = pull_frame,
            .release_frame = release_frame,
            .sps_pps_bypass = sps_pps_bypass,
            .has_extension = false,
        };
    }
    

    ctx->ev = event_new(
        base, -1, EV_PERSIST | EV_TIMEOUT, send_video_packet_cb, (void *)ctx);
    assert(ctx->ev);

    event_add(
        ctx->ev, &(const struct timeval){
                     .tv_sec = 0,
                     .tv_usec = /*1e6 / fps*/10,
                 });
    *ev = ctx->ev;
    (*streams_playing)++;

    return DYN(VideoCtx, SmolRTSP_Droppable, ctx);
}

static void send_video_packet_cb(evutil_socket_t fd, short events, void *arg) {
    (void)fd;
    (void)events;

    // Start timing
    struct timespec start_time, end_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    VideoCtx *ctx = arg;
    static int frame_count = 0;
    if (ctx->pull_frame && ctx->release_frame) {
        uint8_t *video_data;
        size_t video_size;

        int pull_result = ctx->pull_frame((void **)&video_data, &video_size);
        if (pull_result) {
            struct timeval tv = {.tv_sec = 0, .tv_usec = /*1e6 / ctx->fps*/10};
            event_add(ctx->ev, &tv);
            return;
        }
        
        U8Slice99 video = U8Slice99_new(video_data, video_size);

        SmolRTSP_NalStartCodeTester start_code_tester;
        if ((start_code_tester = smolrtsp_determine_start_code(video)) == NULL) {
            printf("%s:Invalid video file.\n", __func__);
            struct timeval tv = {.tv_sec = 0, .tv_usec = /*1e6 / ctx->fps*/10};
            event_add(ctx->ev, &tv);
            return;
        }

        ctx->video = video;
        ctx->start_code_tester = start_code_tester;
        ctx->nalu_start = NULL;
        ctx->has_extension = false;
        ctx->extension.frameIndex++;
        ctx->extension.frameSize = video_size;

        //printf("Frame[%d], size: %d\n", ctx->extension.frameIndex, ctx->extension.frameSize);
    }
    
again:
    if (U8Slice99_is_empty(ctx->video)) {
        if (ctx->pull_frame && ctx->release_frame) {
            send_nalu(ctx);
            ctx->release_frame(NULL, NULL);
            struct timeval tv = {.tv_sec = 0, .tv_usec = 10/*1e6 / ctx->fps*/};
            event_add(ctx->ev, &tv);
        } else {
            send_nalu(ctx);
            event_del(ctx->ev);
            (*ctx->streams_playing)--;
            if (0 == *ctx->streams_playing) {
                bufferevent_trigger_event(ctx->bev, BEV_EVENT_EOF, 0);
            }
        }
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

    if (unit_type == SMOLRTSP_H264_NAL_UNIT_AUD || 
        unit_type == SMOLRTSP_H264_NAL_UNIT_CODED_SLICE_NON_IDR ||
        unit_type == SMOLRTSP_H264_NAL_UNIT_CODED_SLICE_IDR
    ) {
        ctx->timestamp += ctx->sample_rate / ctx->fps;
        au_found = true;
    }
    //printf("timestamp: %d-%d-%d\n", ctx->timestamp, SmolRTSP_RtpTimestamp_Raw(ctx->timestamp), SmolRTSP_RtpTimestamp_SysClockUs(ctx->timestamp));
    if (ctx->has_extension) {
        if (SmolRTSP_NalTransport_send_packet_ext(
                ctx->transport, SmolRTSP_RtpTimestamp_Raw(ctx->timestamp), nalu, &ctx->extension, sizeof(ctx->extension)) ==
            -1) {
            perror("Failed to send RTP/NAL");
        }
    } else {
        if (SmolRTSP_NalTransport_send_packet(
                ctx->transport, SmolRTSP_RtpTimestamp_Raw(ctx->timestamp), nalu) ==
            -1) {
            perror("Failed to send RTP/NAL");
        }
    }
    

    return au_found;
}

static bool send_h265_nalu(VideoCtx *ctx) {
    
    const SmolRTSP_NalUnit nalu = {
        .header = SmolRTSP_NalHeader_H265(
            SmolRTSP_H265NalHeader_parse(ctx->nalu_start[0])),
        .payload = U8Slice99_from_ptrdiff(ctx->nalu_start + 1, ctx->video.ptr),
    };

    bool au_found = false;
  
    if (SmolRTSP_NalHeader_unit_type(nalu.header) ==
        SMOLRTSP_H265_NAL_UNIT_AUD_NUT) {
        ctx->timestamp += ctx->sample_rate / ctx->fps;
        au_found = true;
    }
    
    if (SmolRTSP_NalTransport_send_packet(
            ctx->transport, SmolRTSP_RtpTimestamp_Raw(ctx->timestamp), nalu) ==
        -1) {
        perror("Failed to send RTP/NAL");
    }
    
    return au_found;
}

static bool send_nalu(VideoCtx *ctx) {
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
        printf("Invalid parameter.\n");
        return NULL;
    }

    struct rtsp_server *server = (struct rtsp_server *)malloc(sizeof(struct rtsp_server));
    if (!server) {
        printf("Failed to allocate memory for server.\n");
        return NULL;
    }
    memset(server, 0, sizeof(struct rtsp_server));
    server->param = *param;
    srand(time(NULL));
    // Create event base
    server->base = event_base_new();
    if (!server->base) {
        printf("event_base_new failed.\n");
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
        printf("evconnlistener_new_bind failed.\n");
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
        printf("Invalid server pointer.\n");
        return EXIT_FAILURE;
    }

    struct rtsp_server *rtsp_server = (struct rtsp_server *)server;

    if (rtsp_server->is_running) {
        printf("Server is already running on port %d.\n", rtsp_server->param.port);
        return EXIT_SUCCESS;
    }

    printf("Server started on port %d.\n", rtsp_server->param.port);
    rtsp_server->is_running = true;

    // Create thread to run event loop
    pthread_t thread_id;
    if (pthread_create(&thread_id, NULL, event_loop_thread, rtsp_server) != 0) {
        perror("pthread_create failed");
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
        printf("Invalid server pointer.\n");
        return EXIT_FAILURE;
    }

    struct rtsp_server *rtsp_server = (struct rtsp_server *)server;

    if (!rtsp_server->is_running) {
        printf("Server is not running.\n");
        return EXIT_SUCCESS;
    }

    // Stop the event loop
    event_base_loopbreak(rtsp_server->base);
    rtsp_server->is_running = false;
    puts("Server stopped.");
    return EXIT_SUCCESS;
}

int destroy_server(void *server)
{
    if (!server) {
        printf("Invalid server pointer.\n");
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
    puts("Server destroyed.");
    return EXIT_SUCCESS;
}

int register_function(void *server, func_id_t id, func_t func)
{
    if (!server) {
        printf("Invalid server pointer.\n");
        return EXIT_FAILURE;
    }

    struct rtsp_server *rtsp_server = (struct rtsp_server *)server;
    rtsp_server->funcs[id] = func;
    return EXIT_SUCCESS;
}

int is_server_running(void *server)
{
    if (!server) {
        printf("Invalid server pointer.\n");
        return 0;
    }

    struct rtsp_server *rtsp_server = (struct rtsp_server *)server;
    return rtsp_server->is_running? 1:0;
}

int set_server_param(void *server, int param_id, void *param, size_t size)
{
    if (!server) {
        printf("Invalid server pointer.\n");
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
            printf("Invalid param id.\n");
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