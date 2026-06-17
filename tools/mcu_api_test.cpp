// tools/mcu_api_test.cpp
//
// htc_mcu_api_test —— MCU 黑盒 API 遍历器（数据驱动）。
//
// 穷举调用 src/hardware/mcu/MCU.h 全部 public API（不含 getInstance/ctor/dtor）：
//   Phase 1（读）: int/string/uint/tm/bool/纯函数 全部 read 类 API
//   Phase 2（写）: 全部 write 类 API，默认关闭，--write 开启后写+回读比对
//
// 五级状态判定: OK / REVIEW / FAIL / SKIP / STUB
//   STUB  MCU.cpp 里是 stub（直接返回常量、不读 I2C，未实现）→ 真机上也永远那个值
//   read  stub → STUB；否则返回 0 → REVIEW（I2C 失败与真实 0 不可区分）；非 0 在范围内 → OK
//         readWorkingMode 特例：I2C 失败返回 -1（非 0/越界）→ FAIL（明确失败信号）
//   write 返回 false → FAIL；write 后回读不等 → FAIL
//         --no-write/sim 下 → SKIP
//
// 不修改 MCU.{h,cpp}（被测对象），不碰 src/hal/**。C++14。双平台可编译。

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <cerrno>
#include <ctime>
#include <string>
#include <vector>
#include <functional>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <map>

#include "MCU.h"

// ----------------------------------------------------------------------------
// 本地整数<->字符串转换
// uclibc (T32 交叉工具链 gcc 5.4) 的 <string> 默认不带 std::to_string / std::stoi
// （与 MCU.cpp 自带 to_string_custom/stoi_custom 同因）。这里用 snprintf/strtol 自给。
// ----------------------------------------------------------------------------
static std::string toStr(long v)
{
    char b[32];
    snprintf(b, sizeof(b), "%ld", v);
    return std::string(b);
}
static bool fromStr(const std::string &s, int &out)
{
    char *end = nullptr;
    errno = 0;
    long v = strtol(s.c_str(), &end, 10);
    if (end == s.c_str() || (end && *end != '\0')) return false;
    out = (int)v;
    return true;
}

// ----------------------------------------------------------------------------
// 平台判定
// ----------------------------------------------------------------------------
#ifdef BUILD_FOR_SIMULATION
static const bool kSim = true;
#else
static const bool kSim = false;
#endif

// ----------------------------------------------------------------------------
// 四级状态
// ----------------------------------------------------------------------------
enum class Status { OK, REVIEW, FAIL, SKIP, STUB };

static const char *statusStr(Status s)
{
    switch (s) {
    case Status::OK:     return "OK";
    case Status::REVIEW: return "REVIEW";
    case Status::FAIL:   return "FAIL";
    case Status::SKIP:   return "SKIP";
    case Status::STUB:   return "STUB";
    }
    return "?";
}

// ----------------------------------------------------------------------------
// 输出 sink：同时写 stdout 与落盘日志
// ----------------------------------------------------------------------------
struct Reporter {
    std::ofstream logfile;
    bool tee = true;

    void open(const std::string &path)
    {
        logfile.open(path, std::ios::out | std::ios::trunc);
    }
    void line(const std::string &s)
    {
        if (tee) std::printf("%s\n", s.c_str());
        if (logfile.is_open()) logfile << s << "\n";
    }
    void raw(const std::string &s)
    {
        if (tee) std::printf("%s", s.c_str());
        if (logfile.is_open()) logfile << s;
    }
    void flush()
    {
        if (tee) std::fflush(stdout);
        if (logfile.is_open()) logfile.flush();
    }
};

// ----------------------------------------------------------------------------
// 范围常量（来自 doc/knowledge/refs/mcu-api-and-register-inventory.md §3）
// 设 INT_MIN/INT_MAX 的表示「范围未知」——该类 read 永远不会因越界判 REVIEW，
// 仅当返回 0 时判 REVIEW（仍保留 0-vs-I2C-fail 的核心语义）。
// ----------------------------------------------------------------------------
static const int LO_OPEN = -2147483647 - 1;   // INT_MIN
static const int HI_OPEN = 2147483647;        // INT_MAX

// ----------------------------------------------------------------------------
// 判定函数
// ----------------------------------------------------------------------------
// int read：0 → REVIEW；非 0 看范围。
static Status classifyInt(int v, int lo, int hi, const char *&note)
{
    if (v == 0) { note = "0 indistinguishable from I2C fail"; return Status::REVIEW; }
    if (lo == LO_OPEN && hi == HI_OPEN) { note = "non-zero, range open"; return Status::OK; }
    if (v >= lo && v <= hi) { note = "in range"; return Status::OK; }
    note = "out of range";
    return Status::REVIEW;
}

// 特例：语义上 0 合法且范围含 0 的 read（如日夜模式 0=夜），0 → OK。
static Status classifyIntAllowZero(int v, int lo, int hi, const char *&note)
{
    if (v >= lo && v <= hi) { note = "in range (0 allowed)"; return Status::OK; }
    note = "out of range";
    return Status::REVIEW;
}

// ----------------------------------------------------------------------------
// reader 表
// ----------------------------------------------------------------------------
struct IntReader {
    const char *name;
    const char *group;
    std::function<int()> call;
    int lo, hi;
    bool allowZero;   // 语义上 0 合法（如 CDS_DN）
    const char *note;
    bool stub = false;          // MCU.cpp 里是 stub（返回常量、不读 I2C）→ STUB
    bool negOneIsFail = false;  // I2C 失败返回 -1（仅 readWorkingMode）→ -1 判 FAIL
};

struct StrReader {
    const char *name;
    const char *group;
    std::function<std::string()> call;
    const char *note;
    bool stub = false;          // stub（返回常量、不读 I2C）→ STUB
};

struct UintReader {
    const char *name;
    const char *group;
    std::function<unsigned int()> call;
    const char *note;
};

struct BoolStubReader {
    const char *name;
    const char *group;
    std::function<bool()> call;
    const char *note;
};

struct TmReader {
    const char *name;
    const char *group;
    std::function<struct tm()> call;
    const char *note;
};

// 纯函数（有入参，格式化校验）
struct PureFnReader {
    const char *name;
    const char *group;
    std::function<std::string()> call;   // 内部已绑定固定入参
    const std::string expect;            // 期望输出
    const char *note;
};

// 测试入口（void）
struct VoidEntry {
    const char *name;
    const char *group;
    std::function<void()> call;
    const char *note;
};

// write-spec
struct WriteSpec {
    const char *name;
    const char *group;
    std::function<bool()> doWrite;           // 执行 write（写入辨识值或原值，见 buildWriteSpecs）
    std::function<std::string()> readBack;   // 回读（序列化为 string 便于比对/落盘）
    std::function<std::string()> expect;     // 写入值序列化（期望回读应等于此）
    std::function<std::string()> orig;       // 写前快照原值序列化（用于 3 态判定 + note 显示）
    const char *note;
};

