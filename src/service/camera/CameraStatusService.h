#pragma once

#include <json/json.h>

#include <string>

namespace service {

class CameraStatusService {
public:
    static CameraStatusService& getInstance();

    Json::Value getStatusJson(const std::string& group = "all") const;

private:
    CameraStatusService() = default;
};

} // namespace service
