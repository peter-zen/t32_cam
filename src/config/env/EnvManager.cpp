#include <fstream>
#include <algorithm>
#include <cstdlib>
#include "EnvManager.h"

EnvManager::EnvManager()
{
	this->envMap.clear();
}
using namespace std;
void EnvManager::trimSpace(std::string &s)
{
	s.erase(s.begin(), find_if(s.begin(), s.end(),
				   [](int ch) { return !isspace(ch); }));
	s.erase(find_if(s.rbegin(), s.rend(),
			[](int ch) { return !isspace(ch); })
			.base(),
		s.end());
}
/*
Parse environment variables from INI format file, as shown below:
[ENV]
        #Parameters
        COMPUTE_PARAMS_PATH=params/compute_params.config
        SMOOTH_PARAMS_PATH=params/smoothing_setting.config
        STABLE_PARAMS_PATH=/params/stabilization_params.config

        #live
        SENSOR_DEVID=/dev/video42
        IMU_DEVID=iio:device1

        #playback
        VIDEO_FILE=gyroflow_videos/test.mp4
        IMU_FILE=gyroflow_videos/test.csv
*/
bool EnvManager::parsePrimaryEnv(const std::string &filename)
{
	ifstream file(filename);
	if (!file.is_open()) {
		cerr << "Failed to open file: " << filename << endl;
		return false;
	}

	string line;
	string currentSection;
	bool isEnvSection = false;
	while (getline(file, line)) {
		trimSpace(line);
		if (line.empty() || line[0] == ';' || line[0] == '#') {
			continue;
		}

		if (line.front() == '[' && line.back() == ']') {
			currentSection = line.substr(1, line.size() - 2);
			isEnvSection = (currentSection == "ENV");
			continue;
		}

		if (isEnvSection) {
			size_t delimiterPos = line.find('=');
			if (delimiterPos != string::npos) {
				string key = line.substr(0, delimiterPos);
				string value = line.substr(delimiterPos + 1);
				trimSpace(key);
				trimSpace(value);
				this->envMap[key] = value;
			}
		}
	}
	file.close();
	return true;
}

void EnvManager::setEnv(const std::string &key, const std::string &value)
{
	std::lock_guard<std::mutex> lock(envMutex);
	this->envMap[key] = value;
}

std::string EnvManager::getEnv(const std::string &key,
			       const std::string &defaultValue) const
{
	std::lock_guard<std::mutex> lock(envMutex);
	auto it = this->envMap.find(key);
	if (it != this->envMap.end()) {
		return it->second;
	}
	const char* envValue = std::getenv(key.c_str());
	if (envValue != nullptr) {
		return envValue;
	}
	return defaultValue;
}

std::shared_ptr<EnvManager> EnvManager::getInstance()
{
	static std::shared_ptr<EnvManager> instance = nullptr;
	static std::once_flag flag;
	std::call_once(flag, []() { instance.reset(new EnvManager()); });
	return instance;
}
