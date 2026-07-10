// wm — 新建独立 binary（wm-app-spec）。重组 Phase-1 已验证模块（record/snap/upload/
// ntp/mcu）成 3 模式一次性任务程序：`wm -m 0`(CAPTURE_ONLY) / `-m 1`(CAPTURE+UPLOAD) /
// `-m 2`(UPLOAD_ONLY)。丢弃 crash 不收敛的 htc_workmode_app。
//
// main 骨架照搬 workmode_app.cpp（防 IMP-residue crash 的承重部分逐字复用）：
//   commonStartup → installSignalHandlers → -m 解析 → commonStartupPostDispatch
//   → setCleanupHook(stopAutoSwitch 是防 crash 关键) → 时间链 → upload mgmt 配置
//   → WmScheduler.run（内部 UploadTask 自扫 SD 上传、capture-Done 经 SlotOutputPort 唤醒）
//   → wm_exit 尾（shutdown → writebackMcu → flush → poweroff）。
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
#include <fstream>

#include "wm_scheduler.h"     // app_workmode::WmScheduler + WmMode
#include "wm_time.h"          // app_workmode::acquireTimeChain / writebackMcuTime
#include "capture_lane.h"     // app_workmode::CaptureLane (m0/m1)
#include "pir_trigger.h"      // app_workmode::SimPirTrigger / IPirTrigger (m0/m1)
#include "wm_paths.h"         // wm-local storage roots under /mnt/huntcam
#include "wm_ingest.h"        // ingestQuickSnapManifest (m2 /tmp media → desc)
#include "WorkModeRunner.h"   // CMD_* (仅为 commonStartupPostDispatch 的 netif 选择)
#include "ProcessLifecycle.h" // app_lifecycle::ProcessLifecycle + Startup/ShutdownContext
#include "StoragePaths.h"     // storage::StoragePaths (S1 path layout)
#include "DeviceConfig.h"
#include "Common.h"
#include "Logger.h"
#include "ElogInit.h"
#include "EnvManager.h"
#include "misc/Misc.h"
#include "Settings.h"
#include "StringConvert.h"    // stoi_custom / to_string_custom (uClibc-safe)
#include "app.h"              // ENV_FILE_PATHNAME, POWER_HOLD_PIN
#include "Power.h"
#include "DayNightSwitch.h"
#include "GPIO.h"             // GPIO, GPIO_VALUE, GPIO_DIRECTION, POWER_HOLD_PIN

namespace {

#ifdef BUILD_FOR_SIMULATION
std::string normalizePath(const std::string& path) {
    char resolved[PATH_MAX];
    if (realpath(path.c_str(), resolved) != nullptr) return std::string(resolved);
    return path;
}
#else
std::string trimIniValue(const std::string& value) {
    size_t start = 0;
    while (start < value.size() && (value[start] == ' ' || value[start] == '\t')) ++start;
    size_t end = value.size();
    while (end > start && (value[end - 1] == ' ' || value[end - 1] == '\t' ||
                           value[end - 1] == '\r' || value[end - 1] == '\n')) {
        --end;
    }
    return value.substr(start, end - start);
}

std::string readIniString(const std::string& path, const std::string& section, const std::string& key) {
    std::ifstream file(path);
    if (!file.is_open()) return "";

    std::string current;
    std::string line;
    while (std::getline(file, line)) {
        line = trimIniValue(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line.front() == '[' && line.back() == ']') {
            current = line.substr(1, line.size() - 2);
            continue;
        }
        if (current != section) continue;
        size_t pos = line.find('=');
        if (pos == std::string::npos) continue;
        std::string k = trimIniValue(line.substr(0, pos));
        if (k == key) return trimIniValue(line.substr(pos + 1));
    }
    return "";
}

bool isTestPid(const std::string& pid) {
    return pid.empty() ||
           pid.find("MCUTEST") == 0 ||
           pid.find("TEST_") == 0 ||
           pid.find("TEST-") == 0;
}

void selectWmHardwareConfig(std::string& note) {
    auto env = EnvManager::getInstance();
    const char* explicitConfig = std::getenv("HTC_WM_CONFIG_FILE");
    if (explicitConfig && explicitConfig[0] != '\0') {
        env->setEnv("CONFIG_FILE", explicitConfig);
        setenv("CONFIG_FILE", explicitConfig, 1);
        note = std::string("HTC_WM_CONFIG_FILE override: ") + explicitConfig;
        return;
    }

    const std::string current = env->getEnv("CONFIG_FILE", "");
    const std::string currentPid = readIniString(current, INI_SECTION_DEVICE, INI_KEY_PID);
    const std::string huntcamConfig = "/mnt/huntcam/config.ini";
    const std::string huntcamPid = readIniString(huntcamConfig, INI_SECTION_DEVICE, INI_KEY_PID);

    if (isTestPid(currentPid) && !isTestPid(huntcamPid)) {
        env->setEnv("CONFIG_FILE", huntcamConfig);
        setenv("CONFIG_FILE", huntcamConfig.c_str(), 1);
        note = std::string("CONFIG_FILE switched from ") + current + " (pid=" + currentPid +
               ") to " + huntcamConfig + " (pid=" + huntcamPid + ")";
    }
}
#endif  // BUILD_FOR_SIMULATION

}  // namespace

