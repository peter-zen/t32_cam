#include "IspOsdManager.h"
#include "IspOsdFont.h"
#include "Logger.h"
#include "../../config/setting/Settings.h"
#include <cstring>
#include <ctime>
#include <unistd.h>

namespace hal {

IspOsdManager* IspOsdManager::s_instance = nullptr;

IspOsdManager::IspOsdManager()
    : sensorCount_(0), running_(false), started_(false) {
}

IspOsdManager::~IspOsdManager() {
    exit();
}

bool IspOsdManager::init(int sensorCount) {
    sensorCount_ = sensorCount;
    Logger::log(LogLevel::INFO, "IspOsdManager: init start, sensorCount=%d", sensorCount_);

    for (int i = 0; i < sensorCount_; ++i) {
        Logger::log(LogLevel::INFO, "IspOsdManager: CreateOsdRgn(%d)...", i);
        int handle = IMP_ISP_Tuning_CreateOsdRgn(i, NULL);
        if (handle < 0) {
            Logger::log(LogLevel::ERROR, "IspOsdManager: CreateOsdRgn(%d) failed, handle=%d", i, handle);
            return false;
        }
        Logger::log(LogLevel::INFO, "IspOsdManager: CreateOsdRgn(%d) success, handle=%d", i, handle);
        handles_.push_back(handle);

        size_t bufSize = LETTER_NUM * REGION_HEIGHT * REGION_WIDTH * sizeof(uint32_t);
        uint32_t* buf = new uint32_t[LETTER_NUM * REGION_HEIGHT * REGION_WIDTH];
        memset(buf, 0, bufSize);
        buffers_.push_back(buf);
        Logger::log(LogLevel::INFO, "IspOsdManager: buffer allocated, size=%zu", bufSize);
    }

    s_instance = this;
    Logger::log(LogLevel::INFO, "IspOsdManager: init done, sensorCount=%d", sensorCount_);
    return true;
}

void IspOsdManager::start() {
    if (started_.exchange(true)) {
        return;
    }
    running_ = true;
    thread_ = std::thread(&IspOsdManager::updateLoop, this);
    Logger::log(LogLevel::INFO, "IspOsdManager: updateLoop started");
}

void IspOsdManager::exit() {
    Logger::log(LogLevel::INFO, "IspOsdManager: exit start");
    if (s_instance == this) {
        s_instance = nullptr;
    }
    started_ = false;
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }

    for (int i = 0; i < sensorCount_; ++i) {
        if (i < static_cast<int>(handles_.size()) && handles_[i] >= 0) {
            int ret = IMP_ISP_Tuning_ShowOsdRgn(i, handles_[i], 0);
            Logger::log(LogLevel::INFO, "IspOsdManager: ShowOsdRgn(%d,%d,0) ret=%d", i, handles_[i], ret);
            ret = IMP_ISP_Tuning_DestroyOsdRgn(i, handles_[i]);
            Logger::log(LogLevel::INFO, "IspOsdManager: DestroyOsdRgn(%d,%d) ret=%d", i, handles_[i], ret);
        }
        if (i < static_cast<int>(buffers_.size()) && buffers_[i]) {
            delete[] buffers_[i];
        }
    }
    handles_.clear();
    buffers_.clear();
    sensorCount_ = 0;
    Logger::log(LogLevel::INFO, "IspOsdManager: exit done");
}

