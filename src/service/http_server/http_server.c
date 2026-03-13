/**
 * @file http_server.c
 * @brief HTTP Server 实现
 *
 * 基于 CivetWeb 的轻量级 HTTP Server 实现。
 */

#include "http_server.h"
#include "http_api.h"
#include <civetweb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 默认配置 */
#define DEFAULT_PORT        80
#define DEFAULT_NUM_THREADS 2

/* 服务器状态 */
static struct mg_context* g_ctx = NULL;
static int g_running = 0;
static HttpServerConfig g_config;

/* 默认请求处理器 */
static int log_message_handler(const struct mg_connection* conn, const char* message) {
    (void)conn;

    if (message != NULL) {
        printf("[HTTP][CIVETWEB] %s\n", message);
    }

    return 0;
}

static int default_handler(struct mg_connection* conn, void* cbdata) {
    (void)cbdata;
    
    const struct mg_request_info* req_info = mg_get_request_info(conn);
    
    mg_printf(conn,
              "HTTP/1.1 404 Not Found\r\n"
              "Content-Type: application/json\r\n"
              "Connection: close\r\n\r\n"
              "{\"error\":\"Not Found\",\"path\":\"%s\"}",
              req_info->local_uri);
    
    return 404;
}

int http_server_init(const HttpServerConfig* config) {
    if (g_ctx != NULL) {
        printf("[HTTP] Already initialized\n");
        return -1;
    }
    
    /* 使用默认配置或用户配置 */
    if (config != NULL) {
        g_config = *config;
    } else {
        g_config.port = DEFAULT_PORT;
        g_config.document_root = NULL;
        g_config.num_threads = DEFAULT_NUM_THREADS;
    }
    
    /* 确保端口有效 */
    if (g_config.port <= 0 || g_config.port > 65535) {
        g_config.port = DEFAULT_PORT;
    }
    
    if (g_config.num_threads <= 0) {
        g_config.num_threads = DEFAULT_NUM_THREADS;
    }
    
    printf("[HTTP] Initialized with port=%d, threads=%d\n", 
           g_config.port, g_config.num_threads);
    
    return 0;
}

int http_server_start(void) {
    if (g_running) {
        printf("[HTTP] Already running\n");
        return -1;
    }
    
    /* 构建配置选项 */
    char port_str[16];
    char threads_str[16];
    snprintf(port_str, sizeof(port_str), "%d", g_config.port);
    snprintf(threads_str, sizeof(threads_str), "%d", g_config.num_threads);
    
    const char* options[] = {
        "listening_ports", port_str,
        "num_threads", threads_str,
        "request_timeout_ms", "10000",
        NULL
    };
    
    /* 初始化 CivetWeb 库 */
    /* 注意: mg_init_library 返回初始化的特性位掩码
     * 当传入 MG_FEATURES_DEFAULT(0) 时，返回 0 是正常的
     * 我们使用 MG_FEATURES_FILES 来确保返回非零值 */
    unsigned features = mg_init_library(MG_FEATURES_FILES);
    printf("[HTTP] CivetWeb library initialized with features: 0x%x\n", features);
    
    /* 启动服务器 */
    struct mg_callbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.log_message = log_message_handler;
    
    struct mg_init_data init_data;
    struct mg_error_data error_data;
    char error_text[256];

    memset(&init_data, 0, sizeof(init_data));
    memset(&error_data, 0, sizeof(error_data));
    memset(error_text, 0, sizeof(error_text));

    error_data.text = error_text;
    error_data.text_buffer_size = sizeof(error_text);

    init_data.callbacks = &callbacks;
    init_data.user_data = NULL;
    init_data.configuration_options = options;

    g_ctx = mg_start2(&init_data, &error_data);
    if (g_ctx == NULL) {
        printf("[HTTP] Failed to start server: code=%u sub=%u text=%s\n",
               error_data.code,
               error_data.code_sub,
               error_data.text != NULL ? error_data.text : "(none)");
        mg_exit_library();
        return -1;
    }
    
    /* 注册 API 路由 */
    http_api_register(g_ctx);
    
    /* 注册默认处理器 */
    mg_set_request_handler(g_ctx, "/**", default_handler, NULL);
    
    g_running = 1;
    printf("[HTTP] Server started on port %d\n", g_config.port);
    
    return 0;
}

int http_server_stop(void) {
    if (!g_running || g_ctx == NULL) {
        printf("[HTTP] Not running\n");
        return -1;
    }

    http_api_v1_shutdown();
    
    mg_stop(g_ctx);
    mg_exit_library();
    
    g_ctx = NULL;
    g_running = 0;
    
    printf("[HTTP] Server stopped\n");
    return 0;
}

void http_server_deinit(void) {
    if (g_running) {
        http_server_stop();
    }
    
    memset(&g_config, 0, sizeof(g_config));
    printf("[HTTP] Deinitialized\n");
}

int http_server_is_running(void) {
    return g_running;
}
