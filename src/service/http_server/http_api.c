/**
 * @file http_api.c
 * @brief HTTP API 实现
 *
 * 实现设备信息、传感器数据、参数管理、录像控制等 API。
 * 替代原有的 RemoteCtrlClient TCP 通信功能。
 */

#include "http_api.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>

/* JSON 响应辅助宏 */
#define JSON_RESPONSE_OK(conn, json) \
    mg_printf(conn, \
              "HTTP/1.1 200 OK\r\n" \
              "Content-Type: application/json\r\n" \
              "Access-Control-Allow-Origin: *\r\n" \
              "Connection: close\r\n\r\n" \
              "%s", json)

#define JSON_RESPONSE_ERROR(conn, code, msg) \
    mg_printf(conn, \
              "HTTP/1.1 %d %s\r\n" \
              "Content-Type: application/json\r\n" \
              "Access-Control-Allow-Origin: *\r\n" \
              "Connection: close\r\n\r\n" \
              "{\"error\":\"%s\"}", code, msg, msg)

/* 读取 POST body */
static int read_post_body(struct mg_connection* conn, char* buf, size_t buf_size) {
    int content_len = 0;
    const char* content_len_str = mg_get_header(conn, "Content-Length");
    if (content_len_str) {
        content_len = atoi(content_len_str);
    }
    if (content_len <= 0 || (size_t)content_len >= buf_size) {
        return -1;
    }
    int read_len = mg_read(conn, buf, content_len);
    if (read_len > 0) {
        buf[read_len] = '\0';
    }
    return read_len;
}

/**
 * @brief GET /api/health - 健康检查
 */
static int api_health(struct mg_connection* conn, void* cbdata) {
    (void)cbdata;
    JSON_RESPONSE_OK(conn, "{\"status\":\"ok\"}");
    return 200;
}

/**
 * @brief GET /api/device/info - 获取设备信息
 * 替代: MSG_RC_GET_HW_INFO
 */
static int api_device_info(struct mg_connection* conn, void* cbdata) {
    (void)cbdata;
    
    /* TODO: 从实际模块获取数据 */
    char json[1024];
    snprintf(json, sizeof(json),
             "{"
             "\"status\":0,"
             "\"pid\":\"T32-CAM-001\","
             "\"camera_ver\":\"1.0.0\","
             "\"camera_model\":\"T32\","
             "\"camera_build\":\"2025-01-06\","
             "\"mcu_ver\":\"MCU-1.0.0\""
             "}");
    
    JSON_RESPONSE_OK(conn, json);
    return 200;
}

/**
 * @brief GET /api/sensor/data - 获取传感器数据
 * 替代: MSG_RC_GET_SENSOR_INFO
 */
static int api_sensor_data(struct mg_connection* conn, void* cbdata) {
    (void)cbdata;
    
    /* 获取当前时间 */
    char datetime_buf[32];
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    strftime(datetime_buf, sizeof(datetime_buf), "%Y-%m-%dT%H:%M:%S.000", tm_info);
    
    /* TODO: 从 HAL MCU 获取真实数据 */
    char json[1024];
    snprintf(json, sizeof(json),
             "{"
             "\"status\":0,"
             "\"battery\":3700,"
             "\"battery_type\":1,"
             "\"battery_level\":85,"
             "\"ext_power\":12000,"
             "\"sdcard_capacity\":32000,"
             "\"sdcard_used\":8000,"
             "\"cds\":500,"
             "\"temp\":\"25\","
             "\"press\":\"1013\","
             "\"rh\":\"60\","
             "\"datetime\":\"%s\""
             "}", datetime_buf);
    
    JSON_RESPONSE_OK(conn, json);
    return 200;
}

/**
 * @brief GET /api/params - 获取所有参数
 * 替代: MSG_RC_GET_PARAM_ALL
 */
static int api_params_get(struct mg_connection* conn, void* cbdata) {
    (void)cbdata;
    
    /* TODO: 从 Settings 获取真实数据 */
    char json[4096];
    snprintf(json, sizeof(json),
             "{"
             "\"status\":0,"
             "\"param\":{"
             "\"CAM_Mode\":0,"
             "\"CAM_ImageSize\":{\"options\":[\"2M\",\"4M\",\"8M\",\"12M\"],\"Selected\":3},"
             "\"CAM_Shooting\":1,"
             "\"CAM_MaxShooting\":99,"
             "\"CAM_VideoSize\":{\"options\":[\"720P/30FPS\",\"1080P/30FPS\",\"4K/30FPS\"],\"Selected\":2},"
             "\"CAM_VideoLength\":30,"
             "\"PIR_Enable\":1,"
             "\"PIR_Sensitivity\":2,"
             "\"PIR_Interval\":5,"
             "\"Timer_Enable\":0,"
             "\"Timer_Interval\":\"00:30\","
             "\"Other_Stamp\":1,"
             "\"Other_Cycle\":1,"
             "\"Dev_Name\":\"Camera-001\""
             "}"
             "}");
    
    JSON_RESPONSE_OK(conn, json);
    return 200;
}