int main(int argc, char* argv[])
{
    // 提前声明（goto wm_exit 路径不跨非平凡初始化）。
    std::shared_ptr<DeviceConfig> config;
    int command = 0;
    app_workmode::WmMode wm_mode = app_workmode::WmMode::UploadOnly;
    bool ntpSynced = false;
    std::string ntpServer = "www.aidetcloud.com:123";  // 默认；config 加载后覆盖
    std::string configSelectionNote;

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
    auto storagePaths = std::make_shared<storage::StoragePaths>(cfg.simRootPath, "media");
    app_workmode::setStorage(storagePaths);
    cfg.dbPath    = storagePaths->dataDb();
    cfg.mediaRoot = storagePaths->mediaRoot();
    cfg.logRoot   = (envLogDir && envLogDir[0] != '\0') ? std::string(envLogDir) : (cfg.simRootPath + "/logs");
    cfg.logFile   = cfg.logRoot + "/app.log";
#else
    cfg.isSimulation = false;
    EnvManager::getInstance()->parsePrimaryEnv(ENV_FILE_PATHNAME);  // 必须在最开始
    selectWmHardwareConfig(configSelectionNote);
    auto storagePaths = std::make_shared<storage::StoragePaths>("/mnt/sdcard", "media");
    app_workmode::setStorage(storagePaths);
    cfg.dbPath    = EnvManager::getInstance()->getEnv("DB_PATH", storagePaths->dataDb());
    cfg.mediaRoot = storagePaths->mediaRoot();
    cfg.logRoot   = "/mnt/sdcard/logs";
    cfg.logFile   = cfg.logRoot + "/app.log";
#endif

    // Debug: HTC_NO_MEDIA_SCANNER=1 skips MediaScanner (the heaviest concurrent thread)
    // to bisect whether it interacts with cm==1 record and causes the wedge.
    if (const char *e = std::getenv("HTC_NO_MEDIA_SCANNER")) {
        if (e[0] == '1') cfg.skipMediaScanner = true;
    }

    // Debug: HTC_NO_MCU=1 makes MCU::getInstance() skip I2C probe and short-circuit
    // every read/write (no [MCU]read failed noise when the board is missing). Pairs
    // with the if (!iic) return guards injected in src/hardware/mcu/MCU.cpp.
    if (std::getenv("HTC_NO_MCU")) {
        Logger::log(LogLevel::INFO, "[wm] MCU disabled (HTC_NO_MCU)");
    }

    // --- -m <0|1|2|3> 解析（前移到 commonStartup 之前，使 lean flag 在 S2/S3/S4 生效）---
    {
        const bool is_mode_cmd =
            (argc >= 3) && (std::string(argv[1]) == "-m" || std::string(argv[1]) == "--mode");
        if (is_mode_cmd) {
            int m = stoi_custom(argv[2]);
            if (m < 0 || m > 3) {
                Logger::log(LogLevel::ERROR, "%s invalid -m %d (expect 0|1|2|3), power off", __func__, m);
                Power::getInstance()->requestShutdown();
                sleep(10);
                return -1;
            }
            wm_mode = static_cast<app_workmode::WmMode>(m);
        } else {
            Logger::log(LogLevel::ERROR, "%s invalid command (expect -m <0|1|2|3>), power off", __func__);
            Power::getInstance()->requestShutdown();
            sleep(10);
            return -1;
        }

        // lean 启动（m2/m3）：无卡 / 无 DB / 无重传（wm-app-spec §2.1）
        const bool lean = (wm_mode == app_workmode::WmMode::UploadOnly
                        || wm_mode == app_workmode::WmMode::Heartbeat);
        if (lean) {
            cfg.skipDatabase = true;
            cfg.skipMediaScanner = true;
            cfg.skipFactoryConfig = true;
            cfg.skipUpdateConfig = true;
    #ifndef BUILD_FOR_SIMULATION
            // lean: SD 可选，缺则 log 回退 /tmp/wm.log
            if (access("/mnt/sdcard", W_OK) != 0) {
                cfg.logRoot = "/tmp";
                cfg.logFile = "/tmp/wm.log";
            }
    #endif
        }
    }

    // --- S1-S8 + signal install ---
    app_lifecycle::ProcessLifecycle lc;
    if (!lc.commonStartup(cfg)) return -1;
    if (!lc.installSignalHandlers()) return -1;
#ifndef BUILD_FOR_SIMULATION
    // Default off: keep app.log authoritative. Async elog (commit 5da4c1a)
    // already moved fwrite off the hot threads (caller only does vsnprintf +
    // ring push; a consumer thread does fwrite + 100ms flush), so the original
    // "synchronously flush every log line" concern is gone. But the serial UART
    // is a slow device — flooding it slows the single consumer thread and raises
    // drop-oldest on app.log. Set HTC_SERIAL_LOG=1 to also emit to the serial
    // console for live bring-up debugging (parallel to app.log).
    const char* serialEnv = std::getenv("HTC_SERIAL_LOG");
    if (!(serialEnv && serialEnv[0] == '1')) {
        elog_set_terminal_output(false);
    }
#endif
    if (!configSelectionNote.empty()) {
        Logger::log(LogLevel::INFO, "[wm] %s", configSelectionNote.c_str());
    }
    Logger::log(LogLevel::INFO, "[wm] CONFIG_FILE=%s",
                EnvManager::getInstance()->getEnv("CONFIG_FILE", "").c_str());

    // Ensure wm-local output dirs exist before snap/record/upload paths are used.
    Misc::createDirectory(cfg.mediaRoot);
    Misc::createDirectory(app_workmode::wmUploadPath());

    // Debug: HTC_LOG_DEBUG=1 lowers elog filter to DEBUG on HW (default INFO)
    // so module-level DEBUG logs surface. Used for cm==1 wedge diagnosis; no-op
    // when unset. (hal/ IMP call-level rc trace is separate: HTC_HAL_TRACE=1.)
    if (const char* dbg = std::getenv("HTC_LOG_DEBUG")) {
        if (dbg[0] == '1') Logger::setLogLevel(LogLevel::DEBUG);
    }

    // --- 日志当前模式（-m 解析已在 commonStartup 前完成） ---
    Logger::log(LogLevel::INFO, "[wm] op=boot mode=%d", static_cast<int>(wm_mode));

    // command 只为 commonStartupPostDispatch 的 S11 netif 选择（wm 不跑 cascade）：
    // m0 离线；m1/m2/m3 需网络。镜像 workModeToCommand 的 netif 位。
    if (wm_mode == app_workmode::WmMode::CaptureOnly) {
        command = CMD_SNAP;
    } else if (wm_mode == app_workmode::WmMode::CaptureUpload) {
        command = CMD_SNAP | CMD_CONN_NET | CMD_DHCP | CMD_NTP;
    } else if (wm_mode == app_workmode::WmMode::UploadOnly
            || wm_mode == app_workmode::WmMode::Heartbeat) {
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

    // --- Upload lane 配置（m1/m2）：mgmt server 给新 UploadTask lazy connect 用 ---
    std::string ms_ip;
    int ms_port = 0;
    if (wm_mode != app_workmode::WmMode::CaptureOnly) {
        ms_ip   = config->get(INI_SECTION_SERVER, INI_KEY_MS_IP, "");
        ms_port = config->get(INI_SECTION_SERVER, INI_KEY_MS_PORT, 0);
        if (!ms_ip.empty() && ms_port > 0) {
            auto pid = config->get(INI_SECTION_DEVICE, INI_KEY_PID, "");
            Logger::log(LogLevel::INFO, "[wm] upload config server=%s:%d pid=%s",
                        ms_ip.c_str(), ms_port, pid.c_str());
        } else {
            Logger::log(LogLevel::WARNING, "[wm] mgmt server not configured; upload will fail (desc kept on SD)");
        }
    }

    // --- 调度器旋钮 ---
    // idle-grace 默认 2s：上传完即关机（grill 2026-06-28 拍板：wm 不做 PIR 合并窗口——
    // 后续 PIR 走完整冷启流程，换取更快关机/省电）。env HTC_WM_IDLE_GRACE_MS 可覆盖。
    int64_t idleGraceMs = 2000;
    if (const char* e = std::getenv("HTC_WM_IDLE_GRACE_MS")) { int v = std::atoi(e); if (v > 0) idleGraceMs = v; }
    int64_t uploadTimeoutMs = 60000;
    if (const char* e = std::getenv("HTC_UPLOAD_TIMEOUT_MS")) { int v = std::atoi(e); if (v > 0) uploadTimeoutMs = v; }

    // --- Capture lane + trigger（m0/m1 才有；m2/m3 无捕获）---
    std::shared_ptr<app_workmode::CaptureLane>  captureLane;
    std::shared_ptr<app_workmode::IPirTrigger>  pirTrigger;
    if (wm_mode != app_workmode::WmMode::UploadOnly
        && wm_mode != app_workmode::WmMode::Heartbeat) {
        // 算有效 cameraMode（同 capture_lane 逻辑：Settings + HTC_WM_CAMERA_MODE env 覆盖），
        // 暴露给 hal 的 buildResidentChannels 做 selective preBind：wm 只建本模式需要的通道
        // （cm==0 拍照不建 H264 CH0 → 省 ~1.84MB 连续 buf_base，根除低内存 crash；wm 永不建 preview CH1）。
        // 必须在 scheduler.run()（首次 capture 触发 sharedVideo→init→preBind）之前设。
        {
            uint8_t cm = Settings::getInstance()->cameraMode;
            if (const char* e = std::getenv("HTC_WM_CAMERA_MODE")) cm = static_cast<uint8_t>(stoi_custom(e));
            setenv("HTC_HAL_RESIDENT_MODE", to_string_custom(static_cast<int>(cm)).c_str(), 1);
            Logger::log(LogLevel::INFO, "[wm] HAL resident mode=%d (selective preBind)", (int)cm);
        }
        captureLane = std::make_shared<app_workmode::CaptureLane>();
        int pirIntervalMs = 10000;  // SimPirTrigger 默认 10s（HTC_SIM_PIR_INTERVAL_MS 可调）
        if (const char* e = std::getenv("HTC_SIM_PIR_INTERVAL_MS")) { int v = std::atoi(e); if (v > 0) pirIntervalMs = v; }
        // 0=无限；HTC_SIM_PIR_COUNT=N 触发 N 次后停（建模「动物离开」，测多 trigger robustness）。
        int pirCount = 0;
        if (const char* e = std::getenv("HTC_SIM_PIR_COUNT")) { int v = std::atoi(e); if (v >= 0) pirCount = v; }
        pirTrigger = std::make_shared<app_workmode::SimPirTrigger>(pirIntervalMs, pirCount);
    }

    // --- m2 ingest：读 /tmp/media/info.json → 造 desc 到 /tmp 扫描目录 ---
    if (wm_mode == app_workmode::WmMode::UploadOnly) {
        app_workmode::ingestQuickSnapManifest("/tmp");
    }

    // --- 主循环（长驻直到关机）---
    app_workmode::WmScheduler scheduler(lc, wm_mode, captureLane, pirTrigger,
                                        ms_ip, ms_port, idleGraceMs, uploadTimeoutMs);
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
    // UploadTask（新 type=2）的生命周期由 scheduler 全权管理：run() 内 idle-grace
    // 自然 drain 完才返回；signal/upload-timeout 关机时 UploadTask::stop() 断阻塞 I/O
    // 并 join 线程（在 scheduler.run() 返回前完成）。故此处无需单独 flush+stop
    // （未传完的 desc F_UploadedTag 保持 0，下次 -m 2 重传）。
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