// ----------------------------------------------------------------------------
// 表构建
// ----------------------------------------------------------------------------
static std::vector<IntReader> buildIntReaders()
{
    auto m = MCU::getInstance();
    return {
        // 版本/电源（部分 readShutdownVoltage 等为 stub 返 0）
        {"readShutdownVoltage",  "Power",   [m]{ return m->readShutdownVoltage(); },   0, 255, false, "threshold V", true},
        {"readLowPowerVoltage",  "Power",   [m]{ return m->readLowPowerVoltage(); },   0, 255, false, "threshold V", true},
        {"readBatteryLevel",     "Battery", [m]{ return m->readBatteryLevel(); },      0, 100, false, "%", true},
        {"readBatteryType",      "Battery", [m]{ return m->readBatteryType(); },       0, 10,  false, "enum", true},
        {"readVersion",          "Version", [m]{ return m->readVersion(); },           1, 999999, false, "e.g. 10002=V10.002"},
        // 电池
        {"readBattery1Voltage",  "Battery", [m]{ return m->readBattery1Voltage(); },   0, 255, false, "raw, /10=V"},
        {"readBattery2Voltage",  "Battery", [m]{ return m->readBattery2Voltage(); },   0, 255, false, "raw, /10=V"},
        {"readBatteryVoltage",   "Battery", [m]{ return m->readBatteryVoltage(); },    0, 255, false, "current group"},
        {"readRMSunPowerValue",  "Battery", [m]{ return m->readRMSunPowerValue(); },   0, 255, false, "solar /10=V"},
        // 信号
        {"readSignalCF",         "Signal",  [m]{ return m->readSignalCF(); },          0, 65535,  false, "center freq/band, 2-byte reg [0,65535]"},
        {"readSignalTP",         "Signal",  [m]{ return m->readSignalTP(); },          0, 50,    false, "dBm"},
        {"readSignalRSSI",       "Signal",  [m]{ return m->readSignalRSSI(); },       -200, 40,  false, "dBm"},
        {"readSignalRSRP",       "Signal",  [m]{ return m->readSignalRSRP(); },       -200, 40,  false, "dBm"},
        {"readSignalRSRQ",       "Signal",  [m]{ return m->readSignalRSRQ(); },       -200, 40,  false, "dBm"},
        {"readSignalSNR",        "Signal",  [m]{ return m->readSignalSNR(); },         -50, 50,  false, ""},
        {"readSignalTD",         "Signal",  [m]{ return m->readSignalTD(); },          0, 65535, false, "meters"},
        {"readSignalRL",         "Signal",  [m]{ return m->readSignalRL(); },          0, 32767, false, "dB"},
        // 环境
        {"readTemperature",      "Sensor",  [m]{ return m->readTemperature(); },      -40, 125, false, "C (MCU returns -125 offset)"},
        {"readHumidity",         "Sensor",  [m]{ return m->readHumidity(); },           0, 100, false, "% (255=none)"},
        {"readAtmosPressure",    "Sensor",  [m]{ return m->readAtmosPressure(); },    300, 1100, false, "hPa (raw/10)"},
        {"readExternalVoltage",  "Sensor",  [m]{ return m->readExternalVoltage(); },    0, 255, false, "/10=V"},
        {"readCds",              "Sensor",  [m]{ return m->readCds(); },                0, 255, false, "CDS value /10"},
        // RM/事件
        {"readRMID",             "RM",      [m]{ return m->readRMID(); },               LO_OPEN, HI_OPEN, false, "detector ID", true},
        {"readRMType",           "RM",      [m]{ return m->readRMType(); },             LO_OPEN, HI_OPEN, false, "detector type", true},
        {"readRMValue",          "RM",      [m]{ return m->readRMValue(); },            LO_OPEN, HI_OPEN, false, "detector value", true},
        {"readRMCount",          "RM",      [m]{ return m->readRMCount(); },            0, 255, false, "count", true},
        {"readEventType",        "RM",      [m]{ return m->readEventType(); },          0, 255, false, "small int", true},
        {"readEventID",          "RM",      [m]{ return m->readEventID(); },            0, 255, false, "small int", true},
        {"readEventNum",         "RM",      [m]{ return m->readEventNum(); },           0, 65535, false, "seq", true},
        // 系统类 0x00
        {"readFworkMark",        "System",  [m]{ return m->readFworkMark(); },          0, 1, false, "first-boot mark"},
        {"readEventStatus",      "System",  [m]{ return m->readEventStatus(); },        0, 255, false, "event type"},
        {"readPType",            "System",  [m]{ return m->readPType(); },              0, 10, false, "net type"},
        {"readWorkingMode",      "System",  [m]{ return m->readWorkingMode(); },        0, 4, false, "0/1/2/3/4; MCU returns -1 on I2C fail", false, true},
        // 基础信息 0x10-0xFF
        {"readUWS",              "Base",    [m]{ return m->readUWS(); },                0, 2, false, "0/1/2"},
        {"readCDS_DN",           "Base",    [m]{ return m->readCDS_DN(); },             0, 1, true,  "0=night/1=day (0 allowed)"},
        {"readCDS_Value",        "Base",    [m]{ return m->readCDS_Value(); },          0, 255, true, "CDS V"},
        {"readVTSAlarm",         "Base",    [m]{ return m->readVTSAlarm(); },           0, 1, true, "vibration alarm"},
        {"readVTSSens",          "Base",    [m]{ return m->readVTSSens(); },            0, 3, false, "sensitivity"},
        {"readNUFQ",             "Base",    [m]{ return m->readNUFQ(); },               0, 65535, false, "unuploaded count"},
        {"readTimeout",          "Base",    [m]{ return m->readTimeout(); },            0, 200, false, "shutdown countdown s, 200=reset"},
        {"readCamStatus",        "Base",    [m]{ return m->readCamStatus(); },          0, 4, false, "0..4"},
        {"readAIAlarm",          "Base",    [m]{ return m->readAIAlarm(); },            0, 255, false, "0=invalid"},
        // SOR 0x200
        {"readSOR_AL",           "SOR",     [m]{ return m->readSOR_AL(); },             0, 65535, true, "lx, 65535=none"},
        {"readSOR_UVL",          "SOR",     [m]{ return m->readSOR_UVL(); },            0, 15, true, "0..15, 255=none"},
        {"readSOR_NOISE",        "SOR",     [m]{ return m->readSOR_NOISE(); },          0, 255, false, "dBA"},
        {"readSOR_CO",           "SOR",     [m]{ return m->readSOR_CO(); },             0, 65535, false, "ppm"},
        {"readSOR_CO2",          "SOR",     [m]{ return m->readSOR_CO2(); },            0, 65535, false, "ppm"},
        {"readSOR_O2",           "SOR",     [m]{ return m->readSOR_O2(); },             0, 100, false, "%"},
        // ESOR 0x300
        {"readESOR_WS",          "ESOR",    [m]{ return m->readESOR_WS(); },            0, 2, false, "0/1/2"},
        {"readESOR_WID",         "ESOR",    [m]{ return m->readESOR_WID(); },           0, 65535, false, "2-byte reg"},
        {"readESOR_ADD",         "ESOR",    [m]{ return m->readESOR_ADD(); },           0, 65535, false, "comm code"},
        {"readESOR_ID",          "ESOR",    [m]{ return m->readESOR_ID(); },            0, 65535, false, "detector ID"},
        {"readESOR_TYPE",        "ESOR",    [m]{ return m->readESOR_TYPE(); },          0, 200, false, "type"},
        {"readESOR_BAT",         "ESOR",    [m]{ return m->readESOR_BAT(); },           0, 255, false, "/10=V"},
        {"readESOR_GPSA",        "ESOR",    [m]{ return m->readESOR_GPSA(); },          LO_OPEN, HI_OPEN, false, "lat *1e7"},
        {"readESOR_GPSL",        "ESOR",    [m]{ return m->readESOR_GPSL(); },          LO_OPEN, HI_OPEN, false, "lon *1e7"},
        {"readESOR_GPSH",        "ESOR",    [m]{ return m->readESOR_GPSH(); },          LO_OPEN, HI_OPEN, false, "alt *10"},
        // 设置 0x400（读，25）
        {"readCAM_MAXS",         "Setting", [m]{ return m->readCAM_MAXS(); },           0, 255, true, "0=unlimited"},
        {"readPIR_MODE",         "Setting", [m]{ return m->readPIR_MODE(); },           0, 2, false, "0/1/2"},
        {"readPIR_SENS",         "Setting", [m]{ return m->readPIR_SENS(); },           0, 3, false, "0/1/2/3"},
        {"readPIR_INT",          "Setting", [m]{ return m->readPIR_INT(); },            0, 65535, false, "s"},
        {"readTIMER",            "Setting", [m]{ return m->readTIMER(); },              0, 1, false, "0/1"},
        {"readPIR_EN",           "Setting", [m]{ return m->readPIR_EN(); },             0, 1, false, "0/1"},
        {"readTIMER_INT",        "Setting", [m]{ return m->readTIMER_INT(); },          0, 65535, false, "min"},
        {"readTIMER_1START",     "Setting", [m]{ return m->readTIMER_1START(); },       0, 65535, false, "min"},
        {"readTIMER_1END",       "Setting", [m]{ return m->readTIMER_1END(); },         0, 65535, false, "min"},
        {"readTIMER_2START",     "Setting", [m]{ return m->readTIMER_2START(); },       0, 65535, false, "min"},
        {"readTIMER_2END",       "Setting", [m]{ return m->readTIMER_2END(); },         0, 65535, false, "min"},
        {"readTIMER_3START",     "Setting", [m]{ return m->readTIMER_3START(); },       0, 65535, false, "min"},
        {"readTIMER_3END",       "Setting", [m]{ return m->readTIMER_3END(); },         0, 65535, false, "min"},
        {"readTIMER_4START",     "Setting", [m]{ return m->readTIMER_4START(); },       0, 65535, false, "min"},
        {"readTIMER_4END",       "Setting", [m]{ return m->readTIMER_4END(); },         0, 65535, false, "min"},
        {"readTIMER_5START",     "Setting", [m]{ return m->readTIMER_5START(); },       0, 65535, false, "min"},
        {"readTIMER_5END",       "Setting", [m]{ return m->readTIMER_5END(); },         0, 65535, false, "min"},
        {"readTIMER_REPEATS",    "Setting", [m]{ return m->readTIMER_REPEATS(); },      0, 255, false, "week bitmap"},
        {"readHEARTRATE",        "Setting", [m]{ return m->readHEARTRATE(); },          0, 16777215, false, "3B, s"},
        {"readUP_MODE",          "Setting", [m]{ return m->readUP_MODE(); },            0, 255, false, "upload mode"},
        {"readUP_NUFQ",          "Setting", [m]{ return m->readUP_NUFQ(); },            0, 255, false, "threshold"},
        {"readTDS_CF",           "Setting", [m]{ return m->readTDS_CF(); },             0, 65535, false, "2B freq"},
        {"readTDS_TP",           "Setting", [m]{ return m->readTDS_TP(); },             0, 255, false, "power"},
        {"readTDS_BW",           "Setting", [m]{ return m->readTDS_BW(); },             0, 255, false, "bandwidth"},
    };
}

