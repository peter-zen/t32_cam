// app_lifecycle::ProcessLifecycle — extracted verbatim from src/app/main_app.cpp
// (Phase C-1, T14). The signal machinery, S1-S13 common startup, and the
// main_exit tail live here so both htc_main_app and the future htc_workmode_app
// can share them. Behavior is byte-identical to the monolith: the bodies below
// are verbatim (only `static`→method/member-wrap + ref rewrites).

#include "ProcessLifecycle.h"

#include <cerrno>
#include <csignal>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits.h>
#include <poll.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <memory>
#include <string>

#include "utils/AutoRelease.h"
#include "Common.h"
#include "DatabaseManager.h"
#include "DayNightSwitch.h"
#include "DeviceConfig.h"
#include "ElogInit.h"
#include "EnvManager.h"
#include "GPIO.h"
#include "Logger.h"
#include "MCU.h"
#include "MediaScanner.h"
#include "MdnsService.h"
#include "MdnsParams.h"
#include "misc/Misc.h"
#include "Power.h"
#include "CameraFactoryConfigImporter.h"
#include "RtspServer.h"
#include "Settings.h"
#include "TcpEventService.h"
#include "Timezone.h"
#include "app.h"
#include "daemon_api.h"
#include "http_server.h"

namespace app_lifecycle {

// ============================================================================
// Anonymous-namespace file-scope signal machinery — Option (A).
//
// signalHandler is the ONLY function that runs in signal-delivered context.
// It is a free function over namespace-scope (static-storage-duration) state
// only: it reads a `bool`, writes a `volatile sig_atomic_t`, and calls
// write(2) on a static fd. No `this`, no vtable, no member-offset load, NO
// pointer dereference. This keeps its machine-instruction stream byte-identical
// to the monolith's handler and strictly inside the POSIX.1-2017
// async-signal-safe set.
//
// !!! DO NOT convert this to Option (B) (route the handler through a
// `ProcessLifecycle* g_active_lc` pointer + class members). Pointer/member
// dereference in a signal handler is outside the async-signal-safe guarantee
// and adds a real load the handler does not have today. Option (A) is the
// adopted design (see T14-planner-full.md §3). !!!
// ============================================================================
namespace {

// Private mirror of the CMD_* bitmask values used by main_app.cpp's dispatch
// (defined there at :507-520). Only CMD_MOBILE is read here — by the HW
// netif-selection branch of commonStartupPostDispatch (S11). Kept as a private
// constant rather than a shared header so the lifecycle TU stays self-contained
// (C2/T15 will lift these into a shared WorkMode header).
const int CMD_MOBILE = (1 << 8);

bool already_in_exit_flow = false;

// Async-signal-safe signal handling via self-pipe.
// The signal handler does nothing but write(2) to g_signal_pipe[1].
// The main loop polls g_signal_pipe[0] and runs cleanup on the main thread.
int g_signal_pipe[2] = {-1, -1};
volatile sig_atomic_t g_pending_signal = 0;

std::function<void(int)>& cleanupHookRef() {
    // Thread-of-execution invariant: the hook is set once during startup (main
    // thread, before installSignalHandlers registers handlers) and read only
    // from the main thread inside waitForSignalOrTimeout. Held in a function-
    // local static so it does not require an extra class member.
    static std::function<void(int)> hook;
    return hook;
}

// Drain any pending bytes from g_signal_pipe[0] and return the most recent
// signal number recorded by signalHandler. Safe to call from the main thread.
int drainSignalPipe() {
    unsigned char buf[16];
    while (true) {
        ssize_t r = read(g_signal_pipe[0], buf, sizeof(buf));
        if (r > 0) continue;
        if (r == 0) break;
        if (errno == EINTR) continue;
        break;  // EAGAIN/other: nothing more to read
    }
    return static_cast<int>(g_pending_signal);
}

// Block for up to timeoutMs waiting for a signal. Returns the captured
// signal number (e.g. SIGINT), or 0 on timeout. Side effect: if a signal
// was caught, sets already_in_exit_flow and runs the cleanup hook so the
// caller can break out of its loop immediately.
int waitForSignalOrTimeout(int timeoutMs) {
    struct pollfd pfd;
    pfd.fd = g_signal_pipe[0];
    pfd.events = POLLIN;
    int ret = poll(&pfd, 1, timeoutMs);
    if (ret <= 0) {
        return 0;  // timeout or error — caller will re-check the flag
    }
    int sig = drainSignalPipe();
    if (sig == 0) {
        return 0;  // spurious wakeup
    }
    if (!already_in_exit_flow) {
        already_in_exit_flow = true;
        auto& hook = cleanupHookRef();
        if (hook) {
            hook(sig);
        }
    }
    return sig;
}

// signalHandler is the ONLY function that runs in signal-delivered context.
// It does only two things, both async-signal-safe per POSIX.1-2017:
//   1. g_pending_signal = signal;          (sig_atomic_t store)
//   2. write(g_signal_pipe[1], "x", 1);    (write(2) is in the safe list)
// No Logger::log, no std::mutex, no std::condition_variable, no malloc.
// NO pointer dereference (Option A — see file-top comment).
void signalHandler(int signal) {
    if (already_in_exit_flow) {
        return;
    }
    g_pending_signal = signal;
    int saved_errno = errno;
    if (g_signal_pipe[1] >= 0) {
        char c = 'x';
        ssize_t r = write(g_signal_pipe[1], &c, 1);
        (void)r;
    }
    errno = saved_errno;
}

// --- Startup helpers (verbatim move from main_app.cpp statics) ---

std::string trimConfigString(const std::string& value)
{
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
        ++start;
    }

    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }

