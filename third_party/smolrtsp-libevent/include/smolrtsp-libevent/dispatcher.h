#pragma once

#include <smolrtsp/controller.h>

#include <event2/bufferevent.h>

#ifdef __cplusplus
extern "C" {
#endif
void smolrtsp_libevent_cb(struct bufferevent *bev, void *ctx);

void *smolrtsp_libevent_ctx(SmolRTSP_Controller controller);
SmolRTSP_Controller smolrtsp_libevent_ctx_controller(void *ctx);
void smolrtsp_libevent_ctx_free(void *ctx);

#ifdef __cplusplus
}
#endif