// Phase C-2 (T15): the work-mode execution extracted verbatim from
// src/app/main_app.cpp. The cascade, the -wm switch, and the 5 -wm-reachable
// static helpers live here and ONLY here. Behavior byte-identical to HEAD:
// only static/local→ctx./lc. ref rewrites + goto main_exit→return Continue
// (+ the invalid-mode default→return TerminalExit). See T15-planner-full.md.

#include "WorkModeRunner.h"
#include "RtspWorkMode.h"
#include "WorkMode.h"
#include "ProcessLifecycle.h"

#include "MgmtServClient.h"
#include "RtspServer.h"
#include "http_server.h"
#include "DeviceConfig.h"
#include "ProductConfig.h"
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
#include "Disk.h"
#include "AudioRecorder.h"
#include "AudioParams.h"
#include "StringConvert.h"
#include "app.h"
#include "DayNightSwitch.h"
#include "Power.h"
#include "utils/AutoRelease.h"
#include "time/rtc/RTC.h"
#include "DatabaseManager.h"
#include "MdnsService.h"
#include "MdnsParams.h"
#include "TcpEventService.h"
#include "UsbDongle.h"
#include "manifest/Manifest.h"

#include <iostream>
#include <fstream>
#include <string>
#include <unistd.h>
#include <cstdlib>
#include <ctime>
#include <cstring>
#include <iomanip>
#include <json/json.h>
#include <queue>
#include <thread>
#include <chrono>
#include <malloc.h>   // malloc_trim (glibc extension)
#include <mutex>
#include <atomic>
#include <future>
#include <unordered_map>
#include <algorithm>

using namespace network;
using namespace media;

