#include <iostream>
#include <fstream>
#include <string>
#include <unistd.h>
#include <cstdlib>
#include <ctime>
#include <cctype>
#include <cstring>
#include <limits.h>
#include <sys/stat.h>
#include <iomanip>
#include <sys/time.h>
#include <signal.h>
#include <json/json.h>
#include <csignal>
#include <queue>
#include <thread>
#include <chrono>
#include <malloc.h>   // malloc_trim (glibc extension)
#include <mutex>
#include <atomic>
#include <future>
#include <unordered_map>
#include <algorithm>
#include <fcntl.h>    // fcntl, O_NONBLOCK
#include <poll.h>     // poll, POLLIN
#include <cerrno>     // errno, EINTR, EAGAIN


#include "MgmtServClient.h"
#include "WorkModeRunner.h"
#include "DeviceConfig.h"
#include "Common.h"
#include "Logger.h"
#include "ElogInit.h"
#include "EnvManager.h"
#include "misc/Misc.h"
#include "Settings.h"
#include "MCU.h"
#include "StringConvert.h"
#include "WorkMode.h"
#include "app.h"
#include "ProcessLifecycle.h"
#include "Power.h"
#include "DayNightSwitch.h"
#include "utils/AutoRelease.h"
#include "daemon_api.h"

using namespace network;

// Used by main() to resolve the SIM path inputs that feed StartupConfig (S1).
// (The lifecycle TU has its own copy for its post-startup paths.)
static std::string normalizePath(const std::string& path)
{
    char resolved[PATH_MAX];
    if (realpath(path.c_str(), resolved) != nullptr) {
        return std::string(resolved);
    }
    return path;
}

static bool syncWithMCU()
{
    auto devconf = DeviceConfig::getInstance();
    auto mcu = MCU::getInstance();
    //PID
    {
        auto pid = mcu->readPID();
        if (!pid.empty()) {
            devconf->set(INI_SECTION_DEVICE, INI_KEY_PID, pid);
        }
    }

    //UPID & UPWD
    {
        auto upid = mcu->readUPID();
        auto upwd = mcu->readUPWD();
        if (!upid.empty() && !upwd.empty()) {
            devconf->set(INI_SECTION_SYS, INI_KEY_UPID, upid);
            devconf->set(INI_SECTION_SYS, INI_KEY_UPWD, upwd);
        }
    }
    devconf->flush();

    //RTC
    {
        time_t now = time(nullptr);
        struct tm* datetime = localtime(&now);
        if (datetime != nullptr) {
            mcu->setDatetime(datetime);
        }
    }
    return true;
}

// 简单的 INI 配置解析器
static std::unordered_map<std::string, std::unordered_map<std::string, std::string>> parseIniFile(const std::string& filename)
{
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> config;
    std::ifstream file(filename);
    if (!file.is_open()) {
        return config;
    }
    
    std::string currentSection;
    std::string line;
    while (std::getline(file, line)) {
        // 去除首尾空白
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        size_t end = line.find_last_not_of(" \t\r\n");
        line = line.substr(start, end - start + 1);
        
        // 跳过空行和注释
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        
        // 解析 section
        if (line[0] == '[' && line.back() == ']') {
            currentSection = line.substr(1, line.size() - 2);
            continue;
        }
        
        // 解析 key=value
        size_t eqPos = line.find('=');
        if (eqPos != std::string::npos) {
            std::string key = line.substr(0, eqPos);
            std::string value = line.substr(eqPos + 1);
            
            // 去除 key 和 value 的首尾空白
            start = key.find_first_not_of(" \t");
            end = key.find_last_not_of(" \t");
            if (start != std::string::npos) key = key.substr(start, end - start + 1);
            
            start = value.find_first_not_of(" \t");
            end = value.find_last_not_of(" \t");
            if (start != std::string::npos) value = value.substr(start, end - start + 1);
            
            config[currentSection][key] = value;
        }
    }
    return config;
}

