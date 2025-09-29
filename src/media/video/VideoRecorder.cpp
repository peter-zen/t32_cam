#include <vector>
#include <thread>
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
#include "sample-common.h"
#include "minimp4.h"
#include "VideoRecorder.h"
#include "DayNightSwitch.h"

using namespace media;

extern "C" {
    extern struct chn_conf chn[];
    extern int direct_switch;
    extern int S_RC_METHOD;
}

static int writeCallback(int64_t offset, const void *buffer, size_t size, void *token)
{
    FILE *f = (FILE*)token;
    fseek(f, offset, SEEK_SET);
    return fwrite(buffer, 1, size, f) != size;
}

VideoRecorderParams::VideoRecorderParams()
{
    this->nchannels = 1;
	this->fps = 15;
	this->width = 1920;
	this->height = 1080;
}

void VideoRecorderParams::setVideoSize(int width, int height)
{ 
	this->width = width; 
	this->height = height; 
}

void VideoRecorderParams::getVideoSize(int &width, int &height) const
{ 
	width = this->width; 
	height = this->height; 
}

int VideoRecorderParams::getFrameSourceChnNum() const
{
	return nchannels;
}

void VideoRecorderParams::setFrameSourceChnNum(int nchannels)
{ 
	this->nchannels = nchannels;
}

void VideoRecorderParams::setFps(int fps)
{
    this->fps = fps;
}

int VideoRecorderParams::getFps() const
{
    return this->fps;
}

void VideoRecorderParams::setBitrate(int bitrate)
{
    this->bitrate = bitrate;
}
int VideoRecorderParams::getBitrate() const
{
    return this->bitrate;
}

VideoRecorder::VideoRecorder()
    : initialized(initialize())
	, stopRecording(false)
{

}

VideoRecorder::VideoRecorder(const VideoRecorderParams &params)
    : initialized(initialize())
    , params(params)
{
	
}

VideoRecorder::~VideoRecorder()
{
    deinitialize();
}

bool VideoRecorder::setParams(const VideoRecorderParams& params) {
    this->params = params;
    return true;
}

bool VideoRecorder::initialize()
{
    int ret = 0;
	int width = 0, height = 0;

	/* Step.1 System init */
	ret = sample_system_init();
	if (ret < 0) {
		Logger::log(LogLevel::ERROR,"System init failed");
		return false;
	}
    Logger::log(LogLevel::DEBUG, "System init success");

	this->params.getVideoSize(width, height);
    Logger::log(LogLevel::INFO, "Video width:%d height:%d", width, height);
	chn[VIDEO_RECORDER_CHN_NUM].fs_chn_attr.scaler.enable = 1;
    chn[VIDEO_RECORDER_CHN_NUM].fs_chn_attr.scaler.outwidth = width;
    chn[VIDEO_RECORDER_CHN_NUM].fs_chn_attr.scaler.outheight = height;
    chn[VIDEO_RECORDER_CHN_NUM].fs_chn_attr.picWidth = width;
    chn[VIDEO_RECORDER_CHN_NUM].fs_chn_attr.picHeight = height;

    /* Step.2 FrameSource init */
    ret = sample_framesource_init();
    if (ret < 0) {
        Logger::log(LogLevel::ERROR, "FrameSource init failed");
        sample_system_exit();
        return false;
    }
    Logger::log(LogLevel::DEBUG, "FrameSource init success");
    /* Step.3 Encoder init */
	if (chn[VIDEO_RECORDER_CHN_NUM].enable) {
		ret = IMP_Encoder_CreateGroup(chn[VIDEO_RECORDER_CHN_NUM].index);
		if (ret < 0) {
			Logger::log(LogLevel::ERROR, "Encoder CreateGroup(%d) failed", chn[VIDEO_RECORDER_CHN_NUM].index);
			sample_framesource_exit();
			sample_system_exit();
			return false;
		}
	}
    Logger::log(LogLevel::DEBUG, "Encoder CreateGroup success");

    if (!initVideo()) {
        Logger::log(LogLevel::ERROR, "Video init failed");
		if (chn[VIDEO_RECORDER_CHN_NUM].enable) {
			IMP_Encoder_DestroyGroup(chn[VIDEO_RECORDER_CHN_NUM].index);
		}
        sample_framesource_exit();
        sample_system_exit();
        return false;
    }
    Logger::log(LogLevel::DEBUG, "Video init success");

	/* Step.4 Bind */
	if (chn[VIDEO_RECORDER_CHN_NUM].enable) {
		ret = IMP_System_Bind(&chn[VIDEO_RECORDER_CHN_NUM].framesource_chn, &chn[VIDEO_RECORDER_CHN_NUM].imp_encoder);
		if (ret < 0) {
			Logger::log(LogLevel::ERROR, "Bind FrameSource%d and Encoder%d failed", chn[VIDEO_RECORDER_CHN_NUM].framesource_chn.groupID, chn[VIDEO_RECORDER_CHN_NUM].imp_encoder.groupID);
			return false;
		}
	}
    Logger::log(LogLevel::DEBUG, "Bind success");
    return true;
}