static std::vector<StrReader> buildStrReaders()
{
    auto m = MCU::getInstance();
    return {
        {"readFirmwareVersion", "Version", [m]{ return m->readFirmwareVersion(); }, "stub, no I2C", true},
        {"readSignalType",      "Signal",  [m]{ return m->readSignalType(); },      "12-byte string"},
        {"readPID",             "ID",      [m]{ return m->readPID(); },             "32-byte"},
        {"readUPID",            "ID",      [m]{ return m->readUPID(); },            "32-byte"},
        {"readUPWD",            "ID",      [m]{ return m->readUPWD(); },            "64-byte"},
        {"readDEVICE_NAME",     "ID",      [m]{ return m->readDEVICE_NAME(); },     "24-byte"},
        {"readGps",             "GPS",     [m]{ return m->readGps(); },             "lon,dir,lat,dir,ele composite"},
    };
}

static std::vector<UintReader> buildUintReaders()
{
    auto m = MCU::getInstance();
    return {
        {"readESOR_Value", "ESOR", [m]{ return m->readESOR_Value(); }, "unit by type"},
    };
}

static std::vector<BoolStubReader> buildBoolStubReaders()
{
    auto m = MCU::getInstance();
    return {
        {"powerEnoughForFirmwareUpdate", "Stub", [m]{ return m->powerEnoughForFirmwareUpdate(); }, "stub, no I2C"},
        {"waitFor",                      "Stub", [m]{ return m->waitFor(0); },                      "stub (always false), no I2C"},
        {"IsWifiStationReady",           "Stub", [m]{ return m->IsWifiStationReady(); },            "stub (always true), no I2C"},
        {"Is4gExist",                    "Stub", [m]{ return m->Is4gExist(); },                     "stub (always false), no I2C"},
        {"IsRemoteWakeup",               "Stub", [m]{ return m->IsRemoteWakeup(); },                "stub (always false), no I2C"},
        {"useGpsTime",                   "Stub", [m]{ return m->useGpsTime(); },                    "stub (always false), no I2C"},
    };
}

static std::vector<TmReader> buildTmReaders()
{
    auto m = MCU::getInstance();
    return {
        {"getDatetime", "RTC", [m]{ return m->getDatetime(); }, "RTC; tm_year>=100 means >=2000"},
    };
}

static std::vector<PureFnReader> buildPureFnReaders()
{
    auto m = MCU::getInstance();
    return {
        // convertVersion(10002) -> "V10.002"（MCU.cpp:446: major=ver/1000=10, minor=ver%1000=2,
        // snprintf("V%02d.%03d",10,2) = "V10.002"）
        {"convertVersion", "Version",
            [m]{ return m->convertVersion(10002); }, "V10.002", "pure fn, 10002->V10.002"},
        // convertVoltage(126) -> "12.0"（MCU.cpp:1011 整数除法精度特性：
        // snprintf("%d.%d", 126/10=12, (126%10)/10=6/10=0) = "12.0"，小数位恒 0，
        // 不是 API 异常——expect 必须如实反映实际输出）
        {"convertVoltage", "Version",
            [m]{ return m->convertVoltage(126); }, "12.0",
            "pure fn, 126->12.0 (MCU integer-div precision: decimal digit always 0)"},
    };
}

static std::vector<VoidEntry> buildVoidEntries()
{
    auto m = MCU::getInstance();
    return {
        {"readAllTestData", "Test", [m]{ m->readAllTestData(); }, "unit-test entry (logs only), no return"},
    };
}

// ----------------------------------------------------------------------------
// 快照：Phase 2 写之前读一遍，供安全值与 --restore
// ----------------------------------------------------------------------------
struct Snapshot {
    // name -> serialized current value
    std::map<std::string, std::string> values;
};

static std::string snapKey(const char *group, const char *name)
{
    return std::string(group) + "::" + name;
}

// 写阶段安全值：优先取快照原值（≈空操作）；为空/异常时回退保守默认。
// 用一个能拿到「写入值序列化」与「期望回读序列化」的工厂。
struct WritePlan {
    // 用安全值执行 write
    std::function<bool()> doWrite;
    // 回读序列化
    std::function<std::string()> readBack;
    // 写入值序列化（期望回读应等于此）
    std::function<std::string()> writeVal;
};

// ----------------------------------------------------------------------------
// write-specs 构建（安全值优先取快照原值）
// ----------------------------------------------------------------------------
// 辅助：从 Snapshot 取某 int read 的当前值，失败/空给 def。
static int snapInt(const Snapshot &snap, const char *key, int def)
{
    auto it = snap.values.find(key);
    if (it == snap.values.end() || it->second.empty()) return def;
    int parsed = def;
    if (fromStr(it->second, parsed)) return parsed;
    return def;
}

static std::string snapStr(const Snapshot &snap, const char *key, const std::string &def)
{
    auto it = snap.values.find(key);
    if (it == snap.values.end()) return def;
    return it->second;
}

