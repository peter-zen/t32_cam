#include <iostream>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <string>

class EnvManager {
    public:
	static std::shared_ptr<EnvManager> getInstance();
	bool parsePrimaryEnv(const std::string &filename);
	void setEnv(const std::string &key, const std::string &value);
	std::string getEnv(const std::string &key,
			   const std::string &defaultValue = "") const;

    private:
	EnvManager();
	EnvManager(const EnvManager &) = delete;
	EnvManager &operator=(const EnvManager &) = delete;
	void trimSpace(std::string &s);
	mutable std::mutex envMutex;
	std::unordered_map<std::string, std::string> envMap;
};