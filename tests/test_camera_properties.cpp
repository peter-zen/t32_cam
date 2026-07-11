#include "../src/config/env/EnvManager.h"
#include "../src/config/devconf/DeviceConfig.h"
#include "../src/config/devconf/ProductConfig.h"
#include "../src/config/setting/Settings.h"
#include "../src/service/camera/CameraFactoryConfigImporter.h"
#include "../src/service/camera/CameraParameterRegistry.h"
#include "../src/service/camera/CameraPropertyService.h"
#include "../src/service/camera/CameraStatusService.h"
#include "../src/media/video/VideoParams.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {

void check(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "CHECK failed: " << expression << " at " << __FILE__ << ":" << line << std::endl;
        std::exit(1);
    }
}

#define CHECK(condition) check((condition), #condition, __LINE__)

const char* kTestDir = "/tmp/t32_camera_property_test";
const char* kConfigPath = "/tmp/t32_camera_property_test/config.json";
const char* kSettingPath = "/tmp/t32_camera_property_test/setting.json";
const char* kProductPath = "/tmp/t32_camera_property_test/product.json";

void normalizeSettingsFileForTest(const std::string& path);
void writeJson(const std::string& path, const Json::Value& root);
bool writeProductFile(const std::string& path);

bool copyFile(const std::string& from, const std::string& to) {
    std::ifstream input(from, std::ios::binary);
    std::ofstream output(to, std::ios::binary);
    if (!input.is_open() || !output.is_open()) {
        return false;
    }
    output << input.rdbuf();
    return output.good();
}

bool writeConfigFile(const std::string& path) {
    // T25 Phase-3: config fixture as JSON (DeviceConfig parses JSON after
    // Phase-1). SYSTEM section removed (BR*/UPID/PWD/voltage migrated to
    // Settings). POLICY + Functions stay in DeviceConfig.
    Json::Value root(Json::objectValue);
    Json::Value policy(Json::objectValue);
    policy["Record"] = "1";
    root["POLICY"] = policy;
    Json::Value functions(Json::objectValue);
    functions["Photo_DS_EN"] = "0";
    functions["Video_DS_EN"] = "1";
    functions["Timer_Range_MAX"] = "3";
    functions["Stamp_EN"] = "1";
    functions["RWakeup _SET"] = "0";
    root["Functions"] = functions;
    writeJson(path, root);
    return true;
}

void prepareFiles() {
    mkdir(kTestDir, 0777);
    CHECK(writeConfigFile(kConfigPath));
    CHECK(writeProductFile(kProductPath));
    CHECK(copyFile(std::string(TEST_PROJECT_ROOT) + "/res/setting.json", kSettingPath));
    normalizeSettingsFileForTest(kSettingPath);
}

Json::Value loadJson(const std::string& path) {
    std::ifstream file(path);
    CHECK(file.is_open());

    Json::Value root;
    Json::CharReaderBuilder reader;
    std::string errors;
    bool ok = Json::parseFromStream(reader, file, &root, &errors);
    CHECK(ok);
    return root;
}

void writeJson(const std::string& path, const Json::Value& root) {
    std::ofstream file(path);
    CHECK(file.is_open());

    Json::StreamWriterBuilder writer;
    writer["indentation"] = "  ";
    std::unique_ptr<Json::StreamWriter> jsonWriter(writer.newStreamWriter());
    jsonWriter->write(root, &file);
    CHECK(file.good());
}

// Seed a minimal product.json (PRODUCT_FILE). T24 introduced PRODUCT-kind
// fields (BOOT.* + DEVICE-static CSSID/CPWD/SPKVOL), which factory import now
// routes to product.json via writeProductDelta. Without PRODUCT_FILE set,
// factory import fails at the product-persist step (T24 regression). The
// values here mirror factory defaults in res/product.json.
bool writeProductFile(const std::string& path) {
    Json::Value root(Json::objectValue);
    Json::Value boot(Json::objectValue);
    boot["PType"] = "1";
    boot["PModel"] = "T32";
    root["BOOT"] = boot;
    Json::Value device(Json::objectValue);
    device["CSSID"] = "CKV";
    root["DEVICE"] = device;
    writeJson(path, root);
    return true;
}

void normalizeSettingsFileForTest(const std::string& path) {
    Json::Value root = loadJson(path);
    root["videoSize"] = VIDEO_SIZE_FHD_30FPS;
    root["bitRate_1080p"] = 16;
    writeJson(path, root);
}