/**
 * @brief POST /api/params - 设置参数
 * 替代: MSG_RC_SET_PARAM
 */
static int api_params_set(struct mg_connection* conn, void* cbdata) {
    (void)cbdata;
    
    const struct mg_request_info* req_info = mg_get_request_info(conn);
    
    if (strcmp(req_info->request_method, "POST") != 0) {
        JSON_RESPONSE_ERROR(conn, 405, "Method Not Allowed");
        return 405;
    }
    
    /* 读取请求体 */
    char body[4096];
    int body_len = read_post_body(conn, body, sizeof(body));
    if (body_len <= 0) {
        JSON_RESPONSE_ERROR(conn, 400, "Invalid request body");
        return 400;
    }
    
    printf("[HTTP API] Set params: %s\n", body);
    
    /* TODO: 解析 JSON 并设置参数到 Settings */
    
    JSON_RESPONSE_OK(conn, "{\"status\":0}");
    return 200;
}

/**
 * @brief POST /api/params/reset - 重置参数
 * 替代: MSG_RC_RESET_PARAM
 */
static int api_params_reset(struct mg_connection* conn, void* cbdata) {
    (void)cbdata;
    
    const struct mg_request_info* req_info = mg_get_request_info(conn);
    
    if (strcmp(req_info->request_method, "POST") != 0) {
        JSON_RESPONSE_ERROR(conn, 405, "Method Not Allowed");
        return 405;
    }
    
    printf("[HTTP API] Reset params requested\n");
    
    /* TODO: 调用 Settings 重置功能 */
    
    JSON_RESPONSE_OK(conn, "{\"status\":0}");
    return 200;
}

/**
 * @brief POST /api/system/datetime - 设置日期时间
 * 替代: MSG_RC_SET_DATETIME
 */
static int api_system_datetime(struct mg_connection* conn, void* cbdata) {
    (void)cbdata;
    
    const struct mg_request_info* req_info = mg_get_request_info(conn);
    
    if (strcmp(req_info->request_method, "POST") != 0) {
        JSON_RESPONSE_ERROR(conn, 405, "Method Not Allowed");
        return 405;
    }
    
    char body[256];
    int body_len = read_post_body(conn, body, sizeof(body));
    if (body_len <= 0) {
        JSON_RESPONSE_ERROR(conn, 400, "Invalid request body");
        return 400;
    }
    
    printf("[HTTP API] Set datetime: %s\n", body);
    
    /* TODO: 解析日期时间并设置系统时间、RTC、MCU */
    
    JSON_RESPONSE_OK(conn, "{\"status\":0}");
    return 200;
}

/**
 * @brief POST /api/system/workmode - 进入工作模式
 * 替代: MSG_RC_ENTER_WORK_MODE
 */
static int api_system_workmode(struct mg_connection* conn, void* cbdata) {
    (void)cbdata;
    
    const struct mg_request_info* req_info = mg_get_request_info(conn);
    
    if (strcmp(req_info->request_method, "POST") != 0) {
        JSON_RESPONSE_ERROR(conn, 405, "Method Not Allowed");
        return 405;
    }
    
    printf("[HTTP API] Enter work mode requested\n");
    
    /* TODO: 调用 Power::getInstance()->requestChangeMode() */
    
    JSON_RESPONSE_OK(conn, "{\"status\":0}");
    return 200;
}

/**
 * @brief POST /api/storage/format - 格式化 SD 卡
 * 替代: MSG_RC_FORMAT_SDCARD
 */
static int api_storage_format(struct mg_connection* conn, void* cbdata) {
    (void)cbdata;
    
    const struct mg_request_info* req_info = mg_get_request_info(conn);
    
    if (strcmp(req_info->request_method, "POST") != 0) {
        JSON_RESPONSE_ERROR(conn, 405, "Method Not Allowed");
        return 405;
    }
    
    printf("[HTTP API] Format SD card requested\n");
    
    /* TODO: 调用 SD 卡格式化功能 */
    
    JSON_RESPONSE_OK(conn, "{\"status\":0}");
    return 200;
}

/**
 * @brief GET /api/storage/info - 获取存储信息
 */
static int api_storage_info(struct mg_connection* conn, void* cbdata) {
    (void)cbdata;
    
    /* TODO: 从 Disk 模块获取真实数据 */
    char json[256];
    snprintf(json, sizeof(json),
             "{"
             "\"status\":0,"
             "\"total\":32000,"
             "\"free\":24000,"
             "\"used\":8000"
             "}");
    
    JSON_RESPONSE_OK(conn, json);
    return 200;
}

/**
 * @brief GET /api/record/status - 获取录像状态
 */
static int api_record_status(struct mg_connection* conn, void* cbdata) {
    (void)cbdata;
    
    /* TODO: 从录像模块获取真实状态 */
    char json[256];
    snprintf(json, sizeof(json),
             "{"
             "\"status\":0,"
             "\"recording\":false,"
             "\"duration\":0,"
             "\"file\":null"
             "}");
    
    JSON_RESPONSE_OK(conn, json);
    return 200;
}

