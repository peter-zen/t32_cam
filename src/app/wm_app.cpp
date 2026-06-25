// wm — 新建独立 binary（wm-app-spec）。重组 Phase-1 已验证模块（record/snap/upload/
// ntp/mcu）成 3 模式一次性任务程序：`wm -m 0`(CAPTURE_ONLY) / `-m 1`(CAPTURE+UPLOAD) /
// `-m 2`(UPLOAD_ONLY)。丢弃 crash 不收敛的 htc_workmode_app。
//
// main 骨架照搬 workmode_app.cpp（防 IMP-residue crash 的承重部分逐字复用）：
//   commonStartup → installSignalHandlers → -m 解析 → commonStartupPostDispatch
//   → setCleanupHook(stopAutoSwitch 是防 crash 关键) → 时间链 → uploadWorker.start
//   → WmScheduler.run → wm_exit 尾（shutdown → upload stop → writebackMcu → flush → poweroff）。
// IMP 懒初始化（sharedVideo 单例），进程内永不 IMP_System_Exit。
//
// Slice 1：只接通 m2（UploadOnly）端到端。m0/m1（capture lane + trigger）= Slice 2。
//
// Dual-platform：BUILD_FOR_SIMULATION=ON (build_sim) + T32 toolchain (build)。
// uClibc-safe：用 stoi_custom/to_string_custom，无 std::stoi/std::to_string。

#include <iostream>
#include <string>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <limits.h>
#include <csignal>
#include <memory>

#include "wm_scheduler.h"     // app_workmode::WmScheduler + WmMode
#include "wm_time.h"          // app_workmode::acquireTimeChain / writebackMcuTime
#include "upload_worker.h"    // app_workmode::UploadWorker
#include "capture_lane.h"     // app_workmode::CaptureLane (m0/m1)
#include "pir_trigger.h"      // app_workmode::SimPirTrigger / IPirTrigger (m0/m1)
#include "WorkModeRunner.h"   // CMD_* (仅为 commonStartupPostDispatch 的 netif 选择)
#include "ProcessLifecycle.h" // app_lifecycle::ProcessLifecycle + Startup/ShutdownContext
#include "DeviceConfig.h"
#include "Common.h"
#include "Logger.h"
#include "EnvManager.h"
#include "misc/Misc.h"
#include "Settings.h"
#include "StringConvert.h"    // stoi_custom / to_string_custom (uClibc-safe)
#include "app.h"              // ENV_FILE_PATHNAME, POWER_HOLD_PIN, INI_*, MEDIA_UPLOAD_PATH
#include "Power.h"
#include "DayNightSwitch.h"
#include "GPIO.h"             // GPIO, GPIO_VALUE, GPIO_DIRECTION, POWER_HOLD_PIN

namespace {

std::string normalizePath(const std::string& path) {
    char resolved[PATH_MAX];
    if (realpath(path.c_str(), resolved) != nullptr) return std::string(resolved);
    return path;
}

}  // namespace