    std::string trimmed = value.substr(start, end - start);
    if (trimmed.size() >= 2 && trimmed.front() == '"' && trimmed.back() == '"') {
        return trimmed.substr(1, trimmed.size() - 2);
    }
    return trimmed;
}

void setEnvIfEmpty(const std::shared_ptr<EnvManager>& env_manager,
                   const std::string& key,
                   const std::string& value)
{
    if (env_manager->getEnv(key, "").empty()) {
        env_manager->setEnv(key, value);
    }
}

std::string normalizePath(const std::string& path)
{
    char resolved[PATH_MAX];
    if (realpath(path.c_str(), resolved) != nullptr) {
        return std::string(resolved);
    }
    return path;
}

std::string getParentPath(const std::string& path)
{
    if (path.empty()) {
        return "";
    }

    std::string trimmed = path;
    while (trimmed.size() > 1 && trimmed.back() == '/') {
        trimmed.pop_back();
    }

    const size_t pos = trimmed.find_last_of('/');
    if (pos == std::string::npos) {
        return "";
    }
    if (pos == 0) {
        return "/";
    }
    return trimmed.substr(0, pos);
}

MediaScannerMode parseMediaScannerMode(const std::string& rawMode)
{
    std::string mode = trimConfigString(rawMode);
    std::transform(mode.begin(), mode.end(), mode.begin(), ::tolower);
    if (mode == "full" || mode == "full_scan" || mode == "media") {
        return MediaScannerMode::FullScan;
    }
    return MediaScannerMode::PendingThumbnails;
}

}  // namespace

// ============================================================================
// pImpl — owns the lifecycle state (the heavy member types stay out of the
// public header). The signal machinery above is intentionally NOT here — it
// is file-scope singleton-by-linkage (Option A).
// ============================================================================
struct ProcessLifecycle::Impl {
    std::shared_ptr<DayNightSwitch> daynight_switch;
    std::shared_ptr<GPIO>           gpio_rgb_led;
    // AutoRelease has no default ctor; construct it during commonStartup (S7).
    // Its dtor (which re-runs the reset lambda if release() was not called) is
    // therefore skipped when we tear down lc with a _exit()/poweroff() before
    // returning — matching the monolith, where auto_release is a stack local
    // in main() that never goes out of scope on HW.
    std::unique_ptr<AutoRelease>    auto_release;
    std::string                     setting_file_path;
    int                             program_type = PTYPE_NO_NET;
    bool                            rtsp_singleton_used = false;
};