static void printUsage(char *argv[])
{
    std::cout << "Usage: " << argv[0] << " <command> [options]" << std::endl;
    std::cout << "Commands:" << std::endl;
    std::cout << "  -w, --wifi\t\tConnect to a Wi-Fi network" << std::endl;
    std::cout << "  -d, --dhcp\t\tGet IP address from DHCP server" << std::endl;
    std::cout << "  -n, --ntp\t\tSync time with NTP server" << std::endl;
    std::cout << "  -h, --help\t\tDisplay this help message" << std::endl;
    std::cout << "  -a, --auth\t\tAuthenticate with the management server" << std::endl;
    std::cout << "  -hb, --heartbeat\tSend a heartbeat message to the management server" << std::endl;
    std::cout << "  -s, --snap\t\tSnap an image" << std::endl;
    std::cout << "  -u, --upload\t\tUpload a file to the storage server" << std::endl;
    std::cout << "  -ar, --audio-record\tRecord audio" << std::endl;
    std::cout << "  -vr, --video-record\tRecord video" << std::endl;
    std::cout << "  -m, --mobile\t\tConnect to the mobile network (use --no-rtsp, --force-day, --record-stream1 for FPS debug)" << std::endl;
    std::cout << "  -rs, --rtsp-server\tStart the RTSP server (use --no-audio to disable audio)" << std::endl;
    std::cout << "  -grtc, --get-rtc\tGet RTC time" << std::endl;
    std::cout << "  -srtc, --set-rtc\tSet RTC time" << std::endl;
    std::cout << "  -uv, --uvc\t\tStart the UVC" << std::endl;
}


