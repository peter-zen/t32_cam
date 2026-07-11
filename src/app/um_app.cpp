// um — 新建独立 binary（um-app-spec）。长驻交互服务程序：组合已验证的库
// (rtsp/http/mdns/daynight/mcu) 成稳定的「长驻服务」，与 wm（一次性任务）对应。
// 架构 = server-lifecycle（bring-up / idle-wait / teardown），非 wm task-scheduler。
// 关机 = idle-timeout 自关机（无 RTSP 客户端 + 无 HTTP 请求 持续 T → requestShutdown）。
//
// main 骨架照搬 wm_app.cpp（ProcessLifecycle 三阶段 + 时间链 + poweroff 尾）；
// bring-up/teardown 顺序照搬 WorkModeRunner CMD_MOBILE（:580-700）；
// idle-wait loop 照搬 event_loop（:49-87，树里唯一的 keepRunning idiom）。
// IMP 懒初始化（RtspServer 单例），进程内永不 IMP_System_Exit——um 不重做 main_app
// 的 IMP boot，RtspServer::getInstance() 构造即拉起整个 sensor→encoder→streaming 栈。
//
// Slice 2a：完整 server-lifecycle + idle-timeout→poweroff 通路。RTSP/HTTP 活跃信号
// （idle 续命）= Slice 2b/2c；本步 UmIdleCore 无活跃输入 → idle-timeout 必触发，
// 正好验证 spec §11.1「idle-timeout teardown 与 ctrl-c 同条路径」。
//
// Dual-platform：BUILD_FOR_SIMULATION=ON (build_sim) + T32 toolchain (build)。
// uClibc-safe：用 to_string_custom，无 std::to_string。

#include <iostream>
#include <string>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <limits.h>
#include <csignal>
#include <memory>

#include "um_idle.h"          // app_usermode::UmIdleCore + UmIdleConfig
#include "wm_time.h"          // app_workmode::acquireTimeChain / writebackMcuTime
#include "WorkModeRunner.h"   // CMD_MOBILE (仅为 commonStartupPostDispatch 的 netif 选择)
#include "ProcessLifecycle.h" // app_lifecycle::ProcessLifecycle + Startup/ShutdownContext
#include "StoragePaths.h"     // storage::StoragePaths (S1 path layout)
#include "DeviceConfig.h"
#include "ProductConfig.h"
#include "Common.h"
#include "Logger.h"
#include "ElogInit.h"
#include "EnvManager.h"
#include "misc/Misc.h"
#include "Settings.h"
#include "StringConvert.h"    // to_string_custom (uClibc-safe)
#include "app.h"              // POWER_HOLD_PIN
#include "Power.h"
#include "DayNightSwitch.h"
#include "GPIO.h"             // GPIO, GPIO_VALUE, GPIO_DIRECTION, POWER_HOLD_PIN
#include "RtspServer.h"       // media::RtspServer
#include "http_server.h"      // http_server_init/start/stop/deinit (C API)
#include "MdnsService.h"      // service::MdnsService
#include "MdnsParams.h"       // service::buildMdnsParams / isMdnsEnabled
#include "TcpEventService.h"  // service::TcpEventService
#include "CameraServiceFactory.h"  // service::CameraServiceFactory (prewarm snap channel)

namespace {

#ifdef BUILD_FOR_SIMULATION
std::string normalizePath(const std::string& path) {
    char resolved[PATH_MAX];
    if (realpath(path.c_str(), resolved) != nullptr) return std::string(resolved);
    return path;
}
#endif

// idle-wait 滴答时钟：单调时钟（不受 NTP/RTC 调整影响），喂给 UmIdleCore::tick。
int64_t steadyNowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// 配置端口读取 + 范围兜底（等价 WorkModeRunner.cpp:82 的 static getConfiguredPort）。
uint16_t getConfiguredPort(const std::shared_ptr<DeviceConfig>& config,
                           const std::string& section,
                           const std::string& key,
                           uint16_t default_port) {
    int configured_port = config->get(section, key, static_cast<int>(default_port));
    if (configured_port <= 0 || configured_port > 65535) return default_port;
    return static_cast<uint16_t>(configured_port);
}

// um CLI：无模式，只 --no-* / --force-day 开关（spec §1/§7）。
struct UmCliFlags {
    bool noRtsp   = false;
    bool noHttp   = false;
    bool noMdns   = false;
    bool noAudio  = false;
    bool forceDay = false;
};

UmCliFlags parseFlags(int argc, char* argv[]) {
    UmCliFlags f;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if      (a == "--no-rtsp")   f.noRtsp = true;
        else if (a == "--no-http")   f.noHttp = true;
        else if (a == "--no-mdns")   f.noMdns = true;
        else if (a == "--no-audio")  f.noAudio = true;
        else if (a == "--force-day") f.forceDay = true;
        else Logger::log(LogLevel::WARNING, "[um] unknown arg: %s", a.c_str());
    }
    return f;
}

}  // namespace

