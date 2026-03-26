#pragma once

#include <cstdlib>
#include <fstream>
#include <limits.h>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace hal {
namespace sim_resource {

inline bool isAbsolutePath(const std::string& path) {
    return !path.empty() && path[0] == '/';
}

inline std::string dirnamePath(const std::string& path) {
    const size_t pos = path.find_last_of("/\\");
    if (pos == std::string::npos) {
        return ".";
    }
    if (pos == 0) {
        return "/";
    }
    return path.substr(0, pos);
}

inline std::string basenamePath(const std::string& path) {
    const size_t pos = path.find_last_of("/\\");
    if (pos == std::string::npos) {
        return path;
    }
    return path.substr(pos + 1);
}

inline std::string joinPath(const std::string& base, const std::string& name) {
    if (base.empty()) {
        return name;
    }
    if (name.empty()) {
        return base;
    }
    if (base.back() == '/') {
        return base + name;
    }
    return base + "/" + name;
}

inline std::string executableDir() {
    char result[PATH_MAX] = {0};
    const ssize_t count = readlink("/proc/self/exe", result, PATH_MAX - 1);
    if (count <= 0) {
        return "";
    }
    return dirnamePath(std::string(result, static_cast<size_t>(count)));
}

inline std::string pickJsonString(const std::string& cfg, const std::string& key) {
    const std::string pat = "\"" + key + "\"";
    const size_t p = cfg.find(pat);
    if (p == std::string::npos) {
        return "";
    }
    const size_t q = cfg.find(':', p);
    if (q == std::string::npos) {
        return "";
    }
    const size_t s = cfg.find('"', q);
    if (s == std::string::npos) {
        return "";
    }
    const size_t e = cfg.find('"', s + 1);
    if (e == std::string::npos) {
        return "";
    }
    return cfg.substr(s + 1, e - s - 1);
}

inline std::vector<std::string> configCandidates() {
    std::vector<std::string> paths;

    const char* config_env = std::getenv("SIM_RESOURCE_CONFIG");
    if (config_env && config_env[0] != '\0') {
        paths.emplace_back(config_env);
    }

    const char* dir_env = std::getenv("SIM_RESOURCE_DIR");
    if (dir_env && dir_env[0] != '\0') {
        paths.emplace_back(joinPath(dir_env, "config.json"));
    }

    const std::string exe_dir = executableDir();
    if (!exe_dir.empty()) {
        paths.emplace_back(joinPath(joinPath(exe_dir, "res"), "config.json"));
    }

    paths.emplace_back("res/config.json");
    return paths;
}

inline bool loadConfig(std::string& config_path, std::string& cfg) {
    for (const std::string& candidate : configCandidates()) {
        std::ifstream ifs(candidate);
        if (!ifs.good()) {
            continue;
        }
        std::ostringstream ss;
        ss << ifs.rdbuf();
        config_path = candidate;
        cfg = ss.str();
        return true;
    }
    config_path.clear();
    cfg.clear();
    return false;
}

inline std::string resolveBaseDir(const std::string& config_path, const std::string& cfg) {
    const std::string config_dir = dirnamePath(config_path);
    std::string base = pickJsonString(cfg, "base");
    if (base.empty()) {
        base = pickJsonString(cfg, "base_dir");
    }

    if (base.empty() || base == "." || base == "./") {
        return config_dir;
    }
    if (isAbsolutePath(base)) {
        return base;
    }

    const std::string config_dir_name = basenamePath(config_dir);
    if (base == config_dir_name) {
        return config_dir;
    }

    return joinPath(config_dir, base);
}

inline std::string resolveAssetPath(const std::string& config_path,
                                    const std::string& cfg,
                                    const std::string& key) {
    const std::string value = pickJsonString(cfg, key);
    if (value.empty()) {
        return "";
    }
    if (isAbsolutePath(value)) {
        return value;
    }
    return joinPath(resolveBaseDir(config_path, cfg), value);
}

} // namespace sim_resource
} // namespace hal