int main(int argc, char* argv[])
{
    // 提前声明（goto wm_exit 路径不跨非平凡初始化）。
    std::shared_ptr<DeviceConfig> config;
    int command = 0;
    app_workmode::WmMode wm_mode = app_workmode::WmMode::UploadOnly;
    std::shared_ptr<app_workmode::UploadWorker> uploadWorker;
    bool ntpSynced = false;
    std::string ntpServer = "www.aidetcloud.com:123";  // 默认；config 加载后覆盖

    // --- S1 path inputs → StartupConfig (sim vs HW) ---
    app_lifecycle::StartupConfig cfg;
#ifdef BUILD_FOR_SIMULATION
    cfg.isSimulation = true;
    std::string exePath = Misc::getExecutablePath();
    cfg.projectRootPath = normalizePath(exePath + "/../..");
    std::string defaultSimRootPath = normalizePath(cfg.projectRootPath + "/sim_sdcard_runtime");
    const char* envSimRoot = std::getenv("SIM_SD_ROOT");
    cfg.simRootPath = (envSimRoot && envSimRoot[0] != '\0') ? normalizePath(envSimRoot) : defaultSimRootPath;
    const char* envLogDir = std::getenv("SIM_LOG_DIR");
    cfg.dbPath    = cfg.simRootPath + "/data/db";
    cfg.mediaRoot = cfg.simRootPath + "/DCIM";
    cfg.logRoot   = (envLogDir && envLogDir[0] != '\0') ? std::string(envLogDir) : (cfg.simRootPath + "/logs");
    cfg.logFile   = cfg.logRoot + "/app.log";
#else
    cfg.isSimulation = false;
    EnvManager::getInstance()->parsePrimaryEnv(ENV_FILE_PATHNAME);  // 必须在最开始
    cfg.dbPath    = EnvManager::getInstance()->getEnv("DB_PATH", "/mnt/sdcard/data/db");
    cfg.mediaRoot = "/mnt/sdcard/DCIM";
    cfg.logRoot   = "/mnt/sdcard/logs";
    cfg.logFile   = cfg.logRoot + "/app.log";
#endif

    // --- S1-S8 + signal install ---
    app_lifecycle::ProcessLifecycle lc;
    if (!lc.commonStartup(cfg)) return -1;
    if (!lc.installSignalHandlers()) return -1;

    // Ensure media output dirs exist (a freshly-formatted SD has no media/ yet;
    // without this, snap/record fopen to MEDIA_TARGET_PATH ENOENT before any
    // desc json mkdirs media/upload/ — photo+record would write 0 bytes).
    Misc::createDirectory(MEDIA_TARGET_PATH);
    Misc::createDirectory(MEDIA_UPLOAD_PATH);

    // Debug: HTC_LOG_DEBUG=1 lowers elog filter to DEBUG on HW (default INFO)
    // so module-level DEBUG logs surface. Used for cm==1 wedge diagnosis; no-op
    // when unset. (hal/ IMP call-level rc trace is separate: HTC_HAL_TRACE=1.)
    if (const char* dbg = std::getenv("HTC_LOG_DEBUG")) {
        if (dbg[0] == '1') Logger::setLogLevel(LogLevel::DEBUG);
    }

    // --- -m <0|1|2> 解析 ---
    const bool is_mode_cmd =
        (argc >= 3) && (std::string(argv[1]) == "-m" || std::string(argv[1]) == "--mode");
    if (is_mode_cmd) {
        int m = stoi_custom(argv[2]);
        if (m < 0 || m > 2) {
            Logger::log(LogLevel::ERROR, "%s invalid -m %d (expect 0|1|2), power off", __func__, m);
            Power::getInstance()->requestShutdown();
            sleep(10);
            return -1;
        }
        wm_mode = static_cast<app_workmode::WmMode>(m);
        Logger::log(LogLevel::INFO, "[wm] op=boot mode=%d", m);
    } else {
        Logger::log(LogLevel::ERROR, "%s invalid command (expect -m <0|1|2>), power off", __func__);
        Power::getInstance()->requestShutdown();
        sleep(10);
        return -1;
    }

    // command 只为 commonStartupPostDispatch 的 S11 netif 选择（wm 不跑 cascade）：
    // m0 离线；m1/m2 需网络。镜像 workModeToCommand 的 netif 位。
    if (wm_mode == app_workmode::WmMode::CaptureOnly) {
        command = CMD_SNAP;
    } else if (wm_mode == app_workmode::WmMode::CaptureUpload) {
        command = CMD_SNAP | CMD_CONN_NET | CMD_DHCP | CMD_NTP;
    } else {  // UploadOnly
        command = CMD_CONN_NET | CMD_DHCP | CMD_NTP;
    }

    // --- S9-S13 post-dispatch ---
    if (!lc.commonStartupPostDispatch(cfg, command)) goto wm_exit;

    {  // main work scope — wrapped so the goto above doesn't cross initializers
    config = lc.config();

    // --- setCleanupHook（防 IMP-residue crash：stopAutoSwitch 是关键，逐字复用 workmode_app）---
    lc.setCleanupHook([&lc](int sig) {
        Logger::log(LogLevel::INFO, "[wm] cleanup signal %d", sig);
        if (lc.daynight()) {
            // 关机时绝不 controlISP(DAY)（sensor/ISP enabled 时触发 ISP ISR defog panic）。
            // 先停 auto-switch 线程，防它在 teardown 中 race 进 controlISP。
            lc.daynight()->stopAutoSwitch();
            lc.daynight()->controlIRLed(DayNightState::DAY);   // 纯 GPIO 安全态
            lc.daynight()->controlIRCut(DayNightState::DAY);   // 纯 GPIO 安全态
        }
        if (lc.rgbLed()) lc.rgbLed()->setConstant(GPIO_VALUE::LOW);
        std::string setting_file_path = EnvManager::getInstance()->getEnv("SETTING_FILE_PATH", "");
        if (!setting_file_path.empty()) {
            if (!Settings::getInstance()->saveToJsonFile(setting_file_path)) {
                Logger::log(LogLevel::ERROR, "[wm] failed to save setting file: %s", setting_file_path.c_str());
            }
        }
        if (sig == SIGTERM && Power::getInstance()->isChangeModeRequested()) {
            Logger::log(LogLevel::INFO, "[wm] change mode requested, holding power");
            auto g = GPIO(POWER_HOLD_PIN);
            if (!g.exportGPIO() || !g.setDirection(GPIO_DIRECTION::OUTPUT) || !g.setValue(GPIO_VALUE::HIGH)) {
                Logger::log(LogLevel::ERROR, "[wm] failed to set power hold pin");
            }
        }
    });

    // --- 时间链（spec §6.1）：wm 自跑 RTC→MCU→NTP + ntpSynced（commonStartup 已先 RTC→MCU）---
    {
        auto ntp_ip   = config->get(INI_SECTION_SERVER, INI_KEY_NTP_IP, "");
        auto ntp_port = config->get(INI_SECTION_SERVER, INI_KEY_NTP_PORT, 0);
        if (!ntp_ip.empty() && ntp_port > 0) ntpServer = ntp_ip + ":" + to_string_custom(ntp_port);
        std::string src = app_workmode::acquireTimeChain(ntpServer, ntpSynced);
        Logger::log(LogLevel::INFO, "[wm] op=time source=%s ntp_synced=%d",
                    src.c_str(), ntpSynced ? 1 : 0);
    }

    // --- Upload lane（m1/m2）：启动后台 worker ---
    if (wm_mode != app_workmode::WmMode::CaptureOnly) {
        uploadWorker = std::make_shared<app_workmode::UploadWorker>();
        auto ms_ip   = config->get(INI_SECTION_SERVER, INI_KEY_MS_IP, "");
        auto ms_port = config->get(INI_SECTION_SERVER, INI_KEY_MS_PORT, 0);
        if (!ms_ip.empty() && ms_port > 0) {
            uploadWorker->start(ms_ip, ms_port);
        } else {
            Logger::log(LogLevel::WARNING, "[wm] mgmt server not configured; upload will fail (desc kept on SD)");
        }
    }

    // --- 调度器旋钮 ---
    int64_t idleGraceMs = 30000;
    if (const char* e = std::getenv("HTC_WM_IDLE_GRACE_MS")) { int v = std::atoi(e); if (v > 0) idleGraceMs = v; }
    int64_t uploadTimeoutMs = 60000;
    if (const char* e = std::getenv("HTC_UPLOAD_TIMEOUT_MS")) { int v = std::atoi(e); if (v > 0) uploadTimeoutMs = v; }

    // --- Capture lane + trigger（m0/m1 才有；m2 无捕获）---
    std::shared_ptr<app_workmode::CaptureLane>  captureLane;
    std::shared_ptr<app_workmode::IPirTrigger>  pirTrigger;
    if (wm_mode != app_workmode::WmMode::UploadOnly) {
        captureLane = std::make_shared<app_workmode::CaptureLane>(uploadWorker);
        int pirIntervalMs = 10000;  // SimPirTrigger 默认 10s（HTC_SIM_PIR_INTERVAL_MS 可调）
        if (const char* e = std::getenv("HTC_SIM_PIR_INTERVAL_MS")) { int v = std::atoi(e); if (v > 0) pirIntervalMs = v; }
        pirTrigger = std::make_shared<app_workmode::SimPirTrigger>(pirIntervalMs);
    }

    // --- 主循环（长驻直到关机）---
    app_workmode::WmScheduler scheduler(lc, wm_mode, uploadWorker, captureLane, pirTrigger,
                                        idleGraceMs, uploadTimeoutMs);
    scheduler.run();
    }  // end main work scope

wm_exit:
    // --- shutdown 尾（顺序承重，勿改）---
    {
        app_lifecycle::ShutdownContext sctx;
        sctx.programType   = lc.programType();
        sctx.command       = command;
        sctx.rtcWorkedWell = true;   // wm 自跑链，无 -rtc 入参
        lc.shutdown(sctx);
    }
    // flush+stop（同 workmode_app/EventLoop proven 模式）。UploadWorker::stop() 在阻塞 I/O
    // 上可能卡，靠关机信号（upload-timeout→requestShutdown 或外部 SIGTERM）先中断 worker。
    // 注：spec §3.4 原写「Shutdown 不 flush」——但 UploadWorker::stop 不可靠中断，故沿用
    // proven 的 flush+stop；spec 待更新（见 reviews Slice 1）。
    if (uploadWorker) {
        if (!uploadWorker->flush(30000)) {
            Logger::log(LogLevel::WARNING, "[wm] UploadWorker flush timed out, desc kept on SD");
        }
        uploadWorker->stop();
    }
#ifdef BUILD_FOR_SIMULATION
    Logger::log(LogLevel::INFO, "[SIM] wm exit normally");
    _exit(0);
#else
    // spec §7 关机回写 MCU（仅时间，ntpSynced 规则）。替代 syncWithMCU。
    app_workmode::writebackMcuTime(ntpSynced, ntpServer);
    if (config) config->flush();
#if POWER_MANAGER_ON
    if (std::getenv("HTC_TEST_NO_POWEROFF")) {
        Logger::log(LogLevel::INFO, "[TEST] HTC_TEST_NO_POWEROFF set: _exit(0) instead of poweroff");
        _exit(0);
    }
    Misc::poweroff();
    while (1);
#endif
    return 0;
#endif
}
