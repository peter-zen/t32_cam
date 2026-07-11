// quickSnap — boot-phase first program: snap (<=8M) then fork+execv downstream.
// Replaces htc_media_app as the boot entry point. Reads quicksnap.json (not
// setting.json). Uses fork+execv (no shell / system_call / popen).
// Spec: doc/knowledge/specs/quicksnap-app-spec.md
// Plan: artifacts/T18-planner-full.md

#include "ImageSnap.h"
#include "HalProvider.h"
#include "Logger.h"
#include "Common.h"
#include "GPIO.h"           // include BEFORE app.h: app.h wraps it in extern "C",
                            // and GPIO.h pulls <thread>/<mutex>/<memory> which
                            // must be parsed with C++ linkage. This pre-include
                            // makes the app.h re-include a no-op (include guard).
#include "app.h"
#include "workmode/WorkMode.h"
#include "ProcessLifecycle.h"
#include "time/timezone/Timezone.h"
#include "utils/string/StringConvert.h"
#include "Misc.h"
#include "ElogInit.h"

#include <json/json.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <cstring>
#include <cerrno>

using namespace media;

// ============================================================================
// QuickSnapConfig — 5 fields from quicksnap.json (the single source of truth)
// ============================================================================
struct QuickSnapConfig {
    uint8_t cameraMode   = 0;
    int     force_upload = 0;
    uint8_t burstNumber  = 1;
    uint8_t stillSize    = SNAP_IMG_SIZE_4M;
    std::string timezone;
    std::string network  = "4g";   // T21: "4g" | "wifi"，默认 4G（产品现状）

    static QuickSnapConfig loadOrDefault(const std::string& path);
    bool save(const std::string& path) const;
};

QuickSnapConfig QuickSnapConfig::loadOrDefault(const std::string& path)
{
    QuickSnapConfig cfg;

    std::ifstream file(path);
    if (!file.is_open()) {
        Logger::log(LogLevel::INFO, "quicksnap.json not found at %s, using defaults", path.c_str());
        return cfg;
    }

    Json::Value root;
    Json::Reader reader;
    if (!reader.parse(file, root)) {
        Logger::log(LogLevel::ERROR, "Failed to parse quicksnap.json: %s",
                    reader.getFormattedErrorMessages().c_str());
        file.close();
        return cfg;  // return defaults on parse failure
    }
    file.close();

    if (root.isMember("cameraMode")   && root["cameraMode"].isUInt())   cfg.cameraMode   = root["cameraMode"].asUInt();
    if (root.isMember("force_upload") && root["force_upload"].isInt())  cfg.force_upload = root["force_upload"].asInt();
    if (root.isMember("burstNumber")  && root["burstNumber"].isUInt())  cfg.burstNumber  = root["burstNumber"].asUInt();
    if (root.isMember("stillSize")    && root["stillSize"].isUInt())    cfg.stillSize    = root["stillSize"].asUInt();
    if (root.isMember("timezone")     && root["timezone"].isString())   cfg.timezone     = root["timezone"].asString();
    if (root.isMember("network")     && root["network"].isString())    cfg.network      = root["network"].asString();
    // normalize：未知值降级 "4g" + 警告（只认小写 4g/wifi）
    if (cfg.network != "4g" && cfg.network != "wifi") {
        Logger::log(LogLevel::WARNING,
                    "quicksnap.json network='%s' unknown (expected 4g|wifi), "
                    "falling back to 4g", cfg.network.c_str());
        cfg.network = "4g";
    }

    // --- safety: burst 0 means no photos ---
    if (cfg.burstNumber == 0) {
        cfg.burstNumber = 1;
    }

    Logger::log(LogLevel::INFO,
                "Loaded quicksnap.json: cameraMode=%d force_upload=%d burstNumber=%d "
                "stillSize=%d timezone=%s network=%s",
                cfg.cameraMode, cfg.force_upload, cfg.burstNumber, cfg.stillSize,
                cfg.timezone.c_str(), cfg.network.c_str());
    return cfg;
}

