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
#include "event_loop.h"         // app_workmode::EventLoop (-wm 0 长驻主循环)
#include "record_task.h"        // app_workmode::RecordTask
#include "pir_trigger.h"        // app_workmode::SimPirTrigger / GpioPirTrigger
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
    auto uploadWorker = std::make_shared<app_workmode::UploadWorker>();
    app_workmode::WorkModeContext ctx{lc, is_rtc_work_well, mobile_rtsp_enabled,
                                      rtsp_audio_enabled, argc, argv,
                                      mgmtServClient, storageServClient};
    ctx.uploadWorker = uploadWorker;

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
            // 关机时绝不调 controlISP(DAY)：sensor/ISP 仍 enabled 时
            // IMP_ISP_Tuning_SetISPRunningMode 会触发 ISP 帧中断 defog 刷新
            // (tisp_day_or_night_par_refresh → defog_count_weight35abc) 解引用已释放
            // rmem buffer → kernel panic（devtest 2026-06-21 确定性复现，epc
            // defog_count_weight35abc / BadVA 0 / Kernel panic in interrupt）。关机语义下
            // ISP 模式切换本就多余——ISP 状态由后续 RecordTask::stop()→releaseVideoResources
            // →IngenicVideo::exit() 的官方 teardown(IMP_ISP_DisableTuning/DisableSensor/Close，
            // imp_isp.h:74-102)兜底。先停 auto-switch 线程，防它在 teardown 中 race 进 controlISP。
            lc.daynight()->stopAutoSwitch();
            lc.daynight()->controlIRLed(DayNightState::DAY);   // 纯 GPIO，保留（IR-LED 安全态）
            lc.daynight()->controlIRCut(DayNightState::DAY);   // 纯 GPIO，保留（IR-cut 安全态）
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

    // 阶段1: 启动后台上传 worker（录影产物 desc 入队即返回，不被上传阻塞）。
    {
        auto ms_ip = config->get(INI_SECTION_SERVER, INI_KEY_MS_IP, "");
        auto ms_port = config->get(INI_SECTION_SERVER, INI_KEY_MS_PORT, 0);
        if (!ms_ip.empty() && ms_port > 0) {
            uploadWorker->start(ms_ip, ms_port);
        } else {
            Logger::log(LogLevel::WARNING, "UploadWorker: mgmt server not configured, skip start");
        }
    }

    // --- -wm 0 走长驻 EventLoop（PIR 触发录影 + 并行上传）；其他 -wm 走原 runCommands ---
    if (working_mode == workingMode::WORKING_MODE_SNAP_ONLY) {
        int pirIntervalMs = 10000;  // SimPirTrigger 默认 10s 模拟 PIR 间隔
        if (const char* env = std::getenv("HTC_SIM_PIR_INTERVAL_MS")) {
            int v = std::atoi(env);
            if (v > 0) pirIntervalMs = v;
        }
        int64_t uploadTimeoutMs = 60000;  // 上传 timeout 默认 60s（HTC_UPLOAD_TIMEOUT_MS 可调）
        if (const char* env = std::getenv("HTC_UPLOAD_TIMEOUT_MS")) {
            int v = std::atoi(env);  // uClibc 无 std::atoll；int 足够覆盖 ms 级超时
            if (v > 0) uploadTimeoutMs = v;
        }
        auto recordTask = std::make_shared<app_workmode::RecordTask>(uploadWorker);
        auto pirTrigger = std::make_shared<app_workmode::SimPirTrigger>(pirIntervalMs);
        app_workmode::EventLoop loop(lc, recordTask, pirTrigger, uploadWorker, uploadTimeoutMs);
        loop.run();          // 长驻，直到关机（上传完成/timeout/外部信号）
        recordTask->stop();  // 优雅停当前录影（若有）
    } else {
        // --- main_app.cpp:375-378 runWorkMode (TerminalExit → skip the tail) ---
        if (app_workmode::runWorkMode(working_mode, ctx) == app_workmode::CascadeResult::TerminalExit) {
            return -1;   // invalid -wm mode: skip the tail (matches main_app's `return -1`)
        }
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
    // 阶段1: 关机前排空上传队列（30s 超时保底；未传 desc 留 SD，F_UploadedTag=0，下次重传）。
    if (!uploadWorker->flush(30000)) {
        Logger::log(LogLevel::WARNING, "UploadWorker: flush timed out, some desc kept on SD");
    }
    uploadWorker->stop();
#ifdef BUILD_FOR_SIMULATION
    Logger::log(LogLevel::INFO, "[SIM] Program exit normally");
    _exit(0);
#else
    app_lifecycle::syncWithMCU();   // shared with main_app (moved into app_lifecycle, §4)
    config->flush();
#if POWER_MANAGER_ON
    // devtest loop: skip board poweroff so the app returns to the shell and the
    // test harness can re-run it in the same boot. Env-guarded, never on in
    // production. See doc/knowledge/decisions/devtest-automation-loop.md §3/§5.
    if (std::getenv("HTC_TEST_NO_POWEROFF")) {
        Logger::log(LogLevel::INFO, "[TEST] HTC_TEST_NO_POWEROFF set: _exit(0) instead of poweroff");
        _exit(0);
    }
    Misc::poweroff();
    while(1);
#endif
    return 0;
#endif
}