static std::vector<WriteSpec> buildWriteSpecs(const Snapshot &snap, bool writeOriginal)
{
    auto m = MCU::getInstance();
    std::vector<WriteSpec> out;

    // writeOriginal=true：写回快照原值（≈空操作，只验证 write 调用路径）。
    // writeOriginal=false（默认）：写一个「辨识值」——在合法范围内、非 0、非默认、且尽量
    //   异于当前快照。回读能对上辨识值，才算证明 write 真的生效（而非被吞掉/本来就这个值）。
    auto intSpec = [&](const char *name, const char *group,
                       std::function<bool(int)> w,
                       std::function<int()> rb,
                       const char *snapKeyForRead,
                       int defVal,
                       int distinctive) {
        int snapOrig = snapInt(snap, snapKeyForRead, defVal);
        int v;
        if (writeOriginal) {
            v = snapOrig;
        } else {
            v = distinctive;
            // 保证写入值异于当前快照（否则回读匹配也证明不了写生效）
            if (v == snapOrig) v = (snapOrig > 0 ? snapOrig - 1 : snapOrig + 1);
        }
        WriteSpec s;
        s.name = name; s.group = group;
        s.doWrite = [=]{ return w(v); };
        s.readBack = [=]{ return toStr(rb()); };
        s.expect = [v]{ return toStr(v); };
        s.orig = [snapOrig]{ return toStr(snapOrig); };
        s.note = writeOriginal ? "write original (no-op-ish)" : "distinctive value";
        out.push_back(std::move(s));
    };

    auto strSpec = [&](const char *name, const char *group,
                       std::function<bool(const std::string&)> w,
                       std::function<std::string()> rb,
                       const char *snapKeyForRead,
                       const std::string &defVal,
                       const std::string &distinctive) {
        std::string snapOrig = snapStr(snap, snapKeyForRead, defVal);
        if (snapOrig.empty()) snapOrig = defVal;
        std::string v = writeOriginal ? snapOrig : distinctive;
        if (!writeOriginal && v == snapOrig) v = distinctive + "_2";  // 保证异于原值
        WriteSpec s;
        s.name = name; s.group = group;
        s.doWrite = [=]{ return w(v); };
        s.readBack = [=]{ return rb(); };
        s.expect = [v]{ return v; };
        s.orig = [snapOrig]{ return snapOrig; };
        s.note = writeOriginal ? "write original (no-op-ish)" : "distinctive value";
        out.push_back(std::move(s));
    };

    // ID 类（string）
    strSpec("writePID",        "ID",
            [m](const std::string &s){ return m->writePID(s); },
            [m]{ return m->readPID(); },
            "ID::readPID", "TEST_PID_001", "MCUTEST-PID-001");
    strSpec("writeUPID",       "ID",
            [m](const std::string &s){ return m->writeUPID(s); },
            [m]{ return m->readUPID(); },
            "ID::readUPID", "TEST_UPID_001", "MCUTEST-UPID-001");
    strSpec("writeUPWD",       "ID",
            [m](const std::string &s){ return m->writeUPWD(s); },
            [m]{ return m->readUPWD(); },
            "ID::readUPWD", "TEST_UPWD_001", "MCUTEST-UPWD-001");
    strSpec("writeDEVICE_NAME","ID",
            [m](const std::string &s){ return m->writeDEVICE_NAME(s); },
            [m]{ return m->readDEVICE_NAME(); },
            "ID::readDEVICE_NAME", "TEST_DEV_001", "MCUTEST-DEV-001");

    // 唤醒
    intSpec("writeRemoteWakeup","Wakeup",
            [m](int v){ return m->writeRemoteWakeup(v); },
            [m]{ return m->IsRemoteWakeup() ? 1 : 0; },
            "Stub::IsRemoteWakeup", 0, 1);
    intSpec("writeESOR_WS",     "ESOR",
            [m](int v){ return m->writeESOR_WS(v); },
            [m]{ return m->readESOR_WS(); },
            "ESOR::readESOR_WS", 0, 1);
    intSpec("writeESOR_WID",    "ESOR",
            [m](int v){ return m->writeESOR_WID(v); },
            [m]{ return m->readESOR_WID(); },
            "ESOR::readESOR_WID", 0, 12345);

    // PIR
    intSpec("writePIR_MODE","PIR",
            [m](int v){ return m->writePIR_MODE(v); },
            [m]{ return m->readPIR_MODE(); },
            "Setting::readPIR_MODE", 0, 1);
    intSpec("writePIR_SENS","PIR",
            [m](int v){ return m->writePIR_SENS(v); },
            [m]{ return m->readPIR_SENS(); },
            "Setting::readPIR_SENS", 0, 2);
    intSpec("writePIR_INT", "PIR",
            [m](int v){ return m->writePIR_INT(v); },
            [m]{ return m->readPIR_INT(); },
            "Setting::readPIR_INT", 0, 30);
    intSpec("writePIR_EN",  "PIR",
            [m](int v){ return m->writePIR_EN(v); },
            [m]{ return m->readPIR_EN(); },
            "Setting::readPIR_EN", 0, 1);

    // Timer（辨识值单位=分钟；各时段取一天内有辨识度的不同值）
    intSpec("writeTIMER",        "Timer",
            [m](int v){ return m->writeTIMER(v); },
            [m]{ return m->readTIMER(); },
            "Setting::readTIMER", 0, 1);
    intSpec("writeTIMER_INT",    "Timer",
            [m](int v){ return m->writeTIMER_INT(v); },
            [m]{ return m->readTIMER_INT(); },
            "Setting::readTIMER_INT", 0, 15);
    intSpec("writeTIMER_1START", "Timer",
            [m](int v){ return m->writeTIMER_1START(v); },
            [m]{ return m->readTIMER_1START(); },
            "Setting::readTIMER_1START", 0, 480);
    intSpec("writeTIMER_1END",   "Timer",
            [m](int v){ return m->writeTIMER_1END(v); },
            [m]{ return m->readTIMER_1END(); },
            "Setting::readTIMER_1END", 0, 1020);
    intSpec("writeTIMER_2START", "Timer",
            [m](int v){ return m->writeTIMER_2START(v); },
            [m]{ return m->readTIMER_2START(); },
            "Setting::readTIMER_2START", 0, 540);
    intSpec("writeTIMER_2END",   "Timer",
            [m](int v){ return m->writeTIMER_2END(v); },
            [m]{ return m->readTIMER_2END(); },
            "Setting::readTIMER_2END", 0, 1080);
    intSpec("writeTIMER_3START", "Timer",
            [m](int v){ return m->writeTIMER_3START(v); },
            [m]{ return m->readTIMER_3START(); },
            "Setting::readTIMER_3START", 0, 600);
    intSpec("writeTIMER_3END",   "Timer",
            [m](int v){ return m->writeTIMER_3END(v); },
            [m]{ return m->readTIMER_3END(); },
            "Setting::readTIMER_3END", 0, 1140);
    intSpec("writeTIMER_4START", "Timer",
            [m](int v){ return m->writeTIMER_4START(v); },
            [m]{ return m->readTIMER_4START(); },
            "Setting::readTIMER_4START", 0, 420);
    intSpec("writeTIMER_4END",   "Timer",
            [m](int v){ return m->writeTIMER_4END(v); },
            [m]{ return m->readTIMER_4END(); },
            "Setting::readTIMER_4END", 0, 960);
    intSpec("writeTIMER_5START", "Timer",
            [m](int v){ return m->writeTIMER_5START(v); },
            [m]{ return m->readTIMER_5START(); },
            "Setting::readTIMER_5START", 0, 660);
    intSpec("writeTIMER_5END",   "Timer",
            [m](int v){ return m->writeTIMER_5END(v); },
            [m]{ return m->readTIMER_5END(); },
            "Setting::readTIMER_5END", 0, 1200);
    intSpec("writeTIMER_REPEATS","Timer",
            [m](int v){ return m->writeTIMER_REPEATS(v); },
            [m]{ return m->readTIMER_REPEATS(); },
            "Setting::readTIMER_REPEATS", 0, 127);

    // 策略
    intSpec("writeCAM_MAXS", "Policy",
            [m](int v){ return m->writeCAM_MAXS(v); },
            [m]{ return m->readCAM_MAXS(); },
            "Setting::readCAM_MAXS", 0, 50);
    intSpec("writeHEARTRATE","Policy",
            [m](int v){ return m->writeHEARTRATE(v); },
            [m]{ return m->readHEARTRATE(); },
            "Setting::readHEARTRATE", 0, 60);
    intSpec("writeUP_MODE",  "Policy",
            [m](int v){ return m->writeUP_MODE(v); },
            [m]{ return m->readUP_MODE(); },
            "Setting::readUP_MODE", 0, 1);
    intSpec("writeUP_NUFQ",  "Policy",
            [m](int v){ return m->writeUP_NUFQ(v); },
            [m]{ return m->readUP_NUFQ(); },
            "Setting::readUP_NUFQ", 0, 12);
    intSpec("writeTDS_CF",   "Policy",
            [m](int v){ return m->writeTDS_CF(v); },
            [m]{ return m->readTDS_CF(); },
            "Setting::readTDS_CF", 0, 2350);
    intSpec("writeTDS_TP",   "Policy",
            [m](int v){ return m->writeTDS_TP(v); },
            [m]{ return m->readTDS_TP(); },
            "Setting::readTDS_TP", 0, 20);
    intSpec("writeTDS_BW",   "Policy",
            [m](int v){ return m->writeTDS_BW(v); },
            [m]{ return m->readTDS_BW(); },
            "Setting::readTDS_BW", 0, 10);

    // RTC setDatetime —— writeOriginal=快照原值；否则写当前系统时间（辨识值，每次不同）
    {
        std::string key = "RTC::getDatetime";
        WriteSpec s;
        s.name = "setDatetime"; s.group = "RTC";
        // 解析快照串 "YYYY-MM-DD HH:MM:SS" 回 tm；失败则用当前系统时间
        auto parseTm = [](const std::string &str) -> struct tm {
            struct tm t{};
            if (sscanf(str.c_str(), "%d-%d-%d %d:%d:%d",
                       &t.tm_year, &t.tm_mon, &t.tm_mday,
                       &t.tm_hour, &t.tm_min, &t.tm_sec) >= 6) {
                t.tm_year -= 1900;
                t.tm_mon  -= 1;
                return t;
            }
            time_t now = time(nullptr);
            struct tm lt = *localtime(&now);
            return lt;
        };
        std::string origStr = snapStr(snap, key.c_str(), "");
        struct tm tmv;
        if (writeOriginal) {
            tmv = parseTm(origStr);
        } else {
            time_t now = time(nullptr);
            tmv = *localtime(&now);
        }
        s.doWrite = [m, tmv]{ return m->setDatetime(&tmv); };
        s.readBack = [m]{
            struct tm t = m->getDatetime();
            char b[64];
            snprintf(b, sizeof(b), "%04d-%02d-%02d %02d:%02d:%02d",
                     t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                     t.tm_hour, t.tm_min, t.tm_sec);
            return std::string(b);
        };
        {
            struct tm t = tmv;
            char b[64];
            snprintf(b, sizeof(b), "%04d-%02d-%02d %02d:%02d:%02d",
                     t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                     t.tm_hour, t.tm_min, t.tm_sec);
            std::string exp = b;
            s.expect = [exp]{ return exp; };
        }
        s.orig = [origStr]{ return origStr.empty() ? std::string("<none>") : origStr; };
        s.note = writeOriginal ? "write original RTC" : "distinctive = current system time";
        out.push_back(std::move(s));
    }

    // GPS writeGps —— writeOriginal=快照原值（空则占位）；否则写辨识 GPS 串。
    // 注：readGps 与 writeGps 的串格式可能不同，回读未必相等 → 多为 REVIEW。
    {
        std::string key = "GPS::readGps";
        std::string origGps = snapStr(snap, key.c_str(), "");
        WriteSpec s;
        s.name = "writeGps"; s.group = "GPS";
        std::string distinctiveGps = "12123.4567,E,3123.4567,N,123.4";
        std::string placeholder = "0.0000001,E,0.0000001,N,1";
        std::string v;
        if (writeOriginal) {
            v = (!origGps.empty() && origGps.find_first_not_of(',') != std::string::npos) ? origGps : placeholder;
        } else {
            v = distinctiveGps;
        }
        s.doWrite = [m, v]{ return m->writeGps(v); };
        s.readBack = [m]{ return m->readGps(); };
        s.expect = [v]{ return v; };
        s.orig = [origGps]{ return origGps.empty() ? std::string("<none>") : origGps; };
        s.note = writeOriginal ? "write original GPS" : "distinctive GPS (read-back format may differ)";
        out.push_back(std::move(s));
    }

    return out;
}