bool QuickSnapConfig::save(const std::string& path) const
{
    Json::Value root;
    root["cameraMode"]   = cameraMode;
    root["force_upload"] = force_upload;
    root["burstNumber"]  = burstNumber;
    root["stillSize"]    = stillSize;
    root["timezone"]     = timezone;
    // T21: save 全量重建 root → 不补塞 network 会在 force_upload 写回时丢字段。
    // normalize 直接改了 cfg.network（误拼值被降级后写回也降级——force_upload 写回频次极低）。
    root["network"]      = network;

    std::ofstream file(path);
    if (!file.is_open()) {
        Logger::log(LogLevel::ERROR, "Failed to open quicksnap.json for writing: %s", path.c_str());
        return false;
    }

    Json::StreamWriterBuilder writer;
    std::unique_ptr<Json::StreamWriter> sw(writer.newStreamWriter());
    sw->write(root, &file);
    file.close();
    return true;
}

// ============================================================================
// spawn — fork+execv without shell (no system/system_call/popen)
// ============================================================================
static int spawn(const char* path, char* const argv[])
{
    pid_t pid = fork();
    if (pid < 0) {
        Logger::log(LogLevel::ERROR, "fork() failed: %s", strerror(errno));
        return -1;
    }
    if (pid == 0) {
        // Child: close inherited FDs (except stdin/stdout/stderr) before execv.
        // Old media_app used system() -> sh which implicitly closed FDs;
        // fork+execv bypasses sh so we must clean up ourselves.
        long maxfd = sysconf(_SC_OPEN_MAX);
        for (int fd = 3; fd < maxfd; fd++) {
            close(fd);
        }
        execv(path, argv);
        Logger::log(LogLevel::ERROR, "execv(%s) failed: %s", path, strerror(errno));
        _exit(127);
    }
    // Parent: do not wait — child becomes orphan and init adopts it.
    Logger::log(LogLevel::INFO, "spawned child pid=%d for %s", pid, path);
    return 0;
}

#ifndef BUILD_FOR_SIMULATION
// ============================================================================
// spawnAndWait — fork+execv + waitpid (synchronous). Returns child exit code on
// normal exit, or -1 on fork/exec/wait failure or signal death.
// ============================================================================
static int spawnAndWait(const char* path, char* const argv[])
{
    pid_t pid = fork();
    if (pid < 0) {
        Logger::log(LogLevel::ERROR, "spawnAndWait: fork() failed: %s", strerror(errno));
        return -1;
    }
    if (pid == 0) {
        // Child: same FD cleanup as spawn() (avoid leaking parent FDs to child)
        long maxfd = sysconf(_SC_OPEN_MAX);
        for (int fd = 3; fd < maxfd; fd++) {
            close(fd);
        }
        execv(path, argv);
        Logger::log(LogLevel::ERROR, "spawnAndWait: execv(%s) failed: %s", path, strerror(errno));
        _exit(127);
    }
    // Parent: wait synchronously
    int status = 0;
    pid_t w = waitpid(pid, &status, 0);
    if (w < 0) {
        Logger::log(LogLevel::ERROR, "spawnAndWait: waitpid(pid=%d) failed: %s",
                    (int)pid, strerror(errno));
        return -1;
    }
    if (WIFSIGNALED(status)) {
        Logger::log(LogLevel::ERROR, "spawnAndWait: %s (pid=%d) killed by signal %d",
                    path, (int)pid, WTERMSIG(status));
        return -1;  // signal death = failure (do not spawn wm)
    }
    int code = WEXITSTATUS(status);
    Logger::log(LogLevel::INFO, "spawnAndWait: %s (pid=%d) exited code=%d",
                path, (int)pid, code);
    return code;
}
#endif  // BUILD_FOR_SIMULATION