void test_property_schema_and_values() {
    service::CameraPropertyService& properties = service::CameraPropertyService::getInstance();
    Json::Value all = properties.getAllPropertiesJson();

    CHECK(all.isMember("resolution"));
    CHECK(all.isMember("fps"));
    CHECK(all["resolution"]["value"].asString() == "1920x1080");
    CHECK(all["fps"]["value"].asInt() == 30);

    bool has720 = false;
    bool has1080 = false;
    for (const auto& option : all["resolution"]["options"]) {
        if (option.asString() == "1280x720") {
            has720 = true;
        } else if (option.asString() == "1920x1080") {
            has1080 = true;
        } else if (option.asString() == "2560x1440") {
            CHECK(false && "2K should not be enabled when MVideo=1");
        }
    }

    CHECK(has720);
    CHECK(has1080);
}

void test_registry_enumeration() {
    bool hasBoot = false;
    bool hasCamera = false;
    bool hasAudio = false;
    bool hasPir = false;
    bool hasTimer = false;
    bool hasNetwork = false;
    bool hasServer = false;
    bool hasSystem = false;
    bool hasAi = false;
    bool hasDeviceStatus = false;
    bool hasSignalStatus = false;
    bool hasSensorStatus = false;
    bool hasSpacedRawName = false;

    for (const auto& definition : service::getCameraParameterDefinitions()) {
        CHECK(!definition.id.empty());
        CHECK(!definition.rawName.empty());
        CHECK(!definition.group.empty());
        CHECK(definition.storage.kind != service::ParameterStorageKind::NONE);

        if (definition.classification == service::ParameterClassification::FACTORY && definition.group == "BOOT") {
            hasBoot = true;
        } else if (definition.classification == service::ParameterClassification::PROPERTY &&
                   definition.group == "Camera_Setting") {
            hasCamera = true;
        } else if (definition.classification == service::ParameterClassification::PROPERTY &&
                   definition.group == "Audio_Setting") {
            hasAudio = true;
        } else if (definition.classification == service::ParameterClassification::PROPERTY &&
                   definition.group == "PIR_Setting") {
            hasPir = true;
        } else if (definition.classification == service::ParameterClassification::PROPERTY &&
                   definition.group == "Timer_Setting") {
            hasTimer = true;
        } else if (definition.classification == service::ParameterClassification::PROPERTY &&
                   definition.group == "Network_Setting") {
            hasNetwork = true;
        } else if (definition.classification == service::ParameterClassification::PROPERTY &&
                   definition.group == "Server_Setting") {
            hasServer = true;
        } else if (definition.classification == service::ParameterClassification::PROPERTY &&
                   definition.group == "System_Setting") {
            hasSystem = true;
        } else if (definition.classification == service::ParameterClassification::PROPERTY &&
                   definition.group == "AI_Setting") {
            hasAi = true;
        } else if (definition.classification == service::ParameterClassification::STATUS &&
                   definition.group == "Device") {
            hasDeviceStatus = true;
        } else if (definition.classification == service::ParameterClassification::STATUS &&
                   definition.group == "Signal") {
            hasSignalStatus = true;
        } else if (definition.classification == service::ParameterClassification::STATUS &&
                   definition.group == "Sensor") {
            hasSensorStatus = true;
        }

        if (definition.rawName == "CAM_ Ffixed_Shutter" ||
            definition.rawName == "RWakeup _SET" ||
            definition.rawName == "GPS_ Enable") {
            hasSpacedRawName = true;
        }
    }

    CHECK(hasBoot);
    CHECK(hasCamera);
    CHECK(hasAudio);
    CHECK(hasPir);
    CHECK(hasTimer);
    CHECK(hasNetwork);
    CHECK(hasServer);
    CHECK(hasSystem);
    CHECK(hasAi);
    CHECK(hasDeviceStatus);
    CHECK(hasSignalStatus);
    CHECK(hasSensorStatus);
    CHECK(hasSpacedRawName);
}