void VideoRecorder::deinitialize()
{
    if (initialized) {
        for (auto& thread : threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        threads.clear();

        /* Step.8 UnBind */
        int ret = 0;
		if (chn[VIDEO_RECORDER_CHN_NUM].enable) {
			ret = IMP_System_UnBind(&chn[VIDEO_RECORDER_CHN_NUM].framesource_chn, &chn[VIDEO_RECORDER_CHN_NUM].imp_encoder);
			if (ret < 0) {
				Logger::log(LogLevel::ERROR, "UnBind FrameSource%d and Encoder%d failed", chn[VIDEO_RECORDER_CHN_NUM].framesource_chn.groupID, chn[VIDEO_RECORDER_CHN_NUM].imp_encoder.groupID);
				return;
			}
		}

        /* Step.9 Encoder exit */
        if (!uninitVideo()) {
            Logger::log(LogLevel::ERROR, "Video uninit failed");
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

bool VideoRecorder::record(const std::string &filename, int duration)
{
    return record(filename, nullptr, duration);
}


bool VideoRecorder::record(const std::string &filename, std::function<void(bool)> onRecordDone, int duration)
{
    if (!initialized) {
        Logger::log(LogLevel::ERROR, "VideoRecorder is not initialized");
        if (onRecordDone) {
            onRecordDone(false);
        }
        return false;
    }
    /* Step.5 Stream On */
    int ret = sample_framesource_streamon();
    if (ret < 0) {
        Logger::log(LogLevel::ERROR, "FrameSource StreamOn failed");
		if (chn[VIDEO_RECORDER_CHN_NUM].enable) {
			IMP_System_UnBind(&chn[VIDEO_RECORDER_CHN_NUM].framesource_chn, &chn[VIDEO_RECORDER_CHN_NUM].imp_encoder);
		}
        if (onRecordDone) {
            onRecordDone(false);
        }
        return false;
    }

	//day-night switch
	daynight_switch(true);

    /* Step.6 Get stream */
    bool result = true;

    if (onRecordDone) {
        this->threads.emplace_back([this, duration, filename, onRecordDone]() {
            bool nonBlockingResult = true;
			if (chn[VIDEO_RECORDER_CHN_NUM].enable && sensorFilter(VIDEO_RECORDER_CHN_NUM)) {
				if (!this->record(chn[VIDEO_RECORDER_CHN_NUM].index, chn[VIDEO_RECORDER_CHN_NUM].payloadType, filename, duration)) {
					nonBlockingResult = false;
				}
			}

            if (onRecordDone) {
                onRecordDone(nonBlockingResult);
            }
            sample_framesource_streamoff();
        });
        return true;
    } else {
		if (chn[VIDEO_RECORDER_CHN_NUM].enable && sensorFilter(VIDEO_RECORDER_CHN_NUM)) {
			if (!record(chn[VIDEO_RECORDER_CHN_NUM].index, chn[VIDEO_RECORDER_CHN_NUM].payloadType, filename, duration)) {
				result = false;
			}
		}
    }
	
    ret = sample_framesource_streamoff();
    if (ret < 0) {
        Logger::log(LogLevel::ERROR, "FrameSource StreamOff failed");
		if (chn[VIDEO_RECORDER_CHN_NUM].enable) {
			IMP_System_UnBind(&chn[VIDEO_RECORDER_CHN_NUM].framesource_chn, &chn[VIDEO_RECORDER_CHN_NUM].imp_encoder);
		}
        result = false;
    }

    if (onRecordDone) {
        onRecordDone(result);
    }
    return result;
}

bool VideoRecorder::record(int chnNum, int payloadType, const std::string &filename, int duration)
{
    int i = 0;
    int ret = 0;
	IMPFSI2DAttr i2d_attr;
	int s32picWidth = 0, s32picHeight = 0;

    ret = IMP_Encoder_StartRecvPic(chnNum);
    if (ret < 0) {
        Logger::log(LogLevel::ERROR, "IMP_Encoder_StartRecvPic(%d) failed", chnNum);
        return false;
    }
    Logger::log(LogLevel::DEBUG, "IMP_Encoder_StartRecvPic(%d) success", chnNum);
    memset(&i2d_attr, 0, sizeof(IMPFSI2DAttr));

    ret = IMP_FrameSource_GetI2dAttr(chnNum, &i2d_attr);
    if(ret < 0){
        Logger::log(LogLevel::ERROR, "IMP_FrameSource_GetI2dAttr(%d) failed", chnNum);
        IMP_Encoder_StopRecvPic(chnNum);
        return false;
    }

    if((1 == i2d_attr.i2d_enable) &&
            ((i2d_attr.rotate_enable) && (i2d_attr.rotate_angle == 90 || i2d_attr.rotate_angle == 270))){
        /* this depend on your sensor or channels */
        s32picWidth  = chn[chnNum].fs_chn_attr.picHeight;
        s32picHeight = chn[chnNum].fs_chn_attr.picWidth;
    } else {
        s32picWidth  = chn[chnNum].fs_chn_attr.picWidth;
        s32picHeight = chn[chnNum].fs_chn_attr.picHeight;
    }

    Logger::log(LogLevel::DEBUG, "chnNum:%d, picWidth:%d, picHeight:%d", chnNum, s32picWidth, s32picHeight);
    Logger::log(LogLevel::DEBUG, "%s: Open file %s", __func__, filename.c_str());

    //mux stream to file
    FILE *fp = fopen(filename.c_str(), "wb");
    if (!fp) {
        IMP_Encoder_StopRecvPic(chnNum);
        return false;
    }
    MP4E_mux_t *muxer = MP4E_open(0, 0, fp, writeCallback);

    if (!muxer) {
        IMP_Encoder_StopRecvPic(chnNum);
        fclose(fp);
        return false;
    }

    mp4_h26x_writer_t mp4wr;
    if (MP4E_STATUS_OK != mp4_h26x_write_init(&mp4wr, muxer, 352, 288, payloadType == PT_H265)) {
        IMP_Encoder_StopRecvPic(chnNum);
        MP4E_close(muxer);
        fclose(fp);
        return false;
    }

	IMPEncoderFrmRate frmRate;
	IMP_Encoder_GetChnFrmRate(chnNum, &frmRate);
	//int fps = params.getFps();
	int fps = frmRate.frmRateNum / frmRate.frmRateDen;
	int frameCount = duration*fps;
	Logger::log(LogLevel::DEBUG, "%s: duration:%d, frameCount:%d, fps:%d", __func__, duration, frameCount, fps);
	auto checkRecordCondition = [&frameCount, duration, this]() {
		if (this->stopRecording) {
			return false;
		}

		if (duration == 0) {
			return true;
		}

        if (frameCount > 0) {
            frameCount--;
            return true;
        }

		return false;
	};

    while (checkRecordCondition()) {
    	/* Polling stream, set timeout as 1000msec */
        ret = IMP_Encoder_PollingStream(chnNum, 1000);
        if (ret < 0) {
            Logger::log(LogLevel::ERROR, "IMP_Encoder_PollingStream(%d) timeout", chnNum);
            IMP_Encoder_StopRecvPic(chnNum);
            MP4E_close(muxer);
        	fclose(fp);
            return false;
        }

        IMPEncoderStream stream;
        /* Get stream */
        ret = IMP_Encoder_GetStream(chnNum, &stream, 1);
        if (ret < 0) {
            Logger::log(LogLevel::ERROR, "IMP_Encoder_GetStream(%d) failed", chnNum);
            IMP_Encoder_StopRecvPic(chnNum);
            MP4E_close(muxer);
            mp4_h26x_write_close(&mp4wr);
        	fclose(fp);
            return false;
        }
        
        
        for (i = 0; i < (int)stream.packCount; i++) {
            size_t datasize = stream.pack[i].length;
            uint8_t *inputData = (uint8_t*)stream.pack[i].virAddr;
            size_t pos = 0;
            while (pos < datasize) {
                //get NAL size
				ssize_t nal_size = getNALSize(inputData, datasize);
				if (nal_size < 4) {
					pos += 1;
					continue;
				}
				
                if (MP4E_STATUS_OK != mp4_h26x_write_nal(&mp4wr, inputData + pos, nal_size, 90000 / fps)) {
                    IMP_Encoder_ReleaseStream(chnNum, &stream);
                    IMP_Encoder_StopRecvPic(chnNum);
                    MP4E_close(muxer);
                    mp4_h26x_write_close(&mp4wr);
                    fclose(fp);
                    return false;
                }
                pos += nal_size;
            }
        }
        IMP_Encoder_ReleaseStream(chnNum, &stream);
    }

    MP4E_close(muxer);
	mp4_h26x_write_close(&mp4wr);
	fclose(fp);

    ret = IMP_Encoder_StopRecvPic(chnNum);
    if (ret < 0) {
        Logger::log(LogLevel::ERROR, "IMP_Encoder_StopRecvPic(%d) failed", chnNum);
        return false;
    }
    return true;
}

bool VideoRecorder::sensorFilter(int index)
{
    switch (SENSOR_NUM) {
        case IMPISP_TOTAL_ONE: return index == 0 || index == 1;
        case IMPISP_TOTAL_TWO: return index == 0 || index == 3;
        case IMPISP_TOTAL_THR: return index == 0 || index == 3 || index == 6;
        case IMPISP_TOTAL_FOU: return index == 0 || index == 3 || index == 6 || index == 9;
        default: return false;
    }
}

bool VideoRecorder::initVideo()
{
	int ret = 0;
	int chnNum = 0;
	int s32picWidth = 0, s32picHeight = 0;
	IMPEncoderAttr *enc_attr;
	IMPEncoderRcAttr *rc_attr;
	IMPFSChnAttr *imp_chn_attr_tmp;
	IMPEncoderCHNAttr channel_attr;
	IMPFSI2DAttr i2d_attr;

	if (chn[VIDEO_RECORDER_CHN_NUM].enable) {
		imp_chn_attr_tmp = &chn[VIDEO_RECORDER_CHN_NUM].fs_chn_attr;
		memset(&channel_attr, 0, sizeof(IMPEncoderCHNAttr));
		memset(&i2d_attr, 0, sizeof(IMPFSI2DAttr));

		ret = IMP_FrameSource_GetI2dAttr(chn[VIDEO_RECORDER_CHN_NUM].index, &i2d_attr);
		if(ret < 0){
			Logger::log(LogLevel::ERROR, "IMP_FrameSource_GetI2dAttr(%d) failed", chn[VIDEO_RECORDER_CHN_NUM].index);
			return false;
		}

		if((1 == i2d_attr.i2d_enable) &&
				((i2d_attr.rotate_enable) && (i2d_attr.rotate_angle == 90 || i2d_attr.rotate_angle == 270))){
			/* this depend on your sensor or channels */
			s32picWidth  = chn[VIDEO_RECORDER_CHN_NUM].fs_chn_attr.picHeight;
			s32picHeight = chn[VIDEO_RECORDER_CHN_NUM].fs_chn_attr.picWidth;
		} else {
			s32picWidth  = chn[VIDEO_RECORDER_CHN_NUM].fs_chn_attr.picWidth;
			s32picHeight = chn[VIDEO_RECORDER_CHN_NUM].fs_chn_attr.picHeight;
		}

		enc_attr = &channel_attr.encAttr;
		enc_attr->enType = chn[VIDEO_RECORDER_CHN_NUM].payloadType;
		if ((s32picHeight > 1920) || (s32picHeight == 1920)) {
			enc_attr->bufSize = s32picWidth * s32picHeight * 3 / 2;
		} else if ((s32picHeight > 1520) || (s32picHeight == 1520)) {
			enc_attr->bufSize = s32picWidth * s32picHeight * 3 / 8;
		} else if ((s32picHeight > 1080) || (s32picHeight == 1080)) {
			enc_attr->bufSize = s32picWidth * s32picHeight / 2;
		} else {
			enc_attr->bufSize = s32picWidth * s32picHeight * 3 / 4;
		}
		enc_attr->profile   = 1;
		enc_attr->picWidth  = s32picWidth;
		enc_attr->picHeight = s32picHeight;
		rc_attr = &channel_attr.rcAttr;
		rc_attr->attrHSkip.hSkipAttr.skipType = IMP_Encoder_STYPE_N1X;
		rc_attr->attrHSkip.hSkipAttr.m = 3;
		rc_attr->attrHSkip.hSkipAttr.n = 4;
		rc_attr->attrHSkip.hSkipAttr.maxSameSceneCnt = 0;
		rc_attr->attrHSkip.hSkipAttr.bEnableScenecut = 0;
		rc_attr->attrHSkip.hSkipAttr.bBlackEnhance = 0;
		rc_attr->attrHSkip.maxHSkipType = IMP_Encoder_STYPE_N1X;
		if (chn[VIDEO_RECORDER_CHN_NUM].payloadType == PT_H264) {
			chnNum = chn[VIDEO_RECORDER_CHN_NUM].index;
			rc_attr->outFrmRate.frmRateNum = imp_chn_attr_tmp->outFrmRateNum;
			rc_attr->outFrmRate.frmRateDen = imp_chn_attr_tmp->outFrmRateDen;
			rc_attr->maxGop = 2 * rc_attr->outFrmRate.frmRateNum / rc_attr->outFrmRate.frmRateDen;
			if (S_RC_METHOD == ENC_RC_MODE_FIXQP) {
				rc_attr->attrRcMode.rcMode = ENC_RC_MODE_FIXQP;
				rc_attr->attrRcMode.attrH264FixQp.IQp = 35;
				rc_attr->attrRcMode.attrH264FixQp.PQp = 35;
				rc_attr->attrRcMode.attrH264FixQp.blkQpEn = 0;
			} else if (S_RC_METHOD == ENC_RC_MODE_CBR) {
				rc_attr->attrRcMode.rcMode = ENC_RC_MODE_CBR;
				rc_attr->attrRcMode.attrH264Cbr.maxQp = 45;
				rc_attr->attrRcMode.attrH264Cbr.minQp = 15;
				rc_attr->attrRcMode.attrH264Cbr.blkQpEn = 0;
				rc_attr->attrRcMode.attrH264Cbr.initialQp = 35;
				rc_attr->attrRcMode.attrH264Cbr.maxIQp = 45;
				rc_attr->attrRcMode.attrH264Cbr.minIQp = 15;
				rc_attr->attrRcMode.attrH264Cbr.outBitRate = BITRATE_720P_Kbs;
				rc_attr->attrRcMode.attrH264Cbr.iBiasLvl = 0;
				rc_attr->attrRcMode.attrH264Cbr.IPfrmQPDelta = 8;
				rc_attr->attrRcMode.attrH264Cbr.PPfrmQPDelta = 8;
				rc_attr->attrRcMode.attrH264Cbr.staticTime = 2;
				rc_attr->attrRcMode.attrH264Cbr.flucLvl = 2;
				rc_attr->attrRcMode.attrH264Cbr.qualityLvl = 3;
				rc_attr->attrRcMode.attrH264Cbr.maxIprop = 100;
				rc_attr->attrRcMode.attrH264Cbr.minIprop = 1;
				rc_attr->attrRcMode.attrH264Cbr.maxIPictureSize = rc_attr->attrRcMode.attrH264Cbr.outBitRate * 6 / 5;
				rc_attr->attrRcMode.attrH264Cbr.maxPPictureSize = rc_attr->attrRcMode.attrH264Cbr.maxIPictureSize * 3 / 5;
			} else if (S_RC_METHOD == ENC_RC_MODE_VBR) {
				rc_attr->attrRcMode.rcMode = ENC_RC_MODE_VBR;
				rc_attr->attrRcMode.attrH264Vbr.maxQp = 45;
				rc_attr->attrRcMode.attrH264Vbr.minQp = 15;
				rc_attr->attrRcMode.attrH264Vbr.blkQpEn = 0;
				rc_attr->attrRcMode.attrH264Vbr.initialQp = 35;
				rc_attr->attrRcMode.attrH264Vbr.maxIQp = 45;
				rc_attr->attrRcMode.attrH264Vbr.minIQp = 15;
				rc_attr->attrRcMode.attrH264Vbr.maxBitRate = BITRATE_720P_Kbs;
				rc_attr->attrRcMode.attrH264Vbr.iBiasLvl = 0;
				rc_attr->attrRcMode.attrH264Vbr.changePos = 80;
				rc_attr->attrRcMode.attrH264Vbr.staticTime = 2;
				rc_attr->attrRcMode.attrH264Vbr.qualityLvl = 6;
				rc_attr->attrRcMode.attrH264Vbr.maxIprop = 100;
				rc_attr->attrRcMode.attrH264Vbr.minIprop = 1;
				rc_attr->attrRcMode.attrH264Vbr.maxIPictureSize = rc_attr->attrRcMode.attrH264Vbr.maxBitRate * 6 / 5;
				rc_attr->attrRcMode.attrH264Vbr.maxPPictureSize = rc_attr->attrRcMode.attrH264Vbr.maxIPictureSize * 3 / 5;
			} else if (S_RC_METHOD == ENC_RC_MODE_SMART) {
				rc_attr->attrRcMode.rcMode = ENC_RC_MODE_SMART;
				rc_attr->attrRcMode.attrH264Smart.maxQp = 45;
				rc_attr->attrRcMode.attrH264Smart.minQp = 15;
				rc_attr->attrRcMode.attrH264Smart.blkQpEn = 0;
				rc_attr->attrRcMode.attrH264Smart.initialQp = 35;
				rc_attr->attrRcMode.attrH264Smart.maxIQp = 45;
				rc_attr->attrRcMode.attrH264Smart.minIQp = 15;
				rc_attr->attrRcMode.attrH264Smart.maxBitRate = BITRATE_720P_Kbs;
				rc_attr->attrRcMode.attrH264Smart.iBiasLvl = 0;
				rc_attr->attrRcMode.attrH264Smart.changePos = 80;
				rc_attr->attrRcMode.attrH264Smart.staticTime = 2;
				rc_attr->attrRcMode.attrH264Smart.qualityLvl = 6;
				rc_attr->attrRcMode.attrH264Smart.maxIprop = 100;
				rc_attr->attrRcMode.attrH264Smart.minIprop = 1;
				rc_attr->attrRcMode.attrH264Smart.minStillRate = 25;
				rc_attr->attrRcMode.attrH264Smart.maxStillQp = 35;
				rc_attr->attrRcMode.attrH264Smart.superSmartEn = 0;
				rc_attr->attrRcMode.attrH264Smart.supSmtStillLvl = 5;
				rc_attr->attrRcMode.attrH264Smart.supSmtStillRateLvl = 2;
				rc_attr->attrRcMode.attrH264Smart.maxSupSmtStillRate = 20;
				rc_attr->attrRcMode.attrH264Smart.maxIPictureSize = rc_attr->attrRcMode.attrH264Smart.maxBitRate * 6 / 5;
				rc_attr->attrRcMode.attrH264Smart.maxPPictureSize = rc_attr->attrRcMode.attrH264Smart.maxIPictureSize * 3 / 5;
			} else if (S_RC_METHOD == ENC_RC_MODE_CVBR) {
				rc_attr->attrRcMode.rcMode = ENC_RC_MODE_CVBR;
				rc_attr->attrRcMode.attrH264CVbr.maxQp = 45;
				rc_attr->attrRcMode.attrH264CVbr.minQp = 15;
				rc_attr->attrRcMode.attrH264CVbr.blkQpEn = 0;
				rc_attr->attrRcMode.attrH264CVbr.initialQp = 35;
				rc_attr->attrRcMode.attrH264CVbr.maxIQp = 45;
				rc_attr->attrRcMode.attrH264CVbr.minIQp = 15;
				rc_attr->attrRcMode.attrH264CVbr.maxBitRate = BITRATE_720P_Kbs;
				rc_attr->attrRcMode.attrH264CVbr.longMaxBitRate = rc_attr->attrRcMode.attrH264CVbr.maxBitRate * 4 / 5;
				rc_attr->attrRcMode.attrH264CVbr.longMinBitRate = rc_attr->attrRcMode.attrH264CVbr.maxBitRate * 3 / 5;
				rc_attr->attrRcMode.attrH264CVbr.iBiasLvl = 0;
				rc_attr->attrRcMode.attrH264CVbr.changePos = 80;
				rc_attr->attrRcMode.attrH264CVbr.shortStatTime = 2;
				rc_attr->attrRcMode.attrH264CVbr.longStatTime = 60;
				rc_attr->attrRcMode.attrH264CVbr.qualityLvl = 6;
				rc_attr->attrRcMode.attrH264CVbr.maxIprop = 100;
				rc_attr->attrRcMode.attrH264CVbr.minIprop = 1;
				rc_attr->attrRcMode.attrH264CVbr.extraBitRate = 5;
				rc_attr->attrRcMode.attrH264CVbr.maxIPictureSize = rc_attr->attrRcMode.attrH264CVbr.maxBitRate * 6 / 5;
				rc_attr->attrRcMode.attrH264CVbr.maxPPictureSize = rc_attr->attrRcMode.attrH264CVbr.maxIPictureSize * 3 / 5;
			} else if (S_RC_METHOD == ENC_RC_MODE_AVBR){
				rc_attr->attrRcMode.rcMode = ENC_RC_MODE_AVBR;
				rc_attr->attrRcMode.attrH264AVbr.maxQp = 45;
				rc_attr->attrRcMode.attrH264AVbr.minQp = 15;
				rc_attr->attrRcMode.attrH264AVbr.blkQpEn = 0;
				rc_attr->attrRcMode.attrH264AVbr.initialQp = 35;
				rc_attr->attrRcMode.attrH264AVbr.maxIQp = 45;
				rc_attr->attrRcMode.attrH264AVbr.minIQp = 15;
				rc_attr->attrRcMode.attrH264AVbr.maxBitRate = BITRATE_720P_Kbs;
				rc_attr->attrRcMode.attrH264AVbr.iBiasLvl = 0;
				rc_attr->attrRcMode.attrH264AVbr.changePos = 80;
				rc_attr->attrRcMode.attrH264AVbr.staticTime = 2;
				rc_attr->attrRcMode.attrH264AVbr.qualityLvl = 6;
				rc_attr->attrRcMode.attrH264AVbr.maxIprop = 100;
				rc_attr->attrRcMode.attrH264AVbr.minIprop = 1;
				rc_attr->attrRcMode.attrH264AVbr.minStillRate = 25;
				rc_attr->attrRcMode.attrH264AVbr.maxStillQp = 35;
				rc_attr->attrRcMode.attrH264AVbr.maxIPictureSize = rc_attr->attrRcMode.attrH264AVbr.maxBitRate * 6 / 5;
				rc_attr->attrRcMode.attrH264AVbr.maxPPictureSize = rc_attr->attrRcMode.attrH264AVbr.maxIPictureSize * 3 / 5;
			} else {
				Logger::log(LogLevel::ERROR, "S_RC_METHOD error!");
				return false;
			}
		} else if (chn[VIDEO_RECORDER_CHN_NUM].payloadType == PT_H265) { /* PT_H265 */
			chnNum = chn[VIDEO_RECORDER_CHN_NUM].index;
			rc_attr = &channel_attr.rcAttr;
			rc_attr->outFrmRate.frmRateNum = imp_chn_attr_tmp->outFrmRateNum;
			rc_attr->outFrmRate.frmRateDen = imp_chn_attr_tmp->outFrmRateDen;
			rc_attr->maxGop = 2 * rc_attr->outFrmRate.frmRateNum / rc_attr->outFrmRate.frmRateDen;
			if (S_RC_METHOD == ENC_RC_MODE_FIXQP) {
				rc_attr->attrRcMode.rcMode = ENC_RC_MODE_FIXQP;
				rc_attr->attrRcMode.attrH265FixQp.IQp = 35;
				rc_attr->attrRcMode.attrH265FixQp.PQp = 35;
				rc_attr->attrRcMode.attrH265FixQp.blkQpEn = 0;
			} else if (S_RC_METHOD == ENC_RC_MODE_CBR) {
				rc_attr->attrRcMode.rcMode = ENC_RC_MODE_CBR;
				rc_attr->attrRcMode.attrH265Cbr.maxQp = 45;
				rc_attr->attrRcMode.attrH265Cbr.minQp = 15;
				rc_attr->attrRcMode.attrH265Cbr.blkQpEn = 0;
				rc_attr->attrRcMode.attrH265Cbr.initialQp = 35;
				rc_attr->attrRcMode.attrH265Cbr.maxIQp = 45;
				rc_attr->attrRcMode.attrH265Cbr.minIQp = 15;
				rc_attr->attrRcMode.attrH265Cbr.outBitRate = BITRATE_720P_Kbs;
				rc_attr->attrRcMode.attrH265Cbr.iBiasLvl = 0;
				rc_attr->attrRcMode.attrH265Cbr.IPfrmQPDelta = 8;
				rc_attr->attrRcMode.attrH265Cbr.PPfrmQPDelta = 8;
				rc_attr->attrRcMode.attrH265Cbr.staticTime = 2;
				rc_attr->attrRcMode.attrH265Cbr.flucLvl = 2;
				rc_attr->attrRcMode.attrH265Cbr.qualityLvl = 3;
				rc_attr->attrRcMode.attrH265Cbr.maxIprop = 100;
				rc_attr->attrRcMode.attrH265Cbr.minIprop = 1;
				rc_attr->attrRcMode.attrH265Cbr.maxIPictureSize = rc_attr->attrRcMode.attrH265Cbr.outBitRate * 6 / 5;
				rc_attr->attrRcMode.attrH265Cbr.maxPPictureSize = rc_attr->attrRcMode.attrH265Cbr.maxIPictureSize * 3 / 5;
			} else if (S_RC_METHOD == ENC_RC_MODE_VBR) {
				rc_attr->attrRcMode.rcMode = ENC_RC_MODE_VBR;
				rc_attr->attrRcMode.attrH265Vbr.maxQp = 45;
				rc_attr->attrRcMode.attrH265Vbr.minQp = 15;
				rc_attr->attrRcMode.attrH265Vbr.blkQpEn = 0;
				rc_attr->attrRcMode.attrH265Vbr.initialQp = 35;
				rc_attr->attrRcMode.attrH265Vbr.maxIQp = 45;
				rc_attr->attrRcMode.attrH265Vbr.minIQp = 15;
				rc_attr->attrRcMode.attrH265Vbr.maxBitRate = BITRATE_720P_Kbs;
				rc_attr->attrRcMode.attrH265Vbr.iBiasLvl = 0;
				rc_attr->attrRcMode.attrH265Vbr.changePos = 80;
				rc_attr->attrRcMode.attrH265Vbr.staticTime = 2;
				rc_attr->attrRcMode.attrH265Vbr.qualityLvl = 6;
				rc_attr->attrRcMode.attrH265Vbr.maxIprop = 100;
				rc_attr->attrRcMode.attrH265Vbr.minIprop = 1;
				rc_attr->attrRcMode.attrH265Vbr.maxIPictureSize = rc_attr->attrRcMode.attrH265Vbr.maxBitRate * 6 / 5;
				rc_attr->attrRcMode.attrH265Vbr.maxPPictureSize = rc_attr->attrRcMode.attrH265Vbr.maxIPictureSize * 3 / 5;
			} else if (S_RC_METHOD == ENC_RC_MODE_SMART) {
				rc_attr->attrRcMode.rcMode = ENC_RC_MODE_SMART;
				rc_attr->attrRcMode.attrH265Smart.maxQp = 45;
				rc_attr->attrRcMode.attrH265Smart.minQp = 15;
				rc_attr->attrRcMode.attrH265Smart.blkQpEn = 0;
				rc_attr->attrRcMode.attrH265Smart.initialQp = 35;
				rc_attr->attrRcMode.attrH265Smart.maxIQp = 45;
				rc_attr->attrRcMode.attrH265Smart.minIQp = 15;
				rc_attr->attrRcMode.attrH265Smart.maxBitRate = BITRATE_720P_Kbs;
				rc_attr->attrRcMode.attrH265Smart.iBiasLvl = 0;
				rc_attr->attrRcMode.attrH265Smart.changePos = 80;
				rc_attr->attrRcMode.attrH265Smart.staticTime = 2;
				rc_attr->attrRcMode.attrH265Smart.qualityLvl = 6;
				rc_attr->attrRcMode.attrH265Smart.maxIprop = 100;
				rc_attr->attrRcMode.attrH265Smart.minIprop = 1;
				rc_attr->attrRcMode.attrH265Smart.minStillRate = 25;
				rc_attr->attrRcMode.attrH265Smart.maxStillQp = 35;
				rc_attr->attrRcMode.attrH265Smart.superSmartEn = 0;
				rc_attr->attrRcMode.attrH265Smart.supSmtStillLvl = 5;
				rc_attr->attrRcMode.attrH265Smart.supSmtStillRateLvl = 2;
				rc_attr->attrRcMode.attrH265Smart.maxSupSmtStillRate = 20;
				rc_attr->attrRcMode.attrH265Smart.maxIPictureSize = rc_attr->attrRcMode.attrH265Smart.maxBitRate * 6 / 5;
				rc_attr->attrRcMode.attrH265Smart.maxPPictureSize = rc_attr->attrRcMode.attrH265Smart.maxIPictureSize * 3 / 5;
			} else if (S_RC_METHOD == ENC_RC_MODE_CVBR) {
				rc_attr->attrRcMode.rcMode = ENC_RC_MODE_CVBR;
				rc_attr->attrRcMode.attrH265CVbr.maxQp = 45;
				rc_attr->attrRcMode.attrH265CVbr.minQp = 15;
				rc_attr->attrRcMode.attrH265CVbr.blkQpEn = 0;
				rc_attr->attrRcMode.attrH265CVbr.initialQp = 35;
				rc_attr->attrRcMode.attrH265CVbr.maxIQp = 45;
				rc_attr->attrRcMode.attrH265CVbr.minIQp = 15;
				rc_attr->attrRcMode.attrH265CVbr.maxBitRate = BITRATE_720P_Kbs;
				rc_attr->attrRcMode.attrH265CVbr.longMaxBitRate = rc_attr->attrRcMode.attrH265CVbr.maxBitRate * 4 / 5;
				rc_attr->attrRcMode.attrH265CVbr.longMinBitRate = rc_attr->attrRcMode.attrH265CVbr.maxBitRate * 3 / 5;
				rc_attr->attrRcMode.attrH265CVbr.iBiasLvl = 0;
				rc_attr->attrRcMode.attrH265CVbr.changePos = 80;
				rc_attr->attrRcMode.attrH265CVbr.shortStatTime = 2;
				rc_attr->attrRcMode.attrH265CVbr.longStatTime = 60;
				rc_attr->attrRcMode.attrH265CVbr.qualityLvl = 6;
				rc_attr->attrRcMode.attrH265CVbr.maxIprop = 100;
				rc_attr->attrRcMode.attrH265CVbr.minIprop = 1;
				rc_attr->attrRcMode.attrH265CVbr.extraBitRate = 5;
				rc_attr->attrRcMode.attrH265CVbr.maxIPictureSize = rc_attr->attrRcMode.attrH265CVbr.maxBitRate * 6 / 5;
				rc_attr->attrRcMode.attrH265CVbr.maxPPictureSize = rc_attr->attrRcMode.attrH265CVbr.maxIPictureSize * 3 / 5;
			} else if (S_RC_METHOD == ENC_RC_MODE_AVBR) {
				rc_attr->attrRcMode.rcMode = ENC_RC_MODE_AVBR;
				rc_attr->attrRcMode.attrH265AVbr.maxQp = 45;
				rc_attr->attrRcMode.attrH265AVbr.minQp = 15;
				rc_attr->attrRcMode.attrH265AVbr.blkQpEn = 0;
				rc_attr->attrRcMode.attrH265AVbr.initialQp = 35;
				rc_attr->attrRcMode.attrH265AVbr.maxIQp = 45;
				rc_attr->attrRcMode.attrH265AVbr.minIQp = 15;
				rc_attr->attrRcMode.attrH265AVbr.maxBitRate = BITRATE_720P_Kbs;
				rc_attr->attrRcMode.attrH265AVbr.iBiasLvl = 0;
				rc_attr->attrRcMode.attrH265AVbr.changePos = 80;
				rc_attr->attrRcMode.attrH265AVbr.staticTime = 2;
				rc_attr->attrRcMode.attrH265AVbr.qualityLvl = 6;
				rc_attr->attrRcMode.attrH265AVbr.maxIprop = 100;
				rc_attr->attrRcMode.attrH265AVbr.minIprop = 1;
				rc_attr->attrRcMode.attrH265AVbr.minStillRate = 25;
				rc_attr->attrRcMode.attrH265AVbr.maxStillQp = 35;
				rc_attr->attrRcMode.attrH265AVbr.maxIPictureSize = rc_attr->attrRcMode.attrH265AVbr.maxBitRate * 6 / 5;
				rc_attr->attrRcMode.attrH265AVbr.maxPPictureSize = rc_attr->attrRcMode.attrH265AVbr.maxIPictureSize * 3 / 5;
			} else {
				Logger::log(LogLevel::ERROR, "S_RC_METHOD error!");
				return false;
			}
		}

		if(direct_switch == 1) {
			if (0 == chn[VIDEO_RECORDER_CHN_NUM].index)
				channel_attr.bEnableIvdc = true;
		} else if (direct_switch == 2) {
			if (0 == chn[VIDEO_RECORDER_CHN_NUM].index || 3 == chn[VIDEO_RECORDER_CHN_NUM].index)
				channel_attr.bEnableIvdc = true;
		} else if (direct_switch == 3) {
			if (0 == chn[VIDEO_RECORDER_CHN_NUM].index || 3 == chn[VIDEO_RECORDER_CHN_NUM].index || 6 == chn[VIDEO_RECORDER_CHN_NUM].index)
				channel_attr.bEnableIvdc = true;
		} else if (direct_switch == 4) {
			if (0 == chn[VIDEO_RECORDER_CHN_NUM].index || 3 == chn[VIDEO_RECORDER_CHN_NUM].index || 6 == chn[VIDEO_RECORDER_CHN_NUM].index || 9 == chn[VIDEO_RECORDER_CHN_NUM].index)
				channel_attr.bEnableIvdc = true;
		}

		ret = IMP_Encoder_CreateChn(chnNum, &channel_attr);
		if (ret < 0) {
			Logger::log(LogLevel::ERROR, "IMP_Encoder_CreateChn(%d) failed", chnNum);
			return false;
		}

		ret = IMP_Encoder_RegisterChn(chn[VIDEO_RECORDER_CHN_NUM].index, chnNum);
		if (ret < 0) {
			Logger::log(LogLevel::ERROR, "IMP_Encoder_RegisterChn(group%d, chn%d) failed", chn[VIDEO_RECORDER_CHN_NUM].index, chnNum);
			return false;
		}
	}

    return true;
}

bool VideoRecorder::uninitVideo(void)
{
	int ret = 0;
	int chnNum = 0;
	IMPEncoderCHNStat chn_stat;

	if (chn[VIDEO_RECORDER_CHN_NUM].enable) {
		chnNum = chn[VIDEO_RECORDER_CHN_NUM].index;
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

ssize_t VideoRecorder::getNALSize(uint8_t *buf, ssize_t size)
{
    ssize_t pos = 3;
    while ((size - pos) > 3)
    {
        if (buf[pos] == 0 && buf[pos + 1] == 0 && buf[pos + 2] == 1)
            return pos;
        if (buf[pos] == 0 && buf[pos + 1] == 0 && buf[pos + 2] == 0 && buf[pos + 3] == 1)
            return pos;
        pos++;
    }
    return size;
}

bool VideoRecorder::stopRecorder()
{
    stopRecording = true;
    return true;
}

bool VideoRecorder::daynight_switch(bool on)
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
		daynight_controller->startAutoSwithch();
    } else {
        daynight_controller->controlISP(DayNightState::DAY);
        daynight_controller->controlIRCut(DayNightState::DAY);
        daynight_controller->controlIRLed(DayNightState::DAY);
		daynight_controller->suspendAutoSwitch();
    }

    return true;
}