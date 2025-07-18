#include "ImageSnap.h"
#include <mutex>
#include <fstream>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "Common.h"
#include "Logger.h"
#include "Jpeg.h"
#include "sample-common.h"
#include <vector>
#include <thread>
using namespace media;

extern "C" {
    extern struct chn_conf chn[];
    extern int direct_switch;
}
ImageSnapParams::ImageSnapParams()
{
    this->nchannels = 1;
}

void ImageSnapParams::setImageSize(int width, int height)
{ 
	this->width = width; 
	this->height = height; 
}

void ImageSnapParams::getImageSize(int &width, int &height) const
{ 
	width = this->width; 
	height = this->height; 
}

int ImageSnapParams::getFrameSourceChnNum() const
{
	return nchannels;
}

void ImageSnapParams::setFrameSourceChnNum(int nchannels)
{ 
	this->nchannels = nchannels;
}

ImageSnap::ImageSnap()
    : initialized(initialize())
    , runMode(RunMode::BLOCKING)
{

}

ImageSnap::ImageSnap(const ImageSnapParams &params)
    : initialized(initialize())
    , params(params)
    , runMode(RunMode::BLOCKING)
{
	
}

ImageSnap::~ImageSnap()
{
    deinitialize();
}

bool ImageSnap::setParams(const ImageSnapParams& params) {
    this->params = params;
    return true;
}

bool ImageSnap::initialize()
{
    int i = 0;
    int ret = 0;

	/* Step.1 System init */
	ret = sample_system_init();
	if (ret < 0) {
		Logger::log(LogLevel::ERROR,"System init failed\n");
		return false;
	}
    Logger::log(LogLevel::DEBUG, "System init success\n");
    /* Step.2 FrameSource init */
    ret = sample_framesource_init();
    if (ret < 0) {
        Logger::log(LogLevel::ERROR, "FrameSource init failed");
        sample_system_exit();
        return false;
    }
    Logger::log(LogLevel::DEBUG, "FrameSource init success\n");
    /* Step.3 Encoder init */
    for (i = 0; i < FS_CHN_NUM; i++) {
        if (chn[i].enable) {
            ret = IMP_Encoder_CreateGroup(chn[i].index);
            if (ret < 0) {
                Logger::log(LogLevel::ERROR, "Encoder CreateGroup(%d) failed", chn[i].index);
                sample_framesource_exit();
                sample_system_exit();
                return false;
            }
        }
    }
    Logger::log(LogLevel::DEBUG, "Encoder CreateGroup success\n");
    if (!initJpeg()) {
        Logger::log(LogLevel::ERROR, "Jpeg init failed");
        for (i = 0; i < FS_CHN_NUM; i++) {
            if (chn[i].enable) {
                IMP_Encoder_DestroyGroup(chn[i].index);
            }
        }
        sample_framesource_exit();
        sample_system_exit();
        return false;
    }
    Logger::log(LogLevel::DEBUG, "Jpeg init success\n");
#ifdef JPEGQUAILTY_CHANGE
    int jpegQp = 20;
    for (i = 0; i < FS_CHN_NUM; i++) {
        if (chn[i].enable) {
            IMP_Encoder_SetJpegQp(12+chn[i].index/3, jpegQp);
        }
    }
#endif
	/* Step.4 Bind */
    for (i = 0; i < FS_CHN_NUM; i++) {
        if (chn[i].enable) {
            ret = IMP_System_Bind(&chn[i].framesource_chn, &chn[i].imp_encoder);
            if (ret < 0) {
                Logger::log(LogLevel::ERROR, "Bind FrameSource%d and Encoder%d failed", chn[i].framesource_chn.groupID, chn[i].imp_encoder.groupID);
                return false;
            }
        }
    }
    Logger::log(LogLevel::DEBUG, "Bind success\n");
    return true;
}

