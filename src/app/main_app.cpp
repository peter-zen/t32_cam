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
#include "RtspServer.h"
#include "RtspWorkMode.h"
#include "http_server.h"
#include "DeviceConfig.h"
#include "Common.h"
#include "Logger.h"
#include "ElogInit.h"
#include "ImageSnap.h"
#include "VideoRecorder.h"
#include "CameraRecorder.h"
#include "RecordingPostProcess.h"
#include "MetadataDao.h"
#include "EnvManager.h"
#include "misc/Misc.h"
#include "utils/crc/CRC.h"
#include "Settings.h"
#include "MCU.h"
#include "Disk.h"
#include "AudioRecorder.h"
#include "AudioParams.h"
#include "StringConvert.h"
#include "WorkMode.h"
#include "app.h"
#include "ProcessLifecycle.h"
#include "DayNightSwitch.h"
#include "Power.h"
#include "utils/AutoRelease.h"
#include "time/rtc/RTC.h"
#include "daemon_api.h"
#include "DatabaseManager.h"
#include "MediaScanner.h"
#include "MdnsService.h"
#include "MdnsParams.h"
#include "TcpEventService.h"
#include "Timezone.h"
#include "UsbDongle.h"
#include "CameraFactoryConfigImporter.h"
#include "manifest/Manifest.h"

using namespace network;

using namespace media;

static std::string getCurrentTimeFormatted()
{
    time_t now = time(nullptr);
    struct tm* nowtime = localtime(&now);

    std::stringstream ss;
    ss << std::put_time(nowtime, "%Y%m%d_%H%M%S");
    Misc::getDateTime();
    return ss.str();
}

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

