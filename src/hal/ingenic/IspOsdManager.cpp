#include "IspOsdManager.h"
#include "IspOsdFont.h"
#include "Logger.h"
#include "../../config/setting/Settings.h"
#include <cerrno>
#include <cstring>
#include <ctime>
#include <unistd.h>

namespace hal {

IspOsdManager* IspOsdManager::s_instance = nullptr;

IspOsdManager::IspOsdManager()
    : timeHandle_(-1), reservedHandle_(-1), useLegacyBlock_(false), running_(false), started_(false) {
}

IspOsdManager::~IspOsdManager() {
    exit();
}

bool IspOsdManager::init(int sensorCount) {
    (void)sensorCount;
    if (buffer_.empty()) {
        buffer_.assign(LETTER_NUM * REGION_HEIGHT * REGION_WIDTH, 0);
    }
    s_instance = this;
    Logger::log(LogLevel::INFO, "IspOsdManager: init done, region will be created on stream start");
    return true;
}

bool IspOsdManager::prepare() {
    return ensureRegion();
}

void IspOsdManager::start() {
    if (started_.exchange(true)) {
        return;
    }
    if (!ensureRegion()) {
        started_ = false;
        return;
    }
    running_ = true;
    thread_ = std::thread(&IspOsdManager::updateLoop, this);
    Logger::log(LogLevel::INFO, "IspOsdManager: updateLoop started");
}

void IspOsdManager::stop() {
    started_ = false;
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }

    if (timeHandle_ >= 0) {
        hide();
        int ret = IMP_ISP_Tuning_DestroyOsdRgn(0, timeHandle_);
        if (ret < 0) {
            Logger::log(LogLevel::WARNING, "IspOsdManager: DestroyOsdRgn(%d) ret=%d", timeHandle_, ret);
        }
        timeHandle_ = -1;
    }

    if (reservedHandle_ >= 0) {
        int ret = IMP_ISP_Tuning_ShowOsdRgn(0, reservedHandle_, 0);
        if (ret < 0) {
            Logger::log(LogLevel::WARNING, "IspOsdManager: ShowOsdRgn(%d,0) ret=%d", reservedHandle_, ret);
        }
        ret = IMP_ISP_Tuning_DestroyOsdRgn(0, reservedHandle_);
        if (ret < 0) {
            Logger::log(LogLevel::WARNING, "IspOsdManager: DestroyOsdRgn(%d) ret=%d", reservedHandle_, ret);
        }
        reservedHandle_ = -1;
    }

    useLegacyBlock_ = false;
}

void IspOsdManager::exit() {
    if (s_instance == this) {
        s_instance = nullptr;
    }
    stop();
    buffer_.clear();
    Logger::log(LogLevel::INFO, "IspOsdManager: exit done");
}

bool IspOsdManager::ensureRegion() {
    if (timeHandle_ >= 0) {
        return true;
    }

    if (buffer_.empty()) {
        buffer_.assign(LETTER_NUM * REGION_HEIGHT * REGION_WIDTH, 0);
    }

    timeHandle_ = IMP_ISP_Tuning_CreateOsdRgn(0, NULL);
    if (timeHandle_ < 0) {
        Logger::log(LogLevel::ERROR, "IspOsdManager: CreateOsdRgn(time) failed, handle=%d errno=%d", timeHandle_, errno);
        return false;
    }

    reservedHandle_ = IMP_ISP_Tuning_CreateOsdRgn(0, NULL);
    if (reservedHandle_ < 0) {
        Logger::log(LogLevel::ERROR, "IspOsdManager: CreateOsdRgn(reserved) failed, handle=%d errno=%d", reservedHandle_, errno);
        IMP_ISP_Tuning_DestroyOsdRgn(0, timeHandle_);
        timeHandle_ = -1;
        return false;
    }

    Logger::log(LogLevel::INFO, "IspOsdManager: CreateOsdRgn done, time=%d reserved=%d", timeHandle_, reservedHandle_);
    return true;
}

