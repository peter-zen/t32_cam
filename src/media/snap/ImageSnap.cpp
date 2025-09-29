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
#include "DayNightSwitch.h"

using namespace media;

extern "C" {
    extern struct chn_conf chn[];
    extern int direct_switch;
}
ImageSnapParams::ImageSnapParams()
{
    this->nchannels = 1;
    this->width = 1920; 
	this->height = 1080; 
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
{

}

ImageSnap::ImageSnap(const ImageSnapParams &params)
    : initialized(initialize())
    , params(params)
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
    int ret = 0;

	/* Step.1 System init */
	ret = sample_system_init();
	if (ret < 0) {
		Logger::log(LogLevel::ERROR,"System init failed");
		return false;
	}
    Logger::log(LogLevel::DEBUG, "System init success");
    /* Step.2 FrameSource init */
    ret = sample_framesource_init();
    if (ret < 0) {
        Logger::log(LogLevel::ERROR, "FrameSource init failed");
        sample_system_exit();
        return false;
    }
    Logger::log(LogLevel::DEBUG, "FrameSource init success");
    /* Step.3 Encoder init */
    if (chn[SNAP_SENSOR_CHN_NUM].enable) {
        ret = IMP_Encoder_CreateGroup(chn[SNAP_SENSOR_CHN_NUM].index);
        if (ret < 0) {
            Logger::log(LogLevel::ERROR, "Encoder CreateGroup(%d) failed", chn[SNAP_SENSOR_CHN_NUM].index);
            sample_framesource_exit();
            sample_system_exit();
            return false;
        }
    }
    Logger::log(LogLevel::DEBUG, "Encoder CreateGroup success");
    if (!initJpeg()) {
        Logger::log(LogLevel::ERROR, "Jpeg init failed");
        if (chn[SNAP_SENSOR_CHN_NUM].enable) {
            IMP_Encoder_DestroyGroup(chn[SNAP_SENSOR_CHN_NUM].index);
        }
        sample_framesource_exit();
        sample_system_exit();
        return false;
    }
    Logger::log(LogLevel::DEBUG, "Jpeg init success");
#ifdef JPEGQUAILTY_CHANGE
    int jpegQp = 20;
    if (chn[SNAP_SENSOR_CHN_NUM].enable) {
        IMP_Encoder_SetJpegQp(12+chn[SNAP_SENSOR_CHN_NUM].index/3, jpegQp);
    }
#endif
	/* Step.4 Bind */
    if (chn[SNAP_SENSOR_CHN_NUM].enable) {
        ret = IMP_System_Bind(&chn[SNAP_SENSOR_CHN_NUM].framesource_chn, &chn[SNAP_SENSOR_CHN_NUM].imp_encoder);
        if (ret < 0) {
            Logger::log(LogLevel::ERROR, "Bind FrameSource%d and Encoder%d failed", chn[SNAP_SENSOR_CHN_NUM].framesource_chn.groupID, chn[SNAP_SENSOR_CHN_NUM].imp_encoder.groupID);
            return false;
        }
    }
    Logger::log(LogLevel::DEBUG, "Bind success");
    return true;
}