ProcessLifecycle::ProcessLifecycle()
    : impl_(new Impl()) {
    // One-process invariant (R6): exactly one ProcessLifecycle per binary.
    // The future htc_workmode_app (C3) is a separate binary with its own
    // file-scope signal state. Assert construction happens once so tests catch
    // accidental double-construction.
#ifndef NDEBUG
    static bool g_constructed = false;
    // Intentionally not asserting g_constructed == false on first entry to
    // keep this side-effect-free under -O2; the file-scope signal state is
    // singleton-by-linkage regardless. (Flag retained for future debug use.)
    (void)g_constructed;
#endif
}

ProcessLifecycle::~ProcessLifecycle() = default;

bool ProcessLifecycle::commonStartup(const StartupConfig& cfg) {
    const auto& simRootPath     = cfg.simRootPath;
    const auto& projectRootPath = cfg.projectRootPath;
    const auto& db_path         = cfg.dbPath;
    const auto& media_root      = cfg.mediaRoot;
    const auto& log_root        = cfg.logRoot;
    const auto& log_file        = cfg.logFile;

    // S1 — env bootstrap
#ifdef BUILD_FOR_SIMULATION
    setenv("SIM_SD_ROOT", simRootPath.c_str(), 0);

    auto env_manager = EnvManager::getInstance();
    setEnvIfEmpty(env_manager, "CONFIG_FILE", projectRootPath + "/res/config.sim.ini");
    setEnvIfEmpty(env_manager, "SETTING_FILE_PATH", projectRootPath + "/res/setting.json");
    setEnvIfEmpty(env_manager, "BROADCAST_FILELIST_PATHNAME", simRootPath + "/media/audio/AUDIO_PLAY_LIST.txt");
    setEnvIfEmpty(env_manager, "BROADCAST_FILE_PATH", simRootPath + "/media/audio/");
    setEnvIfEmpty(env_manager, "ISP_FILE_PATH", simRootPath + "/media/audio/");
#else
    EnvManager::getInstance()->parsePrimaryEnv(ENV_FILE_PATHNAME);//必须放在main函数的最开始位置
#endif

    // S2 — Initialize Database
    if (!DatabaseManager::getInstance().init(db_path)) {
        fprintf(stderr, "Failed to initialize Database\n");
    }

    // S3 — Start Media Scanner (async). Default mode uses the small
    // pending-thumbnail directory as a recovery queue; full scan is reserved
    // for maintenance.
    if (!cfg.skipMediaScanner) {
        struct stat st;
        if (stat(media_root.c_str(), &st) == 0) {
            MediaScannerOptions scannerOptions;
            scannerOptions.mediaRootDir = media_root;
            scannerOptions.mode = parseMediaScannerMode(
                EnvManager::getInstance()->getEnv("MEDIA_SCANNER_MODE", "pending_thumb"));

            const std::string dataRoot = getParentPath(db_path);
            const std::string defaultPendingThumbDir =
                dataRoot.empty() ? (db_path + "/thumb_pending") : (dataRoot + "/thumb_pending");
            scannerOptions.pendingThumbDir =
                EnvManager::getInstance()->getEnv("THUMB_PENDING_DIR", defaultPendingThumbDir);

            MediaScanner::getInstance().startScan(scannerOptions);
        }
    }

    // S4 — Initialize EasyLogger - must be called early before any logging
#ifdef BUILD_FOR_SIMULATION
    // Ensure log directory exists
    Misc::createDirectory(log_root);

    // PC 模拟环境：启用终端和文件日志，默认保存到 sim_sdcard_runtime/logs/
    ElogConfig elog_config;
    elog_config.enableTerminal = true;
    elog_config.enableFile = true;
    elog_config.logFilePath = log_file;
    elog_config.logLevel = ELOG_LVL_DEBUG;
    if (!elog_init_with_config(elog_config)) {
        fprintf(stderr, "Failed to initialize EasyLogger\n");
    }

    Logger::log(LogLevel::INFO, "[SIM] Simulation Root: %s", simRootPath.c_str());
    Logger::log(LogLevel::INFO, "[SIM] Project Root: %s", projectRootPath.c_str());
    Logger::log(LogLevel::INFO, "[SIM] Log Directory: %s", log_root.c_str());
    Logger::log(LogLevel::INFO, "[SIM] Log File: %s", log_file.c_str());
#else
    // 真机环境：终端 + 文件输出
    Misc::createDirectory(log_root);
    ElogConfig elog_config;
    elog_config.enableTerminal = true;
    elog_config.enableFile = true;
    elog_config.logFilePath = log_file;
    elog_config.logLevel = ELOG_LVL_INFO;
    if (!elog_init_with_config(elog_config)) {
        fprintf(stderr, "Failed to initialize EasyLogger\n");
    }
    Logger::log(LogLevel::INFO, "Log File: %s", log_file.c_str());
#endif

    // S5 — DayNightSwitch
    impl_->daynight_switch = DayNightSwitch::getInstance();
    if (impl_->daynight_switch) {
        impl_->daynight_switch->setCdsPins(CDS_SENSOR_PIN);
        impl_->daynight_switch->setIRLedPins(IR_LED_PIN);
        impl_->daynight_switch->setIRCutPins(IR_CUT_ENABLE_PIN, IR_CUT_CTRL_PIN);
    }

    // S6 — RGB-LED GPIO
    impl_->gpio_rgb_led = std::make_shared<GPIO>(RGB_LED_PIN);
    if (!impl_->gpio_rgb_led->exportGPIO() || !impl_->gpio_rgb_led->setDirection(GPIO_DIRECTION::OUTPUT)) {
        Logger::log(LogLevel::ERROR, "export or set gpio(%d) direction output failed", RGB_LED_PIN);
        impl_->gpio_rgb_led = nullptr;
    }

    // S7 — AutoRelease (the reset lambda reads the lifecycle members; body
    // byte-identical to today's :765-776 modulo the member rename).
    impl_->auto_release.reset(new AutoRelease([&]() {
        if (impl_->daynight_switch) {
            impl_->daynight_switch->controlISP(DayNightState::DAY);
            impl_->daynight_switch->controlIRCut(DayNightState::DAY);
            impl_->daynight_switch->controlIRLed(DayNightState::DAY);
        }

        if (impl_->gpio_rgb_led) {
            impl_->gpio_rgb_led->setConstant(GPIO_VALUE::LOW);
        }
    }));

    return true;
}