// ============================================================================
// prepareNetwork — synchronous uplink bring-up via htc_net_app.
// network: "4g" -> --type usb --usb-bringup; "wifi" -> --type wifi (MCU creds).
// Returns true if uplink is up (or bypassed on sim); false -> abort (no spawn wm).
// ============================================================================
static bool prepareNetwork(const std::string& network, const std::string& execDir)
{
#ifdef BUILD_FOR_SIMULATION
    // SIM: no real 4G dongle / WiFi HW; htc_net_app exits non-zero on sim.
    // Bypass to let sim smoke reach spawn wm. network 值不影响 sim 行为。
    Logger::log(LogLevel::INFO,
                "SIM: bypass network prepare (network=%s, no HW in sim)",
                network.c_str());
    return true;
#else
    std::string netBin = execDir.empty() ? "htc_net_app" : (execDir + "/htc_net_app");
    const char* netPath = netBin.c_str();

    if (network == "wifi") {
        // WiFi: 不带凭据，htc_net_app 从 MCU 寄存器读 UPID/UPWD
        // (net_app.cpp:212-217: --ssid 空 -> readUPID/readUPWD)。
        Logger::log(LogLevel::INFO,
                    "preparing WiFi: spawnAndWait %s --type wifi (creds from MCU)",
                    netBin.c_str());
        char* const netArgv[] = {
            const_cast<char*>("htc_net_app"),
            const_cast<char*>("--type"),
            const_cast<char*>("wifi"),
            nullptr
        };
        int rc = spawnAndWait(netPath, netArgv);
        if (rc != 0) {
            // Exit code meaning (net_app_logic.h:26-33):
            //   3 connect (含 No SSID — MCU 无凭据) / 4 DHCP / 5 MCU gated / 6 arg
            Logger::log(LogLevel::ERROR,
                        "WiFi prepare failed (exit=%d) — abort spawn wm. "
                        "[3=connect/NoSSID 4=dhcp 5=mcu-gated 6=arg]", rc);
            return false;
        }
        Logger::log(LogLevel::INFO, "WiFi prepare ok (exit=0), proceeding to spawn wm");
        return true;
    } else {
        // 4G (默认): --type usb --usb-bringup（沿用 T20）
        Logger::log(LogLevel::INFO,
                    "preparing 4G: spawnAndWait %s --type usb --usb-bringup",
                    netBin.c_str());
        char* const netArgv[] = {
            const_cast<char*>("htc_net_app"),
            const_cast<char*>("--type"),
            const_cast<char*>("usb"),
            const_cast<char*>("--usb-bringup"),
            nullptr
        };
        int rc = spawnAndWait(netPath, netArgv);
        if (rc != 0) {
            // 2 driver / 3 connect / 4 DHCP / 6 arg（5 = MCU gated 是 wifi-only，usb 路径不出现）
            Logger::log(LogLevel::ERROR,
                        "4G prepare failed (exit=%d) — abort spawn wm. "
                        "[2=driver 3=connect 4=dhcp 6=arg]", rc);
            return false;
        }
        Logger::log(LogLevel::INFO, "4G prepare ok (exit=0), proceeding to spawn wm");
        return true;
    }
#endif
}

// ============================================================================
// getCurrentTimeFormatted — "YYYYMMDD_HHMMSS" for directory / filename
// ============================================================================
static std::string getCurrentTimeFormatted()
{
    time_t now = time(nullptr);
    struct tm* time_info = localtime(&now);

    std::stringstream ss;
    ss << std::put_time(time_info, "%Y%m%d_%H%M%S");
    return ss.str();
}