void ImageSnap::deinitialize()
{
    if (initialized) {
        daynight_switch(false);
        for (auto& thread : threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        threads.clear();

        /* Step.8 UnBind */
        int ret = 0;
        if (chn[SNAP_SENSOR_CHN_NUM].enable) {
            ret = IMP_System_UnBind(&chn[SNAP_SENSOR_CHN_NUM].framesource_chn, &chn[SNAP_SENSOR_CHN_NUM].imp_encoder);
            if (ret < 0) {
                Logger::log(LogLevel::ERROR, "UnBind FrameSource%d and Encoder%d failed", chn[SNAP_SENSOR_CHN_NUM].framesource_chn.groupID, chn[SNAP_SENSOR_CHN_NUM].imp_encoder.groupID);
                return;
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
        if (chn[SNAP_SENSOR_CHN_NUM].enable) {
            IMP_System_UnBind(&chn[SNAP_SENSOR_CHN_NUM].framesource_chn, &chn[SNAP_SENSOR_CHN_NUM].imp_encoder);
        }
        if (onSnapDone) {
            onSnapDone(false);
        }
        return false;
    }

    /* day-night switch */
    daynight_switch(true);

    bool result = true;

    if (onSnapDone) {
        this->threads.emplace_back([this, filenames, onSnapDone]() {
            bool nonBlockingResult = true;
            if (chn[SNAP_SENSOR_CHN_NUM].enable && sensorFilter(SNAP_SENSOR_CHN_NUM)) {
                int chnNum = (PT_JPEG << 16) | (12 + chn[SNAP_SENSOR_CHN_NUM].index / 3);
                if (!this->snap(chnNum, filenames)) {
                    nonBlockingResult = false;
                }
            }
        
            if (onSnapDone) {
                onSnapDone(nonBlockingResult);
            }
            sample_framesource_streamoff();
        });
        return true;
    } else {
        if (chn[SNAP_SENSOR_CHN_NUM].enable && sensorFilter(SNAP_SENSOR_CHN_NUM)) {
            int chnNum = (PT_JPEG << 16) | (12 + chn[SNAP_SENSOR_CHN_NUM].index / 3);
            if (!snap(chnNum, filenames)) {
                result = false;
            }
        }
    }
	
    ret = sample_framesource_streamoff();
    if (ret < 0) {
        Logger::log(LogLevel::ERROR, "FrameSource StreamOff failed");
        if (chn[SNAP_SENSOR_CHN_NUM].enable) {
            IMP_System_UnBind(&chn[SNAP_SENSOR_CHN_NUM].framesource_chn, &chn[SNAP_SENSOR_CHN_NUM].imp_encoder);
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
            IMP_Encoder_StopRecvPic(chnNum);
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
            IMP_Encoder_StopRecvPic(chnNum);
            return false;
        }

        /* Polling JPEG Snap, set timeout as 1000msec */
        ret = IMP_Encoder_PollingStream(chnNum, 1000);
        if (ret < 0) {
            Logger::log(LogLevel::ERROR, "IMP_Encoder_PollingStream(%d) timeout", chnNum);
            fclose(fp); 
            IMP_Encoder_StopRecvPic(chnNum);
            return false;
        }

        IMPEncoderStream stream;
        /* Get JPEG Snap */
        ret = IMP_Encoder_GetStream(chnNum, &stream, 1);
        if (ret < 0) {
            Logger::log(LogLevel::ERROR, "IMP_Encoder_GetStream(%d) failed", chnNum);
            fclose(fp);
            IMP_Encoder_StopRecvPic(chnNum);
            return false;
        }

        for (i = 0; i < stream.packCount; i++) {
            size_t written = fwrite((void *)stream.pack[i].virAddr, 1, stream.pack[i].length, fp); 
            if (written != stream.pack[i].length) {
                Logger::log(LogLevel::ERROR, "stream write failed");
                fclose(fp);
                IMP_Encoder_ReleaseStream(chnNum, &stream);
                IMP_Encoder_StopRecvPic(chnNum);
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

bool ImageSnap::sensorFilter(int index)
{
    switch (SENSOR_NUM) {
        case IMPISP_TOTAL_ONE: return index == 0 || index == 1;
        case IMPISP_TOTAL_TWO: return index == 0 || index == 3;
        case IMPISP_TOTAL_THR: return index == 0 || index == 3 || index == 6;
        case IMPISP_TOTAL_FOU: return index == 0 || index == 3 || index == 6 || index == 9;
        default: return false;
    }
}
    
bool ImageSnap::initJpeg()
{
    int ret = 0;
    IMPEncoderAttr *enc_attr;
    IMPEncoderRcAttr *rc_attr;
    IMPEncoderCHNAttr channel_attr;
    IMPFSChnAttr *imp_chn_attr_tmp;
    int width = 0;
    int height = 0;

    if (chn[SNAP_SENSOR_CHN_NUM].enable && sensorFilter(chn[SNAP_SENSOR_CHN_NUM].index)) {
        imp_chn_attr_tmp = &chn[SNAP_SENSOR_CHN_NUM].fs_chn_attr;
        memset(&channel_attr, 0, sizeof(IMPEncoderCHNAttr));
        enc_attr = &channel_attr.encAttr;
        enc_attr->enType = PT_JPEG;
        enc_attr->bufSize = 0;
        enc_attr->profile = 0;
        this->params.getImageSize(width, height);
        Logger::log(LogLevel::INFO, "ImageSnap", "JPEG width:%d height:%d", width, height);
        enc_attr->picWidth = width;//imp_chn_attr_tmp->picWidth;
        enc_attr->picHeight = height;//imp_chn_attr_tmp->picHeight;
        rc_attr = &channel_attr.rcAttr;
        rc_attr->attrRcMode.rcMode = ENC_RC_MODE_FIXQP;
        rc_attr->attrRcMode.attrJPEGFixQp.qp = 40;
        Logger::log(LogLevel::DEBUG, "direct_switch:%d, index:%d", direct_switch, chn[SNAP_SENSOR_CHN_NUM].index);
        if(direct_switch == 1) {
            if (0 == chn[SNAP_SENSOR_CHN_NUM].index)
                channel_attr.bEnableIvdc = true;
        } else if (direct_switch == 2) {
            if (0 == chn[SNAP_SENSOR_CHN_NUM].index || 3 == chn[SNAP_SENSOR_CHN_NUM].index)
                channel_attr.bEnableIvdc = true;
        } else if (direct_switch == 3) {
            if (0 == chn[SNAP_SENSOR_CHN_NUM].index || 3 == chn[SNAP_SENSOR_CHN_NUM].index || 6 == chn[SNAP_SENSOR_CHN_NUM].index)
                channel_attr.bEnableIvdc = true;
        } else if (direct_switch == 4) {
            if (0 == chn[SNAP_SENSOR_CHN_NUM].index || 3 == chn[SNAP_SENSOR_CHN_NUM].index || 6 == chn[SNAP_SENSOR_CHN_NUM].index || 9 == chn[SNAP_SENSOR_CHN_NUM].index)
                channel_attr.bEnableIvdc = true;
        }

        /* Create Channel */
        ret = IMP_Encoder_CreateChn(12+chn[SNAP_SENSOR_CHN_NUM].index/3, &channel_attr);
        if (ret < 0) {
            Logger::log(LogLevel::ERROR, "IMP_Encoder_CreateChn(%d) failed", 12+chn[SNAP_SENSOR_CHN_NUM].index/3);
            return false;
        }

        /* Register Channel */
        ret = IMP_Encoder_RegisterChn(chn[SNAP_SENSOR_CHN_NUM].index, 12+chn[SNAP_SENSOR_CHN_NUM].index/3);
        if (ret < 0) {
            Logger::log(LogLevel::ERROR, "IMP_Encoder_RegisterChn(group%d, chn%d) failed", chn[SNAP_SENSOR_CHN_NUM].index, 12+chn[SNAP_SENSOR_CHN_NUM].index/3);
            return false;
        }
    }
    return true;
}

bool ImageSnap::uninitJpeg(void)
{
    int ret = 0;
    int chnNum = 0;
    IMPEncoderCHNStat chn_stat;

    if (chn[SNAP_SENSOR_CHN_NUM].enable && sensorFilter(chn[SNAP_SENSOR_CHN_NUM].index)) {
        chnNum = 12+chn[SNAP_SENSOR_CHN_NUM].index/3;

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

    return true;
}

bool ImageSnap::daynight_switch(bool on)
{
    auto daynight_controller = DayNightSwitch::getInstance();
    if (!daynight_controller) {
        return false;
    }

    daynight_controller->setCdsPins(CDS_SENSOR_PIN);
    daynight_controller->setIRLedPins(IR_LED_PIN);
    daynight_controller->setIRCutPins(IR_CUT_ENABLE_PIN, IR_CUT_CTRL_PIN);

    if (on) {
        auto daynight_state = daynight_controller->getDayNightState();
        daynight_controller->controlISP(daynight_state);
        daynight_controller->controlIRCut(daynight_state);
        daynight_controller->controlIRLed(daynight_state);
    } else {
        daynight_controller->controlISP(DayNightState::DAY);
        daynight_controller->controlIRCut(DayNightState::DAY);
        daynight_controller->controlIRLed(DayNightState::DAY);
    }

    return true;
}