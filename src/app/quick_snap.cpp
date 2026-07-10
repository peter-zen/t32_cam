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

    // --- safety: burst 0 means no photos ---
    if (cfg.burstNumber == 0) {
        cfg.burstNumber = 1;
    }

    Logger::log(LogLevel::INFO,
                "Loaded quicksnap.json: cameraMode=%d force_upload=%d burstNumber=%d stillSize=%d timezone=%s",
                cfg.cameraMode, cfg.force_upload, cfg.burstNumber, cfg.stillSize, cfg.timezone.c_str());
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
// doSnap — ImageSnap (<=8M HW scaler) + info.json manifest
// ============================================================================
static int doSnap(const QuickSnapConfig& config, bool rtcOk, bool& impInitialized)
{
    impInitialized = false;
    // --- create /tmp/media/ ---
    auto baseDir = std::string(QUICK_SNAP_DIR);
    if (!Misc::createDirectory(baseDir, 0777)) {
        Logger::log(LogLevel::ERROR, "Failed to create directory: %s", baseDir.c_str());
        return -1;
    }

    // --- timestamp subdirectory ---
    std::string timeStr = getCurrentTimeFormatted();
    std::string dirPath;
    if (rtcOk) {
        dirPath = baseDir + timeStr;
    } else {
        dirPath = baseDir + "pic";
    }

    if (!Misc::createDirectory(dirPath, 0777)) {
        Logger::log(LogLevel::ERROR, "Failed to create directory: %s", dirPath.c_str());
        return -1;
    }

    // --- generate filenames ---
    std::vector<std::string> fileNames;
    for (int i = 0; i < config.burstNumber; i++) {
        if (rtcOk) {
            fileNames.push_back(dirPath + "/" + timeStr + "_" + to_string_custom(i + 1) + ".JPG");
        } else {
            fileNames.push_back(dirPath + "/" + to_string_custom(i + 1) + ".JPG");
        }
    }

    // --- stillSize clamp (memory-only, never written back to json) ---
    int snapSizeIndex = config.stillSize;
    if (snapSizeIndex > SNAP_IMG_SIZE_8M) {
        snapSizeIndex = SNAP_IMG_SIZE_8M;
    }
    if (snapSizeIndex >= SNAP_IMG_SIZE_MAX) {
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

    // --- write info.json manifest ---
    Json::Value root;
    Json::Value imageArray(Json::arrayValue);
    for (const auto& fileName : fileNames) {
        imageArray.append(std::string(fileName.substr(fileName.find_last_of("/") + 1)));
    }
    if (rtcOk) {
        root["files"] = imageArray;
        root["dir"] = timeStr;
    } else {
        root["files"] = imageArray;
        root["dir"] = "pic";
    }

    std::string jsonFilePath = std::string(QUICK_SNAP_DIR) + "info.json";
    std::ofstream jsonFile(jsonFilePath);
    if (!jsonFile.is_open()) {
        Logger::log(LogLevel::ERROR, "Failed to open file: %s", jsonFilePath.c_str());
        return -1;
    }

    Json::StreamWriterBuilder writerBuilder;
    std::unique_ptr<Json::StreamWriter> jsonWriter(writerBuilder.newStreamWriter());
    jsonWriter->write(root, &jsonFile);
    jsonFile.close();

    Logger::log(LogLevel::INFO, "info.json written: %s (%zu files)", jsonFilePath.c_str(), fileNames.size());
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

    // photo-only: selective preBind, skip H.264 encoder channels to avoid CMA
    // exhaustion crash (see IngenicVideo.cpp:1122-1127).
    setenv("HTC_HAL_RESIDENT_MODE", "0", 1);

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
    if (willSnap) {
        if (doSnap(config, rtcOk, impInitialized) < 0) {
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
        Logger::log(LogLevel::INFO, "spawning wm -m 2");
        const char* wmPath = wmBin.c_str();
        char* const wmArgv[] = {
            const_cast<char*>("wm"),
            const_cast<char*>("-m"),
            const_cast<char*>("2"),
            nullptr
        };
        spawn(wmPath, wmArgv);
        return 0;
    }

    case workingMode::WORKING_MODE_UPLOAD_ONLY: {
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