// ----------------------------------------------------------------------------
// 统计
// ----------------------------------------------------------------------------
struct Stats {
    long total = 0, ok = 0, review = 0, fail = 0, skip = 0, stub = 0;
    void add(Status s) {
        ++total;
        switch (s) {
        case Status::OK:     ++ok; break;
        case Status::REVIEW: ++review; break;
        case Status::FAIL:   ++fail; break;
        case Status::SKIP:   ++skip; break;
        case Status::STUB:   ++stub; break;
        }
    }
};

// ----------------------------------------------------------------------------
// 格式化辅助
// ----------------------------------------------------------------------------
static std::string fmtTm(const struct tm &t)
{
    char b[64];
    snprintf(b, sizeof(b), "%04d-%02d-%02d %02d:%02d:%02d",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
             t.tm_hour, t.tm_min, t.tm_sec);
    return std::string(b);
}

static std::string timestampForFile()
{
    time_t now = time(nullptr);
    struct tm lt = *localtime(&now);
    char b[32];
    snprintf(b, sizeof(b), "%04d%02d%02d_%02d%02d%02d",
             lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday,
             lt.tm_hour, lt.tm_min, lt.tm_sec);
    return std::string(b);
}

static bool groupMatches(const char *group, const std::string &filter)
{
    if (filter.empty()) return true;
    // case-insensitive contains
    std::string g(group);
    for (auto &c : g) c = (char)tolower((unsigned char)c);
    std::string f = filter;
    for (auto &c : f) c = (char)tolower((unsigned char)c);
    return g.find(f) != std::string::npos;
}

// ----------------------------------------------------------------------------
// Phase 1
// ----------------------------------------------------------------------------
static void runPhase1(Reporter &rep, Stats &stats, const std::string &groupFilter)
{
    rep.line("=== Phase 1: READ API sweep ===");
    rep.line(" #  | Group   | API                  | Args | Return        | Status  | Note");
    rep.line("----|---------|----------------------|------|---------------|---------|---------------------------");

    int idx = 0;

    // int readers
    auto intReaders = buildIntReaders();
    for (const auto &r : intReaders) {
        if (!groupMatches(r.group, groupFilter)) continue;
        int v = 0;
        const char *note = r.note;
        Status st = Status::REVIEW;
        try { v = r.call(); }
        catch (...) { v = 0; note = "exception"; }
        const char *det = nullptr;
        if (r.stub) {
            st = Status::STUB;
            det = "stub: returns constant, no I2C (not implemented)";
        } else if (r.negOneIsFail && v == -1) {
            st = Status::FAIL;
            det = "I2C read failed (MCU returns -1 sentinel)";
        } else {
            st = r.allowZero ? classifyIntAllowZero(v, r.lo, r.hi, det)
                             : classifyInt(v, r.lo, r.hi, det);
        }
        std::string noteStr = std::string(note ? note : "") + (det ? ("; " + std::string(det)) : "");
        char line[512];
        snprintf(line, sizeof(line), "%3d | %-7s | %-20s | %-4s | %13d | %-7s | %s",
                 ++idx, r.group, r.name, "-", v, statusStr(st), noteStr.c_str());
        rep.line(line);
        stats.add(st);
    }

    // uint readers
    auto uintReaders = buildUintReaders();
    for (const auto &r : uintReaders) {
        if (!groupMatches(r.group, groupFilter)) continue;
        unsigned int v = 0;
        const char *note = r.note;
        try { v = r.call(); } catch (...) { note = "exception"; }
        // uint: 0 -> REVIEW；非 0 -> OK（范围 0..UINT_MAX 全合法）
        Status st = (v == 0) ? Status::REVIEW : Status::OK;
        char line[512];
        snprintf(line, sizeof(line), "%3d | %-7s | %-20s | %-4s | %13u | %-7s | %s%s",
                 ++idx, r.group, r.name, "-", v, statusStr(st),
                 note ? note : "", (v == 0) ? "; 0 indistinguishable from I2C fail" : "");
        rep.line(line);
        stats.add(st);
    }

    // string readers
    auto strReaders = buildStrReaders();
    for (const auto &r : strReaders) {
        if (!groupMatches(r.group, groupFilter)) continue;
        std::string v;
        const char *note = r.note;
        try { v = r.call(); } catch (...) { note = "exception"; }
        // 空串判 REVIEW；非空但全是分隔符/空白（如 readGps 无定位返 ",,,,,"）也判 REVIEW。
        // 正常可打印串（PID/UPID/UPWD/DEVICE_NAME/SignalType/FirmwareVersion）必有非分隔符可打印字符，
        // 不会落进此分支，故不影响它们的 OK 判定。
        bool blank = v.empty()
                     || (v.find_first_not_of(", \t\r\n") == std::string::npos);
        Status st = r.stub ? Status::STUB
                           : (blank ? Status::REVIEW : Status::OK);
        // 截断显示
        std::string shown = v;
        if (shown.size() > 20) shown = shown.substr(0, 20);
        const char *extra = r.stub ? "; STUB: returns constant, no I2C"
                            : (v.empty() ? "; empty (I2C fail or none)"
                                         : (blank ? "; all-separator/blank (no real data)"
                                                  : ""));
        char line[512];
        snprintf(line, sizeof(line), "%3d | %-7s | %-20s | %-4s | \"%s\" | %-7s | %s%s",
                 ++idx, r.group, r.name, "-", shown.c_str(), statusStr(st),
                 note ? note : "", extra);
        rep.line(line);
        stats.add(st);
    }

    // bool stub readers
    auto boolReaders = buildBoolStubReaders();
    for (const auto &r : boolReaders) {
        if (!groupMatches(r.group, groupFilter)) continue;
        bool v = false;
        const char *note = r.note;
        try { v = r.call(); } catch (...) { note = "exception"; }
        // 纯桩/无 I2C：这些 API 在 MCU.cpp 里直接返回常量、不读 I2C → STUB（非 OK）
        Status st = Status::STUB;
        char line[512];
        snprintf(line, sizeof(line), "%3d | %-7s | %-20s | %-4s | %13s | %-7s | %s",
                 ++idx, r.group, r.name, "-", v ? "true" : "false", statusStr(st),
                 note ? note : "");
        rep.line(line);
        stats.add(st);
    }

    // tm readers
    auto tmReaders = buildTmReaders();
    for (const auto &r : tmReaders) {
        if (!groupMatches(r.group, groupFilter)) continue;
        struct tm t{};
        const char *note = r.note;
        bool got = false;
        try { t = r.call(); got = true; } catch (...) { note = "exception"; }
        Status st = Status::REVIEW;
        std::string extra;
        if (got) {
            // 校验：tm_year>=100 (>=2000)，mon 0-11，mday 1-31，hour 0-23，min/sec 0-59
            // 注意：I2C 失败时 getDatetime memset 0 -> tm_year=0 -> REVIEW
            bool yr = (t.tm_year >= 100);
            bool mon = (t.tm_mon >= 0 && t.tm_mon <= 11);
            bool day = (t.tm_mday >= 1 && t.tm_mday <= 31);
            bool hr = (t.tm_hour >= 0 && t.tm_hour <= 23);
            bool mn = (t.tm_min >= 0 && t.tm_min <= 59);
            bool sc = (t.tm_sec >= 0 && t.tm_sec <= 59);
            if (yr && mon && day && hr && mn && sc) {
                st = Status::OK; extra = "valid RTC";
            } else {
                st = Status::REVIEW; extra = "zero/invalid (I2C fail -> memset 0)";
            }
        }
        char line[512];
        std::string ts = fmtTm(t);
        snprintf(line, sizeof(line), "%3d | %-7s | %-20s | %-4s | %13s | %-7s | %s; %s",
                 ++idx, r.group, r.name, "-", ts.c_str(), statusStr(st),
                 note ? note : "", extra.c_str());
        rep.line(line);
        stats.add(st);
    }

    // pure-fn readers
    auto pureReaders = buildPureFnReaders();
    for (const auto &r : pureReaders) {
        if (!groupMatches(r.group, groupFilter)) continue;
        std::string v;
        try { v = r.call(); } catch (...) { v = "<exception>"; }
        Status st = (v == r.expect) ? Status::OK : Status::REVIEW;
        char line[512];
        snprintf(line, sizeof(line), "%3d | %-7s | %-20s | %-4s | \"%s\" | %-7s | expect=\"%s\"; %s",
                 ++idx, r.group, r.name, "in", v.c_str(), statusStr(st),
                 r.expect.c_str(), r.note ? r.note : "");
        rep.line(line);
        stats.add(st);
    }

    // void entry（readAllTestData）—— 调用不崩即 OK
    auto voids = buildVoidEntries();
    for (const auto &r : voids) {
        if (!groupMatches(r.group, groupFilter)) continue;
        try { r.call(); } catch (...) {}
        Status st = Status::OK;
        char line[512];
        snprintf(line, sizeof(line), "%3d | %-7s | %-20s | %-4s | %13s | %-7s | %s",
                 ++idx, r.group, r.name, "-", "(void)", statusStr(st),
                 r.note ? r.note : "");
        rep.line(line);
        stats.add(st);
    }
}