void IspOsdManager::updateLoop() {
    bool lastEnabled = false;

    while (running_) {
        bool enabled = (Settings::getInstance()->stampEn != 0);

        if (enabled) {
            if (applyTimestamp(!lastEnabled)) {
                lastEnabled = true;
            }
        } else {
            if (lastEnabled) {
                hide();
            }
            lastEnabled = false;
        }
        sleep(1);
    }
}

void IspOsdManager::renderTimestamp(const char* str, uint32_t* data) {
    memset(data, 0, LETTER_NUM * REGION_HEIGHT * REGION_WIDTH * sizeof(uint32_t));

    int penpos_t = 0;
    for (int i = 0; i < LETTER_NUM; ++i) {
        const uint32_t* dateData = nullptr;
        int fontadv = 0;

        char c = str[i];
        if (c >= '0' && c <= '9') {
            dateData = gBgramap[c - '0'].pdata;
            fontadv = gBgramap[c - '0'].width;
            penpos_t += fontadv;
        } else if (c == '-') {
            dateData = gBgramap[10].pdata;
            fontadv = gBgramap[10].width;
            penpos_t += fontadv;
        } else if (c == ' ') {
            dateData = gBgramap[11].pdata;
            fontadv = gBgramap[11].width;
            penpos_t += fontadv;
        } else if (c == ':') {
            dateData = gBgramap[12].pdata;
            fontadv = gBgramap[12].width;
            penpos_t += fontadv;
        } else {
            penpos_t += REGION_WIDTH;
            continue;
        }

        for (int j = 0; j < REGION_HEIGHT; ++j) {
            memcpy(&data[j * LETTER_NUM * REGION_WIDTH + penpos_t],
                   &dateData[j * fontadv],
                   fontadv * sizeof(uint32_t));
        }
    }
}

bool IspOsdManager::applyTimestamp(bool show) {
    if (timeHandle_ < 0 || buffer_.empty()) {
        return false;
    }

    char str[64] = {0};
    time_t currTime = time(NULL);
    struct tm* currDate = localtime(&currTime);
    if (!currDate || strftime(str, sizeof(str), "%Y-%m-%d %I:%M:%S", currDate) == 0) {
        return false;
    }

    renderTimestamp(str, buffer_.data());

    if (useLegacyBlock_) {
        return applyTimestampLegacy();
    }

    IMPIspOsdAttrAsm attr;
    memset(&attr, 0, sizeof(attr));
    attr.type = ISP_OSD_REG_PIC;
    attr.stsinglepicAttr.chnOSDAttr.osd_type = IMP_ISP_PIC_ARGB_8888;
    attr.stsinglepicAttr.chnOSDAttr.osd_argb_type = IMP_ISP_ARGB_TYPE_BGRA;
    attr.stsinglepicAttr.chnOSDAttr.osd_pixel_alpha_disable = IMPISP_TUNING_OPS_MODE_DISABLE;
    attr.stsinglepicAttr.pic.pinum = timeHandle_;
    attr.stsinglepicAttr.pic.osd_enable = 1;
    attr.stsinglepicAttr.pic.osd_left = 10;
    attr.stsinglepicAttr.pic.osd_top = 10;
    attr.stsinglepicAttr.pic.osd_width = REGION_WIDTH * LETTER_NUM;
    attr.stsinglepicAttr.pic.osd_height = REGION_HEIGHT;
    attr.stsinglepicAttr.pic.osd_image = reinterpret_cast<char*>(buffer_.data());
    attr.stsinglepicAttr.pic.osd_stride = REGION_WIDTH * LETTER_NUM * 4;

    int ret = IMP_ISP_Tuning_SetOsdRgnAttr(0, timeHandle_, &attr);
    if (ret < 0) {
        Logger::log(LogLevel::ERROR,
                    "IspOsdManager: SetOsdRgnAttr(%d) ret=%d errno=%d image=%p size=%dx%d stride=%d",
                    timeHandle_, ret, errno, attr.stsinglepicAttr.pic.osd_image,
                    attr.stsinglepicAttr.pic.osd_width, attr.stsinglepicAttr.pic.osd_height,
                    attr.stsinglepicAttr.pic.osd_stride);
        if (applyTimestampLegacy()) {
            useLegacyBlock_ = true;
            Logger::log(LogLevel::WARNING, "IspOsdManager: switched to legacy ISP OSD block API");
            return true;
        }
        return false;
    }

    if (show) {
        ret = IMP_ISP_Tuning_ShowOsdRgn(0, timeHandle_, 1);
        if (ret < 0) {
            Logger::log(LogLevel::ERROR, "IspOsdManager: ShowOsdRgn(%d,1) ret=%d", timeHandle_, ret);
            return false;
        }
    }

    return true;
}