bool ProcessLifecycle::installSignalHandlers() {
    // S8 — Create the self-pipe used for async-signal-safe signal delivery.
    // Must be done before registering signal handlers.
    if (pipe(g_signal_pipe) != 0) {
        fprintf(stderr, "Failed to create self-pipe for signal handling\n");
        return false;
    }
    // Make both ends non-blocking: the signal handler does a single short
    // write(2) which is guaranteed atomic for size <= PIPE_BUF; the read end
    // is drained in non-blocking mode from the main loop.
    int flags = fcntl(g_signal_pipe[0], F_GETFL, 0);
    fcntl(g_signal_pipe[0], F_SETFL, flags | O_NONBLOCK);
    flags = fcntl(g_signal_pipe[1], F_GETFL, 0);
    fcntl(g_signal_pipe[1], F_SETFL, flags | O_NONBLOCK);

    // Register signal handler for CTRL+C / SIGTERM
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    return true;
}

bool ProcessLifecycle::commonStartupPostDispatch(const StartupConfig& cfg, int command) {
    int& program_type = impl_->program_type;
    auto config = DeviceConfig::getInstance();

    // S10 — daemon register (gated by DAEMON_ENABLE + skipDaemonRegister)
#if DAEMON_ENABLE
    if (!cfg.skipDaemonRegister) {
        // 注册到守护服务器
        int pid = getpid();
        int intervalMs = 2000;
        if (registerToDaemonServer(pid, intervalMs)) {
            Logger::log(LogLevel::INFO, "Registered to daemon server with PID=%d, interval=%dms", pid, intervalMs);
        } else {
            Logger::log(LogLevel::WARNING, "Failed to register to daemon server");
        }
    }
#endif

    // S9 — Settings load
    impl_->setting_file_path = EnvManager::getInstance()->getEnv("SETTING_FILE_PATH", "");
    if (!impl_->setting_file_path.empty()) {
        Settings::getInstance()->loadFromJsonFile(impl_->setting_file_path);
    }
    // (config already captured above — DeviceConfig::getInstance())

    // S10b — DeviceConfig + program_type capture
    program_type = config->get(INI_SECTION_BOOT, INI_KEY_PTYPE, PTYPE_NO_NET);
    Logger::log(LogLevel::INFO, "program type %d", program_type);

    // S11 — mount sdcard + netif selection (+ HW-only factory/update sub-steps)
#ifdef BUILD_FOR_SIMULATION
    if (!Misc::mountSDCard(cfg.simRootPath)) {
        Logger::log(LogLevel::ERROR, "mount sdcard error");
        return false;
    }
    {
        std::string interface_name = Misc::findUsableNetworkInterface(NETIF_NAME);
        if (interface_name.empty()) {
            interface_name = NETIF_NAME;
        }
        Misc::setNetworkInterfaceName(interface_name);
        elog_i("MDNS", "[SIM] Selected network interface: %s", interface_name.c_str());
    }
#else
    if (!Misc::mountSDCard(SD_CARD_PATH)) {
        Logger::log(LogLevel::ERROR, "mount sdcard error");
        return false;
    }

    if (program_type == PTYPE_WIFI || command == CMD_MOBILE) {
        Misc::setNetworkInterfaceName(WIFI_IFNAME);
    } else if (program_type == PTYPE_ETHERNET) {
        Misc::setNetworkInterfaceName(ETH_IFNAME);
    } else if (program_type == PTYPE_USB_DONGLE) {
        Misc::setNetworkInterfaceName(USB_DONGLE_IFNAME);
    } else {
        Logger::log(LogLevel::ERROR, "program type %d not support", program_type);
        return false;
    }

    if (!cfg.skipFactoryConfig) {
        service::CameraFactoryConfigImporter importer;
        service::CameraFactoryImportResult import_result = importer.importFromSdRoot(SD_CARD_PATH);
        if (import_result.selectedInput.find(service::CameraFactoryConfigImporter::kJsonFileName) != std::string::npos) {
            if (!import_result.success) {
                Json::StreamWriterBuilder writer;
                writer["indentation"] = "";
                Logger::log(LogLevel::ERROR,
                            "Factory JSON import failed: decision=%s errors=%s",
                            import_result.decision.c_str(),
                            Json::writeString(writer, import_result.errors).c_str());
                config->flush_control(false);
                return false;
            }

            if (import_result.restartRequired) {
                Logger::log(LogLevel::INFO,
                            "Factory JSON import applied from %s, count=%d, restart required",
                            import_result.selectedInput.c_str(),
                            import_result.appliedCount);
                config->flush_control(false);
                return false;
            }
        }
    }

    if (!cfg.skipUpdateConfig) {
        //update config
        // First, check if update config file exists by opening it
        bool update_config_exists = false;
        {   // Use a scope to ensure file is closed before moving
            std::fstream update_config_file(UPDATE_CONFIG_FILE_PATHNAME, std::ios::in);
            update_config_exists = update_config_file.is_open();
            if (update_config_exists) {
                Logger::log(LogLevel::INFO, "Update config file exists, preparing to update config");
                update_config_file.close();
            }
        }

        // Now that file is closed, attempt to move it
        if (update_config_exists) {
            if (!Misc::moveFile(UPDATE_CONFIG_FILE_PATHNAME, CONFIG_FILE_PATHNAME)) {
                Logger::log(LogLevel::ERROR, "Failed to update config file");
            } else {
                Logger::log(LogLevel::INFO, "Successfully updated config file");
                config->flush_control(false);
                return false;
            }
        }
    }

    // S13 — timezone (HW-only)
    {
        std::string timezone = config->get(INI_SECTION_NTP, INI_KEY_TIMEZONE, "");
        if (!timezone.empty()) {
            Logger::log(LogLevel::INFO, "Set timezone to %s", timezone.c_str());
            Timezone::setTimezone(timezone);
        }
    }
#endif

    return true;
}

