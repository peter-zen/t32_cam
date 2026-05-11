#pragma once
#include <vector>
#include <thread>
#include <atomic>
#include <imp/imp_isp.h>

namespace hal {

class IspOsdManager {
public:
    IspOsdManager();
    ~IspOsdManager();

    bool init(int sensorCount);
    bool prepare();
    void start();
    void stop();
    void exit();

    static IspOsdManager* getInstance() { return s_instance; }

private:
    void updateLoop();
    bool ensureRegion();
    void renderTimestamp(const char* str, uint32_t* data);
    bool applyTimestamp(bool show);
    bool applyTimestampLegacy();
    bool hideLegacy();
    bool hide();

    int timeHandle_;
    int reservedHandle_;
    bool useLegacyBlock_;
    std::vector<uint32_t> buffer_;
    std::atomic<bool> running_;
    std::atomic<bool> started_;
    std::thread thread_;

    static IspOsdManager* s_instance;

    static constexpr int LETTER_NUM = 20;
    static constexpr int REGION_WIDTH = 16;
    static constexpr int REGION_HEIGHT = 34;
};

} // namespace hal