int main(int argc, char* argv[])
{
    // 提前声明（goto um_exit 路径不跨非平凡初始化）。
    std::shared_ptr<DeviceConfig> config;
    int command = 0;
    bool ntpSynced = false;
    std::string ntpServer = "www.aidetcloud.com:123";  // 默认；config 加载后覆盖

    // --- S1 path inputs → StartupConfig (sim vs HW) ---
    app_lifecycle::StartupConfig cfg;
    std::shared_ptr<storage::StoragePaths> storagePaths;  // S3: 注入 CameraService
#ifdef BUILD_FOR_SIMULATION
    cfg.isSimulation = true;
    std::string exePath = Misc::getExecutablePath();
    cfg.projectRootPath = normalizePath(exePath + "/../..");
    std::string defaultSimRootPath = normalizePath(cfg.projectRootPath + "/sim_sdcard_runtime");
    const char* envSimRoot = std::getenv("SIM_SD_ROOT");
    cfg.simRootPath = (envSimRoot && envSimRoot[0] != '\0') ? normalizePath(envSimRoot) : defaultSimRootPath;
    const char* envLogDir = std::getenv("SIM_LOG_DIR");
    storagePaths = std::make_shared<storage::StoragePaths>(cfg.simRootPath, "DCIM");
    cfg.dbPath    = storagePaths->dataDb();
    cfg.mediaRoot = storagePaths->mediaRoot();
    cfg.logRoot   = (envLogDir && envLogDir[0] != '\0') ? std::string(envLogDir) : (cfg.simRootPath + "/logs");
    cfg.logFile   = cfg.logRoot + "/app.log";
#else
    cfg.isSimulation = false;
    storagePaths = std::make_shared<storage::StoragePaths>("/mnt/sdcard", "DCIM");
    cfg.dbPath    = EnvManager::getInstance()->getEnv("DB_PATH", storagePaths->dataDb());
    cfg.mediaRoot = storagePaths->mediaRoot();
    cfg.logRoot   = "/mnt/huntcam/logs";   // NFS-shared，devtest 可读（同 wm）
    cfg.logFile   = cfg.logRoot + "/app.log";
#endif

    Misc::createDirectory(cfg.mediaRoot);

    // Debug knobs（同 wm）。
    if (std::getenv("HTC_NO_MCU")) {
        Logger::log(LogLevel::INFO, "[um] MCU disabled (HTC_NO_MCU)");
    }

    // --- S1-S8 + signal install（顺序强制：signal 安装在 commonStartup 后、dispatch 前）---
    app_lifecycle::ProcessLifecycle lc;
    if (!lc.commonStartup(cfg)) return -1;
    // HTC_LOG_DEBUG 必须在 commonStartup（内含 elog_init，HW 默认 INFO）之后调用，
    // 否则 setLogLevel(DEBUG) 被 elog_init_with_config(logLevel=INFO) 覆盖 → DEBUG 失效
    // （曾为 um 顺序 bug：原置于 commonStartup 之前；wm_app.cpp 在 commonStartup 之后，正确）。
    if (const char* dbg = std::getenv("HTC_LOG_DEBUG")) {
        if (dbg[0] == '1') Logger::setLogLevel(LogLevel::DEBUG);
    }
    if (!lc.installSignalHandlers()) return -1;
#ifndef BUILD_FOR_SIMULATION
    elog_set_terminal_output(false);
#endif
    Logger::log(LogLevel::INFO, "[um] op=boot CONFIG_FILE=%s",
                EnvManager::getInstance()->getEnv("CONFIG_FILE", "").c_str());

    // --- CLI flag 解析（无模式，只 --no-* 开关）---
    UmCliFlags flags = parseFlags(argc, argv);
    Logger::log(LogLevel::INFO, "[um] flags no_rtsp=%d no_http=%d no_mdns=%d no_audio=%d force_day=%d",
                flags.noRtsp, flags.noHttp, flags.noMdns, flags.noAudio, flags.forceDay);
    if (flags.forceDay) setenv("HTC_FORCE_RECORD_DAY_MODE", "1", 1);  // CMD_MOBILE :585 同款 env
    if (flags.noAudio)  setenv("HTC_NO_AUDIO", "1", 1);                // 同 -m/--mobile :226；无音频硬件显式关

    // command = CMD_MOBILE（仅供 commonStartupPostDispatch S11 netif 选择；um 要 serve 必有 IP）
    command = CMD_MOBILE;

    // --- S9-S13 post-dispatch ---
    if (!lc.commonStartupPostDispatch(cfg, command)) goto um_exit;

    {  // main work scope — wrapped so the goto above doesn't cross initializers
    config = lc.config();

    // --- idle-timeout 决策内核（提前到 bring-up 前：RTSP/HTTP 活跃回调要引用它）---
    app_usermode::UmIdleConfig idleCfg;
    idleCfg.idleTimeoutMs = 300000;        // spec §7 HTC_UM_IDLE_TIMEOUT_MS 默认 5min（建议 5–10min）
    if (const char* e = std::getenv("HTC_UM_IDLE_TIMEOUT_MS")) {
        long v = std::atol(e); if (v > 0) idleCfg.idleTimeoutMs = v;
    }
    idleCfg.httpActivityWindowMs = 5000;   // spec §4/§7 HTC_UM_HTTP_ACTIVITY_WINDOW_MS
    if (const char* e = std::getenv("HTC_UM_HTTP_ACTIVITY_WINDOW_MS")) {
        long v = std::atol(e); if (v > 0) idleCfg.httpActivityWindowMs = v;
    }
    app_usermode::UmIdleCore idleCore(idleCfg);

    // --- setCleanupHook（mirror wm_app.cpp:233-256；加 HTTP stop）---
    // 首次收到信号（含 idle-timeout 的 self-SIGTERM）时主线程调用一次。
    lc.setCleanupHook([&lc](int sig) {
        Logger::log(LogLevel::INFO, "[um] cleanup signal %d", sig);
        if (lc.daynight()) {
            // 关机绝不 controlISP(DAY)：sensor enabled 时触发 ISP ISR defog panic。
            // 先停 auto-switch 线程，防它 race 进 controlISP。
            lc.daynight()->stopAutoSwitch();
            lc.daynight()->controlIRLed(DayNightState::DAY);   // 纯 GPIO 安全态
            lc.daynight()->controlIRCut(DayNightState::DAY);   // 纯 GPIO 安全态
        }
        if (http_server_is_running()) {  // HTTP 不在 ProcessLifecycle::shutdown()，须显式停
            http_server_stop();
            http_server_deinit();
        }
        if (lc.rgbLed()) lc.rgbLed()->setConstant(GPIO_VALUE::LOW);
        std::string setting_file_path = EnvManager::getInstance()->getEnv("SETTING_FILE_PATH", "");
        if (!setting_file_path.empty()) {
            if (!Settings::getInstance()->saveToJsonFile(setting_file_path)) {
                Logger::log(LogLevel::ERROR, "[um] failed to save setting file: %s", setting_file_path.c_str());
            }
        }
        if (sig == SIGTERM && Power::getInstance()->isChangeModeRequested()) {
            Logger::log(LogLevel::INFO, "[um] change mode requested, holding power");
            auto g = GPIO(POWER_HOLD_PIN);
            if (!g.exportGPIO() || !g.setDirection(GPIO_DIRECTION::OUTPUT) || !g.setValue(GPIO_VALUE::HIGH)) {
                Logger::log(LogLevel::ERROR, "[um] failed to set power hold pin");
            }
        }
    });

    // --- 时间链（复用 wm：RTC→MCU→NTP + ntpSynced；commonStartup 已先 RTC→MCU）---
    {
        auto ntp_ip   = config->get(INI_SECTION_SERVER, INI_KEY_NTP_IP, "");
        auto ntp_port = config->get(INI_SECTION_SERVER, INI_KEY_NTP_PORT, 0);
        if (!ntp_ip.empty() && ntp_port > 0) ntpServer = ntp_ip + ":" + to_string_custom(ntp_port);
        std::string src = app_workmode::acquireTimeChain(ntpServer, ntpSynced);
        Logger::log(LogLevel::INFO, "[um] op=time source=%s ntp_synced=%d",
                    src.c_str(), ntpSynced ? 1 : 0);
    }

    // --- bring-up（按 CMD_MOBILE WorkModeRunner.cpp:584-681 顺序；任一必需失败 goto um_exit）---
    // 1) day/night one-shot
    if (lc.daynight()) {
        const char* forceDay = std::getenv("HTC_FORCE_RECORD_DAY_MODE");
        if (forceDay && strcmp(forceDay, "1") == 0) {
            Logger::log(LogLevel::INFO, "[um] force DAY mode");
            lc.daynight()->controlISP(DayNightState::DAY);
            lc.daynight()->controlIRCut(DayNightState::DAY);
            lc.daynight()->controlIRLed(DayNightState::DAY);
        } else {
            auto s = lc.daynight()->getDayNightState();
            lc.daynight()->controlISP(s);
            lc.daynight()->controlIRCut(s);
            lc.daynight()->controlIRLed(s);
        }
    }

    // 2) ports + wifi + interface + IP（sim 跳过 wifi）
    uint16_t http_port = getConfiguredPort(config, INI_SECTION_MDNS, INI_KEY_MDNS_CTRL_PORT, 80);
    uint16_t rtsp_port = getConfiguredPort(config, INI_SECTION_MDNS, INI_KEY_MDNS_RTSP_PORT, DEFAULT_RTSP_PORT);
#ifndef BUILD_FOR_SIMULATION
    {
        auto ssid = ProductConfig::getInstance()->get(INI_SECTION_DEVICE, INI_KEY_CSSID, "");
        auto pwd  = ProductConfig::getInstance()->get(INI_SECTION_DEVICE, INI_KEY_CPWD, "");
        if (ssid.empty() || pwd.empty()) {
            Logger::log(LogLevel::ERROR, "[um] wifi ssid or pwd empty");
            goto um_exit;
        }
        if (!Misc::connectWifi(ssid, pwd)) { Logger::log(LogLevel::ERROR, "[um] connect wifi error"); goto um_exit; }
        if (!Misc::startDHCP())            { Logger::log(LogLevel::ERROR, "[um] start dhcp error"); goto um_exit; }
    }
#endif
    std::string interface_name = Misc::getNetworkInterfaceName();
#ifdef BUILD_FOR_SIMULATION
    {
        std::string detected = Misc::findUsableNetworkInterface(interface_name);
        if (!detected.empty() && detected != interface_name) {
            interface_name = detected;
            Misc::setNetworkInterfaceName(interface_name);
        }
    }
#endif
    std::string ip_address = Misc::getIPAddress(interface_name);
    if (ip_address.empty()) {
        Logger::log(LogLevel::ERROR, "[um] no IP on interface %s", interface_name.c_str());
        goto um_exit;
    }

    // 3) mDNS（--no-mdns 跳过）
    if (!flags.noMdns && service::isMdnsEnabled(config)) {
        auto mdns_params = service::buildMdnsParams(config, interface_name, ip_address, http_port, rtsp_port);
        if (!service::MdnsService::getInstance()->start(mdns_params)) {
            Logger::log(LogLevel::ERROR, "[um] failed to start mDNS");
            goto um_exit;
        }
    }

    // 4) HTTP（--no-http 跳过）；start() 自动注册 http_api_register_v1，勿另调
    if (!flags.noHttp) {
        HttpServerConfig httpConfig = {static_cast<int>(http_port), nullptr, 2};
        if (http_server_init(&httpConfig) != 0) {
            Logger::log(LogLevel::ERROR, "[um] failed to init HTTP server");
            service::MdnsService::getInstance()->stop();
            goto um_exit;
        }
        if (http_server_start() != 0) {
            Logger::log(LogLevel::ERROR, "[um] failed to start HTTP server");
            http_server_deinit();
            service::MdnsService::getInstance()->stop();
            goto um_exit;
        }
        Logger::log(LogLevel::INFO, "[um] HTTP server started on port %u (%s)",
                    http_port, ip_address.c_str());
    }

    // 5) TcpEvent（非致命，WARNING 继续）
    if (!service::TcpEventService::getInstance()->start(service::kDefaultTcpEventPort)) {
        Logger::log(LogLevel::WARNING, "[um] failed to start TCP event server on port %u",
                    service::kDefaultTcpEventPort);
    }

    // 6) RTSP（--no-rtsp 跳过）— start() 懒初始化整个 IMP 栈（RtspServer 单例）
    if (!flags.noRtsp) {
        lc.markRtspSingletonUsed();   // 必调：gate lc.shutdown() 内的 RtspServer::shutdown()(HAL/IMP)
        // 预热拍照 channel：在 RTSP EnableChn(group1) 前建 group0 encoder 链（官方 Bind-before-enable），
        // 否则拍照时 Bind 落在 FrameSource 使能后 → JPEG polling 超时（um 拍照 bug 根因）。
        if (!flags.noHttp) {
            service::CameraServiceFactory::getInstance(storagePaths)->prewarm();
        }
        media::RtspServer::getInstance()->registerOnsessionPlayCallback([&idleCore]() {
            idleCore.onRtspConnect(steadyNowMs());   // libevent 线程 → 续命
            Logger::log(LogLevel::INFO, "[um] RTSP session play (client active)");
        });
        media::RtspServer::getInstance()->registerOnsessionClosedCallback([&idleCore]() {
            idleCore.onRtspDisconnect(steadyNowMs());
            Logger::log(LogLevel::INFO, "[um] RTSP session closed, waiting for new connection...");
        });
        media::RtspServer::getInstance()->setPort(static_cast<int>(rtsp_port));
        if (!media::RtspServer::getInstance()->start()) {
            Logger::log(LogLevel::ERROR, "[um] failed to start RTSP server");
            if (http_server_is_running()) { http_server_stop(); http_server_deinit(); }
            service::TcpEventService::getInstance()->stop();
            service::MdnsService::getInstance()->stop();
            goto um_exit;
        }
    }

    // --- idle-wait loop（mirror event_loop.cpp:49-87；UmIdleCore 决策 → requestShutdown）---
    constexpr int kPollIntervalMs = 200;   // 同 event_loop kPollIntervalMs
    Logger::log(LogLevel::INFO, "[um] serving (idle-timeout=%lldms); no-rtsp=%d no-http=%d no-mdns=%d",
                static_cast<long long>(idleCfg.idleTimeoutMs), flags.noRtsp, flags.noHttp, flags.noMdns);
    bool shutdownRequested = false;  // 已 requestShutdown，等 SIGTERM 生效，避免重复请求（同 event_loop）
    uint32_t lastHttpSeq = http_server_request_seq();  // HTTP 未起恒为 0，不误触发
    while (lc.keepRunning()) {
        idleCore.tick(steadyNowMs());
        // HTTP 活跃 change-detection：begin_request 每请求 ++（civetweb 线程），poll 变化则续命（镜像 RTSP 接线）
        uint32_t httpSeq = http_server_request_seq();
        if (httpSeq != lastHttpSeq) {
            lastHttpSeq = httpSeq;
            idleCore.onHttpRequest(steadyNowMs());
        }
        if (!shutdownRequested && idleCore.shouldShutdown()) {
            Logger::log(LogLevel::INFO, "[um] idle-timeout reached, shutdown");
            shutdownRequested = true;
            Power::getInstance()->requestShutdown();  // kill(SIGTERM) → 下次 waitForSignal 翻 keepRunning
        }
        lc.waitForSignal(kPollIntervalMs);
    }
    }  // end main work scope

um_exit:
    // --- shutdown 尾（顺序承重，勿改；mirror wm_app.cpp:310-339）---
    {
        app_lifecycle::ShutdownContext sctx;
        sctx.programType   = lc.programType();
        sctx.command       = command;
        sctx.rtcWorkedWell = true;   // um 自跑时间链，无 -rtc 入参
        lc.shutdown(sctx);           // 停 TcpEvent/Mdns + rtsp-gated RtspServer::shutdown()(HAL/IMP)
    }
#ifdef BUILD_FOR_SIMULATION
    Logger::log(LogLevel::INFO, "[SIM] um exit normally");
    _exit(0);
#else
    // spec §6 关机回写 MCU（仅时间，ntpSynced 规则）。
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