void test_registry_property_projection() {
    service::CameraPropertyService& properties = service::CameraPropertyService::getInstance();

    Json::Value all = properties.getPropertiesJson("all", "schema,value");
    CHECK(all["properties"].isArray());
    CHECK(!all.isMember("group_order"));
    CHECK(!all.isMember("groups"));
    CHECK(!all.isMember("properties_by_group"));
    CHECK(all["properties"].size() == 8);
    CHECK(all["properties"][0]["group"].asString() == "Camera_Setting");
    CHECK(all["properties"][0]["items"].isArray());
    CHECK(!all["properties"][0].isMember("properties"));
    CHECK(all["properties"][1]["group"].asString() == "Audio_Setting");
    CHECK(all["properties"][2]["group"].asString() == "PIR_Setting");
    CHECK(all["properties"][3]["group"].asString() == "Timer_Setting");
    CHECK(all["properties"][3]["items"].isArray());
    CHECK(all["properties"][4]["group"].asString() == "Network_Setting");
    CHECK(all["properties"][5]["group"].asString() == "Server_Setting");
    CHECK(all["properties"][6]["group"].asString() == "System_Setting");
    CHECK(all["properties"][7]["group"].asString() == "AI_Setting");

    Json::Value fixedShutter;
    std::string error;
    CHECK(properties.getRegistryPropertyJson("CAM_ Ffixed_Shutter", fixedShutter, "schema,value", &error));
    CHECK(fixedShutter["raw_name"].asString() == "CAM_ Ffixed_Shutter");
    CHECK(!fixedShutter["enabled"].asBool());
    CHECK(fixedShutter.isMember("disabled_reason"));

    Json::Value camMode;
    CHECK(properties.setRegistryPropertyValue("CAM_Mode", Json::Value(1), &camMode, &error) == 0);
    CHECK(camMode["raw_name"].asString() == "CAM_Mode");
    CHECK(camMode["value"].asInt() == 1);

    Json::Value badWrite;
    CHECK(properties.setRegistryPropertyValue("CAM_Mode", Json::Value(99), &badWrite, &error) != 0);
    CHECK(!error.empty());
}

void test_registry_factory_reset() {
    service::CameraPropertyService& properties = service::CameraPropertyService::getInstance();

    Json::Value propertyJson;
    std::string error;
    CHECK(properties.setRegistryPropertyValue("CAM_Mode", Json::Value(1), &propertyJson, &error) == 0);
    CHECK(Settings::getInstance()->cameraMode == 1);

    error.clear();
    Json::Value resetResult;
    CHECK(properties.resetFactoryProperties("all",
                                            {"CAM_Mode", "CAM_ Ffixed_Shutter"},
                                            resetResult,
                                            &error) == 1);
    CHECK(error.empty());
    CHECK(resetResult["total"].asInt() == 2);
    CHECK(resetResult["applied"].asInt() == 1);
    CHECK(resetResult["skipped"].asInt() == 1);
    CHECK(resetResult["failed"].asInt() == 0);
    CHECK(resetResult["results"][0]["raw_name"].asString() == "CAM_Mode");
    CHECK(resetResult["results"][0]["status"].asString() == "applied");
    CHECK(resetResult["results"][0]["applied_value"].asInt() == 0);
    CHECK(resetResult["results"][1]["raw_name"].asString() == "CAM_ Ffixed_Shutter");
    CHECK(resetResult["results"][1]["status"].asString() == "skipped");
    CHECK(resetResult["results"][1]["reason"].asString() == "storage_not_implemented");
    CHECK(Settings::getInstance()->cameraMode == 0);
}

void test_status_projection() {
    Json::Value status = service::CameraStatusService::getInstance().getStatusJson("all");
    CHECK(status["status"].isMember("device"));
    CHECK(status["status"].isMember("signal"));
    CHECK(status["status"].isMember("sensor"));

    bool sawPlaceholder = false;
    bool sawMemory = false;
    for (const auto& item : status["status"]["device"]) {
        if (item["raw_name"].asString() == "Memory") {
            sawMemory = true;
        }
        if (item["available"].isBool() && !item["available"].asBool()) {
            sawPlaceholder = true;
            CHECK(item["source"].asString() == "placeholder");
            CHECK(item["reason"].asString() == "not_implemented");
        }
    }
    CHECK(!sawMemory);
    CHECK(sawPlaceholder);
}

