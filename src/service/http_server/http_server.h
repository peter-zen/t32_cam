/**
 * @file http_server.h
 * @brief HTTP Server 模块接口
 *
 * 基于 CivetWeb 实现的轻量级 HTTP Server，提供 RESTful API 接口。
 * 用于替代原有的 TCP Client 通信方式。
 */

#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include <stdint.h>   /* uint32_t (http_server_request_seq) */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief HTTP Server 配置
 */
typedef struct {
    int port;                   /**< 监听端口 (默认 80) */
    const char* document_root;  /**< 静态文件根目录 (可选) */
    int num_threads;            /**< 工作线程数 (默认 2) */
} HttpServerConfig;

/**
 * @brief 初始化 HTTP Server
 *
 * @param config 服务器配置，NULL 使用默认配置
 * @return 0 成功，-1 失败
 */
int http_server_init(const HttpServerConfig* config);

/**
 * @brief 启动 HTTP Server
 *
 * @return 0 成功，-1 失败
 */
int http_server_start(void);

/**
 * @brief 停止 HTTP Server
 *
 * @return 0 成功，-1 失败
 */
int http_server_stop(void);

/**
 * @brief 释放 HTTP Server 资源
 */
void http_server_deinit(void);

/**
 * @brief 检查 HTTP Server 是否运行中
 *
 * @return 1 运行中，0 未运行
 */
int http_server_is_running(void);

/* 最近请求序号：begin_request 每请求递增。供 idle-timeout 做活跃 change-detection
 * （HTTP 未起时恒为 0）。线程安全：__sync 原子。 */
uint32_t http_server_request_seq(void);

#ifdef __cplusplus
}
#endif

#endif /* HTTP_SERVER_H */