void ImageSnap::deinitialize()
{
    if (initialized) {
        for (auto& thread : threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        threads.clear();

        /* Step.8 UnBind */
        int i = 0;
        int ret = 0;
        for (i = 0; i < FS_CHN_NUM; i++) {
            if (chn[i].enable) {
                ret = IMP_System_UnBind(&chn[i].framesource_chn, &chn[i].imp_encoder);
                if (ret < 0) {
                    Logger::log(LogLevel::ERROR, "UnBind FrameSource%d and Encoder%d failed", chn[i].framesource_chn.groupID, chn[i].imp_encoder.groupID);
                    return;
                }
            }
        }

        /* Step.9 Encoder exit */
        if (!uninitJpeg()) {
            Logger::log(LogLevel::ERROR, "Jpeg uninit failed");
        }
        
        /* Step.10 FrameSource exit */
        ret = sample_framesource_exit();
        if (ret < 0) {
            Logger::log(LogLevel::ERROR, "FrameSource exit failed");
        }

        /* Step.11 System exit */
        ret = sample_system_exit();
        if (ret < 0) {
            Logger::log(LogLevel::ERROR, "System exit failed");
        }
    }
}

bool ImageSnap::snap(const std::string &filename)
{
    std::vector<std::string> filenames;
    filenames.push_back(filename);
    return snap(filenames);
}

bool ImageSnap::snap(const std::vector<std::string> &filenames)
{
    return snap(filenames, nullptr);
}

bool ImageSnap::snap(const std::string &filename, std::function<void(bool)> onSnapDone)
{
	std::vector<std::string> filenames;
    filenames.push_back(filename);
    return snap(filenames, onSnapDone);
}

bool ImageSnap::snap(const std::vector<std::string> &filenames, std::function<void(bool)> onSnapDone)
{
    if (!initialized) {
        Logger::log(LogLevel::ERROR, "ImageSnap is not initialized");
        if (onSnapDone) {
            onSnapDone(false);
        }
        return false;
    }
    /* Step.5 Stream On */
    int ret = sample_framesource_streamon();
    if (ret < 0) {
        Logger::log(LogLevel::ERROR, "FrameSource StreamOn failed");
        for (int i = 0; i < FS_CHN_NUM; ++i) {
            if (chn[i].enable) {
                IMP_System_UnBind(&chn[i].framesource_chn, &chn[i].imp_encoder);
            }
        }
        if (onSnapDone) {
            onSnapDone(false);
        }
        return false;
    }

    bool result = true;

    auto sensorFilter = [](int i) {
        switch (SENSOR_NUM) {
            case IMPISP_TOTAL_ONE: return i == 0;
            case IMPISP_TOTAL_TWO: return i == 0 || i == 3;
            case IMPISP_TOTAL_THR: return i == 0 || i == 3 || i == 6;
            case IMPISP_TOTAL_FOU: return i == 0 || i == 3 || i == 6 || i == 9;
            default: return true;
        }
    };

    if (this->runMode == RunMode::NON_BLOCKING) {
        this->threads.emplace_back([this, filenames, onSnapDone, sensorFilter]() {
            bool nonBlockingResult = true;
            for (int i = 0; i < FS_CHN_NUM; ++i) {
                if (chn[i].enable && sensorFilter(i)) {
                    int chnNum = (PT_JPEG << 16) | (12 + chn[i].index / 3);
                    if (!this->snap(chnNum, filenames)) {
                        nonBlockingResult = false;
                    }
                }
            }
            if (onSnapDone) {
                onSnapDone(nonBlockingResult);
            }
            sample_framesource_streamoff();
        });
        return true;
    } else {
        for (int i = 0; i < FS_CHN_NUM; ++i) {
            if (chn[i].enable && sensorFilter(i)) {
                int chnNum = (PT_JPEG << 16) | (12 + chn[i].index / 3);
                if (!snap(chnNum, filenames)) {
                    result = false;
                }
            }
        }
    }
	
    ret = sample_framesource_streamoff();
    if (ret < 0) {
        Logger::log(LogLevel::ERROR, "FrameSource StreamOff failed");
        for (int i = 0; i < FS_CHN_NUM; ++i) {
            if (chn[i].enable) {
                IMP_System_UnBind(&chn[i].framesource_chn, &chn[i].imp_encoder);
            }
        }
        result = false;
    }

    if (onSnapDone) {
        onSnapDone(result);
    }
    return result;
}

bool ImageSnap::snap(int chnNum, const std::vector<std::string> &filenames)
{
    int i = 0;
    int ret = 0;

    IMPFSI2DAttr i2d_attr;
    int s32picWidth = 0, s32picHeight = 0;

    srand((unsigned int)time(NULL));

    for (auto& filename : filenames) {
        ret = IMP_Encoder_StartRecvPic(chnNum);
        if (ret < 0) {
            Logger::log(LogLevel::ERROR, "IMP_Encoder_StartRecvPic(%d) failed", chnNum);
            return false;
        }
        Logger::log(LogLevel::DEBUG, "IMP_Encoder_StartRecvPic(%d) success", chnNum);
        memset(&i2d_attr, 0, sizeof(IMPFSI2DAttr));

        ret = IMP_FrameSource_GetI2dAttr((chnNum-12)*3, &i2d_attr);
        if(ret < 0){
            Logger::log(LogLevel::ERROR, "IMP_FrameSource_GetI2dAttr(%d) failed", (chnNum-12)*3);
            return false;
        }

        if((1 == i2d_attr.i2d_enable) &&
                ((i2d_attr.rotate_enable) && (i2d_attr.rotate_angle == 90 || i2d_attr.rotate_angle == 270))){
            /* this depend on your sensor or channels */
            s32picWidth  = chn[(chnNum-12)*3].fs_chn_attr.picHeight;
            s32picHeight = chn[(chnNum-12)*3].fs_chn_attr.picWidth;
        } else {
            s32picWidth  = chn[(chnNum-12)*3].fs_chn_attr.picWidth;
            s32picHeight = chn[(chnNum-12)*3].fs_chn_attr.picHeight;
        }

        Logger::log(LogLevel::DEBUG, "chnNum:%d, picWidth:%d, picHeight:%d", chnNum, s32picWidth, s32picHeight);
        Logger::log(LogLevel::DEBUG, "%s: Open file %s", __func__, filename.c_str());

        FILE* fp = fopen(filename.c_str(), "wb"); 
        if (fp == nullptr) {
            Logger::log(LogLevel::ERROR, "open %s failed", filename.c_str());
            return false;
        }

        /* Polling JPEG Snap, set timeout as 1000msec */
        ret = IMP_Encoder_PollingStream(chnNum, 1000);
        if (ret < 0) {
            Logger::log(LogLevel::ERROR, "IMP_Encoder_PollingStream(%d) timeout", chnNum);
            fclose(fp); 
            return false;
        }

        IMPEncoderStream stream;
        /* Get JPEG Snap */
        ret = IMP_Encoder_GetStream(chnNum, &stream, 1);
        if (ret < 0) {
            Logger::log(LogLevel::ERROR, "IMP_Encoder_GetStream(%d) failed", chnNum);
            fclose(fp); 
            return false;
        }

        for (i = 0; i < stream.packCount; i++) {
            size_t written = fwrite((void *)stream.pack[i].virAddr, 1, stream.pack[i].length, fp); 
            if (written != stream.pack[i].length) {
                Logger::log(LogLevel::ERROR, "stream write failed");
                fclose(fp); 
                return false;
            }
        }

        IMP_Encoder_ReleaseStream(chnNum, &stream);

        fclose(fp); 

        ret = IMP_Encoder_StopRecvPic(chnNum);
        if (ret < 0) {
            Logger::log(LogLevel::ERROR, "IMP_Encoder_StopRecvPic(%d) failed", chnNum);
            return false;
        }
    }
    return true;
}

static bool shouldProcessChannel(int index) {
    switch (SENSOR_NUM) {
        case IMPISP_TOTAL_ONE: return index == 0;
        case IMPISP_TOTAL_TWO: return index == 0 || index == 3;
        case IMPISP_TOTAL_THR: return index == 0 || index == 3 || index == 6;
        case IMPISP_TOTAL_FOU: return index == 0 || index == 3 || index == 6 || index == 9;
        default: return false;
    }
}

bool ImageSnap::initJpeg()
{
    int i = 0;
    int ret = 0;
    IMPEncoderAttr *enc_attr;
    IMPEncoderRcAttr *rc_attr;
    IMPEncoderCHNAttr channel_attr;
    IMPFSChnAttr *imp_chn_attr_tmp;

    for (i = 0; i < FS_CHN_NUM; i++) {
        if (chn[i].enable && shouldProcessChannel(chn[i].index)) {
            imp_chn_attr_tmp = &chn[i].fs_chn_attr;
            memset(&channel_attr, 0, sizeof(IMPEncoderCHNAttr));
            enc_attr = &channel_attr.encAttr;
            enc_attr->enType = PT_JPEG;
            enc_attr->bufSize = 0;
            enc_attr->profile = 0;
            enc_attr->picWidth = imp_chn_attr_tmp->picWidth;
            enc_attr->picHeight = imp_chn_attr_tmp->picHeight;
            rc_attr = &channel_attr.rcAttr;
            rc_attr->attrRcMode.rcMode = ENC_RC_MODE_FIXQP;
            rc_attr->attrRcMode.attrJPEGFixQp.qp = 40;
            Logger::log(LogLevel::DEBUG, "direct_switch:%d, index:%d", direct_switch, chn[i].index);
            if(direct_switch == 1) {
                if (0 == chn[i].index)
                    channel_attr.bEnableIvdc = true;
            } else if (direct_switch == 2) {
                if (0 == chn[i].index || 3 == chn[i].index)
                    channel_attr.bEnableIvdc = true;
            } else if (direct_switch == 3) {
                if (0 == chn[i].index || 3 == chn[i].index || 6 == chn[i].index)
                    channel_attr.bEnableIvdc = true;
            } else if (direct_switch == 4) {
                if (0 == chn[i].index || 3 == chn[i].index || 6 == chn[i].index || 9 == chn[i].index)
                    channel_attr.bEnableIvdc = true;
            }

            /* Create Channel */
            ret = IMP_Encoder_CreateChn(12+chn[i].index/3, &channel_attr);
            if (ret < 0) {
                Logger::log(LogLevel::ERROR, "IMP_Encoder_CreateChn(%d) failed", 12+chn[i].index/3);
                return false;
            }

            /* Register Channel */
            ret = IMP_Encoder_RegisterChn(chn[i].index, 12+chn[i].index/3);
            if (ret < 0) {
                Logger::log(LogLevel::ERROR, "IMP_Encoder_RegisterChn(group%d, chn%d) failed", chn[i].index, 12+chn[i].index/3);
                return false;
            }
        }
    }

    return true;
}

bool ImageSnap::uninitJpeg(void)
{
    int i = 0;
    int ret = 0;
    int chnNum = 0;
    IMPEncoderCHNStat chn_stat;

    for (i = 0; i < FS_CHN_NUM; i++) {
        if (chn[i].enable && shouldProcessChannel(chn[i].index)) {
            chnNum = 12+chn[i].index/3;

            memset(&chn_stat, 0, sizeof(IMPEncoderCHNStat));
            ret = IMP_Encoder_Query(chnNum, &chn_stat);
            if (ret < 0) {
                Logger::log(LogLevel::ERROR, "IMP_Encoder_Query(%d) failed", chnNum);
                return false;
            }

            if (chn_stat.registered) {
                ret = IMP_Encoder_UnRegisterChn(chnNum);
                if (ret < 0) {
                    Logger::log(LogLevel::ERROR, "IMP_Encoder_UnRegisterChn(%d) failed", chnNum);
                    return false;
                }

                ret = IMP_Encoder_DestroyChn(chnNum);
                if (ret < 0) {
                    Logger::log(LogLevel::ERROR, "IMP_Encoder_DestroyChn(%d) failed", chnNum);
                    return false;
                }
            }
        }
    }

    return true;
}