namespace {

// --- The 5 -wm-reachable static helpers, moved verbatim from main_app.cpp. ---
// Only file-local; not part of the public API. Bodies unchanged.

static std::string getCurrentTimeFormatted()
{
    time_t now = time(nullptr);
    struct tm* nowtime = localtime(&now);

    std::stringstream ss;
    ss << std::put_time(nowtime, "%Y%m%d_%H%M%S");
    Misc::getDateTime();
    return ss.str();
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

static bool processCmdVideoRecord(bool is_rtc_work_well, app_workmode::UploadWorker* uploadWorker) {
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
    opts.audio = false;  // 默认禁用音频：当前设备无 audio 硬件，开启会导致 audio.ko 在
                         // 无 speaker(spk_gpio=-1)环境下 dsp_config_route_param 内核空指针崩溃
    opts.autoCover = false;  // work mode 不循环覆盖
    // HTC_RECORD_AUDIO=1 时显式启用音频（供有 audio 硬件的设备）。
    {
        const char* envAudio = std::getenv("HTC_RECORD_AUDIO");
        if (envAudio && envAudio[0] == '1') {
            opts.audio = true;
            Logger::log(LogLevel::INFO, "HTC_RECORD_AUDIO=1: recording with audio");
        }
    }
    // 诊断开关:HTC_RECORD_NO_THUMBNAIL=1 时跳过 CH2 缩略图抓取。captureThumbnail 的并发
    // SDK 调用会干扰主码流编码器(见 VideoRecorder.cpp:555 注释:并发 SDK 调用致 driver
    // 状态不一致),在 64MB T32 上导致主码流 polling 超时、mp4 空文件。用于隔离验证主码流。
    {
        const char* envNoThumb = std::getenv("HTC_RECORD_NO_THUMBNAIL");
        if (envNoThumb && envNoThumb[0] == '1') {
            opts.concurrentSnap = false;
            Logger::log(LogLevel::INFO, "HTC_RECORD_NO_THUMBNAIL=1: skip CH2 thumbnail (isolate main stream)");
        }
    }
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
        if (uploadWorker) {
            uploadWorker->enqueue(desc_filename);
            Logger::log(LogLevel::INFO, "Work Mode record: desc enqueued for upload: %s", desc_filename.c_str());
        }
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

}  // namespace

namespace app_workmode {

CascadeResult runCommands(int command, WorkModeContext& ctx)
{
    // Cascade-local aliases over the lifecycle accessors (minimal-diff ref
    // rewrites — T15). The cascade read `config` / `program_type` as locals;
    // they are now ctx.lc.config() / ctx.lc.programType() but the aliases keep
    // the moved body byte-identical. lc.config() returns the DeviceConfig
    // singleton shared_ptr by value; copy it (same underlying instance).
    auto  config       = ctx.lc.config();
    int   program_type = ctx.lc.programType();

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
        std::string rtc_time = ctx.argv[2];
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

    if (command & CMD_SNAP && ctx.isRtcWorkWell) {
        uint8_t camMode = Settings::getInstance()->cameraMode;
        if (camMode == 0) {
            if (!processCmdSnap(ctx.isRtcWorkWell)) {
                return CascadeResult::Continue;
            }
        } else if (camMode == 1) {
            if (!processCmdSnap(ctx.isRtcWorkWell)) {
                return CascadeResult::Continue;
            }
            if (!processCmdVideoRecord(ctx.isRtcWorkWell, ctx.uploadWorker.get())) {
                return CascadeResult::Continue;
            }
        } else if (camMode == 2) {
            if (!processCmdVideoRecord(ctx.isRtcWorkWell, ctx.uploadWorker.get())) {
                return CascadeResult::Continue;
            }
        } else if (camMode == 3) {
            if (!processCmdSnap(ctx.isRtcWorkWell)) {
                Logger::log(LogLevel::WARNING, "quick_snap failed, continue with concurrent record");
            }
            if (!processCmdConcurrentSnapRecord(ctx.isRtcWorkWell)) {
                return CascadeResult::Continue;
            }
        } else {
            Logger::log(LogLevel::WARNING, "cameraMode=%d not supported in work mode", camMode);
        }
    }

    //connect wifi
    if (command & CMD_CONN_NET) {
        if (program_type == PTYPE_WIFI) {
            // T25 Phase-3: UPID/UPWD migrated from DeviceConfig SYSTEM to Settings
            auto wifi_ssid = Settings::getInstance()->upid;
            auto wifi_pwd = Settings::getInstance()->upwd;
            if (wifi_ssid.empty() || wifi_pwd.empty()) {
                Logger::log(LogLevel::ERROR, "wifi ssid or pwd is empty");
                return CascadeResult::Continue;
            }
            if (!Misc::connectWifi(wifi_ssid, wifi_pwd)) {
                Logger::log(LogLevel::ERROR, "connect wifi error");
                return CascadeResult::Continue;
            }
        } else if (program_type == PTYPE_USB_DONGLE) {
            auto usb_dongle = UsbDongle::getInstance();
            if (!usb_dongle->loadDriver()) {
                Logger::log(LogLevel::ERROR, "load usb dongle driver error");
                return CascadeResult::Continue;
            }

            if (!usb_dongle->open()) {
                Logger::log(LogLevel::ERROR, "open usb dongle error");
                return CascadeResult::Continue;
            }

            if (!usb_dongle->preconfig()) {
                Logger::log(LogLevel::ERROR, "usb dongle preconfig error");
                return CascadeResult::Continue;
            }
        } else {
            Logger::log(LogLevel::ERROR, "program type %d not support", program_type);
            return CascadeResult::Continue;
        }
    }

    //dhcp
    if (command & CMD_DHCP) {
        if (!Misc::startDHCP()) {
            Logger::log(LogLevel::ERROR, "start dhcp error");
            return CascadeResult::Continue;
        }
    }

    if (command & CMD_NTP) {
        auto ntp_server_ip = config->get(INI_SECTION_SERVER, INI_KEY_NTP_IP, "");
        auto ntp_server_port = config->get(INI_SECTION_SERVER, INI_KEY_NTP_PORT, 0);
        auto ntp_server = ntp_server_ip + ":" + to_string_custom(ntp_server_port);
        Logger::log(LogLevel::INFO, "ntp server: %s", ntp_server.c_str());
        if (ntp_server.empty()) {
            Logger::log(LogLevel::ERROR, "ntp server is empty");
            return CascadeResult::Continue;
        }
        if (!Misc::ntpSyncAndWait(ntp_server)) {
            return CascadeResult::Continue;
        }

        if (ctx.isRtcWorkWell) {
            time_t now = time(nullptr);
            struct tm* nowtime = localtime(&now);
            RTC::getInstance()->setTime(*nowtime);
        }
    }

    if (command & CMD_SNAP && !ctx.isRtcWorkWell) {
        uint8_t camMode = Settings::getInstance()->cameraMode;
        if (camMode == 0) {
            if (!processCmdSnap(ctx.isRtcWorkWell)) {
                return CascadeResult::Continue;
            }
        } else if (camMode == 1) {
            if (!processCmdSnap(ctx.isRtcWorkWell)) {
                return CascadeResult::Continue;
            }
            if (!processCmdVideoRecord(ctx.isRtcWorkWell, ctx.uploadWorker.get())) {
                return CascadeResult::Continue;
            }
        } else if (camMode == 2) {
            if (!processCmdVideoRecord(ctx.isRtcWorkWell, ctx.uploadWorker.get())) {
                return CascadeResult::Continue;
            }
        } else if (camMode == 3) {
            if (!processCmdSnap(ctx.isRtcWorkWell)) {
                Logger::log(LogLevel::WARNING, "quick_snap failed, continue with concurrent record");
            }
            if (!processCmdConcurrentSnapRecord(ctx.isRtcWorkWell)) {
                return CascadeResult::Continue;
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
        if (ctx.lc.daynight()) {
            const char* forceDay = std::getenv("HTC_FORCE_RECORD_DAY_MODE");
            if (forceDay && strcmp(forceDay, "1") == 0) {
                Logger::log(LogLevel::INFO, "CMD_MOBILE: force DAY mode from --force-day");
                ctx.lc.daynight()->controlISP(DayNightState::DAY);
                ctx.lc.daynight()->controlIRCut(DayNightState::DAY);
                ctx.lc.daynight()->controlIRLed(DayNightState::DAY);
            } else {
                auto daynight_state = ctx.lc.daynight()->getDayNightState();
                ctx.lc.daynight()->controlISP(daynight_state);
                ctx.lc.daynight()->controlIRCut(daynight_state);
                ctx.lc.daynight()->controlIRLed(daynight_state);
            }
        }

        uint16_t http_port = getConfiguredPort(config, INI_SECTION_MDNS, INI_KEY_MDNS_CTRL_PORT, 80);
        uint16_t rtsp_port = getConfiguredPort(config, INI_SECTION_MDNS, INI_KEY_MDNS_RTSP_PORT, DEFAULT_RTSP_PORT);
        auto wifi_ssid = ProductConfig::getInstance()->get(INI_SECTION_DEVICE, INI_KEY_CSSID, "");
        auto wifi_pwd = ProductConfig::getInstance()->get(INI_SECTION_DEVICE, INI_KEY_CPWD, "");
#ifndef BUILD_FOR_SIMULATION
        if (wifi_ssid.empty() || wifi_pwd.empty()) {
            Logger::log(LogLevel::ERROR, "wifi ssid or pwd is empty");
            return CascadeResult::Continue;
        }
        if (!Misc::connectWifi(wifi_ssid, wifi_pwd)) {
            Logger::log(LogLevel::ERROR, "connect wifi error");
            return CascadeResult::Continue;
        }
        if (!Misc::startDHCP()) {
            Logger::log(LogLevel::ERROR, "start dhcp error");
            return CascadeResult::Continue;
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
            return CascadeResult::Continue;
        }

        if (service::isMdnsEnabled(config)) {
            auto mdns_params = service::buildMdnsParams(config, interface_name, ip_address, http_port, rtsp_port);
            if (!service::MdnsService::getInstance()->start(mdns_params)) {
                Logger::log(LogLevel::ERROR, "Failed to start mDNS service");
                return CascadeResult::Continue;
            }
        } else {
            elog_i("MDNS", "mDNS disabled by config");
        }

        // 启动 HTTP Server 替代 RemoteCtrlClient
        HttpServerConfig httpConfig = {static_cast<int>(http_port), nullptr, 2};
        if (http_server_init(&httpConfig) != 0) {
            Logger::log(LogLevel::ERROR, "Failed to init HTTP server");
            service::MdnsService::getInstance()->stop();
            return CascadeResult::Continue;
        }
        if (http_server_start() != 0) {
            Logger::log(LogLevel::ERROR, "Failed to start HTTP server");
            http_server_deinit();
            service::MdnsService::getInstance()->stop();
            return CascadeResult::Continue;
        }
        if (!service::TcpEventService::getInstance()->start(service::kDefaultTcpEventPort)) {
            Logger::log(LogLevel::WARNING, "Failed to start TCP event server on port %u",
                        service::kDefaultTcpEventPort);
        }
        elog_i("MDNS", "HTTP server started on port %u for interface %s (%s)",
               http_port, interface_name.c_str(), ip_address.c_str());

        if (ctx.mobileRtspEnabled) {
            ctx.lc.markRtspSingletonUsed();
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
                return CascadeResult::Continue;
            }
        } else {
            Logger::log(LogLevel::INFO, "RTSP server disabled in mobile mode");
        }

        while (ctx.lc.keepRunning()) {
            (void)ctx.lc.waitForSignal(1000);
        }

        service::MdnsService::getInstance()->stop();

        // 停止 RTSP Server
        if (ctx.mobileRtspEnabled) {
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
        ctx.lc.markRtspSingletonUsed();
        if (!app_workmode::runRtspServerUntilSignal(rtsp_port,
                [&ctx]{ return ctx.lc.keepRunning(); },
                [&ctx](int ms){ (void)ctx.lc.waitForSignal(ms); })) {
            return CascadeResult::Continue;
        }
    }

    if (command & CMD_AUTH || command & CMD_HEARTBEAT || command & CMD_UPLOAD) {
        auto mgmtServerAddr = config->get(INI_SECTION_SERVER, INI_KEY_MS_IP, "");
        Logger::log(LogLevel::INFO, "mgmtServerAddr: %s", mgmtServerAddr.c_str());
        auto mgmtServerPort = config->get(INI_SECTION_SERVER, INI_KEY_MS_PORT, 0);
        Logger::log(LogLevel::INFO, "mgmtServerPort: %d", mgmtServerPort);

        ctx.mgmtServClient = std::make_shared<MgmtServClient>(mgmtServerAddr, mgmtServerPort);
        if (EC_SUCCESS != ctx.mgmtServClient->connect(3000)) {
            Logger::log(LogLevel::ERROR, "connect [%s:%d] failed", mgmtServerAddr.c_str(), mgmtServerPort);
            return CascadeResult::Continue;
        } else {
            Logger::log(LogLevel::INFO, "connect [%s:%d] success", mgmtServerAddr.c_str(), mgmtServerPort);
        }

        if (EC_SUCCESS != ctx.mgmtServClient->authenticate()) {
            Logger::log(LogLevel::ERROR, "auth failed");
            return CascadeResult::Continue;
        } else {
            Logger::log(LogLevel::INFO, "auth success");
        }
    }

    if (command & CMD_HEARTBEAT) {
        ctx.mgmtServClient->sendHeartbeat();
    }

    if (command & CMD_UPLOAD) {
        //assume we have a storage server same as mgmt server
        ctx.storageServClient = ctx.mgmtServClient->newStorageServClient();
        if (ctx.lc.rgbLed()) {
            ctx.lc.rgbLed()->setConstant(GPIO_VALUE::HIGH);
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
                ctx.storageServClient->bindUploadCallback([&descfile_uploaded, target_filename](const std::string &filename, int error_code) {
                    Logger::log(LogLevel::INFO, "upload descfile [%s], error code: %d", filename.c_str(), error_code);
                    if (filename == target_filename) {
                        descfile_uploaded = (error_code==EC_SUCCESS)?true:false;
                    }
                });
                ctx.storageServClient->uploadFile(desc_filename);
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
                ctx.storageServClient->bindUploadCallback([&uploaded_file_list, &allFileUploaded](const std::string &filename, int error_code) {
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
                            ctx.storageServClient->uploadFile(pathname);
                        }
                    }
                }

                while (!ctx.storageServClient->isUploadFinished()) {
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

    return CascadeResult::Continue;
}

int workModeToCommand(enum workingMode mode, WorkModeContext& ctx)
{
    int command = CMD_HELP;
    switch (mode) {
        case WORKING_MODE_SNAP_ONLY:
            command = CMD_SNAP;
            break;
        case WORKING_MODE_UPLOAD_ONLY:
            command = CMD_CONN_NET | CMD_DHCP | CMD_NTP | CMD_UPLOAD;
            if (ctx.lc.rgbLed()) {
                ctx.lc.rgbLed()->asyncBlink(60);
            }
            break;
        case WORKING_MODE_TEST_ONLY:
            {
                if (ctx.lc.rgbLed()) {
                    ctx.lc.rgbLed()->asyncBlink(30);
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
            // Invalid mode: return CMD_HELP. runWorkMode's default case still
            // owns the error log + sleep + TerminalExit (behavior-preserving);
            // callers that only need the command bitmap get a sentinel they
            // can decide on. Side effects (RGB blink) above are byte-identical
            // to the factored switch.
            command = CMD_HELP;
            break;
    }
    return command;
}

CascadeResult runWorkMode(enum workingMode mode, WorkModeContext& ctx)
{
    int command = workModeToCommand(mode, ctx);
    if (command == CMD_HELP) {
        Logger::log(LogLevel::ERROR, "%s Invalid working mode %d, power off", __func__, mode);
        //Power::getInstance()->requestShutdown();
        sleep(10);
        return CascadeResult::TerminalExit;
    }

    return runCommands(command, ctx);
}

}  // namespace app_workmode
