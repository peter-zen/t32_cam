/**
 * @file http_api_v1.cpp
 * @brief /api/v1 HTTP API implementation
 */

#include "http_api.h"
#include "PhotoJobManager.h"
#include "TcpEventService.h"
#include "CameraPropertyService.h"
#include "CameraServiceFactory.h"
#include "../../storage/DatabaseManager.h"
#include "../../storage/MediaScanner.h"
#include "../../storage/MetadataDao.h"

#include <elog.h>
#include <json/json.h>

#include <sys/stat.h>

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <algorithm>
#include <chrono>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <time.h>
#include <vector>

#define TAG "HttpApiV1"

namespace {

using PropertyMap = std::map<std::string, Json::Value>;

static int api_v1_camera_photo_timer(struct mg_connection* conn, void* cbdata);

static int read_post_body(struct mg_connection* conn, std::string& body) {
    const char* content_len_str = mg_get_header(conn, "Content-Length");
    long long content_len = 0;
    if (content_len_str) {
        content_len = atoll(content_len_str);
    }

    if (content_len <= 0) return 0;
    if (content_len > 10 * 1024 * 1024) return -1;

    body.resize(static_cast<size_t>(content_len));
    int read_len = mg_read(conn, &body[0], static_cast<size_t>(content_len));
    if (read_len < 0) return -1;
    body.resize(static_cast<size_t>(read_len));
    return read_len;
}

static void send_json_response(struct mg_connection* conn, int status_code, const Json::Value& data) {
    Json::FastWriter writer;
    std::string json_str = writer.write(data);

    mg_printf(conn,
              "HTTP/1.1 %d %s\r\n"
              "Content-Type: application/json\r\n"
              "Access-Control-Allow-Origin: *\r\n"
              "Connection: close\r\n\r\n"
              "%s",
              status_code,
              (status_code == 200 ? "OK" : "Error"),
              json_str.c_str());
}

static void send_success_response(struct mg_connection* conn, const Json::Value& data = Json::nullValue) {
    Json::Value root;
    root["code"] = 0;
    root["message"] = "success";
    root["data"] = data;
    send_json_response(conn, 200, root);
}

static void send_error_response(struct mg_connection* conn, int code, const std::string& message) {
    Json::Value root;
    root["code"] = code;
    root["message"] = message;
    root["data"] = Json::nullValue;
    send_json_response(conn, 200, root);
}

static void send_http_error(struct mg_connection* conn, int status_code, const std::string& message) {
    Json::Value root;
    root["code"] = status_code;
    root["message"] = message;
    root["data"] = Json::nullValue;
    send_json_response(conn, status_code, root);
}

static void send_binary_response(struct mg_connection* conn,
                                 const char* content_type,
                                 const std::vector<uint8_t>& data) {
    mg_printf(conn,
              "HTTP/1.1 200 OK\r\n"
              "Content-Type: %s\r\n"
              "Content-Length: %zu\r\n"
              "Access-Control-Allow-Origin: *\r\n"
              "Connection: close\r\n\r\n",
              content_type,
              data.size());
    if (!data.empty()) {
        mg_write(conn, data.data(), data.size());
    }
}

static const char* get_local_uri(struct mg_connection* conn) {
    const struct mg_request_info* req_info = mg_get_request_info(conn);
    return (req_info && req_info->local_uri) ? req_info->local_uri : "";
}

static bool uri_equals(struct mg_connection* conn, const char* expected_uri) {
    return strcmp(get_local_uri(conn), expected_uri) == 0;
}

static int reject_unmatched_subpath(struct mg_connection* conn, const char* expected_uri) {
    send_http_error(conn, 404, std::string("Not Found: ") + expected_uri);
    return 404;
}

static bool parse_json_body(struct mg_connection* conn, Json::Value& req_json) {
    std::string body;
    if (read_post_body(conn, body) < 0) {
        send_http_error(conn, 400, "Invalid request body");
        return false;
    }

    Json::Reader reader;
    if (!body.empty() && !reader.parse(body, req_json)) {
        send_http_error(conn, 400, "Invalid JSON");
        return false;
    }
    return true;
}

static int get_query_int(const struct mg_request_info* req_info, const char* key, int default_value) {
    if (!req_info || !req_info->query_string) {
        return default_value;
    }

    char buf[32];
    if (mg_get_var(req_info->query_string, strlen(req_info->query_string), key, buf, sizeof(buf)) > 0) {
        return atoi(buf);
    }
    return default_value;
}

static bool get_query_string_value(const struct mg_request_info* req_info,
                                   const char* key,
                                   std::string& value) {
    if (!req_info || !req_info->query_string) {
        return false;
    }

    char buf[1024];
    if (mg_get_var(req_info->query_string, strlen(req_info->query_string), key, buf, sizeof(buf)) > 0) {
        value = buf;
        return true;
    }
    return false;
}

static std::string get_parent_path(const std::string& path) {
    if (path.empty()) {
        return "";
    }

    const std::string trimmed = (path.size() > 1 && path.back() == '/')
                                    ? path.substr(0, path.size() - 1)
                                    : path;
    const size_t pos = trimmed.find_last_of('/');
    if (pos == std::string::npos) {
        return "";
    }
    if (pos == 0) {
        return "/";
    }
    return trimmed.substr(0, pos);
}

static std::string join_path(const std::string& base, const std::string& name) {
    if (base.empty()) {
        return name;
    }
    if (base.back() == '/') {
        return base + name;
    }
    return base + "/" + name;
}

static bool canonicalize_existing_path(const std::string& path, std::string& canonical) {
    char resolved[PATH_MAX];
    if (realpath(path.c_str(), resolved) == nullptr) {
        return false;
    }
    canonical = resolved;
    return true;
}

static bool is_path_under_root(const std::string& path, const std::string& root) {
    if (path == root) {
        return false;
    }
    const std::string normalizedRoot = (!root.empty() && root.back() == '/') ? root.substr(0, root.size() - 1) : root;
    return path.size() > normalizedRoot.size() &&
           path.compare(0, normalizedRoot.size(), normalizedRoot) == 0 &&
           path[normalizedRoot.size()] == '/';
}

static std::string get_media_root_from_database_path(const std::string& media_db_path) {
    const std::string db_dir = get_parent_path(media_db_path);
    const std::string data_dir = get_parent_path(db_dir);
    const std::string sd_root = get_parent_path(data_dir);
    return sd_root.empty() ? "" : join_path(sd_root, "DCIM");
}

static bool resolve_media_item(const struct mg_request_info* req_info, MediaItem& item, int& status_code, std::string& message) {
    std::string token;
    std::string idValue;
    MetadataDao dao;
    bool found = false;

    if (get_query_string_value(req_info, "token", token) && !token.empty()) {
        found = dao.getMediaByPlaybackToken(token, item);
    } else if (get_query_string_value(req_info, "id", idValue) && !idValue.empty()) {
        char* end = nullptr;
        errno = 0;
        long parsedId = strtol(idValue.c_str(), &end, 10);
        if (errno != 0 || end == idValue.c_str() || *end != '\0' || parsedId <= 0 || parsedId > INT_MAX) {
            status_code = 400;
            message = "Invalid media id";
            return false;
        }
        found = dao.getMediaById(static_cast<int>(parsedId), item);
    } else {
        status_code = 400;
        message = "Missing media id or token";
        return false;
    }

    if (!found) {
        status_code = 404;
        message = "Media not found";
        return false;
    }

    return true;
}

static bool validate_media_file_access(const std::shared_ptr<service::ICameraService>& camera_service,
                                       const MediaItem& item,
                                       std::string& canonicalPath,
                                       int& status_code,
                                       std::string& message) {
    const std::string mediaRoot = get_media_root_from_database_path(camera_service->getMediaDatabasePath());
    std::string canonicalRoot;
    if (mediaRoot.empty() || !canonicalize_existing_path(mediaRoot, canonicalRoot)) {
        status_code = 500;
        message = "Media root unavailable";
        return false;
    }

    struct stat linkStat;
    if (lstat(item.filePath.c_str(), &linkStat) != 0) {
        status_code = 404;
        message = "Media file not found";
        return false;
    }
    if (S_ISLNK(linkStat.st_mode)) {
        status_code = 403;
        message = "Symlink media access is not allowed";
        return false;
    }

    if (!canonicalize_existing_path(item.filePath, canonicalPath)) {
        status_code = 404;
        message = "Media file not found";
        return false;
    }

    struct stat fileStat;
    if (stat(canonicalPath.c_str(), &fileStat) != 0 || !S_ISREG(fileStat.st_mode)) {
        status_code = 403;
        message = "Media target is not a regular file";
        return false;
    }
    if (!is_path_under_root(canonicalPath, canonicalRoot)) {
        status_code = 403;
        message = "Media path is outside media root";
        return false;
    }

    return true;
}

static const char* get_media_mime_type(const MediaItem& item) {
    if (item.type == 1) {
        return "image/jpeg";
    }
    return "video/mp4";
}

static MediaScannerMode parse_media_scanner_mode(const std::string& raw_mode) {
    std::string mode = raw_mode;
    std::transform(mode.begin(), mode.end(), mode.begin(), ::tolower);
    if (mode == "full" || mode == "full_scan" || mode == "media") {
        return MediaScannerMode::FullScan;
    }
    return MediaScannerMode::PendingThumbnails;
}

static void ensure_sim_media_storage_ready(const std::shared_ptr<service::ICameraService>& camera_service) {
#ifdef SIMULATION_MODE
    static std::once_flag once;
    std::call_once(once, [camera_service]() {
        if (!camera_service) {
            return;
        }

        const std::string media_db_path = camera_service->getMediaDatabasePath();
        const std::string db_dir = get_parent_path(media_db_path);
        if (db_dir.empty()) {
            return;
        }

        if (!DatabaseManager::getInstance().init(db_dir)) {
            elog_e(TAG, "Failed to initialize media database: %s", db_dir.c_str());
            return;
        }

        const std::string sd_root = get_parent_path(get_parent_path(db_dir));
        const std::string media_root = sd_root.empty() ? "" : (sd_root + "/DCIM");
        struct stat st;
        if (!media_root.empty() && stat(media_root.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
            const char* env_mode = std::getenv("MEDIA_SCANNER_MODE");
            const char* env_thumb_dir = std::getenv("THUMB_PENDING_DIR");
            const std::string data_root = get_parent_path(db_dir);

            MediaScannerOptions options;
            options.mode = parse_media_scanner_mode(env_mode ? env_mode : "pending_thumb");
            options.mediaRootDir = media_root;
            options.pendingThumbDir = (env_thumb_dir && env_thumb_dir[0] != '\0')
                                          ? std::string(env_thumb_dir)
                                          : (data_root.empty() ? (db_dir + "/thumb_pending") : (data_root + "/thumb_pending"));
            MediaScanner::getInstance().startScan(options);
        }
    });
#else
    (void)camera_service;
#endif
}

static std::shared_ptr<service::ICameraService> get_camera_service() {
    static std::shared_ptr<service::ICameraService> instance = []() {
        std::shared_ptr<service::ICameraService> camera_service = service::CameraServiceFactory::create();
        ensure_sim_media_storage_ready(camera_service);
        return camera_service;
    }();
    return instance;
}

static service::CameraPropertyService& get_property_service() {
    return service::CameraPropertyService::getInstance();
}

static std::string get_filename(const std::string& path) {
    const size_t pos = path.find_last_of("/\\");
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

static Json::UInt64 get_file_size(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0 || st.st_size < 0) {
        return 0;
    }
    return static_cast<Json::UInt64>(st.st_size);
}

static Json::Value build_photo_json(const service::PhotoResult& result) {
    Json::Value photo(Json::objectValue);
    photo["photo_id"] = "photo_" + std::to_string(result.timestamp);
    photo["filename"] = get_filename(result.filePath);
    photo["filepath"] = result.filePath;
    photo["size"] = get_file_size(result.filePath);
    photo["timestamp"] = static_cast<Json::Int64>(result.timestamp);
    return photo;
}

static Json::Value build_photo_job_json(const service::PhotoJobSnapshot& snapshot) {
    Json::Value data(Json::objectValue);
    data["job_id"] = snapshot.jobId;
    data["task_type"] = "single";
    data["status"] = service::photoJobStateToString(snapshot.state);
    data["progress"] = snapshot.progress;
    data["client_request_id"] = snapshot.clientRequestId;
    data["submitted_at"] = static_cast<Json::Int64>(snapshot.submittedAt);
    data["updated_at"] = static_cast<Json::Int64>(snapshot.updatedAt);

    if (snapshot.state == service::PhotoJobState::COMPLETED) {
        data["photo"] = build_photo_json(snapshot.result);
    } else if (snapshot.state == service::PhotoJobState::FAILED) {
        data["error_message"] = snapshot.errorMessage;
    }

    return data;
}

static Json::Value build_async_event_json() {
    Json::Value event(Json::objectValue);
    std::shared_ptr<service::TcpEventService> service = service::TcpEventService::getInstance();
    event["enabled"] = service->isRunning();
    event["connected"] = service->hasClient();
    if (service->isRunning()) {
        event["port"] = service->port();
    }
    event["success_event"] = "camera.photo.completed";
    event["failed_event"] = "camera.photo.failed";
    return event;
}

static std::string build_iso_datetime_string() {
    char datetime_buf[32];
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    strftime(datetime_buf, sizeof(datetime_buf), "%Y-%m-%dT%H:%M:%S.000", tm_info);
    return datetime_buf;
}

static Json::Value build_device_info_json() {
    Json::Value data(Json::objectValue);
    data["pid"] = "T32-CAM-001";
    data["camera_ver"] = "1.0.0";
    data["camera_model"] = "T32";
    data["camera_build"] = "2025-01-06";
    data["mcu_ver"] = "MCU-1.0.0";
    return data;
}

static Json::Value build_sensor_data_json() {
    Json::Value data(Json::objectValue);
    data["battery"] = 3700;
    data["battery_type"] = 1;
    data["battery_level"] = 85;
    data["ext_power"] = 12000;
    data["sdcard_capacity"] = 32000;
    data["sdcard_used"] = 8000;
    data["cds"] = 500;
    data["temp"] = "25";
    data["press"] = "1013";
    data["rh"] = "60";
    data["datetime"] = build_iso_datetime_string();
    return data;
}

static Json::Value build_storage_info_json() {
    Json::Value data(Json::objectValue);
    data["total"] = 32000;
    data["free"] = 24000;
    data["used"] = 8000;
    return data;
}

static const std::map<std::string, PropertyMap>& get_preset_definitions() {
    static const std::map<std::string, PropertyMap> presets = {
        {"default", {{"resolution", Json::Value("1920x1080")}, {"fps", Json::Value(30)}, {"bitrate", Json::Value(16384)}}},
        {"high_quality", {{"resolution", Json::Value("2560x1440")}, {"fps", Json::Value(30)}, {"bitrate", Json::Value(16384)}}},
        {"low_power", {{"resolution", Json::Value("1280x720")}, {"fps", Json::Value(30)}, {"bitrate", Json::Value(8192)}}},
    };
    return presets;
}

static Json::Value build_presets_json() {
    Json::Value presets(Json::arrayValue);

    for (const auto& preset_entry : get_preset_definitions()) {
        const std::string& preset_id = preset_entry.first;
        const PropertyMap& props = preset_entry.second;
        Json::Value preset;
        preset["id"] = preset_id;
        preset["name"] = preset_id == "default" ? "Default" :
                         (preset_id == "high_quality" ? "High Quality" : "Low Power");
        preset["description"] = preset_id == "default" ? "Standard profile" :
                                (preset_id == "high_quality" ? "High quality profile" : "Low power profile");

        Json::Value properties(Json::objectValue);
        for (const auto& prop_entry : props) {
            properties[prop_entry.first] = prop_entry.second;
        }
        preset["properties"] = properties;
        presets.append(preset);
    }

    Json::Value data;
    data["presets"] = presets;
    return data;
}

static Json::Value build_media_item_json(const MediaItem& item) {
    Json::Value jItem(Json::objectValue);
    jItem["id"] = item.id;
    jItem["type"] = item.type;
    jItem["path"] = item.filePath;
    jItem["size"] = static_cast<Json::UInt64>(item.fileSize);
    jItem["timestamp"] = static_cast<Json::UInt64>(item.timestamp);
    jItem["duration"] = item.duration;
    jItem["width"] = item.width;
    jItem["height"] = item.height;
    jItem["container_type"] = item.containerType;
    jItem["playback_capable"] = item.playbackCapable;
    jItem["playback_reason"] = item.playbackReason;
    jItem["range_supported"] = item.rangeSupported;
    jItem["seek_support"] = item.seekSupport;
    jItem["seek_granularity_ms"] = item.seekGranularityMs;
    jItem["effective_gop_frames"] = item.effectiveGopFrames;
    jItem["effective_gop_ms"] = item.effectiveGopMs;
    if (item.type == 1 || item.type == 2) {
        jItem["download_url"] = "/api/v1/camera/files/download?id=" + std::to_string(item.id);
    }
    if (item.playbackCapable && item.type == 2) {
        jItem["playback_url"] = "/api/v1/camera/video/playback?id=" + std::to_string(item.id);
    }
    return jItem;
}

static int send_media_list(struct mg_connection* conn, int media_type, const char* field_name) {
    const struct mg_request_info* req_info = mg_get_request_info(conn);
    int offset = get_query_int(req_info, "offset", 0);
    int limit = get_query_int(req_info, "limit", 20);

    get_camera_service();
    MetadataDao dao;
    const std::vector<MediaItem> items =
        (media_type == 0) ? dao.getTimeline(offset, limit) : dao.getTimelineByType(media_type, offset, limit);
    const int total = (media_type == 0) ? dao.getCount() : dao.getCountByType(media_type);
    if (total < 0) {
        send_error_response(conn, 500, "Failed to get media list");
        return 200;
    }

    Json::Value data(Json::objectValue);
    Json::Value itemArray(Json::arrayValue);
    for (const auto& item : items) {
        itemArray.append(build_media_item_json(item));
    }

    data[field_name] = itemArray;
    data["total"] = total;
    data["offset"] = offset;
    data["limit"] = limit;
    send_success_response(conn, data);
    return 200;
}

static void append_property_constraints(const Json::Value& property, Json::Value& result) {
    if (property.isMember("options")) {
        result["valid_options"] = property["options"];
    }
    if (property.isMember("min")) {
        result["min"] = property["min"];
    }
    if (property.isMember("max")) {
        result["max"] = property["max"];
    }
    if (property.isMember("step")) {
        result["step"] = property["step"];
    }
}

static int apply_properties(const PropertyMap& properties, Json::Value& applied, Json::Value* results = nullptr) {
    int success_count = 0;
    for (const auto& prop_entry : properties) {
        Json::Value propertyJson;
        std::string error;
        int ret = get_property_service().setPropertyValue(prop_entry.first, prop_entry.second, &propertyJson, &error);

        if (ret == 0) {
            applied[prop_entry.first] = propertyJson["value"];
            success_count++;
        }

        if (results) {
            Json::Value result(Json::objectValue);
            result["name"] = prop_entry.first;
            result["value"] = prop_entry.second;
            result["status"] = (ret == 0) ? "success" : "failed";
            if (ret != 0) {
                result["error"] = error.empty() ? "Failed to set property" : error;
                Json::Value detail;
                if (get_property_service().getPropertyJson(prop_entry.first, detail, nullptr)) {
                    append_property_constraints(detail, result);
                }
            }
            results->append(result);
        }
    }
    return success_count;
}

static int api_v1_camera_photo(struct mg_connection* conn, void* cbdata) {
    (void)cbdata;
    const struct mg_request_info* req_info = mg_get_request_info(conn);
    const char* uri = get_local_uri(conn);
    if (strcmp(uri, "/api/v1/camera/photo/timer") == 0) {
        return api_v1_camera_photo_timer(conn, cbdata);
    }
    if (!uri_equals(conn, "/api/v1/camera/photo")) {
        return reject_unmatched_subpath(conn, "/api/v1/camera/photo");
    }
    if (strcmp(req_info->request_method, "POST") != 0) {
        send_http_error(conn, 405, "Method Not Allowed");
        return 405;
    }

    Json::Value req_json;
    if (!parse_json_body(conn, req_json)) {
        return 400;
    }

    int channel = req_json.get("channel", 0).asInt();
    bool save = req_json.get("save", true).asBool();
    std::string format = req_json.get("format", "jpg").asString();
    int quality = req_json.get("quality", 85).asInt();
    std::string response_mode = req_json.get("response_mode", "sync").asString();
    std::string client_request_id = req_json.get("client_request_id", "").asString();

    if (response_mode != "sync" && response_mode != "async") {
        send_http_error(conn, 400, "response_mode must be sync or async");
        return 400;
    }

    if (response_mode == "async") {
        service::PhotoJobRequest request;
        request.channel = channel;
        request.save = save;
        request.format = format;
        request.quality = quality;
        request.clientRequestId = client_request_id;

        service::PhotoJobSnapshot snapshot;
        if (!service::PhotoJobManager::getInstance().submit(get_camera_service(), request, snapshot)) {
            send_error_response(conn, 1001, "Failed to accept photo job");
            return 200;
        }

        Json::Value data(Json::objectValue);
        data["response_mode"] = "async";
        data["status"] = "accepted";
        data["job_id"] = snapshot.jobId;
        data["client_request_id"] = snapshot.clientRequestId;
        data["submitted_at"] = static_cast<Json::Int64>(snapshot.submittedAt);
        data["result_query"] = "/api/v1/camera/photo/status?job_id=" + snapshot.jobId;
        data["event"] = build_async_event_json();
        send_success_response(conn, data);
        return 200;
    }

    service::PhotoResult result;
    int ret = get_camera_service()->takePhoto(channel, save, format, quality, result);
    if (ret == 0) {
        Json::Value data = build_photo_json(result);
        data["response_mode"] = "sync";
        data["status"] = "completed";
        send_success_response(conn, data);
    } else {
        send_error_response(conn, 1001, result.message.empty() ? "Capture failed" : result.message);
    }
    return 200;
}

static int api_v1_device_info(struct mg_connection* conn, void* cbdata) {
    (void)cbdata;
    if (!uri_equals(conn, "/api/v1/device/info")) {
        return reject_unmatched_subpath(conn, "/api/v1/device/info");
    }
    if (strcmp(mg_get_request_info(conn)->request_method, "GET") != 0) {
        send_http_error(conn, 405, "Method Not Allowed");
        return 405;
    }

    send_success_response(conn, build_device_info_json());
    return 200;
}

static int api_v1_device_sensors(struct mg_connection* conn, void* cbdata) {
    (void)cbdata;
    if (!uri_equals(conn, "/api/v1/device/sensors")) {
        return reject_unmatched_subpath(conn, "/api/v1/device/sensors");
    }
    if (strcmp(mg_get_request_info(conn)->request_method, "GET") != 0) {
        send_http_error(conn, 405, "Method Not Allowed");
        return 405;
    }

    send_success_response(conn, build_sensor_data_json());
    return 200;
}

static int api_v1_system_datetime(struct mg_connection* conn, void* cbdata) {
    (void)cbdata;
    if (!uri_equals(conn, "/api/v1/system/datetime")) {
        return reject_unmatched_subpath(conn, "/api/v1/system/datetime");
    }
    if (strcmp(mg_get_request_info(conn)->request_method, "POST") != 0) {
        send_http_error(conn, 405, "Method Not Allowed");
        return 405;
    }

    Json::Value req_json;
    if (!parse_json_body(conn, req_json)) {
        return 400;
    }
    if (!req_json.isMember("datetime") || !req_json["datetime"].isString()) {
        send_http_error(conn, 400, "Missing datetime field");
        return 400;
    }

    elog_i(TAG, "Requested datetime update: %s", req_json["datetime"].asCString());

    Json::Value data(Json::objectValue);
    data["datetime"] = req_json["datetime"].asString();
    data["accepted"] = true;
    send_success_response(conn, data);
    return 200;
}

static int api_v1_system_workmode(struct mg_connection* conn, void* cbdata) {
    (void)cbdata;
    if (!uri_equals(conn, "/api/v1/system/workmode")) {
        return reject_unmatched_subpath(conn, "/api/v1/system/workmode");
    }
    if (strcmp(mg_get_request_info(conn)->request_method, "POST") != 0) {
        send_http_error(conn, 405, "Method Not Allowed");
        return 405;
    }

    Json::Value req_json;
    if (!parse_json_body(conn, req_json)) {
        return 400;
    }
    if (!req_json.isMember("mode") || !req_json["mode"].isInt()) {
        send_http_error(conn, 400, "Missing mode field");
        return 400;
    }

    const int mode = req_json["mode"].asInt();
    elog_i(TAG, "Requested work mode switch: %d", mode);

    Json::Value data(Json::objectValue);
    data["mode"] = mode;
    data["accepted"] = true;
    send_success_response(conn, data);
    return 200;
}

static int api_v1_storage_info(struct mg_connection* conn, void* cbdata) {
    (void)cbdata;
    if (!uri_equals(conn, "/api/v1/storage/info")) {
        return reject_unmatched_subpath(conn, "/api/v1/storage/info");
    }
    if (strcmp(mg_get_request_info(conn)->request_method, "GET") != 0) {
        send_http_error(conn, 405, "Method Not Allowed");
        return 405;
    }

    send_success_response(conn, build_storage_info_json());
    return 200;
}

static int api_v1_storage_format(struct mg_connection* conn, void* cbdata) {
    (void)cbdata;
    if (!uri_equals(conn, "/api/v1/storage/format")) {
        return reject_unmatched_subpath(conn, "/api/v1/storage/format");
    }
    if (strcmp(mg_get_request_info(conn)->request_method, "POST") != 0) {
        send_http_error(conn, 405, "Method Not Allowed");
        return 405;
    }

    Json::Value req_json;
    if (!parse_json_body(conn, req_json)) {
        return 400;
    }

    (void)req_json;
    elog_i(TAG, "Requested storage format");

    Json::Value data(Json::objectValue);
    data["accepted"] = true;
    data["status"] = "scheduled";
    send_success_response(conn, data);
    return 200;
}

static int api_v1_camera_photo_burst(struct mg_connection* conn, void* cbdata) {
    (void)cbdata;
    const struct mg_request_info* req_info = mg_get_request_info(conn);
    if (!uri_equals(conn, "/api/v1/camera/photo/burst")) {
        return reject_unmatched_subpath(conn, "/api/v1/camera/photo/burst");
    }
    if (strcmp(req_info->request_method, "POST") != 0) {
        send_http_error(conn, 405, "Method Not Allowed");
        return 405;
    }

    Json::Value req_json;
    if (!parse_json_body(conn, req_json)) {
        return 400;
    }

    int count = req_json.get("count", 3).asInt();
    int interval = req_json.get("interval", 1000).asInt();
    std::string job_id = "burst_" + std::to_string(time(NULL));

    int ret = get_camera_service()->startBurstPhoto(count, interval, job_id);
    if (ret == 0) {
        Json::Value data;
        data["job_id"] = job_id;
        data["status"] = "processing";
        data["total_count"] = count;
        send_success_response(conn, data);
    } else {
        send_error_response(conn, 1001, "Burst capture failed");
    }
    return 200;
}

static int api_v1_camera_photo_status(struct mg_connection* conn, void* cbdata) {
    (void)cbdata;
    const struct mg_request_info* req_info = mg_get_request_info(conn);
    if (strcmp(mg_get_request_info(conn)->request_method, "GET") != 0) {
        send_http_error(conn, 405, "Method Not Allowed");
        return 405;
    }
    if (!uri_equals(conn, "/api/v1/camera/photo/status")) {
        return reject_unmatched_subpath(conn, "/api/v1/camera/photo/status");
    }

    std::string job_id;
    if (get_query_string_value(req_info, "job_id", job_id) && !job_id.empty()) {
        service::PhotoJobSnapshot snapshot;
        if (!service::PhotoJobManager::getInstance().getSnapshot(job_id, snapshot)) {
            send_error_response(conn, 1007, "Photo job not found");
            return 200;
        }

        send_success_response(conn, build_photo_job_json(snapshot));
        return 200;
    }

    service::PhotoJobSnapshot latestSnapshot;
    if (service::PhotoJobManager::getInstance().getLatestSnapshot(latestSnapshot)) {
        send_success_response(conn, build_photo_job_json(latestSnapshot));
        return 200;
    }

    service::PhotoStatus status = get_camera_service()->getPhotoStatus();
    Json::Value data(Json::objectValue);
    data["status"] = (status.state == service::PhotoState::CAPTURING) ? "capturing" : "idle";
    data["progress"] = status.progress;
    send_success_response(conn, data);
    return 200;
}

static int api_v1_camera_photo_timer(struct mg_connection* conn, void* cbdata) {
    (void)cbdata;
    if (!uri_equals(conn, "/api/v1/camera/photo/timer")) {
        return reject_unmatched_subpath(conn, "/api/v1/camera/photo/timer");
    }
    if (strcmp(mg_get_request_info(conn)->request_method, "POST") != 0) {
        send_http_error(conn, 405, "Method Not Allowed");
        return 405;
    }

    Json::Value req_json;
    if (!parse_json_body(conn, req_json)) {
        return 400;
    }

    const std::string action = req_json.get("action", "start").asString();
    if (action == "start") {
        const int interval = req_json.get("interval", 0).asInt();
        const int count = req_json.get("count", 0).asInt();
        const int channel = req_json.get("channel", 0).asInt();
        if (interval <= 0) {
            send_http_error(conn, 400, "interval must be greater than 0");
            return 400;
        }
        if (count < 0) {
            send_http_error(conn, 400, "count must be greater than or equal to 0");
            return 400;
        }

        const std::string timer_id = "timer_" + std::to_string(time(NULL));
        if (get_camera_service()->startTimerPhoto(channel, interval, count, timer_id) != 0) {
            send_error_response(conn, 1002, "Timer photo already running or failed to start");
            return 200;
        }

        service::TimerPhotoStatus status = get_camera_service()->getTimerPhotoStatus();
        Json::Value data;
        data["timer_id"] = status.jobId;
        data["status"] = status.running ? "running" : "idle";
        data["interval"] = status.intervalMs;
        data["total_count"] = status.totalCount;
        data["completed_count"] = status.completedCount;
        data["channel"] = status.channel;
        send_success_response(conn, data);
        return 200;
    }

    if (action == "stop") {
        service::TimerPhotoStatus status;
        get_camera_service()->stopTimerPhoto(&status);

        Json::Value data;
        data["timer_id"] = status.jobId;
        data["status"] = "stopped";
        data["completed_count"] = status.completedCount;
        send_success_response(conn, data);
        return 200;
    }

    send_http_error(conn, 400, "Unsupported action");
    return 400;
}

static int api_v1_camera_video_start(struct mg_connection* conn, void* cbdata) {
    (void)cbdata;
    const struct mg_request_info* req_info = mg_get_request_info(conn);
    if (!uri_equals(conn, "/api/v1/camera/video/start")) {
        return reject_unmatched_subpath(conn, "/api/v1/camera/video/start");
    }
    if (strcmp(req_info->request_method, "POST") != 0) {
        send_http_error(conn, 405, "Method Not Allowed");
        return 405;
    }

    Json::Value req_json;
    if (!parse_json_body(conn, req_json)) {
        return 400;
    }

    int channel = req_json.get("channel", 0).asInt();
    int duration = req_json.get("duration", 0).asInt();
    bool audio = req_json.get("audio", true).asBool();

    int ret = get_camera_service()->startRecord(channel, duration, audio, "record_id_placeholder");
    if (ret == 0) {
        Json::Value data;
        data["status"] = "recording";
        data["channel"] = channel;
        send_success_response(conn, data);
    } else {
        send_error_response(conn, 1005, "Recording already started or failed");
    }
    return 200;
}

static int api_v1_camera_video_stop(struct mg_connection* conn, void* cbdata) {
    (void)cbdata;
    const struct mg_request_info* req_info = mg_get_request_info(conn);
    if (!uri_equals(conn, "/api/v1/camera/video/stop")) {
        return reject_unmatched_subpath(conn, "/api/v1/camera/video/stop");
    }
    if (strcmp(req_info->request_method, "POST") != 0) {
        send_http_error(conn, 405, "Method Not Allowed");
        return 405;
    }

    int ret = get_camera_service()->stopRecord();
    if (ret == 0) {
        Json::Value data;
        data["status"] = "stopped";
        send_success_response(conn, data);
    } else {
        send_error_response(conn, 1006, "Recording not started or failed to stop");
    }
    return 200;
}

static int api_v1_camera_video_status(struct mg_connection* conn, void* cbdata) {
    (void)cbdata;
    if (strcmp(mg_get_request_info(conn)->request_method, "GET") != 0) {
        send_http_error(conn, 405, "Method Not Allowed");
        return 405;
    }
    if (!uri_equals(conn, "/api/v1/camera/video/status")) {
        return reject_unmatched_subpath(conn, "/api/v1/camera/video/status");
    }

    service::RecordStatus status = get_camera_service()->getRecordStatus();
    Json::Value data;
    data["status"] = (status.state == service::RecordState::RECORDING) ? "recording" : "idle";
    data["duration"] = status.duration;
    if (!status.filePath.empty()) {
        data["filepath"] = status.filePath;
    }
    send_success_response(conn, data);
    return 200;
}

static int api_v1_camera_video_list(struct mg_connection* conn, void* cbdata) {
    (void)cbdata;
    if (!uri_equals(conn, "/api/v1/camera/video/list")) {
        return reject_unmatched_subpath(conn, "/api/v1/camera/video/list");
    }
    if (strcmp(mg_get_request_info(conn)->request_method, "GET") != 0) {
        send_http_error(conn, 405, "Method Not Allowed");
        return 405;
    }
    return send_media_list(conn, 2, "videos");
}

static int api_v1_camera_video_playback(struct mg_connection* conn, void* cbdata) {
    (void)cbdata;
    const struct mg_request_info* req_info = mg_get_request_info(conn);
    if (!uri_equals(conn, "/api/v1/camera/video/playback")) {
        return reject_unmatched_subpath(conn, "/api/v1/camera/video/playback");
    }
    if (strcmp(req_info->request_method, "GET") != 0 && strcmp(req_info->request_method, "HEAD") != 0) {
        send_http_error(conn, 405, "Method Not Allowed");
        return 405;
    }

    MediaItem item;
    int status_code = 200;
    std::string message;
    if (!resolve_media_item(req_info, item, status_code, message)) {
        send_http_error(conn, status_code, message);
        return status_code;
    }

    if (item.type != 2) {
        send_http_error(conn, 404, "Video not found");
        return 404;
    }
    if (!item.playbackCapable) {
        const std::string reason = item.playbackReason.empty() ? "Video is not playback capable" : item.playbackReason;
        send_http_error(conn, 415, reason);
        return 415;
    }
    if (item.containerType != "fmp4" && item.containerType != "mp4") {
        send_http_error(conn, 415, "Unsupported video container");
        return 415;
    }

    std::string canonicalPath;
    std::shared_ptr<service::ICameraService> camera_service = get_camera_service();
    if (!validate_media_file_access(camera_service, item, canonicalPath, status_code, message)) {
        send_http_error(conn, status_code, message);
        return status_code;
    }

    mg_send_mime_file(conn, canonicalPath.c_str(), "video/mp4");
    return 200;
}

static int api_v1_camera_files_download(struct mg_connection* conn, void* cbdata) {
    (void)cbdata;
    const struct mg_request_info* req_info = mg_get_request_info(conn);
    if (!uri_equals(conn, "/api/v1/camera/files/download")) {
        return reject_unmatched_subpath(conn, "/api/v1/camera/files/download");
    }
    if (strcmp(req_info->request_method, "GET") != 0 && strcmp(req_info->request_method, "HEAD") != 0) {
        send_http_error(conn, 405, "Method Not Allowed");
        return 405;
    }

    MediaItem item;
    int status_code = 200;
    std::string message;
    if (!resolve_media_item(req_info, item, status_code, message)) {
        send_http_error(conn, status_code, message);
        return status_code;
    }
    if (item.type != 1 && item.type != 2) {
        send_http_error(conn, 404, "Media not found");
        return 404;
    }

    std::string canonicalPath;
    std::shared_ptr<service::ICameraService> camera_service = get_camera_service();
    if (!validate_media_file_access(camera_service, item, canonicalPath, status_code, message)) {
        send_http_error(conn, status_code, message);
        return status_code;
    }

    const std::string attachmentHeader = "Content-Disposition: attachment; filename=\"" +
                                         get_filename(canonicalPath) + "\"\r\n";
    mg_send_mime_file2(conn, canonicalPath.c_str(), get_media_mime_type(item), attachmentHeader.c_str());
    return 200;
}

static int api_v1_camera_properties_get(struct mg_connection* conn, void* cbdata) {
    (void)cbdata;
    send_success_response(conn, get_property_service().getAllPropertiesJson());
    return 200;
}

static int api_v1_camera_properties_set(struct mg_connection* conn, void* cbdata) {
    (void)cbdata;
    Json::Value req_json;
    if (!parse_json_body(conn, req_json)) {
        return 400;
    }

    Json::Value results(Json::arrayValue);
    int success_count = 0;
    std::vector<std::string> orderedKeys;
    std::set<std::string> seenKeys;

    for (const auto& name : get_property_service().getPropertyNames()) {
        if (req_json.isMember(name)) {
            orderedKeys.push_back(name);
            seenKeys.insert(name);
        }
    }

    for (const auto& key : req_json.getMemberNames()) {
        if (seenKeys.find(key) == seenKeys.end()) {
            orderedKeys.push_back(key);
        }
    }

    for (const auto& key : orderedKeys) {
        Json::Value propertyJson;
        std::string error;
        int ret = get_property_service().setPropertyValue(key, req_json[key], &propertyJson, &error);

        Json::Value result(Json::objectValue);
        result["name"] = key;
        result["value"] = req_json[key];
        result["status"] = (ret == 0) ? "success" : "failed";
        if (ret == 0) {
            result["applied_value"] = propertyJson["value"];
            success_count++;
        } else {
            result["error"] = error.empty() ? "Failed to set property" : error;
            Json::Value detail;
            if (get_property_service().getPropertyJson(key, detail, nullptr)) {
                append_property_constraints(detail, result);
            }
        }
        results.append(result);
    }

    int total_count = static_cast<int>(orderedKeys.size());

    Json::Value data;
    data["total"] = total_count;
    data["success"] = success_count;
    data["failed"] = total_count - success_count;
    data["results"] = results;
    send_success_response(conn, data);
    return 200;
}

static int api_v1_camera_properties(struct mg_connection* conn, void* cbdata) {
    const struct mg_request_info* req_info = mg_get_request_info(conn);
    if (!uri_equals(conn, "/api/v1/camera/properties")) {
        return reject_unmatched_subpath(conn, "/api/v1/camera/properties");
    }
    if (strcmp(req_info->request_method, "POST") == 0) {
        return api_v1_camera_properties_set(conn, cbdata);
    } else if (strcmp(req_info->request_method, "GET") == 0) {
        return api_v1_camera_properties_get(conn, cbdata);
    }

    send_http_error(conn, 405, "Method Not Allowed");
    return 405;
}

static int api_v1_camera_properties_reset(struct mg_connection* conn, void* cbdata) {
    (void)cbdata;
    if (!uri_equals(conn, "/api/v1/camera/properties/reset")) {
        return reject_unmatched_subpath(conn, "/api/v1/camera/properties/reset");
    }
    if (strcmp(mg_get_request_info(conn)->request_method, "POST") != 0) {
        send_http_error(conn, 405, "Method Not Allowed");
        return 405;
    }

    Json::Value req_json;
    if (!parse_json_body(conn, req_json)) {
        return 400;
    }

    std::vector<std::string> names;
    if (req_json.isMember("properties") && req_json["properties"].isArray()) {
        for (const auto& item : req_json["properties"]) {
            names.push_back(item.asString());
        }
    }

    Json::Value applied(Json::objectValue);
    std::string error;
    int reset_count = get_property_service().resetProperties(names, applied, &error);
    if (reset_count < 0) {
        send_http_error(conn, 400, error.empty() ? "Failed to reset properties" : error);
        return 400;
    }

    Json::Value data;
    data["reset_count"] = reset_count;
    data["properties"] = applied;
    send_success_response(conn, data);
    return 200;
}

static int api_v1_camera_properties_single(struct mg_connection* conn, void* cbdata) {
    (void)cbdata;
    const struct mg_request_info* req_info = mg_get_request_info(conn);
    std::string uri = req_info->local_uri;
    const std::string prefix = "/api/v1/camera/properties/";

    if (uri == "/api/v1/camera/properties/reset") {
        return api_v1_camera_properties_reset(conn, cbdata);
    }
    if (uri == "/api/v1/camera/properties") {
        return api_v1_camera_properties(conn, cbdata);
    }
    if (uri.compare(0, prefix.length(), prefix) != 0) {
        return reject_unmatched_subpath(conn, "/api/v1/camera/properties");
    }

    std::string name = uri.substr(prefix.length());
    if (name.empty() || name.find('/') != std::string::npos) {
        return reject_unmatched_subpath(conn, "/api/v1/camera/properties/{name}");
    }

    if (strcmp(req_info->request_method, "GET") == 0) {
        Json::Value data;
        std::string error;
        if (!get_property_service().getPropertyJson(name, data, &error)) {
            send_http_error(conn, 404, error.empty() ? "Property not found" : error);
            return 404;
        }
        send_success_response(conn, data);
        return 200;
    }

    if (strcmp(req_info->request_method, "POST") == 0) {
        Json::Value req_json;
        if (!parse_json_body(conn, req_json)) {
            return 400;
        }
        if (!req_json.isMember("value")) {
            send_http_error(conn, 400, "Missing value field");
            return 400;
        }

        Json::Value propertyJson;
        std::string error;
        int ret = get_property_service().setPropertyValue(name, req_json["value"], &propertyJson, &error);
        if (ret == 0) {
            Json::Value data(Json::objectValue);
            data["name"] = name;
            data["value"] = propertyJson["value"];
            data["updated"] = true;
            send_success_response(conn, data);
        } else {
            Json::Value root(Json::objectValue);
            root["code"] = 400;
            root["message"] = error.empty() ? "Failed to set property" : error;
            Json::Value data(Json::objectValue);
            data["name"] = name;
            data["value"] = req_json["value"];
            Json::Value detail;
            if (get_property_service().getPropertyJson(name, detail, nullptr)) {
                append_property_constraints(detail, data);
            }
            root["data"] = data;
            send_json_response(conn, 200, root);
        }
        return 200;
    }

    send_http_error(conn, 405, "Method Not Allowed");
    return 405;
}

static int api_v1_camera_presets(struct mg_connection* conn, void* cbdata) {
    (void)cbdata;
    const std::string uri = get_local_uri(conn);
    const std::string base = "/api/v1/camera/presets";

    if (uri == base) {
        if (strcmp(mg_get_request_info(conn)->request_method, "GET") != 0) {
            send_http_error(conn, 405, "Method Not Allowed");
            return 405;
        }
        send_success_response(conn, build_presets_json());
        return 200;
    }

    if (uri.compare(0, base.size() + 1, base + "/") == 0) {
        if (strcmp(mg_get_request_info(conn)->request_method, "POST") != 0) {
            send_http_error(conn, 405, "Method Not Allowed");
            return 405;
        }

        std::string preset_id = uri.substr(base.size() + 1);
        auto preset_it = get_preset_definitions().find(preset_id);
        if (preset_it == get_preset_definitions().end()) {
            send_http_error(conn, 404, "Preset not found");
            return 404;
        }

        Json::Value applied(Json::objectValue);
        apply_properties(preset_it->second, applied);

        Json::Value data;
        data["preset_id"] = preset_id;
        data["preset_name"] = preset_id;
        data["applied_properties"] = applied;
        send_success_response(conn, data);
        return 200;
    }

    return reject_unmatched_subpath(conn, base.c_str());
}

static int api_v1_camera_db_media(struct mg_connection* conn, void* cbdata) {
    (void)cbdata;
    if (!uri_equals(conn, "/api/v1/camera/database/media")) {
        return reject_unmatched_subpath(conn, "/api/v1/camera/database/media");
    }
    if (strcmp(mg_get_request_info(conn)->request_method, "GET") != 0) {
        send_http_error(conn, 405, "Method Not Allowed");
        return 405;
    }

    std::string path = get_camera_service()->getMediaDatabasePath();
    mg_send_file(conn, path.c_str());
    return 200;
}

static int api_v1_camera_db_thumb(struct mg_connection* conn, void* cbdata) {
    (void)cbdata;
    if (!uri_equals(conn, "/api/v1/camera/database/thumbnail")) {
        return reject_unmatched_subpath(conn, "/api/v1/camera/database/thumbnail");
    }
    if (strcmp(mg_get_request_info(conn)->request_method, "GET") != 0) {
        send_http_error(conn, 405, "Method Not Allowed");
        return 405;
    }

    std::string path = get_camera_service()->getThumbnailDatabasePath();
    mg_send_file(conn, path.c_str());
    return 200;
}

static int api_v1_camera_photos(struct mg_connection* conn, void* cbdata) {
    (void)cbdata;
    if (!uri_equals(conn, "/api/v1/camera/photos")) {
        return reject_unmatched_subpath(conn, "/api/v1/camera/photos");
    }
    if (strcmp(mg_get_request_info(conn)->request_method, "GET") != 0) {
        send_http_error(conn, 405, "Method Not Allowed");
        return 405;
    }
    return send_media_list(conn, 1, "photos");
}

static int api_v1_camera_files_delete(struct mg_connection* conn, void* cbdata) {
    (void)cbdata;
    if (!uri_equals(conn, "/api/v1/camera/files/delete")) {
        return reject_unmatched_subpath(conn, "/api/v1/camera/files/delete");
    }
    if (strcmp(mg_get_request_info(conn)->request_method, "POST") != 0) {
        send_http_error(conn, 405, "Method Not Allowed");
        return 405;
    }

    Json::Value req_json;
    if (!parse_json_body(conn, req_json)) {
        return 400;
    }
    if (!req_json.isMember("file_ids") || !req_json["file_ids"].isArray()) {
        send_http_error(conn, 400, "Missing file_ids array");
        return 400;
    }

    Json::Value results(Json::arrayValue);
    int success_count = 0;
    for (const auto& item : req_json["file_ids"]) {
        std::string path = item.asString();
        int ret = get_camera_service()->deleteFile(path);

        Json::Value result;
        result["file_id"] = path;
        result["status"] = (ret == 0) ? "success" : "failed";
        results.append(result);
        if (ret == 0) success_count++;
    }

    Json::Value data;
    data["total"] = static_cast<int>(req_json["file_ids"].size());
    data["success"] = success_count;
    data["failed"] = static_cast<int>(req_json["file_ids"].size()) - success_count;
    data["results"] = results;
    send_success_response(conn, data);
    return 200;
}

static int api_v1_camera_preview(struct mg_connection* conn, void* cbdata) {
    (void)cbdata;
    if (!uri_equals(conn, "/api/v1/camera/preview")) {
        return reject_unmatched_subpath(conn, "/api/v1/camera/preview");
    }
    if (strcmp(mg_get_request_info(conn)->request_method, "GET") != 0) {
        send_http_error(conn, 405, "Method Not Allowed");
        return 405;
    }

    const struct mg_request_info* req_info = mg_get_request_info(conn);
    const int channel = get_query_int(req_info, "channel", 0);
    const int width = get_query_int(req_info, "width", 1920);
    const int height = get_query_int(req_info, "height", 1080);

    std::string format = "jpeg";
    get_query_string_value(req_info, "format", format);
    if (format.empty()) {
        format = "jpeg";
    }

    if (format == "jpeg") {
        std::vector<uint8_t> jpeg_data;
        if (get_camera_service()->capturePreviewFrame(channel, width, height, jpeg_data) != 0 || jpeg_data.empty()) {
            send_error_response(conn, 1007, "Failed to capture preview frame");
            return 200;
        }
        send_binary_response(conn, "image/jpeg", jpeg_data);
        return 200;
    }

    if (format == "mjpeg") {
        mg_printf(conn,
                  "HTTP/1.1 200 OK\r\n"
                  "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
                  "Access-Control-Allow-Origin: *\r\n"
                  "Connection: close\r\n\r\n");

        while (true) {
            std::vector<uint8_t> jpeg_data;
            if (get_camera_service()->capturePreviewFrame(channel, width, height, jpeg_data) != 0 ||
                jpeg_data.empty()) {
                break;
            }

            mg_printf(conn,
                      "--frame\r\n"
                      "Content-Type: image/jpeg\r\n"
                      "Content-Length: %zu\r\n\r\n",
                      jpeg_data.size());
            if (mg_write(conn, jpeg_data.data(), jpeg_data.size()) <= 0) {
                break;
            }
            if (mg_printf(conn, "\r\n") <= 0) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        return 200;
    }

    send_http_error(conn, 400, "Unsupported preview format");
    return 400;
}

static int api_v1_camera_thumbnail(struct mg_connection* conn, void* cbdata) {
    (void)cbdata;
    if (!uri_equals(conn, "/api/v1/camera/thumbnail")) {
        return reject_unmatched_subpath(conn, "/api/v1/camera/thumbnail");
    }
    if (strcmp(mg_get_request_info(conn)->request_method, "GET") != 0) {
        send_http_error(conn, 405, "Method Not Allowed");
        return 405;
    }

    std::string file_path;
    if (get_query_string_value(mg_get_request_info(conn), "file_path", file_path)) {
        get_camera_service();
        MetadataDao dao;
        std::vector<uint8_t> data;
        if (!dao.getThumbnail(file_path, data) || data.empty()) {
            send_http_error(conn, 404, "Thumbnail not found");
            return 404;
        }

        send_binary_response(conn, "image/jpeg", data);
        return 200;
    }

    const struct mg_request_info* req_info = mg_get_request_info(conn);
    const int channel = get_query_int(req_info, "channel", 0);
    const int size = get_query_int(req_info, "size", 320);

    std::vector<uint8_t> jpeg_data;
    if (get_camera_service()->capturePreviewFrame(channel, size, size, jpeg_data) != 0 || jpeg_data.empty()) {
        send_error_response(conn, 1008, "Failed to capture thumbnail");
        return 200;
    }

    send_binary_response(conn, "image/jpeg", jpeg_data);
    return 200;
}

} // namespace

extern "C" void http_api_register_v1(struct mg_context* ctx) {
    elog_i(TAG, "Registering V1 APIs");

    mg_set_request_handler(ctx, "/api/v1/device/info", api_v1_device_info, NULL);
    mg_set_request_handler(ctx, "/api/v1/device/sensors", api_v1_device_sensors, NULL);

    mg_set_request_handler(ctx, "/api/v1/system/datetime", api_v1_system_datetime, NULL);
    mg_set_request_handler(ctx, "/api/v1/system/workmode", api_v1_system_workmode, NULL);

    mg_set_request_handler(ctx, "/api/v1/storage/info", api_v1_storage_info, NULL);
    mg_set_request_handler(ctx, "/api/v1/storage/format", api_v1_storage_format, NULL);

    mg_set_request_handler(ctx, "/api/v1/camera/photo/burst", api_v1_camera_photo_burst, NULL);
    mg_set_request_handler(ctx, "/api/v1/camera/photo/status", api_v1_camera_photo_status, NULL);
    mg_set_request_handler(ctx, "/api/v1/camera/photo/timer", api_v1_camera_photo_timer, NULL);
    mg_set_request_handler(ctx, "/api/v1/camera/photo", api_v1_camera_photo, NULL);

    mg_set_request_handler(ctx, "/api/v1/camera/video/start", api_v1_camera_video_start, NULL);
    mg_set_request_handler(ctx, "/api/v1/camera/video/stop", api_v1_camera_video_stop, NULL);
    mg_set_request_handler(ctx, "/api/v1/camera/video/status", api_v1_camera_video_status, NULL);
    mg_set_request_handler(ctx, "/api/v1/camera/video/playback", api_v1_camera_video_playback, NULL);
    mg_set_request_handler(ctx, "/api/v1/camera/video/list", api_v1_camera_video_list, NULL);

    mg_set_request_handler(ctx, "/api/v1/camera/properties/reset", api_v1_camera_properties_reset, NULL);
    mg_set_request_handler(ctx, "/api/v1/camera/properties", api_v1_camera_properties_single, NULL);

    mg_set_request_handler(ctx, "/api/v1/camera/presets", api_v1_camera_presets, NULL);

    mg_set_request_handler(ctx, "/api/v1/camera/database/media", api_v1_camera_db_media, NULL);
    mg_set_request_handler(ctx, "/api/v1/camera/database/thumbnail", api_v1_camera_db_thumb, NULL);
    mg_set_request_handler(ctx, "/api/v1/camera/preview", api_v1_camera_preview, NULL);
    mg_set_request_handler(ctx, "/api/v1/camera/thumbnail", api_v1_camera_thumbnail, NULL);
    mg_set_request_handler(ctx, "/api/v1/camera/photos", api_v1_camera_photos, NULL);
    mg_set_request_handler(ctx, "/api/v1/camera/files/download", api_v1_camera_files_download, NULL);
    mg_set_request_handler(ctx, "/api/v1/camera/files/delete", api_v1_camera_files_delete, NULL);
}

extern "C" void http_api_v1_shutdown(void) {
    service::PhotoJobManager::getInstance().stop();
#ifdef SIMULATION_MODE
    MediaScanner::getInstance().stopScan();
#endif
}