/**
 * @brief POST /api/record/start - 开始录像
 */
static int api_record_start(struct mg_connection* conn, void* cbdata) {
    (void)cbdata;
    
    const struct mg_request_info* req_info = mg_get_request_info(conn);
    
    if (strcmp(req_info->request_method, "POST") != 0) {
        JSON_RESPONSE_ERROR(conn, 405, "Method Not Allowed");
        return 405;
    }
    
    printf("[HTTP API] Record start requested\n");
    
    /* TODO: 调用录像模块开始录像 */
    
    JSON_RESPONSE_OK(conn, "{\"status\":0,\"message\":\"Recording started\"}");
    return 200;
}

/**
 * @brief POST /api/record/stop - 停止录像
 */
static int api_record_stop(struct mg_connection* conn, void* cbdata) {
    (void)cbdata;
    
    const struct mg_request_info* req_info = mg_get_request_info(conn);
    
    if (strcmp(req_info->request_method, "POST") != 0) {
        JSON_RESPONSE_ERROR(conn, 405, "Method Not Allowed");
        return 405;
    }
    
    printf("[HTTP API] Record stop requested\n");
    
    /* TODO: 调用录像模块停止录像 */
    
    JSON_RESPONSE_OK(conn, "{\"status\":0,\"message\":\"Recording stopped\"}");
    return 200;
}

/**
 * @brief GET /api/snapshot - 获取快照
 */
static int api_snapshot(struct mg_connection* conn, void* cbdata) {
    (void)cbdata;
    
    printf("[HTTP API] Snapshot requested\n");
    
    /* TODO: 从编码器获取 JPEG 快照 */
    
    JSON_RESPONSE_OK(conn, "{\"status\":0,\"message\":\"Snapshot not implemented\"}");
    return 200;
}

void http_api_register(struct mg_context* ctx) {
    /* 健康检查 */
    mg_set_request_handler(ctx, "/api/health", api_health, NULL);
    
    /* 设备信息 API (替代 MSG_RC_GET_HW_INFO) */
    mg_set_request_handler(ctx, "/api/device/info", api_device_info, NULL);
    
    /* 传感器数据 API (替代 MSG_RC_GET_SENSOR_INFO) */
    mg_set_request_handler(ctx, "/api/sensor/data", api_sensor_data, NULL);
    
    /* 参数管理 API (替代 MSG_RC_GET_PARAM_ALL, MSG_RC_SET_PARAM, MSG_RC_RESET_PARAM) */
    mg_set_request_handler(ctx, "/api/params", api_params_get, NULL);
    mg_set_request_handler(ctx, "/api/params/set", api_params_set, NULL);
    mg_set_request_handler(ctx, "/api/params/reset", api_params_reset, NULL);
    
    /* 系统控制 API (替代 MSG_RC_SET_DATETIME, MSG_RC_ENTER_WORK_MODE) */
    mg_set_request_handler(ctx, "/api/system/datetime", api_system_datetime, NULL);
    mg_set_request_handler(ctx, "/api/system/workmode", api_system_workmode, NULL);
    
    /* 存储管理 API (替代 MSG_RC_FORMAT_SDCARD) */
    mg_set_request_handler(ctx, "/api/storage/info", api_storage_info, NULL);
    mg_set_request_handler(ctx, "/api/storage/format", api_storage_format, NULL);
    
    /* 录像控制 API */
    mg_set_request_handler(ctx, "/api/record/status", api_record_status, NULL);
    mg_set_request_handler(ctx, "/api/record/start", api_record_start, NULL);
    mg_set_request_handler(ctx, "/api/record/stop", api_record_stop, NULL);
    
    /* 快照 API */
    mg_set_request_handler(ctx, "/api/snapshot", api_snapshot, NULL);
    
    printf("[HTTP API] Registered API endpoints:\n");
    printf("  GET  /api/health           - Health check\n");
    printf("  GET  /api/device/info      - Device info (replaces MSG_RC_GET_HW_INFO)\n");
    printf("  GET  /api/sensor/data      - Sensor data (replaces MSG_RC_GET_SENSOR_INFO)\n");
    printf("  GET  /api/params           - Get all params (replaces MSG_RC_GET_PARAM_ALL)\n");
    printf("  POST /api/params/set       - Set params (replaces MSG_RC_SET_PARAM)\n");
    printf("  POST /api/params/reset     - Reset params (replaces MSG_RC_RESET_PARAM)\n");
    printf("  POST /api/system/datetime  - Set datetime (replaces MSG_RC_SET_DATETIME)\n");
    printf("  POST /api/system/workmode  - Enter work mode (replaces MSG_RC_ENTER_WORK_MODE)\n");
    printf("  GET  /api/storage/info     - Storage info\n");
    printf("  POST /api/storage/format   - Format SD card (replaces MSG_RC_FORMAT_SDCARD)\n");
    printf("  GET  /api/record/status    - Record status\n");
    printf("  POST /api/record/start     - Start recording\n");
    printf("  POST /api/record/stop      - Stop recording\n");
    printf("  GET  /api/snapshot         - Get snapshot\n");
}