void test_factory_template() {
    Json::Value root = loadJson(std::string(TEST_PROJECT_ROOT) + "/res/factory/camera_factory_config.template.json");
    service::CameraFactoryConfigImporter importer;
    Json::Value errors;
    CHECK(importer.validateFactoryConfigJson(root, errors));
    CHECK(errors.empty());
    CHECK(root["factory"]["Functions"].isMember("RWakeup _SET"));
}

void test_factory_importer_json_path() {
    Json::Value root(Json::objectValue);
    root["version"] = 1;
    root["factory"]["BOOT"]["PType"] = 4;
    root["factory"]["DEVICE"]["CSSID"] = "FACTORY_SSID";
    root["properties"]["Camera_Setting"]["CAM_Mode"] = 2;
    writeJson(std::string(kTestDir) + "/camera_factory_config.json", root);

    service::CameraFactoryConfigImporter importer;
    service::CameraFactoryImportResult result = importer.importFromSdRoot(kTestDir);
    CHECK(result.success);
    CHECK(result.restartRequired);
    CHECK(result.appliedCount == 3);
    CHECK(result.decision == "json_selected");
    // PType/CSSID are PRODUCT-kind fields: factory import routes them to
    // product.json (via writeProductDelta) and reloads ProductConfig. Assert
    // on ProductConfig to reflect the T24 carve-out.
    CHECK(ProductConfig::getInstance()->get(INI_SECTION_BOOT, INI_KEY_PTYPE, 0) == 4);
    CHECK(ProductConfig::getInstance()->get(INI_SECTION_DEVICE, INI_KEY_CSSID, "") == "FACTORY_SSID");
    CHECK(Settings::getInstance()->cameraMode == 2);
}

void test_set_and_reset_properties() {
    service::CameraPropertyService& properties = service::CameraPropertyService::getInstance();

    Json::Value propertyJson;
    std::string error;

    CHECK(properties.setPropertyValue("resolution", Json::Value("1920x1080"), &propertyJson, &error) == 0);
    CHECK(properties.setPropertyValue("fps", Json::Value(60), &propertyJson, &error) == 0);
    CHECK(properties.setPropertyValue("bitrate", Json::Value(12288), &propertyJson, &error) == 0);
    CHECK(properties.setPropertyValue("timestamp_overlay", Json::Value(false), &propertyJson, &error) == 0);

    int width = 0;
    int height = 0;
    int fps = 0;
    int bitrateKbps = 0;
    properties.getVideoRecordConfig(width, height, fps, bitrateKbps);
    CHECK(width == 1920);
    CHECK(height == 1080);
    CHECK(fps == 60);
    CHECK(bitrateKbps == 12288);

    std::string value;
    CHECK(properties.getPropertyValueString("timestamp_overlay", value, &error) == 0);
    CHECK(value == "false");

    CHECK(properties.setPropertyValue("resolution", Json::Value("3840x2160"), &propertyJson, &error) != 0);

    Json::Value resetApplied;
    CHECK(properties.resetProperties({"resolution", "fps", "bitrate"}, resetApplied, &error) == 3);
    CHECK(resetApplied["resolution"].asString() == "1920x1080");
    CHECK(resetApplied["fps"].asInt() == 30);
    CHECK(resetApplied["bitrate"].asInt() == 16384);

    Json::Value savedSettings = loadJson(kSettingPath);
    CHECK(savedSettings["videoSize"].asInt() == VIDEO_SIZE_FHD_30FPS);
    CHECK(savedSettings["bitRate_1080p"].asInt() == 16);
}

void test_video_params_bitrate_unit_is_kbps() {
    media::VideoParams params;
    CHECK(params.getBitrate() == 2000);

    params.setBitrate(12288);
    CHECK(params.getBitrate() == 12288);
}

} // namespace

int main() {
    prepareFiles();

    auto env = EnvManager::getInstance();
    env->setEnv("CONFIG_FILE", kConfigPath);
    env->setEnv("PRODUCT_FILE", kProductPath);
    env->setEnv("SETTING_FILE_PATH", kSettingPath);

    CHECK(Settings::getInstance()->loadFromJsonFile(kSettingPath));

    test_registry_enumeration();
    test_factory_template();
    test_factory_importer_json_path();
    test_property_schema_and_values();
    test_registry_property_projection();
    test_registry_factory_reset();
    test_status_projection();
    test_set_and_reset_properties();
    test_video_params_bitrate_unit_is_kbps();

    std::cout << "All camera property tests passed!" << std::endl;
    return 0;
}
