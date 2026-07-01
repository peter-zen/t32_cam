/*
 * This file is part of the EasyLogger Library.
 *
 * Copyright (c) 2015-2019, Armink, <armink.ztl@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * 'Software'), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED 'AS IS', WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Function: Asynchronous output ring buffer (drop-oldest). Backs the
 *           ELOG_ASYNC_OUTPUT_ENABLE path in elog.c. The consumer thread
 *           that drains this ring lives in elog_port.c.
 * Created on: 2026-06-30
 */

#include <elog.h>
#include <pthread.h>
#include <string.h>

#ifndef ELOG_ASYNC_OUTPUT_BUF_SIZE
#define ELOG_ASYNC_OUTPUT_BUF_SIZE 128
#endif

/* One ring slot: the level the core tagged the line with, its length, and the
 * formatted bytes (already color/newline-decorated by the core). */
typedef struct {
    uint8_t level;
    size_t  len;
    char    buf[ELOG_LINE_BUF_SIZE];
} elog_async_slot_t;

/* Statically allocated ring — no malloc, so elog_async_deinit() has nothing to
 * free. This makes the core's elog_async_deinit()-before-elog_port_deinit()
 * ordering safe (the consumer thread, joined in elog_port_deinit, can keep
 * reading the ring after async_deinit ran). */
static elog_async_slot_t s_ring[ELOG_ASYNC_OUTPUT_BUF_SIZE];
static volatile size_t s_head  = 0;   /* pop index (oldest entry) */
static volatile size_t s_tail  = 0;   /* push index (next free / wrap) */
static volatile size_t s_count = 0;   /* entries currently in the ring */
static volatile size_t s_drop_count = 0;
static bool s_enabled = false;
static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;

/**
 * EasyLogger async initialize
 */
ElogErrCode elog_async_init(void) {
    pthread_mutex_lock(&s_lock);
    s_head = 0;
    s_tail = 0;
    s_count = 0;
    s_drop_count = 0;
    s_enabled = false;
    memset(s_ring, 0, sizeof(s_ring));
    pthread_mutex_unlock(&s_lock);
    return ELOG_NO_ERR;
}

/**
 * EasyLogger async deinitialize
 * Nothing to free (static ring); just mark disabled. The consumer thread is
 * joined separately in elog_port_deinit().
 */
ElogErrCode elog_async_deinit(void) {
    pthread_mutex_lock(&s_lock);
    s_enabled = false;
    pthread_mutex_unlock(&s_lock);
    return ELOG_NO_ERR;
}

/**
 * Enable or disable the async output path
 */
void elog_async_enabled(bool enabled) {
    pthread_mutex_lock(&s_lock);
    s_enabled = enabled;
    pthread_mutex_unlock(&s_lock);
}

/**
 * Push one formatted line into the ring.
 *
 * Called by the core under the core output_lock. When the ring is full the
 * oldest entry is dropped (head advances) and the drop counter is bumped, so
 * the ring behaves as a sliding window of the newest entries — the most
 * useful state for post-mortem diagnosis under a burst.
 */
void elog_async_output(uint8_t level, const char *log, size_t size) {
    if (log == NULL || size == 0) {
        return;
    }
    pthread_mutex_lock(&s_lock);
    if (size > ELOG_LINE_BUF_SIZE) {
        size = ELOG_LINE_BUF_SIZE;
    }
    if (s_count == ELOG_ASYNC_OUTPUT_BUF_SIZE) {
        /* full: drop the oldest entry to make room */
        s_head = (s_head + 1) % ELOG_ASYNC_OUTPUT_BUF_SIZE;
        s_drop_count++;
    } else {
        s_count++;
    }
    {
        elog_async_slot_t *slot = &s_ring[s_tail];
        slot->level = level;
        slot->len = size;
        memcpy(slot->buf, log, size);
        s_tail = (s_tail + 1) % ELOG_ASYNC_OUTPUT_BUF_SIZE;
    }
    pthread_mutex_unlock(&s_lock);
}

/**
 * Pop one entry off the ring (non-blocking).
 *
 * @param log       destination buffer
 * @param size      capacity of destination
 * @return bytes copied (0 if the ring is empty)
 */
size_t elog_async_get_log(char *log, size_t size) {
    size_t ret = 0;
    if (log == NULL || size == 0) {
        return 0;
    }
    pthread_mutex_lock(&s_lock);
    if (s_count > 0) {
        elog_async_slot_t *slot = &s_ring[s_head];
        size_t n = slot->len;
        if (n > size) {
            n = size;
        }
        memcpy(log, slot->buf, n);
        s_head = (s_head + 1) % ELOG_ASYNC_OUTPUT_BUF_SIZE;
        s_count--;
        ret = n;
    }
    pthread_mutex_unlock(&s_lock);
    return ret;
}

/**
 * Pop one line off the ring (non-blocking). Each push is exactly one formatted
 * line, so this is equivalent to elog_async_get_log here.
 */
size_t elog_async_get_line_log(char *log, size_t size) {
    return elog_async_get_log(log, size);
}

/*
 * Extension (not part of upstream EasyLogger): cumulative count of entries
 * dropped because the ring was full. The consumer thread reports this
 * periodically for ring-size tuning.
 */
size_t elog_async_get_drop_count(void) {
    size_t c;
    pthread_mutex_lock(&s_lock);
    c = s_drop_count;
    pthread_mutex_unlock(&s_lock);
    return c;
}