// ============================================================================
// doSnap — ImageSnap (<=8M HW scaler)。媒体落 <workDir>/；工作目录经 outDirPath 透出
// 给 main → spawn `wm -m 2 -d <workDir>`。不再写 info.json（wm 自扫目录建 desc）。
// ============================================================================
static int doSnap(const QuickSnapConfig& config, bool& impInitialized,
                  std::string& outDirPath)
{
    impInitialized = false;
    // --- create /tmp/media/ ---
    auto baseDir = std::string(QUICK_SNAP_DIR);
    if (!Misc::createDirectory(baseDir, 0777)) {
        Logger::log(LogLevel::ERROR, "Failed to create directory: %s", baseDir.c_str());
        return -1;
    }

    // --- timestamp subdirectory ---
    // 始终用当前系统时间命名（rtcOk==false 时时间不可信，但仍用）：命名规则统一为
    // YYYYMMDD_HHMMSS，wm 的 isTimestampDir 兜底/SD-resume 才认得；否则 rtc-fail 目录会被
    // 兜底扫描/续传略过（T22 SD 落卡后 pic 会 stranding 在 SD）。时间不准接受——刻意不 clamp，
    // 以免污染 wm syncWithMCU 的 ≥2026 可信门（见 quicksnap-app-spec §5.1）。
    std::string timeStr = getCurrentTimeFormatted();
    std::string dirPath = baseDir + timeStr;

    if (!Misc::createDirectory(dirPath, 0777)) {
        Logger::log(LogLevel::ERROR, "Failed to create directory: %s", dirPath.c_str());
        return -1;
    }
    outDirPath = dirPath;   // 透出工作目录给 main → spawn wm -d

    // --- generate filenames ---
    std::vector<std::string> fileNames;
    for (int i = 0; i < config.burstNumber; i++) {
        fileNames.push_back(dirPath + "/" + timeStr + "_" + to_string_custom(i + 1) + ".JPG");
    }

    // --- stillSize clamp (memory-only, never written back to json) ---
    // 钳到 4M（2560×1440 = sensor-native，两维均不超 → isLargeImage=false → HW encoder 路径，
    // 只需 JPEG CH12）。>4M（8M 3840×2160…）任一维超 sensor-native → 走 strip 路径，
    // strip 需 sensor framesource CH0（ImageSnap.cpp:471 EnableChn(0)）；但 photo-only HAL
    // （residentMode=0）只建 CH12、不建 CH0（省 ~1.84MB）→ snap 必败。故 quickSnap 精简 HAL
    // 结构上不支持 >4M，clamp 从源头杜绝 strip。见 quicksnap-app-spec §5。
    int snapSizeIndex = config.stillSize;
    if (snapSizeIndex > SNAP_IMG_SIZE_4M || snapSizeIndex >= SNAP_IMG_SIZE_MAX) {
        snapSizeIndex = SNAP_IMG_SIZE_4M;
    }

    Logger::log(LogLevel::INFO, "Snap image size: %d x %d (stillSize=%d capped=%d)",
                SnapImgSize[snapSizeIndex].width, SnapImgSize[snapSizeIndex].height,
                config.stillSize, snapSizeIndex);

    // --- snap with ImageSnap in inner scope ---
    // Inner scope ensures ~ImageSnap (stream stop + DestroyChn) runs BEFORE
    // resetSharedVideo() (IMP_System_Exit), as required by spec section 4.1.
    {
        auto snapParam = ImageSnapParams();
        snapParam.setImageSize(SnapImgSize[snapSizeIndex].width, SnapImgSize[snapSizeIndex].height);
        snapParam.setAEReadyWait(true);
        snapParam.setThumbnailEnabled(false);  // CH2 缩略图 Layer 2 关（配合 HAL withThumb=false，两层都不建 CH2）
        auto imageSnap = std::make_shared<ImageSnap>(snapParam);
        impInitialized = true;  // ImageSnap constructor already initialized IMP
        if (!imageSnap->snap(fileNames)) {
            Logger::log(LogLevel::ERROR, "Failed to snap images");
            return -1;
        }
    } // ~ImageSnap runs here: stream stop + DestroyChn

    // --- capture timestamp ---
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) != -1) {
        Logger::log(LogLevel::INFO, "capture done at %ld ms", ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
    }

    // 媒体已落 outDirPath(<workDir>/)；wm 自扫该目录建 desc，无需 info.json manifest。
    return 0;
}

// ============================================================================
// main — boot sequence per spec section 7 (12 steps)
// ============================================================================
int main(int /*argc*/, char* /*argv*/[])
{
    // Init EasyLogger early so all subsequent log output is visible on device
    elog_init_default();

    bool rtcOk = false;
    workingMode gpioMode = workingMode::WORKING_MODE_MAX;

    // Step 1: record boot timestamp
    struct timespec ts0;
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts0) != -1) {
        Logger::log(LogLevel::INFO, "quickSnap main entry at %ld ms",
                    ts0.tv_sec * 1000 + ts0.tv_nsec / 1000000);
    }

    // Step 2: POWER_HOLD_PIN OUTPUT HIGH (scope forces unexport before mode read)
    {
        auto gpio_power_hold = GPIO(POWER_HOLD_PIN);
        if (!gpio_power_hold.exportGPIO() || !gpio_power_hold.setDirection(GPIO_DIRECTION::OUTPUT)
            || !gpio_power_hold.setValue(GPIO_VALUE::HIGH)) {
            Logger::log(LogLevel::ERROR, "Failed to set POWER_HOLD_PIN");
            // POWER_HOLD_PIN failed — cannot maintain power, exit immediately.
            return 1;
        }
        Logger::log(LogLevel::INFO, "POWER_HOLD_PIN set HIGH");
    }

    // HAL 常驻通道配置在 Step 8（willSnap）里经 HalProvider::start() 声明，不再用 env。

    // Step 3: read GPIO mode