bool IspOsdManager::applyTimestampLegacy() {
    IMPISPOSDAttr osdAttr;
    memset(&osdAttr, 0, sizeof(osdAttr));
    osdAttr.osd_type = IMP_ISP_PIC_ARGB_8888;
    osdAttr.osd_argb_type = IMP_ISP_ARGB_TYPE_BGRA;
    osdAttr.osd_pixel_alpha_disable = IMPISP_TUNING_OPS_MODE_DISABLE;

    int ret = IMP_ISP_Tuning_SetOSDAttr(IMPVI_MAIN, &osdAttr);
    if (ret < 0) {
        Logger::log(LogLevel::ERROR, "IspOsdManager: legacy SetOSDAttr ret=%d errno=%d", ret, errno);
        return false;
    }

    IMPISPOSDBlockAttr block;
    memset(&block, 0, sizeof(block));
    block.pinum = 0;
    block.osd_enable = 1;
    block.osd_left = 10;
    block.osd_top = 10;
    block.osd_width = REGION_WIDTH * LETTER_NUM;
    block.osd_height = REGION_HEIGHT;
    block.osd_image = reinterpret_cast<char*>(buffer_.data());
    block.osd_stride = REGION_WIDTH * LETTER_NUM * 4;

    ret = IMP_ISP_Tuning_SetOSDBlock(IMPVI_MAIN, &block);
    if (ret < 0) {
        Logger::log(LogLevel::ERROR,
                    "IspOsdManager: legacy SetOSDBlock ret=%d errno=%d image=%p size=%dx%d stride=%d",
                    ret, errno, block.osd_image, block.osd_width, block.osd_height, block.osd_stride);
        return false;
    }

    return true;
}

bool IspOsdManager::hideLegacy() {
    IMPISPOSDBlockAttr block;
    memset(&block, 0, sizeof(block));
    block.pinum = 0;
    block.osd_enable = 0;
    block.osd_left = 10;
    block.osd_top = 10;
    block.osd_width = REGION_WIDTH * LETTER_NUM;
    block.osd_height = REGION_HEIGHT;
    block.osd_image = reinterpret_cast<char*>(buffer_.data());
    block.osd_stride = REGION_WIDTH * LETTER_NUM * 4;

    int ret = IMP_ISP_Tuning_SetOSDBlock(IMPVI_MAIN, &block);
    if (ret < 0) {
        Logger::log(LogLevel::WARNING, "IspOsdManager: legacy hide SetOSDBlock ret=%d errno=%d", ret, errno);
        return false;
    }
    return true;
}

bool IspOsdManager::hide() {
    if (useLegacyBlock_) {
        return hideLegacy();
    }

    if (timeHandle_ < 0) {
        return true;
    }

    int ret = IMP_ISP_Tuning_ShowOsdRgn(0, timeHandle_, 0);
    if (ret < 0) {
        Logger::log(LogLevel::WARNING, "IspOsdManager: ShowOsdRgn(%d,0) ret=%d", timeHandle_, ret);
        return false;
    }
    return true;
}

} // namespace hal
