#pragma once

#include <event2/bufferevent.h>
#include <smolrtsp/writer.h>

#ifdef __cplusplus
extern "C" {
#endif  

SmolRTSP_Writer smolrtsp_bufferevent_writer(struct bufferevent *bev);

#ifdef __cplusplus
}
#endif
