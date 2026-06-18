// htc_workmode_app — a thin main() that reproduces `htc_main_app -wm <mode>
// -rtc <status>` by composing the already-extracted app_lifecycle::ProcessLifecycle
// + app_workmode::runWorkMode (T16 Phase C-3). No new logic: every block is a
// verbatim lift of the corresponding main_app.cpp -wm-arm block. The only
// structural difference is that `command` is derived from the mode via the
// shared workModeToCommand() BEFORE commonStartupPostDispatch() — restoring
// the pre-C2 ordering (see T16-planner-full.md §3). This app is UNSPAWNED in
// C3: media_app still spawns htc_main_app -wm until C4/T17 repoints it.
//
// Dual-platform: compiles under BUILD_FOR_SIMULATION=ON (build_sim) and the T32
// toolchain (build). T32 uClibc-safe: zero std::to_string/std::stoi (uses
// stoi_custom). No src/hal/** touched.

#include <iostream>
#include <fstream>
#include <string>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <limits.h>
#include <json/json.h>
#include <csignal>

#include "MgmtServClient.h"
#include "WorkModeRunner.h"     // app_workmode::runWorkMode + workModeToCommand + WorkModeContext + CascadeResult + CMD_*
#include "WorkMode.h"           // enum workingMode
#include "ProcessLifecycle.h"   // app_lifecycle::ProcessLifecycle + Startup/ShutdownContext + syncWithMCU
#include "DeviceConfig.h"
#include "Common.h"
#include "Logger.h"
#include "EnvManager.h"
#include "misc/Misc.h"
#include "Settings.h"
#include "StringConvert.h"      // stoi_custom (uClibc-safe)
#include "app.h"                // ENV_FILE_PATHNAME, POWER_HOLD_PIN, PTYPE_*, INI_*
#include "Power.h"
#include "DayNightSwitch.h"
#include "GPIO.h"               // GPIO, GPIO_VALUE, GPIO_DIRECTION, POWER_HOLD_PIN (cleanupHook body)

using namespace network;

// Used by main() to resolve the SIM path inputs that feed StartupConfig (S1).
// Verbatim copy of main_app.cpp's file-local helper (the lifecycle TU has its
// own copy for its post-startup paths; this one feeds StartupConfig here).
static std::string normalizePath(const std::string& path)
{
    char resolved[PATH_MAX];
    if (realpath(path.c_str(), resolved) != nullptr) {
        return std::string(resolved);
    }
    return path;
}

int main(int argc, char* argv[])
{
    // --- main_app.cpp:168-186 locals (the subset the -wm path touches) ---
    bool is_rtc_work_well = true;
    enum workingMode working_mode = workingMode::WORKING_MODE_MAX;

    // Declared up-front (assigned after dispatch) so the early-fail / goto
    // workmode_exit path does not cross a non-trivial initializer.
    std::shared_ptr<DeviceConfig> config;
    int command = CMD_HELP;

    bool mobile_rtsp_enabled = true;
    bool rtsp_audio_enabled  = true;
    std::shared_ptr<MgmtServClient>   mgmtServClient   = nullptr;
    std::shared_ptr<StorageServClient> storageServClient = nullptr;

    // --- main_app.cpp:188-214 S1 path inputs → StartupConfig (sim vs HW) ---
    app_lifecycle::StartupConfig cfg;
#ifdef BUILD_FOR_SIMULATION
    cfg.isSimulation = true;
    std::string exePath = Misc::getExecutablePath();
    cfg.projectRootPath = normalizePath(exePath + "/../..");
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

    // --- main_app.cpp:216-226 S1-S8 + signal install ---
    app_lifecycle::ProcessLifecycle lc;
    if (!lc.commonStartup(cfg)) {        // S1-S8 (pre-dispatch startup)
        return -1;
    }
    if (!lc.installSignalHandlers()) {   // S8: pipe + signal()
        return -1;
    }

    // --- main_app.cpp:233, 285-299 -wm arg validation ---
    // htc_workmode_app is -wm-only: anything that isn't `-wm`/`--work-mode`
    // with the argc==5 + `-rtc`/`--rtc-status` shape takes the same
    // "Invalid command" poweroff/sleep/return-1 path as main_app's -wm else.
    const bool is_work_mode_cmd =
        (argc >= 2) && (std::string(argv[1]) == "-wm" || std::string(argv[1]) == "--work-mode");
    if (is_work_mode_cmd &&
        argc == 5 && (std::string(argv[3]) == "-rtc" || std::string(argv[3]) == "--rtc-status")) {
        working_mode = (enum workingMode)stoi_custom(argv[2]);
        is_rtc_work_well = (bool)stoi_custom(argv[4]);
        Logger::log(LogLevel::INFO, "%s working mode %d, rtc status %d", __func__, working_mode, is_rtc_work_well);
    } else {
        Logger::log(LogLevel::ERROR, "%s Invalid command %s, power off", __func__,
                    (argc >= 2 ? argv[1] : ""));
        Power::getInstance()->requestShutdown();
        sleep(10);
        return -1;
    }

    // Construct the cascade context (before commonStartupPostDispatch, matching
    // main_app's ordering so the by-ref fields bind these locals — the
    // cleanupHook below captures the SAME mgmtServClient/storageServClient).
    app_workmode::WorkModeContext ctx{lc, is_rtc_work_well, mobile_rtsp_enabled,
                                      rtsp_audio_enabled, argc, argv,
                                      mgmtServClient, storageServClient};

    // --- T16 Phase C-3 §3: derive command BEFORE commonStartupPostDispatch ---
    // Restores the pre-C2 ordering so S11 netif selection sees CMD_MOBILE for
    // `-wm 3` (WORKING_MODE_TEST_ONLY). Same switch + RGB blink side effects as
    // runWorkMode (single source via workModeToCommand). RGB LED was set up in
    // commonStartup S6, so the asyncBlink side effects work here.
    command = app_workmode::workModeToCommand(working_mode, ctx);

    // --- main_app.cpp:313-315 S9-S13 post-dispatch ---
    if (!lc.commonStartupPostDispatch(cfg, command)) {
        goto workmode_exit;
    }

    // --- main_app.cpp:323-361 setCleanupHook (verbatim lambda body) ---
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
            // by the existing workmode_exit path (Misc::poweroff at the bottom).
            // Nothing to do here.
        }
    });

    // --- main_app.cpp:364 config alias for the tail's config->flush() ---
    config = lc.config();

    // --- main_app.cpp:375-378 runWorkMode (TerminalExit → skip the tail) ---
    if (app_workmode::runWorkMode(working_mode, ctx) == app_workmode::CascadeResult::TerminalExit) {
        return -1;   // invalid -wm mode: skip the tail (matches main_app's `return -1`)
    }

workmode_exit:
    // --- main_app.cpp:383-411 shutdown tail ---
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
    app_lifecycle::syncWithMCU();   // shared with main_app (moved into app_lifecycle, §4)
    config->flush();
#if POWER_MANAGER_ON
    Misc::poweroff();
    while(1);
#endif
    return 0;
#endif
}
