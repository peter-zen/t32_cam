#pragma once

#include <atomic>
#include <csignal>
#include <functional>
#include <memory>
#include <string>

// Forward decls — avoid pulling heavy headers into the public header.
class DeviceConfig;
class DayNightSwitch;
class GPIO;

namespace app_lifecycle {

// S1-S13 startup configuration. The skip-* flags let a pure -wm app drop the
// MediaScanner / factory-config / update-config steps that only make sense in
// the full main_app boot. htc_main_app sets none of the skip flags.
struct StartupConfig {
    // Path inputs (resolved by the caller before commonStartup):
    //   sim: projectRootPath + simRootPath (computed in main_app)
    //   hw : ENV_FILE_PATHNAME-driven (EnvManager::parsePrimaryEnv already ran)
    std::string simRootPath;          // empty on HW
    std::string projectRootPath;      // empty on HW
    std::string dbPath;               // simRootPath+"/data/db" | "/mnt/sdcard/data/db"
    std::string mediaRoot;            // simRootPath+"/DCIM"     | "/mnt/sdcard/DCIM"
    std::string logRoot;              // simRootPath+"/logs"     | "/mnt/sdcard/logs"
    std::string logFile;              // logRoot + "/app.log"
    bool isSimulation = false;        // mirrors BUILD_FOR_SIMULATION

    // Skip flags (default false = run the step; htc_main_app leaves all false):
    bool skipDatabase       = false;  // S2 (lean m2/m3: upload/heartbeat are DB-free)
    bool skipMediaScanner   = false;  // S3
    bool skipFactoryConfig  = false;  // HW-only sub-step of S11
    bool skipUpdateConfig   = false;  // HW-only sub-step of S11
    bool skipDaemonRegister = false;  // S10 (gated by DAEMON_ENABLE in main_app today)
};

// Shutdown context: caller-provided out-params + the flags the cascade sets.
// The cascade (which stays in main_app) sets programType / command before
// calling shutdown(); shutdown() reads them to decide the HW poweroff netif
// path and the rtsp_singleton_used gate.
struct ShutdownContext {
    int  programType      = 0;        // config->get(INI_SECTION_BOOT, INI_KEY_PTYPE, ...)
    int  command          = 0;        // CMD_* bitmask (dispatch result)
    bool rtcWorkedWell    = true;     // is_rtc_work_well
    // Callback the caller wires to its mode-local resets (daynight/LED/
    // setting-save/transport-client-null/SIGTERM-power-hold that the old
    // performCleanup owned). Invoked once, on the main thread, the first time
    // waitForSignal() catches a signal.
    std::function<void(int)> cleanupHook;
};

class ProcessLifecycle {
public:
    ProcessLifecycle();
    ~ProcessLifecycle();

    // S1-S8 common startup. Performs: env/DB/MediaScanner/EasyLogger/DayNight/
    // RGB-LED/AutoRelease. (The self-pipe + signal() are installed separately
    // by installSignalHandlers() so the caller can place dispatch between them,
    // matching today's source ordering.)
    // Returns false on a fatal step (pipe() failure) so the caller can bail.
    bool commonStartup(const StartupConfig& cfg);

    // S9-S12 common startup (post-dispatch). Performs: daemon-registration,
    // Settings load, DeviceConfig + program_type capture, SD-mount + netif
    // selection, (HW-only) factory-config + update-config. (HW-only) timezone
    // is set earlier, in commonStartup() as S4b, before S4.5 syncSystemTime.
    // Takes `command` because S11 netif selection reads CMD_MOBILE.
    // Returns false when the caller should goto main_exit (mountSDCard /
    // factory-import / update-config-applied failures, or an unsupported
    // program type on HW).
    bool commonStartupPostDispatch(const StartupConfig& cfg, int command);

    // Create the self-pipe (O_NONBLOCK both ends) + signal(SIGINT/SIGTERM,
    // signalHandler). MUST be called after commonStartup (the pipe is created
    // here, matching today's ordering: pipe created at :780, handlers at :793).
    // Returns false if the self-pipe could not be created (the caller bails).
    bool installSignalHandlers();

    // Returns !already_in_exit_flow (the run-loop predicate).
    bool keepRunning() const;

    // = waitForSignalOrTimeout(timeoutMs) today. Side effect: on first signal,
    // sets already_in_exit_flow and invokes the cleanupHook. Returns the signal
    // number or 0 on timeout.
    int  waitForSignal(int timeoutMs);

    // Caller registers its mode-specific resets (today the body of
    // performCleanup :589-632 minus the transport teardown that shutdown()
    // owns). Invoked once, on the main thread, the first time waitForSignal()
    // catches a signal.
    void setCleanupHook(std::function<void(int)> hook);

    // Set by CMD_MOBILE (:1296) / CMD_RTSP_SERVER (:1336) callers at the SAME
    // source lines — gates RtspServer::getInstance()->shutdown() in shutdown().
    void markRtspSingletonUsed();
    bool rtspSingletonUsed() const;

    // The main_exit tail: TcpEvent/Mdns stop, the rtsp_singleton_used-gated
    // RtspServer::getInstance()->shutdown() (the "before the freeze" gate,
    // :1504-1512), self-pipe close, Settings save, power-hold GPIO (HW),
    // auto_release.release(). Does NOT do the final sim _exit / HW
    // syncWithMCU+config->flush()+Misc::poweroff() — the caller runs those
    // (terminal + app-specific).
    void shutdown(ShutdownContext& ctx);

    // --- Accessors used by the cascade (refs rewritten to lc.) ---
    std::shared_ptr<DayNightSwitch> daynight() const;   // daynight_switch
    std::shared_ptr<GPIO>           rgbLed()   const;   // gpio_rgb_led
    std::shared_ptr<DeviceConfig>   config()   const;   // DeviceConfig::getInstance()
    int                             programType() const; // set in commonStartupPostDispatch (S10)
    const std::string&              settingFilePath() const; // SETTING_FILE_PATH (S9)

    ProcessLifecycle(const ProcessLifecycle&) = delete;
    ProcessLifecycle& operator=(const ProcessLifecycle&) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Sync system time from hardware at startup: /dev/rtc0 first, fall back to the
// MCU if RTC is unreadable/implausible (MCU is always-on and was last calibrated
// by syncWithMCU on the previous shutdown, far quicker than waiting for NTP).
// Returns true if a plausible time was applied. Shared by media_app's
// isRTCWorkWell() and commonStartup() so every app (incl. a directly-run
// htc_workmode_app) boots with a trusted clock. Free function, singletons only.
bool syncSystemTime();

// Terminal HW step run by the caller AFTER shutdown(), before poweroff/return.
// Reads PID/UPID/UPWD from the MCU into DeviceConfig, flushes, and pushes the
// local time into the MCU RTC. Moved verbatim from main_app.cpp (T16 Phase
// C-3) so both htc_main_app and htc_workmode_app share the same body. A free
// function — it touches only singletons (DeviceConfig + MCU), no Impl state.
bool syncWithMCU();

}  // namespace app_lifecycle