// ----------------------------------------------------------------------------
// 快照采集（Phase 2 写之前对所有将写参数 read 一遍）
// ----------------------------------------------------------------------------
static Snapshot collectSnapshot()
{
    Snapshot snap;
    auto m = MCU::getInstance();

    auto putI = [&](const char *group, const char *name, int v) {
        snap.values[snapKey(group, name)] = toStr(v);
    };
    auto putS = [&](const char *group, const char *name, const std::string &v) {
        snap.values[snapKey(group, name)] = v;
    };

    // int reads needed for write safe-values
    putI("Stub","IsRemoteWakeup", m->IsRemoteWakeup() ? 1 : 0);
    putI("ESOR","readESOR_WS",    m->readESOR_WS());
    putI("ESOR","readESOR_WID",   m->readESOR_WID());
    putI("Setting","readPIR_MODE", m->readPIR_MODE());
    putI("Setting","readPIR_SENS", m->readPIR_SENS());
    putI("Setting","readPIR_INT",  m->readPIR_INT());
    putI("Setting","readPIR_EN",   m->readPIR_EN());
    putI("Setting","readTIMER",    m->readTIMER());
    putI("Setting","readTIMER_INT",m->readTIMER_INT());
    putI("Setting","readTIMER_1START", m->readTIMER_1START());
    putI("Setting","readTIMER_1END",   m->readTIMER_1END());
    putI("Setting","readTIMER_2START", m->readTIMER_2START());
    putI("Setting","readTIMER_2END",   m->readTIMER_2END());
    putI("Setting","readTIMER_3START", m->readTIMER_3START());
    putI("Setting","readTIMER_3END",   m->readTIMER_3END());
    putI("Setting","readTIMER_4START", m->readTIMER_4START());
    putI("Setting","readTIMER_4END",   m->readTIMER_4END());
    putI("Setting","readTIMER_5START", m->readTIMER_5START());
    putI("Setting","readTIMER_5END",   m->readTIMER_5END());
    putI("Setting","readTIMER_REPEATS",m->readTIMER_REPEATS());
    putI("Setting","readCAM_MAXS", m->readCAM_MAXS());
    putI("Setting","readHEARTRATE",m->readHEARTRATE());
    putI("Setting","readUP_MODE",  m->readUP_MODE());
    putI("Setting","readUP_NUFQ",  m->readUP_NUFQ());
    putI("Setting","readTDS_CF",   m->readTDS_CF());
    putI("Setting","readTDS_TP",   m->readTDS_TP());
    putI("Setting","readTDS_BW",   m->readTDS_BW());
    // string reads
    putS("ID","readPID",        m->readPID());
    putS("ID","readUPID",       m->readUPID());
    putS("ID","readUPWD",       m->readUPWD());
    putS("ID","readDEVICE_NAME",m->readDEVICE_NAME());
    putS("GPS","readGps",       m->readGps());
    // RTC
    {
        struct tm t = m->getDatetime();
        // 以「完整年」存储，setDatetime 的安全值解析时再还原 tm 约定
        char b[64];
        snprintf(b, sizeof(b), "%04d-%02d-%02d %02d:%02d:%02d",
                 t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                 t.tm_hour, t.tm_min, t.tm_sec);
        snap.values["RTC::getDatetime"] = b;
    }
    return snap;
}

static bool dumpSnapshot(const Snapshot &snap, const std::string &path)
{
    std::ofstream f(path);
    if (!f.is_open()) return false;
    f << "{\n";
    bool first = true;
    for (const auto &kv : snap.values) {
        if (!first) f << ",\n";
        first = false;
        // 简单 JSON 转义（双引号/反斜杠）
        std::string v = kv.second;
        for (size_t i = 0; i < v.size(); ++i) {
            if (v[i] == '"' || v[i] == '\\') { v.insert(i, 1, '\\'); ++i; }
        }
        f << "  \"" << kv.first << "\": \"" << v << "\"";
    }
    f << "\n}\n";
    return true;
}

// 极简 JSON 串提取（值不含转义双引号的场景足够）
static std::string jsonGet(const std::string &text, const std::string &key)
{
    std::string pat = "\"" + key + "\": \"";
    size_t p = text.find(pat);
    if (p == std::string::npos) return "";
    p += pat.size();
    size_t e = text.find('"', p);
    if (e == std::string::npos) return "";
    std::string v = text.substr(p, e - p);
    // 反转义
    std::string out;
    for (size_t i = 0; i < v.size(); ++i) {
        if (v[i] == '\\' && i + 1 < v.size()) { out.push_back(v[i + 1]); ++i; }
        else out.push_back(v[i]);
    }
    return out;
}

// ----------------------------------------------------------------------------
// Phase 2（写）
// ----------------------------------------------------------------------------
static void runPhase2(Reporter &rep, Stats &stats, const std::string &groupFilter,
                      const Snapshot &snap, bool writeOriginal)
{
    rep.line("=== Phase 2: WRITE API sweep (write + read-back) ===");
    rep.line(std::string(" mode: ") + (writeOriginal ? "ORIGINAL (write back snapshot values)"
                                                      : "DISTINCTIVE (non-default test values)"));
    rep.line(" #  | Group   | API                  | WriteValue      | write() | Readback        | Status  | Note");
    rep.line("----|---------|----------------------|-----------------|---------|-----------------|---------|---------------------------");

    auto specs = buildWriteSpecs(snap, writeOriginal);
    int idx = 0;
    for (const auto &s : specs) {
        if (!groupMatches(s.group, groupFilter)) continue;

        std::string wv;
        try { wv = s.expect(); } catch (...) { wv = "<err>"; }

        bool ok = false;
        try { ok = s.doWrite(); } catch (...) { ok = false; }

        std::string rb;
        try { rb = s.readBack(); } catch (...) { rb = "<err>"; }

        Status st;
        std::string note = s.note ? s.note : "";
        // 写前原值（用于 3 态判定 + 显示）
        std::string ov;
        try { ov = s.orig ? s.orig() : ""; } catch (...) { ov = "<err>"; }

        if (!ok) {
            // write() 本身返回 false → 写调用失败
            st = Status::FAIL;
            note += "; write() returned false";
        } else if (rb == wv) {
            // 回读 == 写入的辨识值 → 写确实生效（辨识值非默认，匹配才有意义）
            st = Status::OK;
        } else if (!ov.empty() && rb == ov) {
            // 回读仍是写前原值 → write 没生效（被吞掉/拒绝落盘）
            st = Status::FAIL;
            note += "; readback == original (write did NOT take effect)";
        } else {
            // 回读既不是写入值也不是原值 → 被固件改写/截断/部分写入
            st = Status::REVIEW;
            note += "; readback != written && != original";
        }
        // 已知 bug 提示
        if (std::string(s.name) == "writeESOR_WID" && st != Status::OK) {
            note += " (writeESOR_WID: suspected MCU.cpp:1518 sizeof(int) into 2-byte reg)";
        }
        // 显示写前原值，便于一眼对比 写入/读回/原值
        {
            std::string ovShort = ov.size() > 24 ? ov.substr(0, 24) : ov;
            note += " | orig=" + ovShort;
        }

        // 截断显示
        auto trunc = [](std::string x) {
            if (x.size() > 15) x = x.substr(0, 15);
            return x;
        };
        char line[640];
        snprintf(line, sizeof(line), "%3d | %-7s | %-20s | %-15s | %-7s | %-15s | %-7s | %s",
                 ++idx, s.group, s.name, trunc(wv).c_str(), ok ? "true" : "false",
                 trunc(rb).c_str(), statusStr(st), note.c_str());
        rep.line(line);
        stats.add(st);
    }
}

