// collectStrandedWorkDirs + isTimestampDir-relaxation test — locks that the relaxed
// filter (^\d{8}_\d{6}(_\d+?$) picks up BOTH plain <ts> dirs and collision-suffix
// <ts>_2 dirs (wm-upload-sd-fallback.md §5.4: persist writes <ts>_2 on collision,
// resume must re-scan it). Compiles wm_sweep.cpp directly (light deps: Misc + Logger).

#include "wm_sweep.h"

#include "misc/Misc.h"

#include <iostream>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>

namespace {

int g_failures = 0;

void require(bool cond, const std::string& msg) {
    if (!cond) { std::cerr << "FAIL: " << msg << std::endl; ++g_failures; }
}

bool hasTail(const std::vector<std::string>& v, const std::string& tail) {
    for (const std::string& s : v) {
        if (s.find(tail) != std::string::npos) return true;
    }
    return false;
}

std::string mkSandbox() {
    char tpl[] = "/tmp/t22_sweep_XXXXXX";
    char* d = ::mkdtemp(tpl);
    require(d != nullptr, "mkdtemp");
    return std::string(d) + "/";
}

// 放宽后的过滤须同时收 plain <ts> 与碰撞后缀 <ts>_2；排除非 ts 目录与文件。
void test_picks_plain_and_suffix_dirs() {
    std::string box = mkSandbox();
    require(Misc::createDirectory(box + "20260711_120000"), "mk plain (current)");
    require(Misc::createDirectory(box + "20260710_090000"), "mk older plain (collision root)");
    require(Misc::createDirectory(box + "20260710_090000_2"), "mk older suffix _2 (relaxation)");
    require(Misc::createDirectory(box + "upload"), "mk upload (m1 dir, excluded)");
    require(Misc::createDirectory(box + "notimestamp"), "mk non-ts (excluded)");

    auto r = app_workmode::collectStrandedWorkDirs(box, "");
    require(r.size() == 3, "exactly 3 stranded dirs (2 plain + 1 suffix)");
    require(hasTail(r, "20260711_120000/"), "plain <ts> collected");
    require(hasTail(r, "20260710_090000/"), "older plain <ts> collected");
    require(hasTail(r, "20260710_090000_2/"), "suffix <ts>_2 collected (relaxation)");
    require(!hasTail(r, "upload"), "upload dir excluded");
    require(!hasTail(r, "notimestamp"), "non-ts dir excluded");

    // 升序：老先传。字符串升序下 20260710_090000 < 20260710_090000_2 < 20260711_120000。
    if (r.size() == 3) {
        require(r[0].find("20260710_090000/") != std::string::npos
               && r[0].find("_2") == std::string::npos,
               "oldest plain <ts> sorts first");
        require(r[2].find("20260711_120000/") != std::string::npos, "newest <ts> sorts last");
    }
    Misc::removeDirectory(box);
}

// excludeDir 按 basename 排除（本次 -d 目录）。
void test_exclude_dir() {
    std::string box = mkSandbox();
    require(Misc::createDirectory(box + "20260711_120000"), "mk a");
    require(Misc::createDirectory(box + "20260710_090000"), "mk b");
    auto r = app_workmode::collectStrandedWorkDirs(box, box + "20260711_120000");
    require(r.size() == 1, "excludeDir leaves exactly 1");
    if (!r.empty()) {
        require(r[0].find("20260710_090000") != std::string::npos, "excludeDir removed the right one");
    }
    Misc::removeDirectory(box);
}

// 多位后缀 _999 也要识别（碰撞极端）。
void test_multi_digit_suffix() {
    std::string box = mkSandbox();
    require(Misc::createDirectory(box + "20260711_120000_999"), "mk _999");
    auto r = app_workmode::collectStrandedWorkDirs(box, "");
    require(r.size() == 1 && hasTail(r, "20260711_120000_999/"), "multi-digit suffix collected");
    Misc::removeDirectory(box);
}

}  // namespace

int main() {
    test_picks_plain_and_suffix_dirs();
    test_exclude_dir();
    test_multi_digit_suffix();

    if (g_failures == 0) {
        std::cout << "PASS test_wm_sweep_stranded" << std::endl;
        return 0;
    }
    std::cout << "FAIL test_wm_sweep_stranded (" << g_failures << " assertions failed)" << std::endl;
    return 1;
}