static uint16_t getConfiguredPort(const std::shared_ptr<DeviceConfig>& config,
                                  const std::string& section,
                                  const std::string& key,
                                  uint16_t default_port)
{
    int configured_port = config->get(section, key, static_cast<int>(default_port));
    if (configured_port <= 0 || configured_port > 65535) {
        return default_port;
    }
    return static_cast<uint16_t>(configured_port);
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

static bool processCmdSnap(bool is_rtc_work_well) {
    //move media file from /tmp to sdcard
    std::vector<std::string> file_names;

    std::ifstream jsonFile(QUICK_SNAP_INFO_FILE);
    if (jsonFile.is_open()) {
        Json::Value root;
        Json::CharReaderBuilder readerBuilder;
        std::string errs;
        if (!Json::parseFromStream(readerBuilder, jsonFile, &root, &errs)) {
            Logger::log(LogLevel::ERROR, "Parse json file failed");
            return false;
        }
        auto dir = root["dir"].asString();
        auto files = root["files"];
        std::string oldpath = QUICK_SNAP_DIR + dir + "/*";
        std::string newpath, upload_path, timeStr;
        if (is_rtc_work_well) {
            #if ALL_MEDIA_FILE_IN_ONE_FOLDER
            newpath =  MEDIA_STORE_FOLDER_PATH;
            #else
            newpath =  MEDIA_TARGET_PATH + dir;
            #endif
            upload_path = MEDIA_UPLOAD_PATH + dir;
        } else {
            timeStr = getCurrentTimeFormatted();
            #if ALL_MEDIA_FILE_IN_ONE_FOLDER
            newpath =  MEDIA_STORE_FOLDER_PATH;
            #else
            newpath =  MEDIA_TARGET_PATH + timeStr;
            #endif
            upload_path = MEDIA_UPLOAD_PATH + timeStr;
        }

        if (!Misc::createDirectory(newpath) || !Misc::createDirectory(MEDIA_UPLOAD_PATH) || !Misc::moveFile(oldpath, newpath)) {
            Logger::log(LogLevel::ERROR, "move %s to %s failed", oldpath.c_str(), newpath.c_str());
            return false;
        }

        //create desc file
        for (auto & file : files) {
            std::string filename;
            if (is_rtc_work_well) {
                filename = newpath + "/" + file.asString();
            } else {
                auto oldname = newpath + "/" + file.asString();
                filename = newpath + "/" + timeStr + "_" + file.asString();
                Logger::log(LogLevel::INFO, "rename %s to %s", oldname.c_str(), filename.c_str());
                Misc::moveFile(oldname, filename);
            }
            file_names.push_back(filename);
        }

        auto desc_filename = upload_path + ".json";
        manifest::createDescInfoFile(file_names, desc_filename);
    }

    if (file_names.empty()) {//only for test
        file_names = {
            "./res/20250620_101358.JPG",
            "./res/20250620_101458.JPG",
            "./res/20250620_101558.JPG"
        };
        auto snap_param = ImageSnapParams();
        auto imageSnap = std::make_shared<ImageSnap>(snap_param);
        imageSnap->snap(file_names);
        manifest::createDescInfoFile(file_names, "./res/20250620_101358.json");
    }
    return true;
}

static bool processCmdVideoRecord(bool is_rtc_work_well) {
    (void)is_rtc_work_well;
    using service::camera::CameraRecorder;
    using service::camera::RecordError;
    using service::camera::RecordOptions;
    using service::camera::RecordResult;

    // 同步等待异步录影完成
    std::promise<RecordResult> done;
    auto future = done.get_future();

    // 诊断开关:HTC_RECORD_TMPFS=1 时把录影文件写到 /tmp,绕过 SD 卡,
    // 用于验证 SD 卡写延迟是否为 FPS 瓶颈(见 doc/knowledge/bugs/T32-recording-fps-17-investigation.md 第 6.4 节)。
    // 默认行为不变:仍然写到 MEDIA_TARGET_PATH (SD 卡)。
    std::string record_path;
    {
        const char* envTmpfs = std::getenv("HTC_RECORD_TMPFS");
        if (envTmpfs && envTmpfs[0] == '1') {
            record_path = std::string("/tmp/") + getCurrentTimeFormatted() + ".mp4";
            Logger::log(LogLevel::INFO, "HTC_RECORD_TMPFS=1: writing mp4 to %s (bypassing SD card)", record_path.c_str());
        } else {
            record_path = std::string(MEDIA_TARGET_PATH) + getCurrentTimeFormatted() + ".mp4";
        }
    }
    CameraRecorder recorder;
    RecordOptions opts;
    opts.audio = true;
    opts.autoCover = false;  // work mode 不循环覆盖
    {
        // 诊断开关:HTC_RECORD_BITRATE_KBPS 直接覆盖编码器 bitrate,
        // 用于验证 16 Mbps 是否为 FPS 瓶颈(见 doc/knowledge/bugs/T32-recording-fps-17-investigation.md §6.5)。
        // 默认 0 = 用 CPS 配置(目前 16384 kbps)。
        const char* envBr = std::getenv("HTC_RECORD_BITRATE_KBPS");
        if (envBr && envBr[0] != '\0') {
            int kbps = std::atoi(envBr);
            if (kbps > 0) {
                opts.bitrateKbpsOverride = kbps;
                Logger::log(LogLevel::INFO, "HTC_RECORD_BITRATE_KBPS=%d: bitrate override active", kbps);
            }
        }
    }
    opts.onComplete = [&done](const RecordResult& r) {
        done.set_value(r);
    };

    Logger::log(LogLevel::INFO, "Work Mode record start: %s", record_path.c_str());
    if (!recorder.record(record_path, /*durationSec=*/0, opts)) {
        Logger::log(LogLevel::ERROR, "Work Mode record start failed (CameraRecorder rejected)");
        return false;
    }

    RecordResult r = future.get();  // 阻塞直到录完

    // 1) 立刻落盘缩略图(SDK 帧缓冲还在,但 thumbData_ 已经在 video_recorder_ 里)
    //    WorkMode 路径下 CameraServiceT32 的 onComplete 不会被调用,
    //    所以 saveThumbnail 必须在这里做。
    if (recorder.hasThumbnail()) {
        MetadataDao dao;
        if (dao.saveThumbnail(record_path, recorder.getThumbnailData())) {
            Logger::log(LogLevel::INFO, "Work Mode record: thumbnail saved for %s (%zu bytes)",
                        record_path.c_str(), recorder.getThumbnailData().size());
        } else {
            Logger::log(LogLevel::ERROR, "Work Mode record: saveThumbnail failed for %s",
                        record_path.c_str());
        }
    } else {
        Logger::log(LogLevel::WARNING, "Work Mode record: no thumbnail data for %s",
                    record_path.c_str());
    }

    // 2) 显式释放 SDK 帧缓冲池(~20-50MB),避免函数返回时一次性释放触发 zram swap 尖峰。
    //    之后 recorder 不能再访问(thumbnail / duration 都已取过)。
    //
    // 修复兜底(2026-06-09,见 doc/knowledge/bugs/T32-recording-fps-17-investigation.md §B.1+B.2):
    //   - sync() 先把 page cache 里的脏数据刷盘,减少 SDK 池释放时与 FAT 写竞争
    //   - 释放后 sleep 200ms 让 kswapd 先跑一波,避免瞬时水印骤变触发 zram 风暴
    //   - malloc_trim(0) 把堆碎片归还 OS,减少内核 scan 时累积的匿名页
    ::sync();
    recorder.releaseVideoResources();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    ::malloc_trim(0);

    if (r.error != RecordError::None && r.error != RecordError::UserStop) {
        Logger::log(LogLevel::ERROR, "Work Mode record failed: %s", r.errorMessage.c_str());
        return false;
    }

    // 3) 写 desc JSON(generateDescInfo 走 IIC/MCU,不依赖 SDK 缓冲)
    std::vector<std::string> files = { record_path };
    std::string desc_info;
    if (manifest::generateDescInfo(files, desc_info) == 0) {
        std::string desc_filename = std::string(MEDIA_UPLOAD_PATH) + getCurrentTimeFormatted() + ".json";
        service::camera::RecordingPostProcess::writeWorkModeDescJson(desc_info, desc_filename);
    } else {
        Logger::log(LogLevel::ERROR, "Work Mode record: generateDescInfo failed");
    }
    return true;
}

static bool processCmdConcurrentSnapRecord(bool is_rtc_work_well) {
    (void)is_rtc_work_well;
    auto settings = Settings::getInstance();
    int videoLength = settings->videoLength_l + (settings->videoLength_h << 8);
    if (videoLength <= 0) videoLength = 10;
    int burstNumber = settings->burstNumber;
    if (burstNumber <= 0) burstNumber = 1;

    auto videoParam = std::make_shared<VideoParams>();
    videoParam->setResolution(2560, 1440);
    videoParam->setFrameRate(30);
    videoParam->setBitrate(4000);
    videoParam->setCodecFormat(settings->videoCodec == 2 ? VideoCodecFormat::H265 : VideoCodecFormat::H264);
    videoParam->setRcMode(VideoRcMode::CBR);
    videoParam->setGop(60);

    auto audioParam = std::make_shared<AudioParams>();
    audioParam->setDeviceType(AudioDeviceType::AUDIO_IN);
    audioParam->setDeviceId(1);
    audioParam->setChannelId(0);
    audioParam->setVolume(settings->audioRecordVolume);
    audioParam->setGain(settings->audioRecordGain);
    audioParam->setCodecFormat(AudioCodecFormat::AAC);
    audioParam->setSampleRate(AudioSampleRate::SR_16000);
    audioParam->setChannelCount(1);

    auto recorder = std::make_shared<VideoRecorder>(videoParam, audioParam, true);

    std::string record_path = std::string(MEDIA_TARGET_PATH) + getCurrentTimeFormatted() + ".mp4";
    std::atomic<bool> record_done{false};

    recorder->record(record_path, [&record_done](bool ok) {
        record_done = true;
        Logger::log(LogLevel::INFO, "Work Mode concurrent record done: ok=%d", ok);
    }, videoLength);

    sleep(1);

    std::vector<std::string> snap_files;
    for (int i = 0; i < burstNumber; i++) {
        std::string snap_path = std::string(MEDIA_TARGET_PATH) + getCurrentTimeFormatted() + "_" + to_string_custom(i+1) + ".jpg";
        int jpegQuality = 85;
        if (recorder->captureJpeg(snap_path, jpegQuality)) {
            snap_files.push_back(snap_path);
            Logger::log(LogLevel::INFO, "Work Mode concurrent snap: %s", snap_path.c_str());
        } else {
            Logger::log(LogLevel::ERROR, "Work Mode concurrent snap failed: %s", snap_path.c_str());
        }
        if (i < burstNumber - 1) {
            sleep(1);
        }
    }

    int wait_count = 0;
    while (!record_done && wait_count < videoLength + 10) {
        sleep(1);
        wait_count++;
    }

    std::vector<std::string> all_files = snap_files;
    all_files.push_back(record_path);
    std::string desc_filename = std::string(MEDIA_UPLOAD_PATH) + getCurrentTimeFormatted() + ".json";
    if (!Misc::createDirectory(MEDIA_UPLOAD_PATH)) {
        Logger::log(LogLevel::ERROR, "Failed to create upload directory");
    }
    manifest::createDescInfoFile(all_files, desc_filename);
    return true;
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

#define CMD_HELP 0
#define CMD_CONN_NET (1 << 0)
#define CMD_DHCP (1 << 1)
#define CMD_SNAP (1 << 2)
#define CMD_AUDIO_RECORD (1 << 3)
#define CMD_VIDEO_RECORD (1 << 4)
#define CMD_AUTH (1 << 5)
#define CMD_HEARTBEAT (1 << 6)
#define CMD_UPLOAD (1 << 7)
#define CMD_MOBILE (1 << 8)
#define CMD_RTSP_SERVER (1 << 9)
#define CMD_NTP (1 << 10)
#define CMD_GET_RTC (1 << 11)
#define CMD_SET_RTC (1 << 12)

static bool rtsp_audio_enabled = true;  // RTSP 音频默认开启
static bool mobile_rtsp_enabled = true; // Mobile 模式默认启动 RTSP，调试录像 FPS 时可关闭
static std::shared_ptr<MgmtServClient> mgmtServClient = nullptr;
static std::shared_ptr<StorageServClient> storageServClient = nullptr;

int main(int argc, char* argv[])
{
    bool is_rtc_work_well = true;
    enum workingMode working_mode = workingMode::WORKING_MODE_MAX;

    // Declared up-front (and assigned after dispatch) so `goto main_exit` from
    // the commonStartup / commonStartupPostDispatch failure paths does not cross
    // a non-trivial initializer. Defaults match the original behavior on the
    // early-fail path.
    std::shared_ptr<DeviceConfig> config;
    int program_type = PTYPE_NO_NET;
    int command = CMD_HELP;

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
        if (argc == 5 && (std::string(argv[3]) == "-rtc" || std::string(argv[3]) == "--rtc-status")) {
            working_mode = (enum workingMode)stoi_custom(argv[2]);
            is_rtc_work_well = (bool)stoi_custom(argv[4]);
            Logger::log(LogLevel::INFO, "%s working mode %d, rtc status %d", __func__, working_mode, is_rtc_work_well);
            switch (working_mode) {
                case WORKING_MODE_SNAP_ONLY:
                    command = CMD_SNAP;
                    break;
                case WORKING_MODE_UPLOAD_ONLY:
                    command = CMD_CONN_NET | CMD_DHCP | CMD_NTP | CMD_UPLOAD;
                    if (lc.rgbLed()) {
                        lc.rgbLed()->asyncBlink(60);
                    }
                    break;
                case WORKING_MODE_TEST_ONLY:
                    {
                        if (lc.rgbLed()) {
                            lc.rgbLed()->asyncBlink(30);
                        }
                        command = CMD_MOBILE;
                    }
                    break;
                case WORKING_MODE_SNAP_UPLOAD:
                    command = CMD_SNAP | CMD_CONN_NET | CMD_DHCP | CMD_NTP | CMD_UPLOAD;
                    break;
                case WORKING_MODE_UVC:
                    command = CMD_CONN_NET | CMD_DHCP | CMD_RTSP_SERVER;
                    break;
                default:
                    Logger::log(LogLevel::ERROR, "%s Invalid working mode %d, power off", __func__, working_mode);
                    //Power::getInstance()->requestShutdown();
                    sleep(10);
                    return -1;
            }
        } else {
            Logger::log(LogLevel::ERROR, "%s Invalid command %s, power off", __func__, argv[1]);
            Power::getInstance()->requestShutdown();
            sleep(10);
            return -1;
        }
    }

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
    lc.setCleanupHook([&lc](int sig) {
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

    // Cascade-local aliases over the lifecycle accessors (minimal-diff ref
    // rewrites — see T14-planner-full.md §9). Assigned (not declared) here —
    // they were declared up-front so early `goto main_exit` stays legal.
    config = lc.config();
    program_type = lc.programType();

    //RTC
    if (command & CMD_GET_RTC) {
        struct tm now;
        if(!RTC::getInstance()->getTime(now)) {
            Logger::log(LogLevel::ERROR, "get RTC time error");
        } else {
            Logger::log(LogLevel::INFO, "RTC time: %d-%02d-%02d %02d:%02d:%02d",
                   now.tm_year+YEAR_OFFSET, now.tm_mon+MONTH_OFFSET, now.tm_mday,
                   now.tm_hour, now.tm_min, now.tm_sec);
        }
    }

    if (command & CMD_SET_RTC) {
        std::string rtc_time = argv[2];
        struct tm timeinfo = {0};
        // Parse the string into struct tm
        if (strptime(rtc_time.c_str(), "%Y-%m-%d %H:%M:%S", &timeinfo) == nullptr) {
            Logger::log(LogLevel::ERROR, "Failed to parse time string: %s", rtc_time.c_str());
        } else {
            // strptime already correctly sets tm_year (years since 1900) and tm_mon (0-11)
            if (!RTC::getInstance()->setTime(timeinfo)) {
                Logger::log(LogLevel::ERROR, "set RTC time: %s error", rtc_time.c_str());
            } else {
                Logger::log(LogLevel::INFO, "set RTC time: %s success", rtc_time.c_str());
            }
        }
    }

    if (command & CMD_SNAP && is_rtc_work_well) {
        uint8_t camMode = Settings::getInstance()->cameraMode;
        if (camMode == 0) {
            if (!processCmdSnap(is_rtc_work_well)) {
                goto main_exit;
            }
        } else if (camMode == 1) {
            if (!processCmdSnap(is_rtc_work_well)) {
                goto main_exit;
            }
            if (!processCmdVideoRecord(is_rtc_work_well)) {
                goto main_exit;
            }
        } else if (camMode == 2) {
            if (!processCmdVideoRecord(is_rtc_work_well)) {
                goto main_exit;
            }
        } else if (camMode == 3) {
            if (!processCmdSnap(is_rtc_work_well)) {
                Logger::log(LogLevel::WARNING, "quick_snap failed, continue with concurrent record");
            }
            if (!processCmdConcurrentSnapRecord(is_rtc_work_well)) {
                goto main_exit;
            }
        } else {
            Logger::log(LogLevel::WARNING, "cameraMode=%d not supported in work mode", camMode);
        }
    }

    //connect wifi
    if (command & CMD_CONN_NET) {
        if (program_type == PTYPE_WIFI) {
            auto wifi_ssid = config->get(INI_SECTION_SYS, INI_KEY_UPID, "");
            auto wifi_pwd = config->get(INI_SECTION_SYS, INI_KEY_UPWD, "");
            if (wifi_ssid.empty() || wifi_pwd.empty()) {
                Logger::log(LogLevel::ERROR, "wifi ssid or pwd is empty");
                goto main_exit;
            }
            if (!Misc::connectWifi(wifi_ssid, wifi_pwd)) {
                Logger::log(LogLevel::ERROR, "connect wifi error");
                goto main_exit;
            }
        } else if (program_type == PTYPE_USB_DONGLE) {
            auto usb_dongle = UsbDongle::getInstance();
            if (!usb_dongle->loadDriver()) {
                Logger::log(LogLevel::ERROR, "load usb dongle driver error");
                goto main_exit;
            }

            if (!usb_dongle->open()) {
                Logger::log(LogLevel::ERROR, "open usb dongle error");
                goto main_exit;
            }

            if (!usb_dongle->preconfig()) {
                Logger::log(LogLevel::ERROR, "usb dongle preconfig error");
                goto main_exit;
            }
        } else {
            Logger::log(LogLevel::ERROR, "program type %d not support", program_type);
            goto main_exit;
        }
    }

    //dhcp
    if (command & CMD_DHCP) {
        if (!Misc::startDHCP()) {
            Logger::log(LogLevel::ERROR, "start dhcp error");
            goto main_exit;
        }
    }

    if (command & CMD_NTP) {
        auto ntp_server_ip = config->get(INI_SECTION_SERVER, INI_KEY_NTP_IP, "");
        auto ntp_server_port = config->get(INI_SECTION_SERVER, INI_KEY_NTP_PORT, 0);
        auto ntp_server = ntp_server_ip + ":" + to_string_custom(ntp_server_port);
        Logger::log(LogLevel::INFO, "ntp server: %s", ntp_server.c_str());
        if (ntp_server.empty()) {
            Logger::log(LogLevel::ERROR, "ntp server is empty");
            goto main_exit;
        }
        if (!Misc::ntpSyncAndWait(ntp_server)) {
            goto main_exit;
        }

        if (is_rtc_work_well) {
            time_t now = time(nullptr);
            struct tm* nowtime = localtime(&now);
            RTC::getInstance()->setTime(*nowtime);
        }
    }

    if (command & CMD_SNAP && !is_rtc_work_well) {
        uint8_t camMode = Settings::getInstance()->cameraMode;
        if (camMode == 0) {
            if (!processCmdSnap(is_rtc_work_well)) {
                goto main_exit;
            }
        } else if (camMode == 1) {
            if (!processCmdSnap(is_rtc_work_well)) {
                goto main_exit;
            }
            if (!processCmdVideoRecord(is_rtc_work_well)) {
                goto main_exit;
            }
        } else if (camMode == 2) {
            if (!processCmdVideoRecord(is_rtc_work_well)) {
                goto main_exit;
            }
        } else if (camMode == 3) {
            if (!processCmdSnap(is_rtc_work_well)) {
                Logger::log(LogLevel::WARNING, "quick_snap failed, continue with concurrent record");
            }
            if (!processCmdConcurrentSnapRecord(is_rtc_work_well)) {
                goto main_exit;
            }
        } else {
            Logger::log(LogLevel::WARNING, "cameraMode=%d not supported in work mode", camMode);
        }
    }

    if (command & CMD_AUDIO_RECORD) {
        Logger::log(LogLevel::INFO, "[Main] CMD_AUDIO_RECORD enter");
        
        AudioParams audioParam;
        audioParam.setDeviceType(AudioDeviceType::AUDIO_IN);
        audioParam.setDeviceId(1);  
        audioParam.setChannelId(0);
        audioParam.setVolume(80);    
        audioParam.setGain(28);      
        audioParam.setCodecFormat(AudioCodecFormat::AAC);
        #ifdef BUILD_FOR_SIMULATION
        audioParam.setSampleRate(AudioSampleRate::SR_44100);
        #else
        audioParam.setSampleRate(AudioSampleRate::SR_16000);
        #endif
        audioParam.setChannelCount(1);   
        IAudioRecorder* audioIn = new AudioRecorder();
        audioIn->setAudioParams(audioParam);
        audioIn->setRecordFilePath("./res/audioin_record.aac");
        Logger::log(LogLevel::INFO, "[Main] audioIn start...");
        if (audioIn->start()) {
            sleep(20);
            Logger::log(LogLevel::INFO, "[Main] audioIn stop...");
            audioIn->stop();
            Logger::log(LogLevel::INFO, "[Main] audioIn stop done");
        } else {
            Logger::log(LogLevel::ERROR, "audioIn start failed");
        }
        Logger::log(LogLevel::INFO, "[Main] destroyRecorder(audioIn)...");
        delete audioIn;
        Logger::log(LogLevel::INFO, "[Main] destroyRecorder(audioIn) done");
        Logger::log(LogLevel::INFO, "[Main] CMD_AUDIO_RECORD leave");
    }

    if (command & CMD_VIDEO_RECORD) {
        auto videoParam = std::make_shared<VideoParams>();
        videoParam->setResolution(1920, 1080);
        videoParam->setFrameRate(30);
        videoParam->setBitrate(4000);

        // 配置音频参数
        auto audioParam = std::make_shared<AudioParams>();
        audioParam->setDeviceType(AudioDeviceType::AUDIO_IN);
        audioParam->setDeviceId(1);  
        audioParam->setChannelId(0);
        audioParam->setVolume(80);    
        audioParam->setGain(28);      
        audioParam->setCodecFormat(AudioCodecFormat::AAC);
        #ifdef BUILD_FOR_SIMULATION
        audioParam->setSampleRate(AudioSampleRate::SR_44100);
        #else
        audioParam->setSampleRate(AudioSampleRate::SR_16000);
        #endif
        audioParam->setChannelCount(1);   
    
        auto recorder = std::make_shared<VideoRecorder>(videoParam, audioParam);
        std::string record_path = "./res/" + getCurrentTimeFormatted() + ".mp4";;//MEDIA_STORE_FOLDER_PATH + getCurrentTimeFormatted() + ".mp4";
        Logger::log(LogLevel::INFO, "record to %s", record_path.c_str());
        recorder->record(record_path, 10);
    }

    if (command & CMD_MOBILE) {
        setenv("HTC_TEST_MODE", "1", 1);

        // Day/Night initialization for CMD_MOBILE (covers both -m and -wm 3 paths)
        if (lc.daynight()) {
            const char* forceDay = std::getenv("HTC_FORCE_RECORD_DAY_MODE");
            if (forceDay && strcmp(forceDay, "1") == 0) {
                Logger::log(LogLevel::INFO, "CMD_MOBILE: force DAY mode from --force-day");
                lc.daynight()->controlISP(DayNightState::DAY);
                lc.daynight()->controlIRCut(DayNightState::DAY);
                lc.daynight()->controlIRLed(DayNightState::DAY);
            } else {
                auto daynight_state = lc.daynight()->getDayNightState();
                lc.daynight()->controlISP(daynight_state);
                lc.daynight()->controlIRCut(daynight_state);
                lc.daynight()->controlIRLed(daynight_state);
            }
        }

        uint16_t http_port = getConfiguredPort(config, INI_SECTION_MDNS, INI_KEY_MDNS_CTRL_PORT, 80);
        uint16_t rtsp_port = getConfiguredPort(config, INI_SECTION_MDNS, INI_KEY_MDNS_RTSP_PORT, DEFAULT_RTSP_PORT);
        auto wifi_ssid = config->get(INI_SECTION_DEVICE, INI_KEY_CSSID, "");
        auto wifi_pwd = config->get(INI_SECTION_DEVICE, INI_KEY_CPWD, "");
#ifndef BUILD_FOR_SIMULATION
        if (wifi_ssid.empty() || wifi_pwd.empty()) {
            Logger::log(LogLevel::ERROR, "wifi ssid or pwd is empty");
            goto main_exit;
        }
        if (!Misc::connectWifi(wifi_ssid, wifi_pwd)) {
            Logger::log(LogLevel::ERROR, "connect wifi error");
            goto main_exit;
        }
        if (!Misc::startDHCP()) {
            Logger::log(LogLevel::ERROR, "start dhcp error");
            goto main_exit;
        }
#endif

        std::string interface_name = Misc::getNetworkInterfaceName();
#ifdef BUILD_FOR_SIMULATION
        std::string detected_interface = Misc::findUsableNetworkInterface(interface_name);
        if (!detected_interface.empty() && detected_interface != interface_name) {
            interface_name = detected_interface;
            Misc::setNetworkInterfaceName(interface_name);
        }
#endif

        std::string ip_address = Misc::getIPAddress(interface_name);
        if (ip_address.empty()) {
            Logger::log(LogLevel::ERROR, "No IP address found on interface %s", interface_name.c_str());
            goto main_exit;
        }

        if (service::isMdnsEnabled(config)) {
            auto mdns_params = service::buildMdnsParams(config, interface_name, ip_address, http_port, rtsp_port);
            if (!service::MdnsService::getInstance()->start(mdns_params)) {
                Logger::log(LogLevel::ERROR, "Failed to start mDNS service");
                goto main_exit;
            }
        } else {
            elog_i("MDNS", "mDNS disabled by config");
        }

        // 启动 HTTP Server 替代 RemoteCtrlClient
        HttpServerConfig httpConfig = {static_cast<int>(http_port), nullptr, 2};
        if (http_server_init(&httpConfig) != 0) {
            Logger::log(LogLevel::ERROR, "Failed to init HTTP server");
            service::MdnsService::getInstance()->stop();
            goto main_exit;
        }
        if (http_server_start() != 0) {
            Logger::log(LogLevel::ERROR, "Failed to start HTTP server");
            http_server_deinit();
            service::MdnsService::getInstance()->stop();
            goto main_exit;
        }
        if (!service::TcpEventService::getInstance()->start(service::kDefaultTcpEventPort)) {
            Logger::log(LogLevel::WARNING, "Failed to start TCP event server on port %u",
                        service::kDefaultTcpEventPort);
        }
        elog_i("MDNS", "HTTP server started on port %u for interface %s (%s)",
               http_port, interface_name.c_str(), ip_address.c_str());
        
        if (mobile_rtsp_enabled) {
            lc.markRtspSingletonUsed();
            RtspServer::getInstance()->registerOnsessionClosedCallback([]() {
                Logger::log(LogLevel::INFO, "RTSP session closed in mobile mode, waiting for new connection...");
            });
            RtspServer::getInstance()->setPort(static_cast<int>(rtsp_port));
            if (!RtspServer::getInstance()->start()) {
                Logger::log(LogLevel::ERROR, "Failed to start RTSP server");
                if (http_server_is_running()) {
                    http_server_stop();
                    http_server_deinit();
                }
                service::TcpEventService::getInstance()->stop();
                service::MdnsService::getInstance()->stop();
                goto main_exit;
            }
        } else {
            Logger::log(LogLevel::INFO, "RTSP server disabled in mobile mode");
        }

        while (lc.keepRunning()) {
            (void)lc.waitForSignal(1000);
        }

        service::MdnsService::getInstance()->stop();
        
        // 停止 RTSP Server
        if (mobile_rtsp_enabled) {
            RtspServer::getInstance()->stop();
        }
        
        // 停止 HTTP Server
        if (http_server_is_running()) {
            http_server_stop();
            http_server_deinit();
        }
        service::TcpEventService::getInstance()->stop();
    }

    if (command & CMD_RTSP_SERVER) {
        uint16_t rtsp_port = getConfiguredPort(config, INI_SECTION_MDNS, INI_KEY_MDNS_RTSP_PORT, DEFAULT_RTSP_PORT);
        lc.markRtspSingletonUsed();
        if (!app_workmode::runRtspServerUntilSignal(rtsp_port,
                [&lc]{ return lc.keepRunning(); },
                [&lc](int ms){ (void)lc.waitForSignal(ms); })) {
            goto main_exit;
        }
    }

    if (command & CMD_AUTH || command & CMD_HEARTBEAT || command & CMD_UPLOAD) {
        auto mgmtServerAddr = config->get(INI_SECTION_SERVER, INI_KEY_MS_IP, "");
        Logger::log(LogLevel::INFO, "mgmtServerAddr: %s", mgmtServerAddr.c_str());
        auto mgmtServerPort = config->get(INI_SECTION_SERVER, INI_KEY_MS_PORT, 0);
        Logger::log(LogLevel::INFO, "mgmtServerPort: %d", mgmtServerPort);

        mgmtServClient = std::make_shared<MgmtServClient>(mgmtServerAddr, mgmtServerPort);
        if (EC_SUCCESS != mgmtServClient->connect(3000)) {
            Logger::log(LogLevel::ERROR, "connect [%s:%d] failed", mgmtServerAddr.c_str(), mgmtServerPort);
            goto main_exit;
        } else {
            Logger::log(LogLevel::INFO, "connect [%s:%d] success", mgmtServerAddr.c_str(), mgmtServerPort);
        }

        if (EC_SUCCESS != mgmtServClient->authenticate()) {
            Logger::log(LogLevel::ERROR, "auth failed");
            goto main_exit;
        } else {
            Logger::log(LogLevel::INFO, "auth success");
        }
    }

    if (command & CMD_HEARTBEAT) {
        mgmtServClient->sendHeartbeat();
    }
    
    if (command & CMD_UPLOAD) {
        //assume we have a storage server same as mgmt server
        storageServClient = mgmtServClient->newStorageServClient();
        if (lc.rgbLed()) {
            lc.rgbLed()->setConstant(GPIO_VALUE::HIGH);
        }
        bool descfile_uploaded = false;
        auto desc_filenames = Misc::listFilenames(MEDIA_UPLOAD_PATH);
        for (auto &desc_filename : desc_filenames) {
            desc_filename = MEDIA_UPLOAD_PATH + desc_filename;
            Logger::log(LogLevel::INFO, "desc_filename %s", desc_filename.c_str());
            std::ifstream ifs(desc_filename);
            
            if (!Misc::isJsonFile(desc_filename)) {
                Logger::log(LogLevel::INFO, "%s is not json", desc_filename.c_str());
                Misc::deleteFile(desc_filename);
                continue;
            }

            Json::Value root;
            Json::Reader reader;
            if (!reader.parse(ifs, root)) {
                Logger::log(LogLevel::ERROR, "Failed to parse JSON file: %s", desc_filename.c_str());
                Misc::deleteFile(desc_filename);
                continue;
            }

            std::string pid = DeviceConfig::getInstance()->get(INI_SECTION_DEVICE, INI_KEY_PID, "");
            if (!root.isMember("F_UploadedTag") ||!root.isMember("device") || !root["device"].isMember("PID") || (root["device"]["PID"].asString() != pid)) {
                Logger::log(LogLevel::ERROR, "PID not match");
                Misc::deleteFile(desc_filename);
                continue;
            }
            
            if (!root.isMember("file_inf")) {
                Logger::log(LogLevel::ERROR, "file_inf not exist");
                Misc::deleteFile(desc_filename);
                continue;
            }
            
            descfile_uploaded = false;
            if (root["F_UploadedTag"].asInt() == 0) {
                //upload file description json file
                Logger::log(LogLevel::INFO, "original desc_filename %s", desc_filename.c_str());
                auto target_filename = desc_filename;
                storageServClient->bindUploadCallback([&descfile_uploaded, target_filename](const std::string &filename, int error_code) {
                    Logger::log(LogLevel::INFO, "upload descfile [%s], error code: %d", filename.c_str(), error_code);
                    if (filename == target_filename) {
                        descfile_uploaded = (error_code==EC_SUCCESS)?true:false;
                    }
                });
                storageServClient->uploadFile(desc_filename);
                auto start_time = std::chrono::steady_clock::now();
                auto now = std::chrono::steady_clock::now();
                while (std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count() < 8) {//must greater than 5s
                    if (descfile_uploaded) {
                        Logger::log(LogLevel::INFO, "descfile %s uploaded", desc_filename.c_str());
                        root["F_UploadedTag"] = 1;
                        std::ofstream ofs(desc_filename);
                        ofs << root.toStyledString();
                        ofs.close();
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    now = std::chrono::steady_clock::now();
                }
            } else {
                descfile_uploaded = true;
            }

            if (descfile_uploaded) {
                bool allFileUploaded = true;
                const Json::Value file_inf_array = root["file_inf"];
                std::vector<std::string> uploaded_file_list;
                storageServClient->bindUploadCallback([&uploaded_file_list, &allFileUploaded](const std::string &filename, int error_code) {
                    Logger::log(LogLevel::INFO, "upload %s, error code: %d", filename.c_str(), error_code);
                    if (error_code == EC_SUCCESS) {
                        uploaded_file_list.push_back(filename);
                    } else {
                        allFileUploaded = false;
                    }
                });
                for (Json::ArrayIndex i = 0; i < file_inf_array.size(); ++i) {
                    if (file_inf_array[i].isMember("F_FileName") && file_inf_array[i].isMember("F_FilePath")) {
                        std::string filepath = file_inf_array[i]["F_FilePath"].asString();
                        std::string filename = file_inf_array[i]["F_FileName"].asString();
                        auto tag = file_inf_array[i]["F_UploadedTag"].asInt();
                        if (tag == 0) {
                            Logger::log(LogLevel::INFO, "uploading file: %s", filename.c_str());
                            auto pathname = filepath + "/" + filename;
                            storageServClient->uploadFile(pathname);
                        }
                    }
                }

                while (!storageServClient->isUploadFinished()) {
                    usleep(1);
                }

                for (auto& filename : uploaded_file_list) {
                    for (Json::ArrayIndex i = 0; i < file_inf_array.size(); ++i) {
                        auto pathname = file_inf_array[i]["F_FilePath"].asString() + "/" + file_inf_array[i]["F_FileName"].asString();
                        if (pathname == filename) {
                            root["file_inf"][i]["F_UploadedTag"] = 1;
                            
                            auto file_manage_type = config->get(INI_SECTION_POLICY, INI_KEY_FILE_MANAGE, 0);
                            if (file_manage_type == FILE_MANAGE_DELETE) {
                                Misc::deleteFile(pathname);
                            }
                        }
                    }
                }

                std::ofstream ofs(desc_filename);
                ofs << root.toStyledString();
                ofs.close();
                if (allFileUploaded) {
                    Logger::log(LogLevel::INFO, "upload all files finished in %s", desc_filename.c_str());
                    //Misc::deleteFile(desc_filename);
                }
            }    
        }
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