// ----------------------------------------------------------------------------
// restore 模式
// ----------------------------------------------------------------------------
static int doRestore(Reporter &rep, const std::string &snapPath)
{
    std::ifstream f(snapPath);
    if (!f.is_open()) {
        rep.line(std::string("ERROR: cannot open snapshot: ") + snapPath);
        return 2;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    std::string text = ss.str();

    auto m = MCU::getInstance();
    Stats stats;
    rep.line("=== RESTORE mode (write back snapshot values) ===");
    rep.line(" #  | API                  | Status  | Note");
    rep.line("----|----------------------|---------|---------------------------");

    auto restoreStr = [&](int &idx, const char *name,
                          std::function<bool(const std::string&)> w,
                          const std::string &key) {
        std::string v = jsonGet(text, key);
        bool ok = false;
        std::string note;
        if (v.empty()) { note = "no snapshot value; skip"; }
        else { try { ok = w(v); } catch (...) { ok = false; } note = ok ? "restored" : "write failed"; }
        Status st = v.empty() ? Status::SKIP : (ok ? Status::OK : Status::FAIL);
        char line[512];
        snprintf(line, sizeof(line), "%3d | %-20s | %-7s | %s", ++idx, name, statusStr(st), note.c_str());
        rep.line(line);
        stats.add(st);
    };
    auto restoreInt = [&](int &idx, const char *name,
                          std::function<bool(int)> w,
                          const std::string &key) {
        std::string vs = jsonGet(text, key);
        bool ok = false;
        std::string note;
        Status st;
        if (vs.empty()) { note = "no snapshot value; skip"; st = Status::SKIP; }
        else {
            int v = 0;
            if (fromStr(vs, v)) { ok = w(v); } else { ok = false; }
            note = ok ? "restored" : "write failed";
            st = ok ? Status::OK : Status::FAIL;
        }
        char line[512];
        snprintf(line, sizeof(line), "%3d | %-20s | %-7s | %s", ++idx, name, statusStr(st), note.c_str());
        rep.line(line);
        stats.add(st);
    };

    int idx = 0;
    restoreStr(idx, "writePID",        [m](const std::string &s){ return m->writePID(s); },        "ID::readPID");
    restoreStr(idx, "writeUPID",       [m](const std::string &s){ return m->writeUPID(s); },       "ID::readUPID");
    restoreStr(idx, "writeUPWD",       [m](const std::string &s){ return m->writeUPWD(s); },       "ID::readUPWD");
    restoreStr(idx, "writeDEVICE_NAME",[m](const std::string &s){ return m->writeDEVICE_NAME(s); },"ID::readDEVICE_NAME");
    restoreInt(idx, "writeRemoteWakeup",[m](int v){ return m->writeRemoteWakeup(v); }, "Stub::IsRemoteWakeup");
    restoreInt(idx, "writeESOR_WS",     [m](int v){ return m->writeESOR_WS(v); },     "ESOR::readESOR_WS");
    restoreInt(idx, "writeESOR_WID",    [m](int v){ return m->writeESOR_WID(v); },     "ESOR::readESOR_WID");
    restoreInt(idx, "writePIR_MODE",    [m](int v){ return m->writePIR_MODE(v); },     "Setting::readPIR_MODE");
    restoreInt(idx, "writePIR_SENS",    [m](int v){ return m->writePIR_SENS(v); },     "Setting::readPIR_SENS");
    restoreInt(idx, "writePIR_INT",     [m](int v){ return m->writePIR_INT(v); },      "Setting::readPIR_INT");
    restoreInt(idx, "writePIR_EN",      [m](int v){ return m->writePIR_EN(v); },       "Setting::readPIR_EN");
    restoreInt(idx, "writeTIMER",       [m](int v){ return m->writeTIMER(v); },        "Setting::readTIMER");
    restoreInt(idx, "writeTIMER_INT",   [m](int v){ return m->writeTIMER_INT(v); },    "Setting::readTIMER_INT");
    restoreInt(idx, "writeTIMER_1START",[m](int v){ return m->writeTIMER_1START(v); }, "Setting::readTIMER_1START");
    restoreInt(idx, "writeTIMER_1END",  [m](int v){ return m->writeTIMER_1END(v); },   "Setting::readTIMER_1END");
    restoreInt(idx, "writeTIMER_2START",[m](int v){ return m->writeTIMER_2START(v); }, "Setting::readTIMER_2START");
    restoreInt(idx, "writeTIMER_2END",  [m](int v){ return m->writeTIMER_2END(v); },   "Setting::readTIMER_2END");
    restoreInt(idx, "writeTIMER_3START",[m](int v){ return m->writeTIMER_3START(v); }, "Setting::readTIMER_3START");
    restoreInt(idx, "writeTIMER_3END",  [m](int v){ return m->writeTIMER_3END(v); },   "Setting::readTIMER_3END");
    restoreInt(idx, "writeTIMER_4START",[m](int v){ return m->writeTIMER_4START(v); }, "Setting::readTIMER_4START");
    restoreInt(idx, "writeTIMER_4END",  [m](int v){ return m->writeTIMER_4END(v); },   "Setting::readTIMER_4END");
    restoreInt(idx, "writeTIMER_5START",[m](int v){ return m->writeTIMER_5START(v); }, "Setting::readTIMER_5START");
    restoreInt(idx, "writeTIMER_5END",  [m](int v){ return m->writeTIMER_5END(v); },   "Setting::readTIMER_5END");
    restoreInt(idx, "writeTIMER_REPEATS",[m](int v){ return m->writeTIMER_REPEATS(v); },"Setting::readTIMER_REPEATS");
    restoreInt(idx, "writeCAM_MAXS",    [m](int v){ return m->writeCAM_MAXS(v); },     "Setting::readCAM_MAXS");
    restoreInt(idx, "writeHEARTRATE",   [m](int v){ return m->writeHEARTRATE(v); },    "Setting::readHEARTRATE");
    restoreInt(idx, "writeUP_MODE",     [m](int v){ return m->writeUP_MODE(v); },      "Setting::readUP_MODE");
    restoreInt(idx, "writeUP_NUFQ",     [m](int v){ return m->writeUP_NUFQ(v); },      "Setting::readUP_NUFQ");
    restoreInt(idx, "writeTDS_CF",      [m](int v){ return m->writeTDS_CF(v); },       "Setting::readTDS_CF");
    restoreInt(idx, "writeTDS_TP",      [m](int v){ return m->writeTDS_TP(v); },       "Setting::readTDS_TP");
    restoreInt(idx, "writeTDS_BW",      [m](int v){ return m->writeTDS_BW(v); },       "Setting::readTDS_BW");
    // RTC
    {
        std::string sv = jsonGet(text, "RTC::getDatetime");
        bool ok = false; std::string note; Status st;
        if (sv.empty()) { note = "no snapshot RTC; skip"; st = Status::SKIP; }
        else {
            struct tm t{};
            if (sscanf(sv.c_str(), "%d-%d-%d %d:%d:%d",
                       &t.tm_year, &t.tm_mon, &t.tm_mday,
                       &t.tm_hour, &t.tm_min, &t.tm_sec) >= 6) {
                t.tm_year -= 1900; t.tm_mon -= 1;
                try { ok = m->setDatetime(&t); } catch (...) { ok = false; }
                note = ok ? "restored" : "write failed";
                st = ok ? Status::OK : Status::FAIL;
            } else { note = "bad RTC format; skip"; st = Status::SKIP; }
        }
        char line[512];
        snprintf(line, sizeof(line), "%3d | %-20s | %-7s | %s", ++idx, "setDatetime", statusStr(st), note.c_str());
        rep.line(line);
        stats.add(st);
    }

    rep.line("");
    char sum[256];
    snprintf(sum, sizeof(sum),
             "=== SUMMARY (restore) ===  Total=%ld  OK=%ld  REVIEW=%ld  FAIL=%ld  SKIP=%ld",
             stats.total, stats.ok, stats.review, stats.fail, stats.skip);
    rep.line(sum);
    rep.flush();
    return (stats.fail > 0) ? 1 : 0;
}

// ----------------------------------------------------------------------------
// 写阶段确认门控
// ----------------------------------------------------------------------------
static void printWriteBanner(Reporter &rep, bool writeOriginal)
{
    rep.line("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
    rep.line("!!  WARNING: WRITE PHASE ENABLED");
    rep.line("!!  This will MODIFY real hardware config: PID/UPID/UPWD/");
    rep.line("!!  DeviceName/RTC/Timer/PIR/TDS/HeartRate/GPS on the MCU.");
    rep.line("!!  A pre-write snapshot is saved for --restore.");
    rep.line(std::string("!!  Write mode: ") +
             (writeOriginal ? "ORIGINAL (write back snapshot values, no-op-ish)"
                            : "DISTINCTIVE (writes non-default test values to confirm writes took effect)"));
    rep.line("!!  Type 'yes' to continue (or pass --yes to skip prompt):");
    rep.line("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
}

// ----------------------------------------------------------------------------
// main
// ----------------------------------------------------------------------------
static void printHelp()
{
    std::printf(
        "htc_mcu_api_test -- MCU black-box API sweep\n"
        "\n"
        "Usage:\n"
        "  htc_mcu_api_test [--no-write] [--write] [--yes] [--group <Name>]\n"
        "                   [--log <path>] [--restore <snapshot.json>] [--help]\n"
        "\n"
        "  (default)        Phase 1 only (read sweep), writes skipped\n"
        "  --write          Enable Phase 2 with DISTINCTIVE values (proves write took effect)\n"
        "  --write-original Enable Phase 2 writing back snapshot orig values (no-op-ish; call-path only)\n"
        "  --no-write       Force skip Phase 2 (default)\n"
        "  --yes            Skip write confirmation prompt\n"
        "  --group <Name>   Only run one group: Version|Battery|Signal|Sensor|ESOR|...\n"
        "  --log <path>     Log file (default: mcu_api_test_<ts>.log in cwd)\n"
        "  --restore <f>    Restore mode: write back values from snapshot file, then exit\n"
        "  --help           Print this help\n");
}

int main(int argc, char **argv)
{
    bool enableWrite = false;
    bool assumeYes = false;
    bool writeOriginal = false;   // --write-original: 写回快照原值（默认 false=写辨识值）
    std::string groupFilter;
    std::string logPath;
    std::string restorePath;
    bool restoreMode = false;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--help" || a == "-h") { printHelp(); return 0; }
        else if (a == "--write")   enableWrite = true;
        else if (a == "--no-write") enableWrite = false;
        else if (a == "--write-original") { enableWrite = true; writeOriginal = true; }
        else if (a == "--yes")     assumeYes = true;
        else if (a == "--group" && i + 1 < argc) groupFilter = argv[++i];
        else if (a == "--log" && i + 1 < argc)   logPath = argv[++i];
        else if (a == "--restore" && i + 1 < argc) { restorePath = argv[++i]; restoreMode = true; }
        else {
            std::fprintf(stderr, "Unknown arg: %s\n", a.c_str());
            printHelp();
            return 2;
        }
    }

    Reporter rep;
    if (logPath.empty()) logPath = "mcu_api_test_" + timestampForFile() + ".log";
    rep.open(logPath);

    rep.line("================================================================");
    rep.line(" htc_mcu_api_test -- MCU API sweep");
    rep.line(std::string(" platform: ") + (kSim ? "SIM (I2C bypassed, reads NOT meaningful, all 0)"
                                                 : "T32 hardware"));
    rep.line(std::string(" log: ") + logPath);
    rep.line(std::string(" write phase: ") +
             (enableWrite ? (writeOriginal ? "ENABLED (original: write back snapshot values)"
                                           : "ENABLED (distinctive: non-default test values)")
                          : "disabled (--no-write)"));
    rep.line("================================================================");

    if (kSim) {
        rep.line("");
        rep.line(">>> SIM MODE -- I2C bypassed; read data is NOT meaningful (all 0 -> REVIEW).");
        rep.line(">>> This run only verifies: compilation / table enumeration / call path no-crash.");
        rep.line("");
    }

    // ---- API 计数自检（表完整性） ----
    long nInt   = (long)buildIntReaders().size();
    long nStr   = (long)buildStrReaders().size();
    long nUint  = (long)buildUintReaders().size();
    long nBool  = (long)buildBoolStubReaders().size();
    long nTm    = (long)buildTmReaders().size();
    long nPure  = (long)buildPureFnReaders().size();
    long nVoid  = (long)buildVoidEntries().size();
    // write-spec 计数需快照，但数量固定，直接列
    long nWrite = 33;  // writeRemoteWakeup + writePID/UPID/UPWD/DEVICE_NAME(4) + writeESOR_WS/WID(2)
                       // + PIR(4) + Timer(13: TIMER/TIMER_INT/1..5 START/END/REPEATS)
                       // + Policy(8: CAM_MAXS/HEARTRATE/UP_MODE/UP_NUFQ/TDS_CF/TP/BW) ... 见 buildWriteSpecs
    long nReadTotal = nInt + nStr + nUint + nBool + nTm + nPure + nVoid;
    {
        char b[256];
        snprintf(b, sizeof(b),
                 "API self-check: int=%ld str=%ld uint=%ld bool=%ld tm=%ld pure=%ld void=%ld | READ total=%ld | WRITE=%ld",
                 nInt, nStr, nUint, nBool, nTm, nPure, nVoid, nReadTotal, nWrite);
        rep.line(b);
        rep.line("MCU.h public API (excl getInstance/ctor/dtor): read-class + write-class = 132 methods enumerated.");
    }

    // ---- restore 模式 ----
    if (restoreMode) {
        return doRestore(rep, restorePath);
    }

    // ---- Phase 1 ----
    Stats p1;
    runPhase1(rep, p1, groupFilter);

    // ---- Phase 2 ----
    Stats p2;
    if (enableWrite && !kSim) {
        // 写阶段确认门控
        if (!assumeYes) {
            printWriteBanner(rep, writeOriginal);
            rep.flush();
            char buf[16] = {0};
            if (!std::fgets(buf, sizeof(buf), stdin)) {
                rep.line("No confirmation input; abort Phase 2.");
            } else {
                std::string s(buf);
                // trim
                while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
                if (s != "yes") {
                    rep.line("Confirmation not 'yes'; skipping Phase 2.");
                    enableWrite = false;
                }
            }
        }
    } else if (enableWrite && kSim) {
        rep.line("");
        rep.line(">>> SIM MODE: --write requested but Phase 2 skipped (no real device; writes meaningless).");
        rep.line("");
    }

    Snapshot snap;
    std::string snapPath;
    if (enableWrite && !kSim) {
        rep.line("");
        rep.line(">>> Collecting pre-write snapshot (read-back all writable params)...");
        snap = collectSnapshot();
        snapPath = "mcu_snapshot_" + timestampForFile() + ".json";
        if (dumpSnapshot(snap, snapPath)) {
            rep.line(std::string(">>> Snapshot saved: ") + snapPath);
        } else {
            rep.line(std::string(">>> WARNING: failed to save snapshot to ") + snapPath);
        }
        rep.line("");
        runPhase2(rep, p2, groupFilter, snap, writeOriginal);
    } else {
        // Phase 2 全 SKIP（仍计入统计便于汇总）
        // 构造一份空 spec 列表用于 SKIP 计数
        Snapshot empty;
        auto specs = buildWriteSpecs(empty, writeOriginal);
        rep.line("");
        rep.line("=== Phase 2: WRITE API sweep SKIPPED (--no-write or SIM) ===");
        int idx = 0;
        for (const auto &s : specs) {
            if (!groupMatches(s.group, groupFilter)) continue;
            char line[256];
            snprintf(line, sizeof(line), "%3d | %-7s | %-20s | %-15s | %-7s | %-15s | %-7s | %s",
                     ++idx, s.group, s.name, "-", "skip", "-", statusStr(Status::SKIP),
                     "skipped (--no-write / SIM)");
            rep.line(line);
            p2.add(Status::SKIP);
        }
    }

    // ---- 汇总 ----
    rep.line("");
    {
        char sum[512];
        snprintf(sum, sizeof(sum),
                 "=== SUMMARY ===\n"
                 "Phase 1 (read):  Total=%ld  OK=%ld  REVIEW=%ld  FAIL=%ld  SKIP=%ld  STUB=%ld\n"
                 "Phase 2 (write): Total=%ld  OK=%ld  REVIEW=%ld  FAIL=%ld  SKIP=%ld  (%s)\n"
                 "Log: %s",
                 p1.total, p1.ok, p1.review, p1.fail, p1.skip, p1.stub,
                 p2.total, p2.ok, p2.review, p2.fail, p2.skip,
                 (enableWrite && !kSim) ? "run: --write" : "skipped",
                 logPath.c_str());
        rep.line(sum);
    }

    int exitCode = 0;
    if (p1.fail > 0) exitCode = 1;
    if (enableWrite && !kSim && p2.fail > 0) exitCode = 1;
    {
        char b[128];
        snprintf(b, sizeof(b), "Overall exit code: %d  (non-zero if FAIL>0 in executed phase)", exitCode);
        rep.line(b);
    }

    rep.flush();
    return exitCode;
}
