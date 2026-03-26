#include "../src/config/env/EnvManager.h"
#include "../src/config/setting/Settings.h"
#include "../src/service/camera/CameraPropertyService.h"

#include <cassert>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {

const char* kTestDir = "/tmp/t32_camera_property_test";
const char* kConfigPath = "/tmp/t32_camera_property_test/config.ini";
const char* kSettingPath = "/tmp/t32_camera_property_test/setting.json";

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
    std::ofstream file(path);
    if (!file.is_open()) {
        return false;
    }

    file << "[BOOT]\n";
    file << "PType=1\n";
    file << "MVideo=1\n";
    file << "\n";
    file << "[SYSTEM]\n";
    file << "BR4K=32\n";
    file << "BR1080P=16\n";
    file << "BR720P=8\n";
    file << "\n";
    file << "[POLICY]\n";
    file << "Record=1\n";
    return file.good();
}

void prepareFiles() {
    mkdir(kTestDir, 0777);
    assert(writeConfigFile(kConfigPath));
    assert(copyFile(std::string(TEST_PROJECT_ROOT) + "/res/setting.json", kSettingPath));
}

Json::Value loadJson(const std::string& path) {
    std::ifstream file(path);
    assert(file.is_open());

    Json::Value root;
    Json::CharReaderBuilder reader;
    std::string errors;
    bool ok = Json::parseFromStream(reader, file, &root, &errors);
    assert(ok);
    return root;
}

void test_property_schema_and_values() {
    service::CameraPropertyService& properties = service::CameraPropertyService::getInstance();
    Json::Value all = properties.getAllPropertiesJson();

    assert(all.isMember("resolution"));
    assert(all.isMember("fps"));
    assert(all["resolution"]["value"].asString() == "1920x1080");
    assert(all["fps"]["value"].asInt() == 30);

    bool has720 = false;
    bool has1080 = false;
    for (const auto& option : all["resolution"]["options"]) {
        if (option.asString() == "1280x720") {
            has720 = true;
        } else if (option.asString() == "1920x1080") {
            has1080 = true;
        } else if (option.asString() == "2560x1440") {
            assert(false && "2K should not be enabled when MVideo=1");
        }
    }

    assert(has720);
    assert(has1080);
}

void test_set_and_reset_properties() {
    service::CameraPropertyService& properties = service::CameraPropertyService::getInstance();

    Json::Value propertyJson;
    std::string error;

    assert(properties.setPropertyValue("resolution", Json::Value("1920x1080"), &propertyJson, &error) == 0);
    assert(properties.setPropertyValue("fps", Json::Value(60), &propertyJson, &error) == 0);
    assert(properties.setPropertyValue("bitrate", Json::Value(12288), &propertyJson, &error) == 0);
    assert(properties.setPropertyValue("timestamp_overlay", Json::Value(false), &propertyJson, &error) == 0);

    int width = 0;
    int height = 0;
    int fps = 0;
    int bitrateKbps = 0;
    properties.getVideoRecordConfig(width, height, fps, bitrateKbps);
    assert(width == 1920);
    assert(height == 1080);
    assert(fps == 60);
    assert(bitrateKbps == 12288);

    std::string value;
    assert(properties.getPropertyValueString("timestamp_overlay", value, &error) == 0);
    assert(value == "false");

    assert(properties.setPropertyValue("resolution", Json::Value("3840x2160"), &propertyJson, &error) != 0);

    Json::Value resetApplied;
    assert(properties.resetProperties({"resolution", "fps", "bitrate"}, resetApplied, &error) == 3);
    assert(resetApplied["resolution"].asString() == "1920x1080");
    assert(resetApplied["fps"].asInt() == 30);
    assert(resetApplied["bitrate"].asInt() == 16384);

    Json::Value savedSettings = loadJson(kSettingPath);
    assert(savedSettings["videoSize"].asInt() == VIDEO_SIZE_FHD_30FPS);
    assert(savedSettings["bitRate_1080p"].asInt() == 16);
}

} // namespace

int main() {
    prepareFiles();

    auto env = EnvManager::getInstance();
    env->setEnv("CONFIG_FILE", kConfigPath);
    env->setEnv("SETTING_FILE_PATH", kSettingPath);

    assert(Settings::getInstance()->loadFromJsonFile(kSettingPath));

    test_property_schema_and_values();
    test_set_and_reset_properties();

    std::cout << "All camera property tests passed!" << std::endl;
    return 0;
}
