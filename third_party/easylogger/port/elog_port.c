/*
 * This file is part of the EasyLogger Library.
 *
 * Copyright (c) 2015, Armink, <armink.ztl@gmail.com>
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
 * Function: Portable interface for HTC firmware project.
 *           Supports both PC simulation (x86_64) and target hardware (Ingenic T32 MIPS).
 * Created on: 2026-01-14
 */

#include <elog.h>
#include <stdio.h>
#include <pthread.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <string.h>

/* Output mutex for thread safety */
static pthread_mutex_t output_lock = PTHREAD_MUTEX_INITIALIZER;

/* Time string buffer - format: "YYYY-MM-DD HH:MM:SS.mmm" (23 chars + null) */
static char time_buf[32] = {0};

/* Process info buffer */
static char p_info_buf[16] = {0};

/* Thread info buffer */
static char t_info_buf[32] = {0};

/* File output handle (optional) */
static FILE *log_file = NULL;

/* Output flags */
static int terminal_output_enabled = 1;
static int file_output_enabled = 0;

/* ------------------------------------------------------------------------
 * Async consumer thread.
 *
 * With ELOG_ASYNC_OUTPUT_ENABLE, the core pushes formatted lines into the
 * elog_async.c ring (under the core output_lock) and never calls
 * elog_port_output() itself. This single dedicated thread drains the ring and
 * performs the fwrite/fflush, so producer threads (RTSP/capture/encode) pay
 * only the cost of vsnprintf + a ring push — never I/O.
 * ------------------------------------------------------------------------ */

/* Provided by elog_async.c (the second is an extension for observability). */
extern size_t elog_async_get_log(char *log, size_t size);
extern size_t elog_async_get_drop_count(void);

static pthread_t s_consumer_thread;
static int s_consumer_running = 0;
static volatile int s_consumer_stop = 0;

#define ELOG_ASYNC_FLUSH_INTERVAL_MS  100       /* max delay before a forced flush */
#define ELOG_ASYNC_POLL_US            5000      /* idle poll interval */
#define ELOG_ASYNC_DROP_REPORT_MS     1000      /* report drops at most ~1/s */

/* Monotonic-ish millisecond clock for flush/drop-report cadence. gettimeofday
 * avoids any -lrt dependency on the T32 toolchains. */
static long elog_now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

/* Write one formatted line to all enabled sinks. Only the single consumer
 * thread calls this in async mode, so no FILE* locking is needed. No flush
 * here — the caller batches flushes. */
static void elog_write_line(const char *log, size_t size) {
    if (terminal_output_enabled) {
        fwrite(log, 1, size, stdout);
    }
    if (file_output_enabled && log_file != NULL) {
        fwrite(log, 1, size, log_file);
    }
}

static void elog_flush_sinks(void) {
    if (terminal_output_enabled) {
        fflush(stdout);
    }
    if (file_output_enabled && log_file != NULL) {
        fflush(log_file);
    }
}

static void *elog_consumer_loop(void *arg) {
    (void)arg;
    char line[ELOG_LINE_BUF_SIZE];
    long last_flush = elog_now_ms();
    long last_drop_report = last_flush;
    size_t last_drop_count = 0;
    int have_unflushed = 0;

    while (!s_consumer_stop) {
        size_t n = elog_async_get_log(line, sizeof(line));
        if (n > 0) {
            elog_write_line(line, n);
            have_unflushed = 1;
        }

        long t = elog_now_ms();
        int ring_idle = (n == 0);

        /* flush when the ring goes idle (natural batching) or every interval */
        if ((ring_idle && have_unflushed) ||
            (t - last_flush) >= ELOG_ASYNC_FLUSH_INTERVAL_MS) {
            if (have_unflushed) {
                elog_flush_sinks();
                have_unflushed = 0;
            }
            last_flush = t;
        }

        /* periodic drop-count report (stderr — never corrupts the log file) */
        if ((t - last_drop_report) >= ELOG_ASYNC_DROP_REPORT_MS) {
            size_t cur = elog_async_get_drop_count();
            if (cur != last_drop_count) {
                fprintf(stderr,
                        "[elog] async ring full: %zu log(s) dropped (total)\n",
                        cur);
                last_drop_count = cur;
            }
            last_drop_report = t;
        }

        if (ring_idle) {
            usleep(ELOG_ASYNC_POLL_US);
        }
    }

    /* shutdown: drain everything that remains, then a final flush so the
     * poweroff/crash-scene logs survive the board freeze */
    for (;;) {
        size_t n = elog_async_get_log(line, sizeof(line));
        if (n == 0) {
            break;
        }
        elog_write_line(line, n);
    }
    elog_flush_sinks();
    return NULL;
}

/**
 * EasyLogger port initialize
 *
 * @return result
 */
