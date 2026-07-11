// wm_sweep — m2 兜底扫描（独立、可开关）。见 wm_sweep.h 与
// doc/design/workmode-m2-workunit-handoff.md §6。

#include "wm_sweep.h"

#include "misc/Misc.h"       // listSubdirectories
#include "Logger.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace app_workmode {

namespace {

// 取目录最后一段（去尾斜杠后）。用于 excludeDir 比较与日志。
std::string dirBasename(const std::string& d) {
    std::string s = d;
    while (!s.empty() && s.back() == '/') s.pop_back();
    auto pos = s.find_last_of('/');
    return (pos == std::string::npos) ? s : s.substr(pos + 1);
}

// ^\d{8}_\d{6}(_\d+)?$ —— quickSnap 时间戳目录 YYYYMMDD_HHMMSS，可选 _<digits> 后缀
// （落卡碰撞时 persist 加的 _2/_3；手写，避免 <regex> 开销）。
bool isTimestampDir(const std::string& s) {
    if (s.size() < 15) return false;
    if (s[8] != '_') return false;
    for (int i = 0; i < 15; ++i) {
        if (i == 8) continue;
        if (s[i] < '0' || s[i] > '9') return false;
    }
    if (s.size() == 15) return true;          // 无后缀（原 tmpfs 工作目录）
    // 尾巴须形如 _<digits>（至少一位数字）：落卡碰撞后缀 _2/_3/...
    if (s[15] != '_') return false;
    for (size_t i = 16; i < s.size(); ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
    }
    return true;
}

}  // namespace

bool sweepEnabled() {
    const char* env = std::getenv("HTC_WM_SWEEP_STRANDED");
    if (env && std::strcmp(env, "0") == 0) return false;   // 仅显式 "0" 关
    return true;   // 默认开（未设或其它值均视为开）
}

bool sdFallbackEnabled() {
    const char* env = std::getenv("HTC_WM_SD_FALLBACK");
    if (env && std::strcmp(env, "0") == 0) return false;   // 仅显式 "0" 关
    return true;   // 默认开：同时 gate persist（超时落卡）与 resume（扫 SD 滞留续传）
}

std::vector<std::string> collectStrandedWorkDirs(const std::string& baseDir,
                                                 const std::string& excludeDir) {
    std::vector<std::string> names;
    const std::string excludeName = dirBasename(excludeDir);   // 空 excludeDir → 不排除

    std::vector<std::string> subs = Misc::listSubdirectories(baseDir);
    for (const std::string& sub : subs) {
        if (!isTimestampDir(sub)) continue;
        if (!excludeName.empty() && sub == excludeName) continue;
        names.push_back(sub);
    }

    std::sort(names.begin(), names.end());   // 升序：老先传

    if (!names.empty()) {
        Logger::log(LogLevel::INFO, "[wm] sweep: found %zu stranded dir(s) under %s",
                    names.size(), baseDir.c_str());
    }

    // 拼成全路径并补尾 '/'（listSubdirectories 只返回名字；UploadTask 做 dir + 文件名 拼接）。
    const std::string base = (baseDir.empty() || baseDir.back() == '/') ? baseDir : baseDir + "/";
    std::vector<std::string> result;
    result.reserve(names.size());
    for (const std::string& n : names) result.push_back(base + n + "/");
    return result;
}

}  // namespace app_workmode
