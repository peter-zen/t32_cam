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
#include <algorithm>
#include "Common.h"
#include "Logger.h"
#include "sample-common.h"
#include "RtspServer.h"
#include "rtsp.h"

using namespace media;

frame_fifo_t RtspServer::frame_fifo = {0};

bool RtspServer::extractSpsPps(const uint8_t* h264Data, size_t dataSize, std::vector<uint8_t>& sps, std::vector<uint8_t>& pps)
{
    if (!h264Data || dataSize < 4) {
        Logger::log(LogLevel::ERROR, "Invalid H264 data or size");
        return false;
    }

    const uint8_t* dataEnd = h264Data + dataSize;
    const uint8_t* pos = h264Data;

    // Define NALU start codes
    const uint8_t startCode3[] = {0x00, 0x00, 0x01};
    const uint8_t startCode4[] = {0x00, 0x00, 0x00, 0x01};

    bool foundSps = false;
    bool foundPps = false;

    while (pos < dataEnd - 3) {
        // Check for 4-byte start code
        if (memcmp(pos, startCode4, 4) == 0) {
            pos += 4;
        }
        // Check for 3-byte start code
        else if (memcmp(pos, startCode3, 3) == 0) {
            pos += 3;
        }
        else {
            pos++;
            continue;
        }

        // Check if we have enough data for NALU header
        if (pos >= dataEnd) {
            break;
        }

        // Get NALU type (first 5 bits of the header)
        uint8_t naluType = pos[0] & 0x1F;

        // Find next start code to determine NALU length
        const uint8_t* nextStart = nullptr;
        const uint8_t* searchPos = pos + 1;

        while (searchPos < dataEnd - 3) {
            if (memcmp(searchPos, startCode4, 4) == 0) {
                nextStart = searchPos;
                break;
            }
            else if (memcmp(searchPos, startCode3, 3) == 0) {
                nextStart = searchPos;
                break;
            }
            searchPos++;
        }

        // If no next start code found, use end of data
        size_t naluLength = (nextStart) ? (nextStart - pos) : (dataEnd - pos);

        // Process SPS (type 7)
        if (naluType == 7) {
            sps.assign(pos, pos + naluLength);
            foundSps = true;
            Logger::log(LogLevel::DEBUG, "Found SPS, length: %zu", naluLength);
        }
        // Process PPS (type 8)
        else if (naluType == 8) {
            pps.assign(pos, pos + naluLength);
            foundPps = true;
            Logger::log(LogLevel::DEBUG, "Found PPS, length: %zu", naluLength);
        }

        // If we found both SPS and PPS, we can exit early
        if (foundSps && foundPps) {
            break;
        }

        // Move to next NALU
        if (nextStart) {
            pos = nextStart;
        } else {
            break;
        }
    }

    if (!foundSps) {
        Logger::log(LogLevel::WARNING, "SPS not found in H264 stream");
    }
    if (!foundPps) {
        Logger::log(LogLevel::WARNING, "PPS not found in H264 stream");
    }

    // Return true if at least one of SPS or PPS was found
    return foundSps || foundPps;
};
size_t RtspServer::frame_buffer_size = 0;
std::function<void(void)> RtspServer::onSessionClosedCallback = nullptr;
using namespace media;

extern "C" {
    extern struct chn_conf chn[];
    extern int direct_switch;
    extern int S_RC_METHOD;
}

std::shared_ptr<RtspServer> RtspServer::getInstance()
{
	static std::shared_ptr<RtspServer> instance = nullptr;
	static std::once_flag flag;
	std::call_once(flag, []() { instance.reset(new RtspServer()); });
	return instance;
}

RtspServer::RtspServer()
    : initialized(initialize())
	, pullFrameThread(nullptr)
	, pullFrameThreadRun(false)
	, alreadyGetSpsPps(false)
{

}

RtspServer::~RtspServer()
{
	stop();
    deinitialize();
}