void IspOsdManager::updateLoop() {
    bool lastEnabled = false;
    int loopCount = 0;

    Logger::log(LogLevel::INFO, "IspOsdManager: updateLoop started");
    Logger::log(LogLevel::INFO, "IspOsdManager: sizeof(IMPIspOsdAttrAsm)=%zu sizeof(IMPISPOSDSingleAttr)=%zu sizeof(IMPISPOSDAttr)=%zu sizeof(IMPISPOSDBlockAttr)=%zu",
                sizeof(IMPIspOsdAttrAsm), sizeof(IMPISPOSDSingleAttr), sizeof(IMPISPOSDAttr), sizeof(IMPISPOSDBlockAttr));

    while (running_) {
        bool enabled = (Settings::getInstance()->stampEn != 0);
        bool stateChanged = (enabled != lastEnabled);
        loopCount++;

        if (loopCount <= 5 || stateChanged) {
            Logger::log(LogLevel::INFO, "IspOsdManager: loop=%d stampEn=%d enabled=%d stateChanged=%d",
                        loopCount, Settings::getInstance()->stampEn, enabled, stateChanged);
        }

        for (int i = 0; i < sensorCount_; ++i) {
            if (enabled) {
                char str[64];
                time_t currTime = time(NULL);
                struct tm* currDate = localtime(&currTime);
                strftime(str, sizeof(str), "%Y-%m-%d %I:%M:%S", currDate);

                if (loopCount <= 5) {
                    Logger::log(LogLevel::INFO, "IspOsdManager: rendering timestamp='%s'", str);
                }

                renderTimestamp(str, buffers_[i]);

                IMPIspOsdAttrAsm attr;
                memset(&attr, 0, sizeof(attr));
                attr.type = ISP_OSD_REG_PIC;
                attr.stsinglepicAttr.chnOSDAttr.osd_type = IMP_ISP_PIC_ARGB_8888;
                attr.stsinglepicAttr.chnOSDAttr.osd_argb_type = IMP_ISP_ARGB_TYPE_BGRA;
                attr.stsinglepicAttr.chnOSDAttr.osd_pixel_alpha_disable = IMPISP_TUNING_OPS_MODE_DISABLE;
                attr.stsinglepicAttr.pic.pinum = handles_[i];
                attr.stsinglepicAttr.pic.osd_enable = 1;
                attr.stsinglepicAttr.pic.osd_left = 10;
                attr.stsinglepicAttr.pic.osd_top = 10;
                attr.stsinglepicAttr.pic.osd_width = REGION_WIDTH * LETTER_NUM;
                attr.stsinglepicAttr.pic.osd_height = REGION_HEIGHT;
                attr.stsinglepicAttr.pic.osd_image = (char*)buffers_[i];
                attr.stsinglepicAttr.pic.osd_stride = REGION_WIDTH * LETTER_NUM * 4;

                if (loopCount <= 5) {
                    Logger::log(LogLevel::INFO, "IspOsdManager: attr type=%d osd_type=%d argb_type=%d alpha_disable=%d overlap=%d group=%d",
                                attr.type, attr.stsinglepicAttr.chnOSDAttr.osd_type,
                                attr.stsinglepicAttr.chnOSDAttr.osd_argb_type,
                                attr.stsinglepicAttr.chnOSDAttr.osd_pixel_alpha_disable,
                                attr.stsinglepicAttr.chnOSDAttr.osd_overlap,
                                attr.stsinglepicAttr.chnOSDAttr.osd_group);
                    Logger::log(LogLevel::INFO, "IspOsdManager: attr pinum=%d enable=%d left=%d top=%d width=%d height=%d stride=%d image=%p",
                                attr.stsinglepicAttr.pic.pinum, attr.stsinglepicAttr.pic.osd_enable,
                                attr.stsinglepicAttr.pic.osd_left, attr.stsinglepicAttr.pic.osd_top,
                                attr.stsinglepicAttr.pic.osd_width, attr.stsinglepicAttr.pic.osd_height,
                                attr.stsinglepicAttr.pic.osd_stride, attr.stsinglepicAttr.pic.osd_image);
                }

                IMPIspOsdAttrAsm getAttr;
                memset(&getAttr, 0, sizeof(getAttr));
                int getRet = IMP_ISP_Tuning_GetOsdRgnAttr(i, handles_[i], &getAttr);
                if (loopCount <= 5) {
                    Logger::log(LogLevel::INFO, "IspOsdManager: GetOsdRgnAttr(%d,%d) ret=%d type=%d pinum=%d enable=%d left=%d top=%d w=%d h=%d stride=%d",
                                i, handles_[i], getRet,
                                getAttr.type, getAttr.stsinglepicAttr.pic.pinum,
                                getAttr.stsinglepicAttr.pic.osd_enable,
                                getAttr.stsinglepicAttr.pic.osd_left,
                                getAttr.stsinglepicAttr.pic.osd_top,
                                getAttr.stsinglepicAttr.pic.osd_width,
                                getAttr.stsinglepicAttr.pic.osd_height,
                                getAttr.stsinglepicAttr.pic.osd_stride);
                }

                int ret = IMP_ISP_Tuning_SetOsdRgnAttr(i, handles_[i], &attr);
                if (ret < 0) {
                    Logger::log(LogLevel::ERROR, "IspOsdManager: SetOsdRgnAttr(%d,%d) ret=%d",
                                i, handles_[i], ret);
                } else if (loopCount <= 5) {
                    Logger::log(LogLevel::INFO, "IspOsdManager: SetOsdRgnAttr(%d,%d) ret=%d",
                                i, handles_[i], ret);
                }

                if (stateChanged) {
                    ret = IMP_ISP_Tuning_ShowOsdRgn(i, handles_[i], 1);
                    Logger::log(LogLevel::INFO, "IspOsdManager: ShowOsdRgn(%d,%d,1) ret=%d",
                                i, handles_[i], ret);
                }
            } else if (stateChanged) {
                int ret = IMP_ISP_Tuning_ShowOsdRgn(i, handles_[i], 0);
                Logger::log(LogLevel::INFO, "IspOsdManager: ShowOsdRgn(%d,%d,0) ret=%d",
                            i, handles_[i], ret);
            }
        }

        lastEnabled = enabled;
        sleep(1);
    }

    Logger::log(LogLevel::INFO, "IspOsdManager: updateLoop ended, total loops=%d", loopCount);
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
        } else if (c == '-') {
            dateData = gBgramap[10].pdata;
            fontadv = gBgramap[10].width;
        } else if (c == ' ') {
            dateData = gBgramap[11].pdata;
            fontadv = gBgramap[11].width;
        } else if (c == ':') {
            dateData = gBgramap[12].pdata;
            fontadv = gBgramap[12].width;
        } else {
            penpos_t += REGION_WIDTH;
            continue;
        }

        for (int j = 0; j < REGION_HEIGHT; ++j) {
            memcpy(&data[j * LETTER_NUM * REGION_WIDTH + penpos_t],
                   &dateData[j * fontadv],
                   fontadv * sizeof(uint32_t));
        }
        penpos_t += fontadv;
    }
}

} // namespace hal
