/**
 * @file http_api.h
 * @brief HTTP API 路由注册
 *
 * 定义和注册所有 RESTful API 端点。
 */

#ifndef HTTP_API_H
#define HTTP_API_H

#include <civetweb.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 注册所有 /api/v1/ 前缀的版本化路由
 *
 * @param ctx CivetWeb 上下文
 */
void http_api_register_v1(struct mg_context* ctx);

/**
 * @brief 停止 /api/v1/camera 相关后台任务
 */
void http_api_v1_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* HTTP_API_H */
