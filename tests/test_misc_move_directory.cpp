// Misc::moveDirectoryRecursive unit test — locks the cross-fs-safe recursive move
// (rename fast-path; EXDEV fallback copy+delete; native open/read/write, no fork-exec)
// added for wm upload SD fallback (doc/knowledge/specs/wm-upload-sd-fallback.md §5.5).
//
// Coverage note: PC sim has no tmpfs/vfat boundary, so these cases exercise the
// SAME-fs rename fast-path. The EXDEV copy+delete branch is structurally identical to
// the proven Misc::removeDirectory skeleton + copyFileNative (open/read/write) and is
// exercised on real hardware (spec §8 AC3). R3 (planner-full) documents this gap.

#include "misc/Misc.h"

#include <cerrno>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {

int g_failures = 0;

void require(bool cond, const std::string& msg) {
    if (!cond) { std::cerr << "FAIL: " << msg << std::endl; ++g_failures; }
}

bool pathExists(const std::string& p) { struct stat st; return ::stat(p.c_str(), &st) == 0; }
bool isDir(const std::string& p) {
    struct stat st;
    if (::stat(p.c_str(), &st) != 0) return false;
    return S_ISDIR(st.st_mode);
}
bool writeFile(const std::string& p, const std::string& body) {
    std::ofstream f(p, std::ios::binary);
    f << body;
    return f.good();
}
std::string readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

std::string mkSandbox(const char* tag) {
    char tpl[64];
    std::snprintf(tpl, sizeof(tpl), "/tmp/t22_%s_XXXXXX", tag);
    char* d = ::mkdtemp(tpl);
    require(d != nullptr, std::string("mkdtemp ") + tag);
    return std::string(d) + "/";
}

// 基本递归 move（sim 同 fs → rename 快路径）：src 含文件 + 子目录，move 后 src 消失、dst 等价。
void test_basic_move_same_fs() {
    std::string box = mkSandbox("move");
    std::string src = box + "src/";
    std::string dst = box + "dst/";
    require(Misc::createDirectory(src), "create src");
    require(writeFile(src + "a.txt", "hello"), "write a.txt");
    require(Misc::createDirectory(src + "sub/"), "create src/sub");
    require(writeFile(src + "sub/b.txt", "world"), "write sub/b.txt");

    require(Misc::moveDirectoryRecursive(src, dst), "move src->dst");
    require(!pathExists(src), "src removed after move");
    require(isDir(dst), "dst is dir");
    require(readFile(dst + "a.txt") == "hello", "dst/a.txt content preserved");
    require(isDir(dst + "sub/"), "dst/sub is dir");
    require(readFile(dst + "sub/b.txt") == "world", "dst/sub/b.txt content preserved");

    Misc::removeDirectory(box);
}

// 源不存在 → false（rename ENOENT，非 EXDEV）。
void test_src_missing_returns_false() {
    std::string box = mkSandbox("move");
    require(!Misc::moveDirectoryRecursive(box + "nope/", box + "dst/"), "missing src -> false");
    require(!pathExists(box + "dst/"), "no dst created on missing src");
    Misc::removeDirectory(box);
}

// 目标父目录不存在 → rename ENOENT（非 EXDEV）→ false。
void test_dst_parent_missing_returns_false() {
    std::string box = mkSandbox("move");
    std::string src = box + "src/";
    require(Misc::createDirectory(src), "create src");
    require(!Misc::moveDirectoryRecursive(src, box + "nodir/dst/"), "missing dst parent -> false");
    require(pathExists(src), "src untouched on failed move");
    Misc::removeDirectory(box);
}

// 空串入参 → false（防御）。
void test_empty_args() {
    require(!Misc::moveDirectoryRecursive("", "/tmp/x"), "empty src -> false");
    require(!Misc::moveDirectoryRecursive("/tmp/x", ""), "empty dst -> false");
}

}  // namespace

int main() {
    test_basic_move_same_fs();
    test_src_missing_returns_false();
    test_dst_parent_missing_returns_false();
    test_empty_args();

    if (g_failures == 0) {
        std::cout << "PASS test_misc_move_directory" << std::endl;
        return 0;
    }
    std::cout << "FAIL test_misc_move_directory (" << g_failures << " assertions failed)" << std::endl;
    return 1;
}