int main(int argc, char* argv[])
{
    bool is_rtc_work_well = true;
    enum workingMode working_mode = workingMode::WORKING_MODE_MAX;

    // Declared up-front (and assigned after dispatch) so `goto main_exit` from
    // the commonStartup / commonStartupPostDispatch failure paths does not cross
    // a non-trivial initializer. Defaults match the original behavior on the
    // early-fail path.
    std::shared_ptr<DeviceConfig> config;
    int command = CMD_HELP;

    // TU-local state, now main()-locals (were file-static pre-T15). The cascade
    // (moved to app_workmode::runCommands) reads mobile_rtsp_enabled via the
    // WorkModeContext; rtsp_audio_enabled is set here but not read by the
    // cascade (kept for intent). mgmtServClient/storageServClient are passed by
    // REF into the ctx so the cleanupHook below nulls the SAME instances.
    bool mobile_rtsp_enabled = true;
    bool rtsp_audio_enabled  = true;
    std::shared_ptr<MgmtServClient>   mgmtServClient   = nullptr;
    std::shared_ptr<StorageServClient> storageServClient = nullptr;

    // --- S1 path inputs (computed here; feed StartupConfig) ---
    app_lifecycle::StartupConfig cfg;
#ifdef BUILD_FOR_SIMULATION
    cfg.isSimulation = true;
    // 动态计算路径，确保文件生成在 build 目录下
    std::string exePath = Misc::getExecutablePath();
    cfg.projectRootPath = normalizePath(exePath + "/../..");  // build_sim/bin/../.. -> project_root
    std::string defaultSimRootPath = normalizePath(cfg.projectRootPath + "/sim_sdcard_runtime");
    const char* envSimRoot = std::getenv("SIM_SD_ROOT");
    cfg.simRootPath = (envSimRoot && envSimRoot[0] != '\0')
                                  ? normalizePath(envSimRoot)
                                  : defaultSimRootPath;
    const char* envLogDir = std::getenv("SIM_LOG_DIR");
    cfg.dbPath   = cfg.simRootPath + "/data/db";
    cfg.mediaRoot = cfg.simRootPath + "/DCIM";
    cfg.logRoot  = (envLogDir && envLogDir[0] != '\0')
                   ? std::string(envLogDir)
                   : (cfg.simRootPath + "/logs");
    cfg.logFile  = cfg.logRoot + "/app.log";
#else
    cfg.isSimulation = false;
    EnvManager::getInstance()->parsePrimaryEnv(ENV_FILE_PATHNAME);//必须放在main函数的最开始位置
    cfg.dbPath   = EnvManager::getInstance()->getEnv("DB_PATH", "/mnt/sdcard/data/db");
    cfg.mediaRoot = "/mnt/sdcard/DCIM";
    cfg.logRoot  = "/mnt/sdcard/logs";
    cfg.logFile  = cfg.logRoot + "/app.log";
#endif

    app_lifecycle::ProcessLifecycle lc;
    if (!lc.commonStartup(cfg)) {        // S1-S8 (pre-dispatch startup)
        // Today commonStartup() cannot fail. Kept as a guard for future
        // skipMediaScanner / fatal-step additions; on failure we bail before the
        // dispatch locals are declared, so a goto main_exit would cross their
        // initializers — bail directly (matches the original pipe()-fail return).
        return -1;
    }
    if (!lc.installSignalHandlers()) {   // S8: pipe + signal()  (:778-794)
        return -1;                       // pipe() failure — matches original bail
    }

    if (argc < 2 || std::string(argv[1]) == "-h" || std::string(argv[1]) == "--help") {
        printUsage(argv);
        return -1;
    }

    const bool is_work_mode_cmd = (std::string(argv[1]) == "-wm" || std::string(argv[1]) == "--work-mode");
    if (!is_work_mode_cmd) {
        if (std::string(argv[1]) == "-w" || std::string(argv[1]) == "--wifi") {
            command = CMD_CONN_NET;
        } else if (std::string(argv[1]) == "-d" || std::string(argv[1]) == "--dhcp") { 
            command = CMD_DHCP;
        } else if (std::string(argv[1]) == "-s" || std::string(argv[1]) == "--snap") {
            command = CMD_SNAP;
        } else if (std::string(argv[1]) == "-qs" || std::string(argv[1]) == "--quick-snap") {
            command = CMD_CONN_NET | CMD_DHCP | CMD_NTP | CMD_SNAP | CMD_UPLOAD;
        } else if (std::string(argv[1]) == "-ar" || std::string(argv[1]) == "--audio-record") {
            command = CMD_AUDIO_RECORD;
        } else if (std::string(argv[1]) == "-vr" || std::string(argv[1]) == "--video-record") {
            command = CMD_VIDEO_RECORD;
        } else if (std::string(argv[1]) == "-a" || std::string(argv[1]) == "--auth") { 
            command = CMD_AUTH;
        } else if (std::string(argv[1]) == "-h" || std::string(argv[1]) == "--heartbeat") {
            command = CMD_HEARTBEAT;
        } else if (std::string(argv[1]) == "-u" || std::string(argv[1]) == "--upload") {
            command = CMD_CONN_NET | CMD_DHCP | CMD_NTP | CMD_UPLOAD;
        } else if (std::string(argv[1]) == "-m" || std::string(argv[1]) == "--mobile") {
            command = CMD_MOBILE;
            for (int i = 2; i < argc; i++) {
                if (std::string(argv[i]) == "--no-rtsp") {
                    mobile_rtsp_enabled = false;
                } else if (std::string(argv[i]) == "--no-audio") {
                    rtsp_audio_enabled = false;
                    setenv("HTC_NO_AUDIO", "1", 1);
                } else if (std::string(argv[i]) == "--force-day") {
                    setenv("HTC_FORCE_RECORD_DAY_MODE", "1", 1);
                } else if (std::string(argv[i]) == "--record-stream1") {
                    setenv("HTC_RECORD_STREAM_ID", "1", 1);
                }
            }
        } else if (std::string(argv[1]) == "-n" || std::string(argv[1]) == "--ntp") {
            command = CMD_CONN_NET | CMD_NTP;
        } else if (std::string(argv[1]) == "-rs" || std::string(argv[1]) == "--rtsp-server") {
            command = CMD_RTSP_SERVER;
            // 检查是否有 --no-audio 参数
            for (int i = 2; i < argc; i++) {
                if (std::string(argv[i]) == "--no-audio") {
                    rtsp_audio_enabled = false;
                }
            }
        } else if (std::string(argv[1]) == "-grtc" || std::string(argv[1]) == "--get-rtc") {
            command = CMD_GET_RTC;
        } else if (std::string(argv[1]) == "-srtc" || std::string(argv[1]) == "--set-rtc") {
            command = CMD_SET_RTC;
        } else {
            printUsage(argv);
            return -1;
        }
    } else {
        // -wm branch: validate argv shape + parse (mode, rtc) here. The
        // switch(working_mode)→command-bitmap map (incl. RGB blink + the
        // invalid-mode poweroff) lives in app_workmode::runWorkMode now.
        if (argc == 5 && (std::string(argv[3]) == "-rtc" || std::string(argv[3]) == "--rtc-status")) {
            working_mode = (enum workingMode)stoi_custom(argv[2]);
            is_rtc_work_well = (bool)stoi_custom(argv[4]);
            Logger::log(LogLevel::INFO, "%s working mode %d, rtc status %d", __func__, working_mode, is_rtc_work_well);
        } else {
            Logger::log(LogLevel::ERROR, "%s Invalid command %s, power off", __func__, argv[1]);
            Power::getInstance()->requestShutdown();
            sleep(10);
            return -1;
        }
    }

    // Construct the cascade context up-front (before commonStartupPostDispatch)
    // so the `goto main_exit` failure path below does not cross a non-trivial
    // initializer (T15). By-value fields (isRtcWorkWell/mobileRtspEnabled/
    // rtspAudioEnabled) are already set by dispatch above; the by-ref fields
    // bind the main()-locals (mgmtServClient/storageServClient are also captured
    // by ref by the cleanupHook below — the SAME instances).
    app_workmode::WorkModeContext ctx{lc, is_rtc_work_well, mobile_rtsp_enabled,
                                      rtsp_audio_enabled, argc, argv,
                                      mgmtServClient, storageServClient};

    // S9-S13 (daemon register / Settings / DeviceConfig + program_type /
    // SD-mount + netif / factory-config / update-config / timezone).
    if (!lc.commonStartupPostDispatch(cfg, command)) {
        goto main_exit;
    }

    // Register the mode-local reset hook. Runs once, on the main thread, the
    // first time lc.waitForSignal() catches a signal. Body is the verbatim
    // performCleanup :590-609 (mode-local resets + setting save) + :616-617
    // (client null) + :619-631 (SIGTERM power-hold). The transport teardown
    // (:611-615) is NOT here — it lives in lc.shutdown() (idempotent stop()
    // at main_exit, avoids a double-stop on the signal path).
    lc.setCleanupHook([&lc, &mgmtServClient, &storageServClient](int sig) {
        Logger::log(LogLevel::INFO, "Processing signal %d on main thread", sig);

        if (lc.daynight()) {
            lc.daynight()->controlISP(DayNightState::DAY);
            lc.daynight()->controlIRLed(DayNightState::DAY);
            lc.daynight()->controlIRCut(DayNightState::DAY);
        }

        if (lc.rgbLed()) {
            lc.rgbLed()->setConstant(GPIO_VALUE::LOW);
        }

        std::string setting_file_path = EnvManager::getInstance()->getEnv("SETTING_FILE_PATH", "");
        if (setting_file_path.empty()) {
            Logger::log(LogLevel::ERROR, "Failed to get setting file path");
        } else {
            if (!Settings::getInstance()->saveToJsonFile(setting_file_path)) {
                Logger::log(LogLevel::ERROR, "Failed to save setting file: %s", setting_file_path.c_str());
            }
        }

        mgmtServClient = nullptr;
        storageServClient = nullptr;

        if (sig == SIGTERM) {
            if (Power::getInstance()->isChangeModeRequested()) {
                Logger::log(LogLevel::INFO, "Change mode requested, holding power on");
                auto gpio_power_hold = GPIO(POWER_HOLD_PIN);
                if (!gpio_power_hold.exportGPIO() || !gpio_power_hold.setDirection(GPIO_DIRECTION::OUTPUT)
                    || !gpio_power_hold.setValue(GPIO_VALUE::HIGH)) {
                    Logger::log(LogLevel::ERROR, "%s Failed to set power hold pin", __func__);
                }
            }
            // Hardware poweroff for the non-change-mode SIGTERM case is handled
            // by the existing main_exit path (Misc::poweroff at the bottom of
            // main()). Nothing to do here.
        }
    });

    // config alias still needed for the tail's config->flush() below.
    config = lc.config();

    // --- The cascade, extracted to app_workmode (T15 Phase C-2). ---
    // The WHOLE if(command & CMD_*) cascade + the -wm switch live in
    // WorkModeRunner.cpp now (single source, shared by -wm and single-shot).
    // Behavior byte-identical: runCommands returns Continue for both normal
    // completion and every ex-`goto main_exit` (the tail runs unconditionally,
    // matching the original single main_exit label). runWorkMode's invalid-mode
    // default returns TerminalExit — honored here as `return -1` (tail skipped),
    // exactly as today's `return -1` from main. `ctx` was constructed before
    // commonStartupPostDispatch (so the goto above stays legal).
    if (is_work_mode_cmd) {
        if (app_workmode::runWorkMode(working_mode, ctx) == app_workmode::CascadeResult::TerminalExit) {
            return -1;   // invalid -wm mode: skip the tail (matches today's `return -1`)
        }
    } else {
        (void)app_workmode::runCommands(command, ctx);
    }

main_exit:
    // The main_exit tail (TcpEvent/Mdns stop, the rtsp_singleton_used-gated
    // RtspServer::getInstance()->shutdown() "before the freeze", self-pipe
    // close, Settings save, power-hold GPIO, auto_release.release()) is owned
    // by the lifecycle. The terminal steps (sim _exit / HW syncWithMCU +
    // config->flush + Misc::poweroff) stay here — they are process-terminal
    // and app-specific. NOTE (WiFi reuse): this path INTENTIONALLY does not
    // rmmod the WiFi driver / kill wpa_supplicant — see performCleanup comment
    // moved into the cleanupHook below.
    {
        app_lifecycle::ShutdownContext sctx;
        sctx.programType = lc.programType();
        sctx.command     = command;
        sctx.rtcWorkedWell = is_rtc_work_well;
        lc.shutdown(sctx);
    }
#ifdef BUILD_FOR_SIMULATION
    Logger::log(LogLevel::INFO, "[SIM] Program exit normally");
    _exit(0);
#else
    syncWithMCU();
    config->flush();
#if POWER_MANAGER_ON
    Misc::poweroff();
    while(1);
#endif
    return 0;
#endif
}