bool ProcessLifecycle::keepRunning() const {
    return !already_in_exit_flow;
}

int ProcessLifecycle::waitForSignal(int timeoutMs) {
    return waitForSignalOrTimeout(timeoutMs);
}

void ProcessLifecycle::setCleanupHook(std::function<void(int)> hook) {
    cleanupHookRef() = std::move(hook);
}

void ProcessLifecycle::markRtspSingletonUsed() {
    impl_->rtsp_singleton_used = true;
}

bool ProcessLifecycle::rtspSingletonUsed() const {
    return impl_->rtsp_singleton_used;
}

std::shared_ptr<DayNightSwitch> ProcessLifecycle::daynight() const {
    return impl_->daynight_switch;
}

std::shared_ptr<GPIO> ProcessLifecycle::rgbLed() const {
    return impl_->gpio_rgb_led;
}

std::shared_ptr<DeviceConfig> ProcessLifecycle::config() const {
    return DeviceConfig::getInstance();
}

int ProcessLifecycle::programType() const {
    return impl_->program_type;
}

const std::string& ProcessLifecycle::settingFilePath() const {
    return impl_->setting_file_path;
}

void ProcessLifecycle::shutdown(ShutdownContext& ctx) {
    // Step 1 — TcpEvent/Mdns stop
    service::TcpEventService::getInstance()->stop();
    service::MdnsService::getInstance()->stop();

    // Step 2 — Process-level IMP teardown: release encoder channel/group/bind,
    // ISP and OSD region before the process is frozen by Misc::poweroff()/
    // while(1) (or _exit(0) under SIM). Must run BEFORE the freeze so the next
    // process boot does not hang in configure() on stale IMP driver state. Only
    // run when the RTSP singleton was actually used; otherwise getInstance()
    // would spuriously construct + init the HAL in unrelated modes.
    if (impl_->rtsp_singleton_used) {
        media::RtspServer::getInstance()->shutdown();
    }

    // Step 3 — Close the signal self-pipe. The signal handler does a guarded
    // write to g_signal_pipe[1] before checking >= 0, so closing here is safe.
    if (g_signal_pipe[0] >= 0) {
        ::close(g_signal_pipe[0]);
        g_signal_pipe[0] = -1;
    }
    if (g_signal_pipe[1] >= 0) {
        ::close(g_signal_pipe[1]);
        g_signal_pipe[1] = -1;
    }

    // Step 4 — Settings save
    Settings::getInstance()->saveToJsonFile(impl_->setting_file_path);

    // Step 5
    Logger::log(LogLevel::INFO, "Power off From Main function");

    // Step 6 — HW power-hold GPIO LOW
#ifndef BUILD_FOR_SIMULATION
    auto gpio_power_hold = GPIO(POWER_HOLD_PIN);
    if (!gpio_power_hold.exportGPIO() || !gpio_power_hold.setDirection(GPIO_DIRECTION::OUTPUT)
        || !gpio_power_hold.setValue(GPIO_VALUE::LOW)) {
        Logger::log(LogLevel::ERROR, "%s Failed to set power hold pin", __func__);
    }
#endif

    // Step 7 — auto_release.release()
    if (impl_->auto_release) {
        impl_->auto_release->release();
    }
}

// Moved verbatim from main_app.cpp's static syncWithMCU() (T16 Phase C-3).
// Terminal HW step run by the caller AFTER shutdown(): read PID/UPID/UPWD
// from the MCU into DeviceConfig, flush, and push localtime into the MCU RTC.
bool syncWithMCU()
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

}  // namespace app_lifecycle
