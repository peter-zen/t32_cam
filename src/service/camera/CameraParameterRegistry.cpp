#include "CameraParameterRegistry.h"

#include "../../common/Common.h"

#include <algorithm>

namespace service {

namespace {

ParameterDefinition makeParam(const std::string& id,
                              const std::string& rawName,
                              const std::string& displayName,
                              ParameterClassification classification,
                              const std::string& group,
                              const std::string& chapter,
                              ParameterValueType type,
                              ParameterPermission permission,
                              const Json::Value& defaultValue,
                              ParameterStorageBinding storage,
                              ParameterAvailability availability) {
    ParameterDefinition definition;
    definition.id = id;
    definition.rawName = rawName;
    definition.displayName = displayName;
    definition.classification = classification;
    definition.group = group;
    definition.chapter = chapter;
    definition.type = type;
    definition.permission = permission;
    definition.defaultValue = defaultValue;
    definition.storage = storage;
    definition.availability = availability;
    return definition;
}

ParameterStorageBinding settingsBinding(const std::string& member) {
    ParameterStorageBinding binding;
    binding.kind = ParameterStorageKind::SETTINGS;
    binding.member = member;
    return binding;
}

ParameterStorageBinding deviceConfigBinding(const std::string& section, const std::string& key) {
    ParameterStorageBinding binding;
    binding.kind = ParameterStorageKind::DEVICE_CONFIG;
    binding.section = section;
    binding.key = key;
    return binding;
}

ParameterStorageBinding placeholderBinding() {
    ParameterStorageBinding binding;
    binding.kind = ParameterStorageKind::PLACEHOLDER;
    return binding;
}

ParameterStorageBinding computedBinding(const std::string& member) {
    ParameterStorageBinding binding;
    binding.kind = ParameterStorageKind::COMPUTED;
    binding.member = member;
    return binding;
}

void addOption(ParameterDefinition& definition, int value) {
    definition.options.push_back(Json::Value(value));
}

void addOption(ParameterDefinition& definition, const char* value) {
    definition.options.push_back(Json::Value(value));
}

void setRange(ParameterDefinition& definition, int min, int max, int step, const std::string& unit = "") {
    definition.range.enabled = true;
    definition.range.min = min;
    definition.range.max = max;
    definition.range.step = step;
    definition.unit = unit;
}

void dependsOn(ParameterDefinition& definition, const std::string& switchName, const std::string& reason) {
    definition.dependency.switchName = switchName;
    definition.dependency.expectedValue = 1;
    definition.dependency.disabledReason = reason;
}

void addFactory(std::vector<ParameterDefinition>& defs,
                const std::string& group,
                const std::string& rawName,
                ParameterValueType type,
                const Json::Value& defaultValue,
                ParameterStorageBinding storage,
                const std::string& displayName = "") {
    defs.push_back(makeParam("factory." + group + "." + rawName,
                             rawName,
                             displayName.empty() ? rawName : displayName,
                             ParameterClassification::FACTORY,
                             group,
                             "2",
                             type,
                             ParameterPermission::FACTORY,
                             defaultValue,
                             storage,
                             storage.kind == ParameterStorageKind::PLACEHOLDER ? ParameterAvailability::PLACEHOLDER
                                                                                : ParameterAvailability::PERSISTED));
}

ParameterDefinition property(const std::string& group,
                             const std::string& rawName,
                             ParameterValueType type,
                             const Json::Value& defaultValue,
                             ParameterStorageBinding storage,
                             const std::string& displayName = "") {
    return makeParam("property." + group + "." + rawName,
                     rawName,
                     displayName.empty() ? rawName : displayName,
                     ParameterClassification::PROPERTY,
                     group,
                     "3/4",
                     type,
                     ParameterPermission::READ_WRITE,
                     defaultValue,
                     storage,
                     storage.kind == ParameterStorageKind::PLACEHOLDER ? ParameterAvailability::PLACEHOLDER
                                                                        : ParameterAvailability::PERSISTED);
}

void addStatus(std::vector<ParameterDefinition>& defs,
               const std::string& group,
               const std::string& rawName,
               ParameterValueType type,
               const Json::Value& defaultValue,
               ParameterStorageBinding storage,
               ParameterAvailability availability,
               const std::string& displayName = "") {
    defs.push_back(makeParam("status." + group + "." + rawName,
                             rawName,
                             displayName.empty() ? rawName : displayName,
                             ParameterClassification::STATUS,
                             group,
                             "5",
                             type,
                             ParameterPermission::READ,
                             defaultValue,
                             storage,
                             availability));
}

std::vector<ParameterDefinition> buildDefinitions() {
    std::vector<ParameterDefinition> defs;

    addFactory(defs, "BOOT", "PType", ParameterValueType::NUMBER, 0,
               deviceConfigBinding(INI_SECTION_BOOT, INI_KEY_PTYPE), "网络类型");
    addFactory(defs, "BOOT", "PModel", ParameterValueType::STRING, "SCT000",
               deviceConfigBinding(INI_SECTION_BOOT, INI_KEY_PMODEL), "产品型号");
    addFactory(defs, "BOOT", "PName", ParameterValueType::STRING, "智感相机",
               deviceConfigBinding(INI_SECTION_BOOT, INI_KEY_PNAME), "产品名称");
    addFactory(defs, "BOOT", "PCompany", ParameterValueType::STRING, "SEESUNG",
               deviceConfigBinding(INI_SECTION_BOOT, INI_KEY_PCOMPANT), "品牌");
    addFactory(defs, "BOOT", "LED_Mode", ParameterValueType::NUMBER, 0,
               placeholderBinding(), "夜间灯光模式");

    addFactory(defs, "DEVICE", "PMAC", ParameterValueType::STRING, "",
               deviceConfigBinding(INI_SECTION_BOOT, INI_KEY_PMAC), "产品MAC地址");
    addFactory(defs, "DEVICE", "PID", ParameterValueType::STRING, "SCT00000S26000000",
               deviceConfigBinding(INI_SECTION_DEVICE, INI_KEY_PID), "产品PID");
    addFactory(defs, "DEVICE", "CSSID", ParameterValueType::STRING, "SCT",
               deviceConfigBinding(INI_SECTION_DEVICE, INI_KEY_CSSID), "热点SSID");
    addFactory(defs, "DEVICE", "CPWD", ParameterValueType::STRING, "12345678",
               deviceConfigBinding(INI_SECTION_DEVICE, INI_KEY_CPWD), "热点密码");
    addFactory(defs, "DEVICE", "UPID", ParameterValueType::STRING, "Null",
               deviceConfigBinding(INI_SECTION_SYS, INI_KEY_UPID), "上级网络设备连接PID");
    addFactory(defs, "DEVICE", "UPWD", ParameterValueType::STRING, "12345678",
               deviceConfigBinding(INI_SECTION_SYS, INI_KEY_UPWD), "上级网络设备连接密码");

    addFactory(defs, "SERVER", "TP_Setting", ParameterValueType::STRING, "10",
               placeholderBinding(), "传输协议选择");
    addFactory(defs, "SERVER", "M_Server", ParameterValueType::STRING, "www.aidetcloud.com:8899",
               deviceConfigBinding(INI_SECTION_SERVER, INI_KEY_MS_IP), "管理服务器地址");
    addFactory(defs, "SERVER", "NTP_Server", ParameterValueType::STRING, "www.aidetcloud.com:123",
               deviceConfigBinding(INI_SECTION_SERVER, INI_KEY_NTP_IP), "NTP服务器");
    addFactory(defs, "SERVER", "NTP_Timezone", ParameterValueType::STRING, "+8",
               deviceConfigBinding(INI_SECTION_NTP, INI_KEY_TIMEZONE), "时区");
    addFactory(defs, "SERVER", "B_Server", ParameterValueType::STRING, "0",
               deviceConfigBinding(INI_SECTION_SERVER, INI_KEY_FS_IP), "备用服务器地址");
    addFactory(defs, "SERVER", "AI_Server", ParameterValueType::STRING, "0",
               placeholderBinding(), "AI服务器地址");

    const char* functionNames[] = {"Photo_DS_EN",     "Photo_Size_MAX",  "Photo_Burst_MAX",
                                   "Video_DS_EN",     "Video_Size_MAX",  "Video_Length_MAX",
                                   "Data_ASD_EN",     "Data_AUD_EN",     "AI_Alarm_SET",
                                   "BServer_EN",      "RWakeup _SET",    "RWakeup _HB",
                                   "Smart_DCVR_EN",   "Video_ATR_EN",    "DHCP_EN",
                                   "Stamp_EN",        "VTS_Alarm_EN",    "Audio_SPK_EN",
                                   "Timer_Range_MAX", "Upload_EN"};
    for (const char* name : functionNames) {
        addFactory(defs, "Functions", name, ParameterValueType::NUMBER,
                   (std::string(name) == "Timer_Range_MAX") ? Json::Value(3) : Json::Value(0),
                   deviceConfigBinding("Functions", name), name);
    }

    ParameterDefinition camMode = property("Camera_Setting", "CAM_Mode", ParameterValueType::NUMBER, 0,
                                           settingsBinding("cameraMode"), "拍摄模式");
    camMode.legacyAliases.push_back("camera_mode");
    for (int value = 0; value <= 5; ++value) addOption(camMode, value);
    defs.push_back(camMode);

    ParameterDefinition imageSize = property("Camera_Setting", "CAM_ImageSize", ParameterValueType::STRING, "4M",
                                             settingsBinding("stillSize"), "照片大小");
    addOption(imageSize, "2M");
    addOption(imageSize, "4M");
#if !defined(SENSOR_TYPE_GC4653) && !defined(SENSOR_TYPE_SC4336P)
    addOption(imageSize, "5M");
    addOption(imageSize, "6M");
#endif
    addOption(imageSize, "8M");
    addOption(imageSize, "16M");
    addOption(imageSize, "24M");
    addOption(imageSize, "32M");
    addOption(imageSize, "42M");
    defs.push_back(imageSize);

    ParameterDefinition imageQuality = property("Camera_Setting", "CAM_ImageQuality", ParameterValueType::NUMBER, 3,
                                                settingsBinding("stillQuality"), "照片质量");
    dependsOn(imageQuality, "Photo_DS_EN", "Photo_DS_EN is disabled");
    setRange(imageQuality, 1, 3, 1);
    defs.push_back(imageQuality);

    ParameterDefinition shootingP = property("Camera_Setting", "CAM_Shooting_P", ParameterValueType::NUMBER, 1,
                                             settingsBinding("burstNumber"), "照片连拍");
    setRange(shootingP, 1, 10, 1);
    defs.push_back(shootingP);

    ParameterDefinition shootingInt = property("Camera_Setting", "CAM_Shooting_INT", ParameterValueType::NUMBER, 100,
                                               settingsBinding("shootingInterval"), "照片连拍间隔");
    dependsOn(shootingInt, "Photo_DS_EN", "Photo_DS_EN is disabled");
    setRange(shootingInt, 100, 2000, 100, "ms");
    defs.push_back(shootingInt);

    ParameterDefinition fixedShutter = property("Camera_Setting", "CAM_ Ffixed_Shutter", ParameterValueType::NUMBER, 0,
                                                placeholderBinding(), "固定快门速度");
    dependsOn(fixedShutter, "Photo_DS_EN", "Photo_DS_EN is disabled");
    setRange(fixedShutter, 0, 5000, 1);
    defs.push_back(fixedShutter);

    ParameterDefinition minShutter = property("Camera_Setting", "CAM_ Min_Shutter", ParameterValueType::NUMBER, 15,
                                              placeholderBinding(), "最低快门速度");
    dependsOn(minShutter, "Photo_DS_EN", "Photo_DS_EN is disabled");
    addOption(minShutter, 10);
    addOption(minShutter, 15);
    addOption(minShutter, 25);
    addOption(minShutter, 50);
    addOption(minShutter, 100);
    addOption(minShutter, 200);
    defs.push_back(minShutter);

    ParameterDefinition videoSize = property("Camera_Setting", "Video_Size", ParameterValueType::STRING, "2K/30FPS",
                                             settingsBinding("videoSize"), "视频大小");
    videoSize.legacyAliases.push_back("resolution");
    videoSize.legacyAliases.push_back("fps");
    videoSize.legacyAliases.push_back("CAM_VideoSize");
    addOption(videoSize, "720P/30FPS");
    addOption(videoSize, "720P/60FPS");
    addOption(videoSize, "1080P/30FPS");
    addOption(videoSize, "1080P/60FPS");
    addOption(videoSize, "2K/30FPS");
    addOption(videoSize, "4K/30FPS");
    defs.push_back(videoSize);

    ParameterDefinition videoEncoded = property("Camera_Setting", "Video_Encoded", ParameterValueType::NUMBER, 1,
                                                settingsBinding("videoCodec"), "视频编码");
    dependsOn(videoEncoded, "Video_DS_EN", "Video_DS_EN is disabled");
    addOption(videoEncoded, 1);
    addOption(videoEncoded, 2);
    defs.push_back(videoEncoded);

    ParameterDefinition bitrateType = property("Camera_Setting", "Video_Bitrate_Type", ParameterValueType::NUMBER, 1,
                                               settingsBinding("videoRcMode"), "视频码率");
    dependsOn(bitrateType, "Video_DS_EN", "Video_DS_EN is disabled");
    for (int value = 1; value <= 4; ++value) addOption(bitrateType, value);
    defs.push_back(bitrateType);

    ParameterDefinition bitrateValue = property("Camera_Setting", "Video_Bitrate_Value", ParameterValueType::NUMBER, 4000,
                                                placeholderBinding(), "码率值");
    bitrateValue.legacyAliases.push_back("bitrate");
    dependsOn(bitrateValue, "Video_DS_EN", "Video_DS_EN is disabled");
    setRange(bitrateValue, 100, 16000, 100, "Kbps");
    defs.push_back(bitrateValue);

    ParameterDefinition videoLength = property("Camera_Setting", "Video_Length", ParameterValueType::NUMBER, 10,
                                               placeholderBinding(), "视频长度");
    videoLength.legacyAliases.push_back("max_record_duration");
    setRange(videoLength, 1, 600, 1, "s");
    defs.push_back(videoLength);

    ParameterDefinition audioVolume = property("Audio_Setting", "Audio_SPK_Volume", ParameterValueType::NUMBER, 50,
                                               deviceConfigBinding(INI_SECTION_DEVICE, INI_KEY_SPKVOL), "喇叭音量");
    dependsOn(audioVolume, "Audio_SPK_EN", "Audio_SPK_EN is disabled");
    setRange(audioVolume, 0, 100, 10);
    defs.push_back(audioVolume);

    ParameterDefinition recordVolume = property("Audio_Setting", "Audio_Record_Volume", ParameterValueType::NUMBER, 80,
                                                 settingsBinding("audioRecordVolume"), "录音音量");
    setRange(recordVolume, 0, 100, 5);
    defs.push_back(recordVolume);

    ParameterDefinition recordGain = property("Audio_Setting", "Audio_Record_Gain", ParameterValueType::NUMBER, 28,
                                               settingsBinding("audioRecordGain"), "录音增益");
    setRange(recordGain, 0, 31, 1);
    defs.push_back(recordGain);

    ParameterDefinition pirMode = property("PIR_Setting", "PIR_Mode", ParameterValueType::NUMBER, 0,
                                           settingsBinding("pirEn"), "PIR模式");
    pirMode.legacyAliases.push_back("pir_enabled");
    addOption(pirMode, 0);
    addOption(pirMode, 1);
    addOption(pirMode, 2);
    defs.push_back(pirMode);

    ParameterDefinition pirSensitivity = property("PIR_Setting", "PIR_Sensitivity", ParameterValueType::NUMBER, 0,
                                                  settingsBinding("ckPirSensitivity"), "PIR灵敏度");
    pirSensitivity.legacyAliases.push_back("pir_sensitivity");
    for (int value = 0; value <= 3; ++value) addOption(pirSensitivity, value);
    defs.push_back(pirSensitivity);

    ParameterDefinition pirInterval = property("PIR_Setting", "PIR_Interval", ParameterValueType::STRING, "00:10",
                                               placeholderBinding(), "触发时间间隔");
    defs.push_back(pirInterval);

    ParameterDefinition pirLimit = property("PIR_Setting", "PIR_MaxShooting", ParameterValueType::NUMBER, 0,
                                            settingsBinding("shootingLimits"), "拍摄限制");
    setRange(pirLimit, 0, 255, 1);
    defs.push_back(pirLimit);

    ParameterDefinition timerEnable = property("Timer_Setting", "Timer_Enable", ParameterValueType::NUMBER, 0,
                                               settingsBinding("timerEn"), "定时");
    addOption(timerEnable, 0);
    addOption(timerEnable, 1);
    defs.push_back(timerEnable);

    defs.push_back(property("Timer_Setting", "Timer_PIR_Enable", ParameterValueType::NUMBER, 1,
                            placeholderBinding(), "PIR开关"));
    defs.push_back(property("Timer_Setting", "Timer_Interval_Time", ParameterValueType::STRING, "00:01",
                            settingsBinding("timerLapse"), "循环拍摄间隔"));
    defs.push_back(property("Timer_Setting", "Timer_1Start", ParameterValueType::STRING, "00:00",
                            settingsBinding("timer1s"), "时间段1-开始"));
    defs.push_back(property("Timer_Setting", "Timer_1End", ParameterValueType::STRING, "23:59",
                            settingsBinding("timer1e"), "时间段1-结束"));
    const char* timerNames[] = {"Timer_2Start", "Timer_2End", "Timer_3Start", "Timer_3End",
                                "Timer_4Start", "Timer_4End", "Timer_5Start", "Timer_5End"};
    for (const char* name : timerNames) {
        ParameterDefinition timer = property("Timer_Setting", name, ParameterValueType::STRING, "00:00",
                                             placeholderBinding(), name);
        dependsOn(timer, "Timer_Range_MAX", "Timer range is disabled by Timer_Range_MAX");
        defs.push_back(timer);
    }
    defs.push_back(property("Timer_Setting", "Timer_Repeats", ParameterValueType::STRING, "1111111",
                            settingsBinding("weekRepeats"), "重复"));

    defs.push_back(property("Network_Setting", "CSSID", ParameterValueType::STRING, "SCTCAM",
                            deviceConfigBinding(INI_SECTION_DEVICE, INI_KEY_CSSID), "热点名称"));
    defs.push_back(property("Network_Setting", "CPWR", ParameterValueType::STRING, "12345678",
                            deviceConfigBinding(INI_SECTION_DEVICE, INI_KEY_CPWD), "热点密码"));
    defs.push_back(property("Network_Setting", "UPID", ParameterValueType::STRING, "NULL",
                            deviceConfigBinding(INI_SECTION_SYS, INI_KEY_UPID), "上级设备PID"));
    defs.push_back(property("Network_Setting", "UPWR", ParameterValueType::STRING, "12345678",
                            deviceConfigBinding(INI_SECTION_SYS, INI_KEY_UPWD), "上级设备密码"));
    const char* networkPlaceholders[] = {"DHCP_ON", "LOCAL_IP", "NETMASK", "GATEWAY", "DNS1", "DNS2"};
    for (const char* name : networkPlaceholders) {
        ParameterDefinition network = property("Network_Setting", name, ParameterValueType::STRING, "0",
                                               placeholderBinding(), name);
        dependsOn(network, "DHCP_EN", "DHCP_EN is disabled");
        defs.push_back(network);
    }

    defs.push_back(property("Server_Setting", "M_Server", ParameterValueType::STRING, "www.aidetcloud.com:8899",
                            deviceConfigBinding(INI_SECTION_SERVER, INI_KEY_MS_IP), "管理服务器"));
    defs.push_back(property("Server_Setting", "NTP_Server", ParameterValueType::STRING, "www.aidetcloud.com:123",
                            deviceConfigBinding(INI_SECTION_SERVER, INI_KEY_NTP_IP), "NTP服务器"));
    defs.push_back(property("Server_Setting", "NTP_Timezone", ParameterValueType::STRING, "+8",
                            deviceConfigBinding(INI_SECTION_NTP, INI_KEY_TIMEZONE), "时区"));
    ParameterDefinition bsServer = property("Server_Setting", "BS_Server", ParameterValueType::STRING, "0",
                                            deviceConfigBinding(INI_SECTION_SERVER, INI_KEY_FS_IP), "备用服务器");
    dependsOn(bsServer, "BServer_EN", "BServer_EN is disabled");
    defs.push_back(bsServer);
    ParameterDefinition aiServer = property("Server_Setting", "AI_Server", ParameterValueType::STRING, "0",
                                            placeholderBinding(), "AI服务器");
    dependsOn(aiServer, "AI_Alarm_SET", "AI_Alarm_SET is disabled");
    defs.push_back(aiServer);

    defs.push_back(property("System_Setting", "Device_Name", ParameterValueType::STRING, "1",
                            settingsBinding("devName"), "设备名称"));
    defs.push_back(property("System_Setting", "GPS_ Enable", ParameterValueType::NUMBER, 1,
                            placeholderBinding(), "定位开关"));
    defs.push_back(property("System_Setting", "GPS_Value", ParameterValueType::STRING, "0,0,0",
                            placeholderBinding(), "地理位置"));
    ParameterDefinition stamp = property("System_Setting", "Stamp", ParameterValueType::NUMBER, 1,
                                         settingsBinding("stampEn"), "信息戳记");
    stamp.legacyAliases.push_back("timestamp_overlay");
    addOption(stamp, 0);
    addOption(stamp, 1);
    defs.push_back(stamp);
    ParameterDefinition stampList = property("System_Setting", "Stamp_List", ParameterValueType::NUMBER, 11111111,
                                             placeholderBinding(), "信息戳记内容");
    dependsOn(stampList, "Stamp_EN", "Stamp_EN is disabled");
    defs.push_back(stampList);
    ParameterDefinition cycle = property("System_Setting", "Cycle", ParameterValueType::NUMBER, 1,
                                         settingsBinding("autoCover"), "自动覆盖");
    cycle.legacyAliases.push_back("loop_recording");
    defs.push_back(cycle);

    const char* uploadProps[] = {"Upload_Protocol", "Upload_Order", "Upload_Mode", "Upload_NUFQ", "Upload_Delete"};
    for (const char* name : uploadProps) {
        ParameterDefinition upload = property("System_Setting", name, ParameterValueType::STRING, "",
                                              placeholderBinding(), name);
        dependsOn(upload, "Upload_EN", "Upload_EN is disabled");
        defs.push_back(upload);
    }
    ParameterDefinition dataSuicide = property("System_Setting", "Data_Suicide", ParameterValueType::NUMBER, 0,
                                               placeholderBinding(), "数据自毁");
    dependsOn(dataSuicide, "Data_ASD_EN", "Data_ASD_EN is disabled");
    defs.push_back(dataSuicide);
    defs.push_back(property("System_Setting", "HeartRate", ParameterValueType::NUMBER, 86400,
                            settingsBinding("heartRate"), "心跳间隔"));
    ParameterDefinition vts = property("System_Setting", "VTS_Sensitivity", ParameterValueType::NUMBER, 2,
                                       placeholderBinding(), "振动灵敏度");
    dependsOn(vts, "VTS_Alarm_EN", "VTS_Alarm_EN is disabled");
    defs.push_back(vts);
    ParameterDefinition remoteWakeup = property("System_Setting", "Remote_Wakeup", ParameterValueType::NUMBER, 0,
                                                settingsBinding("remote_wakeup"), "远程唤醒");
    dependsOn(remoteWakeup, "RWakeup _SET", "RWakeup _SET is disabled");
    defs.push_back(remoteWakeup);

    const char* aiProps[] = {"Target_List", "Focus_Target_List", "Recognition_Rate", "Target_Quantity_Enable",
                             "Increment_Enable", "AI_Enable", "AI_Alarm_Enable"};
    for (const char* name : aiProps) {
        ParameterDefinition ai = property("AI_Setting", name,
                                          std::string(name).find("List") != std::string::npos ? ParameterValueType::STRING_ARRAY
                                                                                               : ParameterValueType::NUMBER,
                                          std::string(name).find("List") != std::string::npos ? Json::Value(Json::arrayValue)
                                                                                               : Json::Value(0),
                                          placeholderBinding(), name);
        dependsOn(ai, "AI_Alarm_SET", "AI_Alarm_SET is disabled");
        defs.push_back(ai);
    }

    addStatus(defs, "Device", "PID", ParameterValueType::STRING, "SCT00000S26000000",
              deviceConfigBinding(INI_SECTION_DEVICE, INI_KEY_PID), ParameterAvailability::PERSISTED, "设备PID");
    addStatus(defs, "Device", "DUID", ParameterValueType::STRING, "0",
              settingsBinding("duid"), ParameterAvailability::PERSISTED, "设备DUID");
    addStatus(defs, "Device", "Device_Name", ParameterValueType::STRING, "",
              settingsBinding("devName"), ParameterAvailability::PERSISTED, "设备名称");
    addStatus(defs, "Device", "PCompany", ParameterValueType::STRING, "SEESUNG",
              deviceConfigBinding(INI_SECTION_BOOT, INI_KEY_PCOMPANT), ParameterAvailability::PERSISTED, "品牌");
    addStatus(defs, "Device", "PModel", ParameterValueType::STRING, "SCT00000",
              deviceConfigBinding(INI_SECTION_BOOT, INI_KEY_PMODEL), ParameterAvailability::PERSISTED, "产品型号");
    addStatus(defs, "Device", "PName", ParameterValueType::STRING, "智感相机",
              deviceConfigBinding(INI_SECTION_BOOT, INI_KEY_PNAME), ParameterAvailability::PERSISTED, "产品名称");
    addStatus(defs, "Device", "FW_Version", ParameterValueType::STRING, "V000.00.000-000000",
              computedBinding("fw_version"), ParameterAvailability::COMPUTED, "固件版本号");
    addStatus(defs, "Device", "MCU_Version", ParameterValueType::STRING, "V00.000",
              placeholderBinding(), ParameterAvailability::PLACEHOLDER, "MCU版本号");
    addStatus(defs, "Device", "Location_LON", ParameterValueType::STRING, "0",
              placeholderBinding(), ParameterAvailability::PLACEHOLDER, "坐标-经度");
    addStatus(defs, "Device", "Location_LAT", ParameterValueType::STRING, "0",
              placeholderBinding(), ParameterAvailability::PLACEHOLDER, "坐标-纬度");
    addStatus(defs, "Device", "Location_ELE", ParameterValueType::STRING, "0",
              placeholderBinding(), ParameterAvailability::PLACEHOLDER, "坐标-高程");
    addStatus(defs, "Device", "Battery_Type", ParameterValueType::NUMBER, 2,
              placeholderBinding(), ParameterAvailability::PLACEHOLDER, "电池组类型");
    addStatus(defs, "Device", "Battery1", ParameterValueType::NUMBER, 0,
              placeholderBinding(), ParameterAvailability::PLACEHOLDER, "电池组1电压");
    addStatus(defs, "Device", "Battery2", ParameterValueType::NUMBER, 0,
              placeholderBinding(), ParameterAvailability::PLACEHOLDER, "电池组2电压");
    addStatus(defs, "Device", "EPower", ParameterValueType::NUMBER, 0,
              placeholderBinding(), ParameterAvailability::PLACEHOLDER, "外部电源");
    addStatus(defs, "Device", "SPower", ParameterValueType::NUMBER, 0,
              placeholderBinding(), ParameterAvailability::PLACEHOLDER, "太阳能板");
    addStatus(defs, "Device", "Device_MAC", ParameterValueType::STRING, "00:00:00:00:00:00",
              placeholderBinding(), ParameterAvailability::PLACEHOLDER, "MAC地址");
    addStatus(defs, "Device", "Device_IP", ParameterValueType::STRING, "0.0.0.0",
              placeholderBinding(), ParameterAvailability::PLACEHOLDER, "设备IP地址");
    addStatus(defs, "Device", "Device_IMEI", ParameterValueType::STRING, "0",
              placeholderBinding(), ParameterAvailability::PLACEHOLDER, "设备识别码");
    addStatus(defs, "Device", "Device_NO", ParameterValueType::STRING, "0",
              placeholderBinding(), ParameterAvailability::PLACEHOLDER, "卡号");
    addStatus(defs, "Device", "Event_Total", ParameterValueType::NUMBER, 0,
              placeholderBinding(), ParameterAvailability::PLACEHOLDER, "总事件数量");
    addStatus(defs, "Device", "Event_NUFQ", ParameterValueType::NUMBER, 0,
              placeholderBinding(), ParameterAvailability::PLACEHOLDER, "未上传数量");

    const char* signalStatus[] = {"Signal_Type", "Signal_CF", "Signal_TP", "Signal_BW", "Signal_RSSI",
                                  "Signal_RSRP", "Signal_RSRQ", "Signal_RL", "Signal_SNR", "Signal_TD"};
    for (const char* name : signalStatus) {
        addStatus(defs, "Signal", name, ParameterValueType::STRING, "0",
                  placeholderBinding(), ParameterAvailability::PLACEHOLDER, name);
    }

    const char* sensorStatus[] = {"Sensor_CDS", "Sensor_TEMPS", "Sensor_RHS", "Sensor_APS", "Sensor_AL",
                                  "Sensor_UVL", "Sensor_NOISE", "Sensor_CO", "Sensor_CO2", "Sensor_O2"};
    for (const char* name : sensorStatus) {
        addStatus(defs, "Sensor", name, ParameterValueType::NUMBER, 0,
                  placeholderBinding(), ParameterAvailability::PLACEHOLDER, name);
    }

    return defs;
}

bool matchesName(const ParameterDefinition& definition, const std::string& name, bool includeAliases) {
    if (definition.rawName == name || definition.id == name) {
        return true;
    }
    if (!includeAliases) {
        return false;
    }
    return std::find(definition.legacyAliases.begin(), definition.legacyAliases.end(), name) !=
           definition.legacyAliases.end();
}

} // namespace

const std::vector<ParameterDefinition>& getCameraParameterDefinitions() {
    static const std::vector<ParameterDefinition> definitions = buildDefinitions();
    return definitions;
}

std::vector<const ParameterDefinition*> getParameterDefinitions(ParameterClassification classification,
                                                               const std::string& group) {
    std::vector<const ParameterDefinition*> result;
    for (const auto& definition : getCameraParameterDefinitions()) {
        if (definition.classification != classification) {
            continue;
        }
        if (!group.empty() && group != "all" && definition.group != group) {
            continue;
        }
        result.push_back(&definition);
    }
    return result;
}

const ParameterDefinition* findParameterDefinition(const std::string& name,
                                                   ParameterClassification classification,
                                                   bool includeAliases) {
    for (const auto& definition : getCameraParameterDefinitions()) {
        if (definition.classification == classification && matchesName(definition, name, includeAliases)) {
            return &definition;
        }
    }
    return nullptr;
}

const ParameterDefinition* findAnyParameterDefinition(const std::string& name, bool includeAliases) {
    for (const auto& definition : getCameraParameterDefinitions()) {
        if (matchesName(definition, name, includeAliases)) {
            return &definition;
        }
    }
    return nullptr;
}

const char* parameterClassificationToString(ParameterClassification classification) {
    switch (classification) {
    case ParameterClassification::FACTORY:
        return "factory";
    case ParameterClassification::PROPERTY:
        return "property";
    case ParameterClassification::STATUS:
        return "status";
    case ParameterClassification::COMMAND:
        return "command";
    default:
        return "unknown";
    }
}

const char* parameterValueTypeToString(ParameterValueType type) {
    switch (type) {
    case ParameterValueType::NUMBER:
        return "number";
    case ParameterValueType::STRING:
        return "string";
    case ParameterValueType::BOOLEAN:
        return "bool";
    case ParameterValueType::STRING_ARRAY:
        return "string_array";
    case ParameterValueType::COMMAND:
        return "command";
    default:
        return "unknown";
    }
}

const char* parameterPermissionToString(ParameterPermission permission) {
    switch (permission) {
    case ParameterPermission::FACTORY:
        return "factory";
    case ParameterPermission::READ:
        return "read";
    case ParameterPermission::WRITE:
        return "write";
    case ParameterPermission::READ_WRITE:
        return "read_write";
    case ParameterPermission::COMMAND:
        return "command";
    default:
        return "unknown";
    }
}

const char* parameterAvailabilityToString(ParameterAvailability availability) {
    switch (availability) {
    case ParameterAvailability::REAL:
        return "real";
    case ParameterAvailability::PERSISTED:
        return "persisted";
    case ParameterAvailability::COMPUTED:
        return "computed";
    case ParameterAvailability::PLACEHOLDER:
        return "placeholder";
    default:
        return "unknown";
    }
}

const char* parameterStorageKindToString(ParameterStorageKind kind) {
    switch (kind) {
    case ParameterStorageKind::NONE:
        return "none";
    case ParameterStorageKind::SETTINGS:
        return "settings";
    case ParameterStorageKind::DEVICE_CONFIG:
        return "device_config";
    case ParameterStorageKind::COMPUTED:
        return "computed";
    case ParameterStorageKind::PLACEHOLDER:
        return "placeholder";
    case ParameterStorageKind::COMMAND:
        return "command";
    default:
        return "unknown";
    }
}

} // namespace service