ElogErrCode elog_port_init(void) {
    /* Initialize mutex (already done statically) */

    /* Get process ID */
    snprintf(p_info_buf, sizeof(p_info_buf), "%d", (int)getpid());

    /* Start the async consumer thread that drains the elog_async.c ring. */
    s_consumer_stop = 0;
    if (pthread_create(&s_consumer_thread, NULL, elog_consumer_loop, NULL) == 0) {
        s_consumer_running = 1;
    } else {
        fprintf(stderr, "[ELOG] failed to start async consumer thread\n");
    }

    return ELOG_NO_ERR;
}

/**
 * EasyLogger port deinitialize
 *
 */
void elog_port_deinit(void) {
    /* Stop the consumer thread first so it drains the remaining ring entries
     * and does a final flush before we close the file. elog_stop() has already
     * disabled new pushes by the time we get here (see elog_deinit_all). */
    if (s_consumer_running) {
        s_consumer_stop = 1;
        pthread_join(s_consumer_thread, NULL);
        s_consumer_running = 0;
    }

    /* Close log file if open */
    if (log_file != NULL) {
        fclose(log_file);
        log_file = NULL;
    }

    /* Destroy mutex */
    pthread_mutex_destroy(&output_lock);
}

/**
 * output log port interface
 *
 * @param log output of log
 * @param size log size
 */
void elog_port_output(const char *log, size_t size) {
    /* Output to terminal */
    if (terminal_output_enabled) {
        /* Use fwrite for better performance with large logs */
        fwrite(log, 1, size, stdout);
        fflush(stdout);
    }
    
    /* Output to file */
    if (file_output_enabled && log_file != NULL) {
        fwrite(log, 1, size, log_file);
        fflush(log_file);
    }
}

/**
 * output lock
 */
void elog_port_output_lock(void) {
    pthread_mutex_lock(&output_lock);
}

/**
 * output unlock
 */
void elog_port_output_unlock(void) {
    pthread_mutex_unlock(&output_lock);
}

/**
 * get current time interface
 * 
 * @return current time string with millisecond precision
 *         format: "YYYY-MM-DD HH:MM:SS.mmm"
 */
const char *elog_port_get_time(void) {
    struct timeval tv;
    struct tm *tm_info;
    
    /* Get current time with microsecond precision */
    gettimeofday(&tv, NULL);
    
    /* Convert to local time */
    tm_info = localtime(&tv.tv_sec);
    
    /* Format: "YYYY-MM-DD HH:MM:SS.mmm" */
    snprintf(time_buf, sizeof(time_buf), 
             "%04d-%02d-%02d %02d:%02d:%02d.%03ld",
             tm_info->tm_year + 1900,
             tm_info->tm_mon + 1,
             tm_info->tm_mday,
             tm_info->tm_hour,
             tm_info->tm_min,
             tm_info->tm_sec,
             tv.tv_usec / 1000);  /* Convert microseconds to milliseconds */
    
    return time_buf;
}

/**
 * get current process info interface
 *
 * @return current process info
 */
const char *elog_port_get_p_info(void) {
    return p_info_buf;
}

/**
 * get current thread info interface
 *
 * @return current thread info
 */
const char *elog_port_get_t_info(void) {
    /* Get thread ID */
    pthread_t tid = pthread_self();
    snprintf(t_info_buf, sizeof(t_info_buf), "%lu", (unsigned long)tid);
    return t_info_buf;
}

/*
 * Extended port functions for configuration
 */

/**
 * Enable or disable terminal output
 *
 * @param enabled 1: enable, 0: disable
 */
void elog_port_set_terminal_output(int enabled) {
    terminal_output_enabled = enabled;
}

/**
 * Enable file output with specified path
 *
 * @param file_path path to log file, NULL to disable
 * @return 0 on success, -1 on failure
 */
int elog_port_set_file_output(const char *file_path) {
    /* Close existing file if open */
    if (log_file != NULL) {
        fclose(log_file);
        log_file = NULL;
    }
    
    if (file_path == NULL) {
        file_output_enabled = 0;
        return 0;
    }
    
    /* Open new log file with truncate mode (overwrite) */
    log_file = fopen(file_path, "w");
    if (log_file == NULL) {
        file_output_enabled = 0;
        fprintf(stderr, "[ELOG] Failed to open log file: %s\n", file_path);
        return -1;
    }
    
    file_output_enabled = 1;
    return 0;
}

/**
 * Get terminal output status
 *
 * @return 1 if enabled, 0 if disabled
 */
int elog_port_get_terminal_output(void) {
    return terminal_output_enabled;
}

/**
 * Get file output status
 *
 * @return 1 if enabled, 0 if disabled
 */
int elog_port_get_file_output(void) {
    return file_output_enabled;
}