bool RtspServer::initialize()
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
	if (chn[RTSP_SENSOR_CHN_NUM].enable) {
		ret = IMP_Encoder_CreateGroup(chn[RTSP_SENSOR_CHN_NUM].index);
		if (ret < 0) {
			Logger::log(LogLevel::ERROR, "Encoder CreateGroup(%d) failed", chn[RTSP_SENSOR_CHN_NUM].index);
			sample_framesource_exit();
			sample_system_exit();
			return false;
		}
	}
    Logger::log(LogLevel::DEBUG, "Encoder CreateGroup success");

    if (!initVideo()) {
        Logger::log(LogLevel::ERROR, "Video init failed");
		if (chn[RTSP_SENSOR_CHN_NUM].enable) {
			IMP_Encoder_DestroyGroup(chn[RTSP_SENSOR_CHN_NUM].index);
		}
        
        sample_framesource_exit();
        sample_system_exit();
        return false;
    }
    Logger::log(LogLevel::DEBUG, "Video init success");

	/* Step.4 Bind */
	if (chn[RTSP_SENSOR_CHN_NUM].enable) {
		ret = IMP_System_Bind(&chn[RTSP_SENSOR_CHN_NUM].framesource_chn, &chn[RTSP_SENSOR_CHN_NUM].imp_encoder);
		if (ret < 0) {
			Logger::log(LogLevel::ERROR, "Bind FrameSource%d and Encoder%d failed", chn[RTSP_SENSOR_CHN_NUM].framesource_chn.groupID, chn[RTSP_SENSOR_CHN_NUM].imp_encoder.groupID);
			return false;
		}
	}
    
    Logger::log(LogLevel::DEBUG, "Bind success");
    return true;
}

