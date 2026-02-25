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
 * @brief 注册所有 API 路由
 *
 * @param ctx CivetWeb 上下文
 */
void http_api_register(struct mg_context* ctx);

#ifdef __cplusplus
}
#endif

#endif /* HTTP_API_H */