#ifdef BUILD_FOR_SIMULATION
    {
        const char* simMode = getenv("HTC_SIM_WORK_MODE");
        int m = simMode ? atoi(simMode) : 1; // default SNAP_UPLOAD
        if (m >= workingMode::WORKING_MODE_SNAP_ONLY && m < workingMode::WORKING_MODE_MAX) {
            gpioMode = static_cast<workingMode>(m);
        } else {
            gpioMode = workingMode::WORKING_MODE_SNAP_UPLOAD;
        }
        Logger::log(LogLevel::INFO, "SIM: HTC_SIM_WORK_MODE=%d -> gpioMode=%d", m, (int)gpioMode);
    }
#else
#if !MCU_EXIST
    WorkMode::setWorkingModePins(WORKING_MODE_CHECK_PIN_0, WORKING_MODE_CHECK_PIN_1);
#endif
    gpioMode = WorkMode::getWorkingMode();
    Logger::log(LogLevel::INFO, "GPIO working mode = %d", (int)gpioMode);
#endif

    // Step 4: read quicksnap.json
#ifdef BUILD_FOR_SIMULATION
    std::string configPath = "./res/quicksnap.json";
#else
    std::string configPath = "/config/htc/quicksnap.json";
#endif
    auto config = QuickSnapConfig::loadOrDefault(configPath);

    // Step 5: set timezone
    if (!config.timezone.empty()) {
        Logger::log(LogLevel::INFO, "Set timezone to %s", config.timezone.c_str());
        Timezone::setTimezone(config.timezone);
    }

    // Step 6: time sync (RTC priority, MCU fallback)
    rtcOk = app_lifecycle::syncSystemTime();
    Logger::log(LogLevel::INFO, "syncSystemTime -> %s", rtcOk ? "ok" : "fail");

    // Step 7: force_upload check + mode router
    bool forceConsumed = false;
    workingMode effectiveMode = gpioMode;
    bool willSnap = false;

    if (config.force_upload == 1) {
        effectiveMode = workingMode::WORKING_MODE_SNAP_UPLOAD; // -> wm -m 2
        forceConsumed = true;
        willSnap = false;
        Logger::log(LogLevel::INFO, "force_upload=1 -> effective_mode=SNAP_UPLOAD, skip snap");
    } else {
        switch (gpioMode) {
            case workingMode::WORKING_MODE_SNAP_ONLY:
                willSnap = (config.cameraMode != 2);
                Logger::log(LogLevel::INFO, "mode=SNAP_ONLY cameraMode=%d will_snap=%d",
                            config.cameraMode, willSnap);
                break;
            case workingMode::WORKING_MODE_SNAP_UPLOAD:
                willSnap = (config.cameraMode != 2);
                Logger::log(LogLevel::INFO, "mode=SNAP_UPLOAD cameraMode=%d will_snap=%d",
                            config.cameraMode, willSnap);
                break;
            case workingMode::WORKING_MODE_UPLOAD_ONLY:
                willSnap = false;
                Logger::log(LogLevel::INFO, "mode=UPLOAD_ONLY (heartbeat), spawn wm -m 3");
                break;
            case workingMode::WORKING_MODE_TEST_ONLY:
                willSnap = false;
                Logger::log(LogLevel::INFO, "mode=TEST_ONLY (user), spawn um");
                break;
            default:
                willSnap = false;
                Logger::log(LogLevel::INFO, "mode=UNKNOWN(%d), no snap", (int)gpioMode);
                break;
        }
    }

    // Step 8: snap (work modes only, with cameraMode != 2 gate)
    bool impInitialized = false;
    std::string snapDir;   // doSnap 透出的工作目录（→ spawn wm -d；force_upload/willSnap=false 时为空）
    if (willSnap) {
        // 声明 HAL 常驻通道：photo-only(residentMode=0 → 只建 JPEG CH12，不建 H264 CH0)，
        // 无缩略图(withThumb=false → 不建 group2/CH14，省内存)。取代旧 HTC_HAL_RESIDENT_MODE env。
        // 须在 doSnap（其内 ImageSnap 构造 → 首次 sharedVideo lazy init）之前。
        hal::HalProvider::start(hal::HalVideoConfig{/*residentMode=*/0, /*withThumb=*/false});
        if (doSnap(config, impInitialized, snapDir) < 0) {
            Logger::log(LogLevel::ERROR, "doSnap failed");
            // Continue to spawn downstream anyway (best effort).
            // impInitialized may still be true if ImageSnap was constructed
            // before the failure (IMP was initialized).
        }
    }

    // Step 9: IMP release — gated on whether ImageSnap was ever constructed
    // (i.e. IMP was initialized), not on snap success.
    if (impInitialized) {
        // doSnap() already destroyed ImageSnap (inner scope), so stream/channel
        // are released. Now drop the HalProvider singleton -> ~IngenicVideo ->
        // IMP_System_Exit. Order per spec section 4.1.
        Logger::log(LogLevel::INFO, "resetting shared video singleton (IMP_System_Exit)");
        hal::HalProvider::resetSharedVideo();
    }

    // Step 10: force_upload read-modify-write back to 0
    if (forceConsumed) {
        config.force_upload = 0;
        if (config.save(configPath)) {
            Logger::log(LogLevel::INFO, "force_upload written back to 0");
        } else {
            Logger::log(LogLevel::ERROR, "failed to write back force_upload=0");
        }
    }

    // Step 11: spawn downstream (after IMP release to minimise fork RSS)
    std::string execDir = Misc::getExecutablePath();
    std::string wmBin = execDir.empty() ? "wm" : (execDir + "/wm");
    std::string umBin = execDir.empty() ? "um" : (execDir + "/um");
    switch (effectiveMode) {
    case workingMode::WORKING_MODE_SNAP_ONLY:
        Logger::log(LogLevel::INFO, "SNAP_ONLY -> exit (no spawn)");
        return 0;

    case workingMode::WORKING_MODE_SNAP_UPLOAD: {
        // Synchronous network prepare before spawn wm (uplink needed for upload).
        if (!prepareNetwork(config.network, execDir)) {
            Logger::log(LogLevel::ERROR,
                        "SNAP_UPLOAD: network prepare failed (network=%s) -> abort, no spawn wm",
                        config.network.c_str());
            return 1;
        }
        // -d 仅在拍了照（snapDir 非空，含 rtc-fail 时仍为时间戳目录）时带上；force_upload
        // （willSnap=false → snapDir 空）不带，wm 靠兜底扫描补传滞留。
        Logger::log(LogLevel::INFO, "spawning wm -m 2%s%s",
                    snapDir.empty() ? "" : " -d ", snapDir.empty() ? "" : snapDir.c_str());
        const char* wmPath = wmBin.c_str();
        std::vector<char*> wmArgv;
        wmArgv.push_back(const_cast<char*>("wm"));
        wmArgv.push_back(const_cast<char*>("-m"));
        wmArgv.push_back(const_cast<char*>("2"));
        if (!snapDir.empty()) {
            wmArgv.push_back(const_cast<char*>("-d"));
            wmArgv.push_back(const_cast<char*>(snapDir.c_str()));
        }
        wmArgv.push_back(nullptr);
        spawn(wmPath, wmArgv.data());
        return 0;
    }

    case workingMode::WORKING_MODE_UPLOAD_ONLY: {
        // Synchronous network prepare before spawn wm (heartbeat also needs uplink).
        if (!prepareNetwork(config.network, execDir)) {
            Logger::log(LogLevel::ERROR,
                        "UPLOAD_ONLY: network prepare failed (network=%s) -> abort, no spawn wm",
                        config.network.c_str());
            return 1;
        }
        Logger::log(LogLevel::INFO, "spawning wm -m 3 (heartbeat)");
        const char* wmPath = wmBin.c_str();
        char* const wmArgv[] = {
            const_cast<char*>("wm"),
            const_cast<char*>("-m"),
            const_cast<char*>("3"),
            nullptr
        };
        spawn(wmPath, wmArgv);
        return 0;
    }

    case workingMode::WORKING_MODE_TEST_ONLY: {
        Logger::log(LogLevel::INFO, "spawning um");
        const char* umPath = umBin.c_str();
        char* const umArgv[] = {
            const_cast<char*>("um"),
            nullptr
        };
        spawn(umPath, umArgv);
        return 0;
    }

    default:
        Logger::log(LogLevel::INFO, "unknown effective mode %d -> exit", (int)effectiveMode);
        return 0;
    }
}
