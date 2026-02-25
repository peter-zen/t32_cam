/**
 * @file test_http_server.c
 * @brief HTTP Server 测试程序
 *
 * 用于验证 HTTP Server 基本功能。
 * 编译: gcc -o test_http test_http_server.c -L. -lhttp_server -lcivetweb -lpthread
 */

#include "http_server.h"
#include <stdio.h>
#include <signal.h>
#include <unistd.h>

static volatile int g_running = 1;

static void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    
    printf("=== HTTP Server Test ===\n\n");
    
    /* 设置信号处理 */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    /* 配置服务器 */
    HttpServerConfig config = {
        .port = 8080,
        .document_root = NULL,
        .num_threads = 2
    };
    
    /* 初始化 */
    if (http_server_init(&config) != 0) {
        printf("Failed to init HTTP server\n");
        return 1;
    }
    
    /* 启动 */
    if (http_server_start() != 0) {
        printf("Failed to start HTTP server\n");
        http_server_deinit();
        return 1;
    }
    
    printf("\nHTTP Server running on http://localhost:8080\n");
    printf("Try these endpoints:\n");
    printf("  curl http://localhost:8080/api/health\n");
    printf("  curl http://localhost:8080/api/device/info\n");
    printf("  curl http://localhost:8080/api/sensor/data\n");
    printf("\nPress Ctrl+C to stop...\n\n");
    
    /* 主循环 */
    while (g_running) {
        sleep(1);
    }
    
    printf("\nShutting down...\n");
    
    /* 停止并清理 */
    http_server_stop();
    http_server_deinit();
    
    printf("Done.\n");
    return 0;
}
