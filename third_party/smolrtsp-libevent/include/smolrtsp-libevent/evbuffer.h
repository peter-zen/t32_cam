#pragma once

#include <event2/buffer.h>
#include <smolrtsp/writer.h>

#ifdef __cplusplus
extern "C" {
#endif

SmolRTSP_Writer smolrtsp_evbuffer_writer(struct evbuffer *evb);
CharSlice99 smolrtsp_evbuffer_slice(struct evbuffer *evb);

#ifdef __cplusplus
}
#endif