void RtspServer::deinitialize()
{
    if (initialized) {
        /* Step.8 UnBind */
        
        int ret = 0;
		if (chn[RTSP_SENSOR_CHN_NUM].enable) {
			ret = IMP_System_UnBind(&chn[RTSP_SENSOR_CHN_NUM].framesource_chn, &chn[RTSP_SENSOR_CHN_NUM].imp_encoder);
			if (ret < 0) {
				Logger::log(LogLevel::ERROR, "UnBind FrameSource%d and Encoder%d failed", chn[RTSP_SENSOR_CHN_NUM].framesource_chn.groupID, chn[RTSP_SENSOR_CHN_NUM].imp_encoder.groupID);
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

bool RtspServer::stop()
{
	if (this->rtsp_server) {
		stop_server(this->rtsp_server);
		destroy_server(this->rtsp_server);
		this->rtsp_server = nullptr;
	}
    
	if (this->pullFrameThread) {
		this->pullFrameThreadRun = false;
		if (this->pullFrameThread && this->pullFrameThread->joinable()) {
			this->pullFrameThread->join();
		}
		this->pullFrameThread = nullptr;
	}
	
    sample_framesource_streamoff();
	return true;
}

bool RtspServer::start()
{
	
    if (!initialized) {
        Logger::log(LogLevel::ERROR, "RtspServer is not initialized");
        return false;
    }
    /* Step.5 Stream On */
    int ret = sample_framesource_streamon();
    if (ret < 0) {
        Logger::log(LogLevel::ERROR, "FrameSource StreamOn failed");
		if (chn[RTSP_SENSOR_CHN_NUM].enable) {
			IMP_System_UnBind(&chn[RTSP_SENSOR_CHN_NUM].framesource_chn, &chn[RTSP_SENSOR_CHN_NUM].imp_encoder);
		}
        return false;
    }

    /* Step.6 Get stream */
	this->pullFrameThreadRun = true;
	this->pullFrameThread = std::make_shared<std::thread>([this]() {
		if (chn[RTSP_SENSOR_CHN_NUM].enable && sensorFilter(RTSP_SENSOR_CHN_NUM)) {
			start(chn[RTSP_SENSOR_CHN_NUM].index, chn[RTSP_SENSOR_CHN_NUM].payloadType);
		}
    });

	return true;
}

int RtspServer::onSessionClosed(void **data, size_t *size)
{
	Logger::log(LogLevel::INFO, "onSessionClosed");
	if (onSessionClosedCallback) {
		onSessionClosedCallback();
	}
	return 0;
}

int RtspServer::pullFrame(void **data, size_t *size)
{
    if (frame_fifo.count == 0) {
        return -1;
    }
	
    frame_buffer_t *frame = &frame_fifo.frames[frame_fifo.head];
    *data = frame->buffer;
    *size = frame->used_size;

	Logger::log(LogLevel::DEBUG, "Frame retrieved from FIFO, remaining: %d", frame_fifo.count);

    return 0;
}

int RtspServer::releaseFrame(void **data, size_t *size)
{
    if (frame_fifo.count == 0) {
        return -1;
    }

	{
		std::unique_lock<std::mutex> lock(frame_fifo.mutex);
		frame_fifo.head = (frame_fifo.head + 1) % FIFO_MAX_FRAMES;
		frame_fifo.count--;
	}

    frame_fifo.not_full.notify_one();

    return 0;
}

bool RtspServer::start(int chnNum, int payloadType)
{
    int i = 0;
    int ret = 0;
	IMPFSI2DAttr i2d_attr;
	int s32picWidth = 0, s32picHeight = 0;

	Logger::log(LogLevel::DEBUG, "RtspServer::start(%d, %d)", chnNum, payloadType);
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

    IMPEncoderFrmRate frmRate;
    IMP_Encoder_GetChnFrmRate(chn[RTSP_SENSOR_CHN_NUM].index, &frmRate);
    int fps = frmRate.frmRateNum / frmRate.frmRateDen;
    Logger::log(LogLevel::DEBUG, "chn[%d] fps = %d", i, fps);
    struct rtsp_server_param rtsp_server_param = {0};
    rtsp_server_param.port = 554;
    Logger::log(LogLevel::DEBUG, "rtsp_server_param.port = %d", rtsp_server_param.port);
    //video
    rtsp_server_param.video_enable = 1;
    rtsp_server_param.video_fps = fps;
	if (chn[RTSP_SENSOR_CHN_NUM].payloadType == PT_H264) {
        rtsp_server_param.video_codec = CODEC_H264;
	} else if (chn[RTSP_SENSOR_CHN_NUM].payloadType == PT_H265) {
	    rtsp_server_param.video_codec = CODEC_H265;
	}
    rtsp_server_param.video_sample_rate = 90000;
    rtsp_server_param.video_stream_id = 1;
    //audio
    rtsp_server_param.audio_enable = 0;
    rtsp_server_param.audio_sample_rate = 16000;
    rtsp_server_param.audio_stream_id = 0;
    rtsp_server_param.audio_samples_per_packet = 160;
    this->rtsp_server = create_server(&rtsp_server_param);
    if (!this->rtsp_server) {
        IMP_Encoder_StopRecvPic(chnNum);
        return false;
    }
	
    frame_fifo.head = 0;
    frame_fifo.tail = 0;
    frame_fifo.count = 0;

    for (int i = 0; i < FIFO_MAX_FRAMES; i++) {
        frame_fifo.frames[i].buffer = malloc(DEFAULT_FRAME_BUFFER_SIZE);
        if (!frame_fifo.frames[i].buffer) {
            Logger::log(LogLevel::ERROR, "Failed to preallocate frame buffer %d", i);
            for (int j = 0; j < i; j++) {
                free(frame_fifo.frames[j].buffer);
                frame_fifo.frames[j].buffer = nullptr;
            }
            IMP_Encoder_StopRecvPic(chnNum);
            return false;
        }
        frame_fifo.frames[i].buffer_size = DEFAULT_FRAME_BUFFER_SIZE;
    }
    Logger::log(LogLevel::DEBUG, "Preallocated %d frame buffers of size %zu bytes", FIFO_MAX_FRAMES, DEFAULT_FRAME_BUFFER_SIZE);

    register_function(this->rtsp_server, FUNC_ID_PULL_VIDEO_FRAME, RtspServer::pullFrame);
	register_function(this->rtsp_server, FUNC_ID_RELEASE_VIDEO_FRAME, RtspServer::releaseFrame);
    register_function(this->rtsp_server, FUNC_ID_PULL_AUDIO_FRAME, nullptr);
	register_function(this->rtsp_server, FUNC_ID_ON_SESSION_CLOSED, RtspServer::onSessionClosed);
    start_server(this->rtsp_server);
	
    while (this->pullFrameThreadRun) {
    	/* Polling stream, set timeout as 1000msec */
        ret = IMP_Encoder_PollingStream(chnNum, 1000);
        if (ret < 0) {
            Logger::log(LogLevel::ERROR, "IMP_Encoder_PollingStream(%d) timeout", chnNum);
            continue;
        }
		
        IMPEncoderStream stream;
        /* Get stream */
        ret = IMP_Encoder_GetStream(chnNum, &stream, 1);
        if (ret < 0) {
            Logger::log(LogLevel::ERROR, "IMP_Encoder_GetStream(%d) failed", chnNum);
            continue;
        }

        size_t total_length = 0;
        for (i = 0; i < (int)stream.packCount; i++) {
            total_length += stream.pack[i].length;
        }
		
        while (frame_fifo.count >= FIFO_MAX_FRAMES) {
            Logger::log(LogLevel::DEBUG, "Frame FIFO is full, waiting...");
			std::unique_lock<std::mutex> lock(frame_fifo.mutex);
            if (frame_fifo.not_full.wait_for(lock, std::chrono::milliseconds(20)) == std::cv_status::timeout) {
                if (!this->pullFrameThreadRun) {
                    IMP_Encoder_ReleaseStream(chnNum, &stream);
                    goto exit_loop;
                }
                continue;
            }
            if (!this->pullFrameThreadRun) {
                IMP_Encoder_ReleaseStream(chnNum, &stream);
                goto exit_loop;
            }
        }

        int current_tail = frame_fifo.tail;
        void *frame_buffer = frame_fifo.frames[current_tail].buffer;
        size_t buffer_size = frame_fifo.frames[current_tail].buffer_size;

        if (total_length > buffer_size) {
            Logger::log(LogLevel::INFO, "Frame size %zu exceeds buffer size %zu, reallocating", total_length, buffer_size);
            void *new_buffer = realloc(frame_buffer, total_length);
            if (!new_buffer) {
                Logger::log(LogLevel::ERROR, "realloc failed for frame data");
                IMP_Encoder_ReleaseStream(chnNum, &stream);
                continue;
            }
            frame_buffer = new_buffer;
			{
				std::unique_lock<std::mutex> lock(frame_fifo.mutex);
				frame_fifo.frames[current_tail].buffer = frame_buffer;
				frame_fifo.frames[current_tail].buffer_size = total_length;
			}
        }
		
        size_t offset = 0;
        for (i = 0; i < (int)stream.packCount; i++) {
            size_t datasize = stream.pack[i].length;
            uint8_t *inputData = (uint8_t*)stream.pack[i].virAddr;
            memcpy((uint8_t*)frame_buffer + offset, inputData, datasize);
            offset += datasize;
        }
		frame_fifo.frames[current_tail].used_size = total_length;
#if PPS_SPS_IN_SDP
		std::vector<uint8_t> sps, pps;
		if (!alreadyGetSpsPps && extractSpsPps(static_cast<uint8_t*>(frame_buffer), total_length, sps, pps)) {
			alreadyGetSpsPps = true;
			uint8_t *sps_data = (uint8_t*)malloc(sps.size());
			uint8_t *pps_data = (uint8_t*)malloc(pps.size());
			for (int i=0; i < sps.size(); i++) {
				sps_data[i] = sps[i];
				Logger::log(LogLevel::DEBUG, "sps_data[%d] = 0x%02x", i, sps_data[i]);
			}
			for (int i=0; i < pps.size(); i++) {
				pps_data[i] = pps[i];
				Logger::log(LogLevel::DEBUG, "pps_data[%d] = 0x%02x", i, pps_data[i]);
			}
			set_server_param(this->rtsp_server, RTSP_SERVER_PARAM_VIDEO_SPS, sps_data, sps.size());
			set_server_param(this->rtsp_server, RTSP_SERVER_PARAM_VIDEO_PPS, pps_data, pps.size());
		}
#endif
		{
			std::unique_lock<std::mutex> lock(frame_fifo.mutex);
			frame_fifo.tail = (frame_fifo.tail + 1) % FIFO_MAX_FRAMES;
			frame_fifo.count++;
		}

        Logger::log(LogLevel::DEBUG, "Frame added to FIFO, count: %d", frame_fifo.count);

        frame_fifo.not_empty.notify_one();
        IMP_Encoder_ReleaseStream(chnNum, &stream);
    }
exit_loop:
	
	ret = IMP_Encoder_StopRecvPic(chnNum);
    if (ret < 0) {
        Logger::log(LogLevel::ERROR, "IMP_Encoder_StopRecvPic(%d) failed", chnNum);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(frame_fifo.mutex);
        for (int i = 0; i < FIFO_MAX_FRAMES; i++) {
            if (frame_fifo.frames[i].buffer) {
                free(frame_fifo.frames[i].buffer);
                frame_fifo.frames[i].buffer = nullptr;
            }
        }
        frame_fifo.head = 0;
        frame_fifo.tail = 0;
        frame_fifo.count = 0;
        frame_fifo.not_empty.notify_all();
        frame_fifo.not_full.notify_all();
    }

    return true;
}

bool RtspServer::sensorFilter(int index)
{
    switch (SENSOR_NUM) {
        case IMPISP_TOTAL_ONE: return index == 0 || index == 1;
        case IMPISP_TOTAL_TWO: return index == 0 || index == 3;
        case IMPISP_TOTAL_THR: return index == 0 || index == 3 || index == 6;
        case IMPISP_TOTAL_FOU: return index == 0 || index == 3 || index == 6 || index == 9;
        default: return false;
    }
}

bool RtspServer::initVideo()
{
	int ret = 0;
	int chnNum = 0;
	int s32picWidth = 0, s32picHeight = 0;
	IMPEncoderAttr *enc_attr;
	IMPEncoderRcAttr *rc_attr;
	IMPFSChnAttr *imp_chn_attr_tmp;
	IMPEncoderCHNAttr channel_attr;
	IMPFSI2DAttr i2d_attr;

	if (chn[RTSP_SENSOR_CHN_NUM].enable) {
		imp_chn_attr_tmp = &chn[RTSP_SENSOR_CHN_NUM].fs_chn_attr;
		memset(&channel_attr, 0, sizeof(IMPEncoderCHNAttr));
		memset(&i2d_attr, 0, sizeof(IMPFSI2DAttr));

		ret = IMP_FrameSource_GetI2dAttr(chn[RTSP_SENSOR_CHN_NUM].index, &i2d_attr);
		if(ret < 0){
			Logger::log(LogLevel::ERROR, "IMP_FrameSource_GetI2dAttr(%d) failed", chn[RTSP_SENSOR_CHN_NUM].index);
			return false;
		}

		if((1 == i2d_attr.i2d_enable) &&
				((i2d_attr.rotate_enable) && (i2d_attr.rotate_angle == 90 || i2d_attr.rotate_angle == 270))){
			/* this depend on your sensor or channels */
			s32picWidth  = chn[RTSP_SENSOR_CHN_NUM].fs_chn_attr.picHeight;
			s32picHeight = chn[RTSP_SENSOR_CHN_NUM].fs_chn_attr.picWidth;
		} else {
			s32picWidth  = chn[RTSP_SENSOR_CHN_NUM].fs_chn_attr.picWidth;
			s32picHeight = chn[RTSP_SENSOR_CHN_NUM].fs_chn_attr.picHeight;
		}

		enc_attr = &channel_attr.encAttr;
		enc_attr->enType = chn[RTSP_SENSOR_CHN_NUM].payloadType;
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
		if (chn[RTSP_SENSOR_CHN_NUM].payloadType == PT_H264) {
			chnNum = chn[RTSP_SENSOR_CHN_NUM].index;
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
		} else if (chn[RTSP_SENSOR_CHN_NUM].payloadType == PT_H265) { /* PT_H265 */
			chnNum = chn[RTSP_SENSOR_CHN_NUM].index;
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
			if (0 == chn[RTSP_SENSOR_CHN_NUM].index)
				channel_attr.bEnableIvdc = true;
		} else if (direct_switch == 2) {
			if (0 == chn[RTSP_SENSOR_CHN_NUM].index || 3 == chn[RTSP_SENSOR_CHN_NUM].index)
				channel_attr.bEnableIvdc = true;
		} else if (direct_switch == 3) {
			if (0 == chn[RTSP_SENSOR_CHN_NUM].index || 3 == chn[RTSP_SENSOR_CHN_NUM].index || 6 == chn[RTSP_SENSOR_CHN_NUM].index)
				channel_attr.bEnableIvdc = true;
		} else if (direct_switch == 4) {
			if (0 == chn[RTSP_SENSOR_CHN_NUM].index || 3 == chn[RTSP_SENSOR_CHN_NUM].index || 6 == chn[RTSP_SENSOR_CHN_NUM].index || 9 == chn[RTSP_SENSOR_CHN_NUM].index)
				channel_attr.bEnableIvdc = true;
		}

		ret = IMP_Encoder_CreateChn(chnNum, &channel_attr);
		if (ret < 0) {
			Logger::log(LogLevel::ERROR, "IMP_Encoder_CreateChn(%d) failed", chnNum);
			return false;
		}
		
		ret = IMP_Encoder_RegisterChn(chn[RTSP_SENSOR_CHN_NUM].index, chnNum);
		if (ret < 0) {
			Logger::log(LogLevel::ERROR, "IMP_Encoder_RegisterChn(group%d, chn%d) failed", chn[RTSP_SENSOR_CHN_NUM].index, chnNum);
			return false;
		}
	}
	

    return true;
}

bool RtspServer::uninitVideo(void)
{
    
	int ret = 0;
	int chnNum = 0;
	IMPEncoderCHNStat chn_stat;

	if (chn[RTSP_SENSOR_CHN_NUM].enable) {
		chnNum = chn[RTSP_SENSOR_CHN_NUM].index;
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

void RtspServer::registerOnsessionClosedCallback(std::function<void(void)> callback)
{
    onSessionClosedCallback = callback;
}

bool RtspServer::isRunning(void)
{
    return !!is_server_running(this->rtsp_server);
}
