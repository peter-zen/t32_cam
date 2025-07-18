/*
	sample-Encoder-changeRcMode.c

	Copyright (C) 2014 Ingenic Semiconductor Co.,Ltd
	The specific instructions for all API calls in this file can be found in the header file of the proj/sdk-lv3/include/api/cn/imp/

	This sample demonstrates how to switch encoding types: main(h264/h265 + jpeg), sec(h265/h264)
			(note：
					chn0----        chn3----        chn6----        chn0----
					chn1---|Main    chn4---|Sec     chn7---|Thr     chn1---|Four
					chn2----        chn5----        chn8----        chn2----
			)
	direct mode can be selected based on direct_switch.
	direct_switch = 1, One direct through: In this case,
		only the main stream of main camera can pass through directly, while the secondary stream of main camera can pass through non directly
	direct_switch = 2, Two direct through: In this case,
		only the main stream of main camera can pass through directly, while the secondary stream of main camera can pass through non directly;
		only the main stream of second camera can pass through directly, while the secondary stream of second camera can pass through non directly;
	direct_switch = 3, Three direct through: In this case,
		only the main stream of main camera can pass through directly, while the secondary stream of main camera can pass through non directly;
		only the main stream of second camera can pass through directly, while the secondary stream of second camera can pass through non directly;
		only the main stream of third camera can pass through directly, while the secondary stream of third camera can pass through non directly;
	direct_switch = 4, Four direct through: In this case,
		only the main stream of main camera can pass through directly, while the secondary stream of main camera can pass through non directly;
		only the main stream of second camera can pass through directly, while the secondary stream of second camera can pass through non directly;
		only the main stream of third camera can pass through directly, while the secondary stream of third camera can pass through non directly;
		only the main stream of fourth camera can pass through directly, while the secondary stream of fourth camera can pass through non directly;
*/

#include "sample-common.h"

#define TAG "sample-Encoder-changeRcMode"
#define CHANGE_RCMODE_CNT 6

extern struct chn_conf chn[];
extern int direct_switch;

static int sample_changerc(int ChangeCnt);

int main(int argc, char *argv[])
{
	int i = 0;
	int ret = 0;

	/* Step.1 System init */
	ret = sample_system_init();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "System init failed\n");
		return -1;
	}

	/* Step.2 FrameSource init */
	ret = sample_framesource_init();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "FrameSource init failed\n");
		return -1;
	}

	/* Step.3 Encoder init */
	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			ret = IMP_Encoder_CreateGroup(chn[i].index);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "Encoder CreateGroup(%d) failed\n", chn[i].index);
				return -1;
			}
		}
	}

	ret = sample_video_init();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "Video init failed\n");
		return -1;
	}

	ret = sample_jpeg_init();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "Jpeg init failed\n");
		return -1;
	}

	/* Step.4 Bind */
	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			ret = IMP_System_Bind(&chn[i].framesource_chn, &chn[i].imp_encoder);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "Bind framesource%d and encoder%d failed\n", chn[i].framesource_chn.groupID, chn[i].imp_encoder.groupID);
				return -1;
			}
		}
	}

	/* Step.5 Stream On */
	ret = sample_framesource_streamon();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "FrameSource StreamOn failed\n");
		return -1;
	}

	/* Step.6 Get stream and Snap */
	ret = sample_start_get_video_stream();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "Get video stream failed\n");
		return -1;
	}

	ret = sample_start_get_jpeg_stream();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "Get jpeg stream failed\n");
		return -1;
	}

	/* Step.7 Change RcMode */
	ret = sample_changerc(5);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "Change RcMode failed\n");
		return -1;
	}

	/* Step.8 Stop get stream */
	sample_stop_get_jpeg_stream();
	sample_stop_get_video_stream();

	/* Step.9 Stream Off */
	ret = sample_framesource_streamoff();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "FrameSource StreamOff failed\n");
		return -1;
	}

	/* Step.10 UnBind */
	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			ret = IMP_System_UnBind(&chn[i].framesource_chn, &chn[i].imp_encoder);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "UnBind framesource%d and encoder%d failed\n", chn[i].framesource_chn.groupID, chn[i].imp_encoder.groupID);
				return -1;
			}
		}
	}

	/* Step.11 Encoder exit */
	ret = sample_jpeg_exit();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "Jpeg exit failed\n");
		return -1;
	}

	ret = sample_video_exit();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "Video exit failed\n");
		return -1;
	}

	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			ret = IMP_Encoder_DestroyGroup(chn[i].index);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "Encoder DestroyGroup(%d) failed\n", chn[i].index);
				return -1;
			}
		}
	}

	/* Step.12 FrameSource exit */
	ret = sample_framesource_exit();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "FrameSource exit failed\n");
		return -1;
	}

	/* Step.13 System exit */
	ret = sample_system_exit();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "System exit failed\n");
		return -1;
	}

	return 0;
}

static int sample_changerc(int ChangeCnt)
{
	int i = 0, j = 0;
	int ret = 0;
	IMPEncoderAttrRcMode rcModeCfg;
	IMPEncoderRcMode rcMode[CHANGE_RCMODE_CNT] = {
		ENC_RC_MODE_FIXQP,
		ENC_RC_MODE_CBR,
		ENC_RC_MODE_VBR,
		ENC_RC_MODE_SMART,
		ENC_RC_MODE_CVBR,
		ENC_RC_MODE_AVBR
	};

	for (i = 0; i < ChangeCnt; i++) {
		for (j = 0; j < FS_CHN_NUM; j++) {
			if (chn[j].enable && chn[j].index == 0) {
				int currRcMode = rcMode[i%CHANGE_RCMODE_CNT];
				if (chn[j].payloadType == PT_H264) {
					if (currRcMode == ENC_RC_MODE_FIXQP) {
						rcModeCfg.rcMode = ENC_RC_MODE_FIXQP;
						rcModeCfg.attrH264FixQp.IQp = 35;
						rcModeCfg.attrH264FixQp.PQp = 35;
						rcModeCfg.attrH264FixQp.blkQpEn = 0;
					} else if (currRcMode == ENC_RC_MODE_CBR) {
						rcModeCfg.rcMode = ENC_RC_MODE_CBR;
						rcModeCfg.attrH264Cbr.maxQp = 45;
						rcModeCfg.attrH264Cbr.minQp = 15;
						rcModeCfg.attrH264Cbr.blkQpEn = 0;
						rcModeCfg.attrH264Cbr.initialQp = 35;
						rcModeCfg.attrH264Cbr.maxIQp = 45;
						rcModeCfg.attrH264Cbr.minIQp = 15;
						rcModeCfg.attrH264Cbr.outBitRate = BITRATE_720P_Kbs;
						rcModeCfg.attrH264Cbr.iBiasLvl = 0;
						rcModeCfg.attrH264Cbr.IPfrmQPDelta = 3;
						rcModeCfg.attrH264Cbr.PPfrmQPDelta = 3;
						rcModeCfg.attrH264Cbr.staticTime = 2;
						rcModeCfg.attrH264Cbr.flucLvl = 1;
						rcModeCfg.attrH264Cbr.qualityLvl = 2;
						rcModeCfg.attrH264Cbr.maxIprop = 20;
						rcModeCfg.attrH264Cbr.minIprop = 1;
						rcModeCfg.attrH264Cbr.maxIPictureSize = rcModeCfg.attrH264Cbr.outBitRate * 6 / 5;
						rcModeCfg.attrH264Cbr.maxPPictureSize = rcModeCfg.attrH264Cbr.maxIPictureSize / 2;
					} else if (currRcMode == ENC_RC_MODE_VBR) {
						rcModeCfg.rcMode = ENC_RC_MODE_VBR;
						rcModeCfg.attrH264Vbr.maxQp = 45;
						rcModeCfg.attrH264Vbr.minQp = 15;
						rcModeCfg.attrH264Vbr.blkQpEn = 0;
						rcModeCfg.attrH264Vbr.initialQp = 35;
						rcModeCfg.attrH264Vbr.maxIQp = 45;
						rcModeCfg.attrH264Vbr.minIQp = 15;
						rcModeCfg.attrH264Vbr.maxBitRate = BITRATE_720P_Kbs;
						rcModeCfg.attrH264Vbr.iBiasLvl = 0;
						rcModeCfg.attrH264Vbr.changePos = 80;
						rcModeCfg.attrH264Vbr.staticTime = 2;
						rcModeCfg.attrH264Vbr.qualityLvl = 2;
						rcModeCfg.attrH264Vbr.maxIprop = 20;
						rcModeCfg.attrH264Vbr.minIprop = 1;
						rcModeCfg.attrH264Vbr.maxIPictureSize = rcModeCfg.attrH264Vbr.maxBitRate * 6 / 5;
						rcModeCfg.attrH264Vbr.maxPPictureSize = rcModeCfg.attrH264Vbr.maxIPictureSize / 2;
					} else if (currRcMode == ENC_RC_MODE_SMART) {
						rcModeCfg.rcMode = ENC_RC_MODE_SMART;
						rcModeCfg.attrH264Smart.maxQp = 45;
						rcModeCfg.attrH264Smart.minQp = 15;
						rcModeCfg.attrH264Smart.blkQpEn = 0;
						rcModeCfg.attrH264Smart.initialQp = 35;
						rcModeCfg.attrH264Smart.maxIQp = 45;
						rcModeCfg.attrH264Smart.minIQp = 15;
						rcModeCfg.attrH264Smart.maxBitRate = BITRATE_720P_Kbs;
						rcModeCfg.attrH264Smart.iBiasLvl = 0;
						rcModeCfg.attrH264Smart.changePos = 80;
						rcModeCfg.attrH264Smart.staticTime = 2;
						rcModeCfg.attrH264Smart.qualityLvl = 2;
						rcModeCfg.attrH264Smart.maxIprop = 20;
						rcModeCfg.attrH264Smart.minIprop = 1;
						rcModeCfg.attrH264Smart.minStillRate = 25;
						rcModeCfg.attrH264Smart.maxStillQp = 35;
						rcModeCfg.attrH264Smart.superSmartEn = 1;
						rcModeCfg.attrH264Smart.supSmtStillLvl = 5;
						rcModeCfg.attrH264Smart.supSmtStillRateLvl = 2;
						rcModeCfg.attrH264Smart.maxSupSmtStillRate = 20;
						rcModeCfg.attrH264Smart.maxIPictureSize = rcModeCfg.attrH264Smart.maxBitRate * 6 / 5;
						rcModeCfg.attrH264Smart.maxPPictureSize = rcModeCfg.attrH264Smart.maxIPictureSize / 2;
					} else if (currRcMode == ENC_RC_MODE_CVBR) {
						rcModeCfg.rcMode = ENC_RC_MODE_CVBR;
						rcModeCfg.attrH264CVbr.maxQp = 45;
						rcModeCfg.attrH264CVbr.minQp = 15;
						rcModeCfg.attrH264CVbr.blkQpEn = 0;
						rcModeCfg.attrH264CVbr.initialQp = 35;
						rcModeCfg.attrH264CVbr.maxIQp = 45;
						rcModeCfg.attrH264CVbr.minIQp = 15;
						rcModeCfg.attrH264CVbr.maxBitRate = BITRATE_720P_Kbs;
						rcModeCfg.attrH264CVbr.longMaxBitRate = rcModeCfg.attrH264CVbr.maxBitRate * 4 / 5;
						rcModeCfg.attrH264CVbr.longMinBitRate = rcModeCfg.attrH264CVbr.maxBitRate * 3 / 5;
						rcModeCfg.attrH264CVbr.iBiasLvl = 0;
						rcModeCfg.attrH264CVbr.changePos = 80;
						rcModeCfg.attrH264CVbr.shortStatTime = 2;
						rcModeCfg.attrH264CVbr.longStatTime = 60;
						rcModeCfg.attrH264CVbr.qualityLvl = 2;
						rcModeCfg.attrH264CVbr.maxIprop = 20;
						rcModeCfg.attrH264CVbr.minIprop = 1;
						rcModeCfg.attrH264CVbr.extraBitRate = 5;
						rcModeCfg.attrH264CVbr.maxIPictureSize = rcModeCfg.attrH264CVbr.maxBitRate * 6 / 5;
						rcModeCfg.attrH264CVbr.maxPPictureSize = rcModeCfg.attrH264CVbr.maxIPictureSize / 2;
					} else if (currRcMode == ENC_RC_MODE_AVBR) {
						rcModeCfg.rcMode = ENC_RC_MODE_AVBR;
						rcModeCfg.attrH264AVbr.maxQp = 45;
						rcModeCfg.attrH264AVbr.minQp = 15;
						rcModeCfg.attrH264AVbr.blkQpEn = 0;
						rcModeCfg.attrH264AVbr.initialQp = 35;
						rcModeCfg.attrH264AVbr.maxIQp = 45;
						rcModeCfg.attrH264AVbr.minIQp = 15;
						rcModeCfg.attrH264AVbr.maxBitRate = BITRATE_720P_Kbs;
						rcModeCfg.attrH264AVbr.iBiasLvl = 0;
						rcModeCfg.attrH264AVbr.changePos = 80;
						rcModeCfg.attrH264AVbr.staticTime = 2;
						rcModeCfg.attrH264AVbr.qualityLvl = 2;
						rcModeCfg.attrH264AVbr.maxIprop = 20;
						rcModeCfg.attrH264AVbr.minIprop = 1;
						rcModeCfg.attrH264AVbr.minStillRate = 25;
						rcModeCfg.attrH264AVbr.maxStillQp = 35;
						rcModeCfg.attrH264AVbr.maxIPictureSize = rcModeCfg.attrH264AVbr.maxBitRate * 6 / 5;
						rcModeCfg.attrH264AVbr.maxPPictureSize = rcModeCfg.attrH264AVbr.maxIPictureSize / 2;
					}
				} else {
					if (currRcMode == ENC_RC_MODE_FIXQP) {
						rcModeCfg.rcMode = ENC_RC_MODE_FIXQP;
						rcModeCfg.attrH265FixQp.IQp = 35;
						rcModeCfg.attrH265FixQp.PQp = 35;
						rcModeCfg.attrH265FixQp.blkQpEn = 0;
					} else if (currRcMode == ENC_RC_MODE_CBR) {
						rcModeCfg.rcMode = ENC_RC_MODE_CBR;
						rcModeCfg.attrH265Cbr.maxQp = 45;
						rcModeCfg.attrH265Cbr.minQp = 15;
						rcModeCfg.attrH265Cbr.blkQpEn = 0;
						rcModeCfg.attrH265Cbr.initialQp = 35;
						rcModeCfg.attrH265Cbr.maxIQp = 45;
						rcModeCfg.attrH265Cbr.minIQp = 15;
						rcModeCfg.attrH265Cbr.outBitRate = BITRATE_720P_Kbs;
						rcModeCfg.attrH265Cbr.iBiasLvl = 0;
						rcModeCfg.attrH265Cbr.IPfrmQPDelta = 3;
						rcModeCfg.attrH265Cbr.PPfrmQPDelta = 3;
						rcModeCfg.attrH265Cbr.staticTime = 2;
						rcModeCfg.attrH265Cbr.flucLvl = 1;
						rcModeCfg.attrH265Cbr.qualityLvl = 2;
						rcModeCfg.attrH265Cbr.maxIprop = 20;
						rcModeCfg.attrH265Cbr.minIprop = 1;
						rcModeCfg.attrH265Cbr.maxIPictureSize = rcModeCfg.attrH265Cbr.outBitRate * 6 / 5;
						rcModeCfg.attrH265Cbr.maxPPictureSize = rcModeCfg.attrH265Cbr.maxIPictureSize / 2;
					} else if (currRcMode == ENC_RC_MODE_VBR) {
						rcModeCfg.rcMode = ENC_RC_MODE_VBR;
						rcModeCfg.attrH265Vbr.maxQp = 45;
						rcModeCfg.attrH265Vbr.minQp = 15;
						rcModeCfg.attrH265Vbr.blkQpEn = 0;
						rcModeCfg.attrH265Vbr.initialQp = 35;
						rcModeCfg.attrH265Vbr.maxIQp = 45;
						rcModeCfg.attrH265Vbr.minIQp = 15;
						rcModeCfg.attrH265Vbr.maxBitRate = BITRATE_720P_Kbs;
						rcModeCfg.attrH265Vbr.iBiasLvl = 0;
						rcModeCfg.attrH265Vbr.changePos = 80;
						rcModeCfg.attrH265Vbr.staticTime = 2;
						rcModeCfg.attrH265Vbr.qualityLvl = 2;
						rcModeCfg.attrH265Vbr.maxIprop = 20;
						rcModeCfg.attrH265Vbr.minIprop = 1;
						rcModeCfg.attrH265Vbr.maxIPictureSize = rcModeCfg.attrH265Vbr.maxBitRate * 6 / 5;
						rcModeCfg.attrH265Vbr.maxPPictureSize = rcModeCfg.attrH265Vbr.maxIPictureSize / 2;
					} else if (currRcMode == ENC_RC_MODE_SMART) {
						rcModeCfg.rcMode = ENC_RC_MODE_SMART;
						rcModeCfg.attrH265Smart.maxQp = 45;
						rcModeCfg.attrH265Smart.minQp = 15;
						rcModeCfg.attrH265Smart.blkQpEn = 0;
						rcModeCfg.attrH265Smart.initialQp = 35;
						rcModeCfg.attrH265Smart.maxIQp = 45;
						rcModeCfg.attrH265Smart.minIQp = 15;
						rcModeCfg.attrH265Smart.maxBitRate = BITRATE_720P_Kbs;
						rcModeCfg.attrH265Smart.iBiasLvl = 0;
						rcModeCfg.attrH265Smart.changePos = 80;
						rcModeCfg.attrH265Smart.staticTime = 2;
						rcModeCfg.attrH265Smart.qualityLvl = 2;
						rcModeCfg.attrH265Smart.maxIprop = 20;
						rcModeCfg.attrH265Smart.minIprop = 1;
						rcModeCfg.attrH265Smart.minStillRate = 25;
						rcModeCfg.attrH265Smart.maxStillQp = 35;
						rcModeCfg.attrH265Smart.superSmartEn = 1;
						rcModeCfg.attrH265Smart.supSmtStillLvl = 5;
						rcModeCfg.attrH265Smart.supSmtStillRateLvl = 2;
						rcModeCfg.attrH265Smart.maxSupSmtStillRate = 20;
						rcModeCfg.attrH265Smart.maxIPictureSize = rcModeCfg.attrH265Smart.maxBitRate * 6 / 5;
						rcModeCfg.attrH265Smart.maxPPictureSize = rcModeCfg.attrH265Smart.maxIPictureSize / 2;
					} else if (currRcMode == ENC_RC_MODE_CVBR) {
						rcModeCfg.rcMode = ENC_RC_MODE_CVBR;
						rcModeCfg.attrH265CVbr.maxQp = 45;
						rcModeCfg.attrH265CVbr.minQp = 15;
						rcModeCfg.attrH265CVbr.blkQpEn = 0;
						rcModeCfg.attrH265CVbr.initialQp = 35;
						rcModeCfg.attrH265CVbr.maxIQp = 45;
						rcModeCfg.attrH265CVbr.minIQp = 15;
						rcModeCfg.attrH265CVbr.maxBitRate = BITRATE_720P_Kbs;
						rcModeCfg.attrH265CVbr.longMaxBitRate = rcModeCfg.attrH265CVbr.maxBitRate * 4 / 5;
						rcModeCfg.attrH265CVbr.longMinBitRate = rcModeCfg.attrH265CVbr.maxBitRate * 3 / 5;
						rcModeCfg.attrH265CVbr.iBiasLvl = 0;
						rcModeCfg.attrH265CVbr.changePos = 80;
						rcModeCfg.attrH265CVbr.shortStatTime = 2;
						rcModeCfg.attrH265CVbr.longStatTime = 60;
						rcModeCfg.attrH265CVbr.qualityLvl = 2;
						rcModeCfg.attrH265CVbr.maxIprop = 20;
						rcModeCfg.attrH265CVbr.minIprop = 1;
						rcModeCfg.attrH265CVbr.extraBitRate = 5;
						rcModeCfg.attrH265CVbr.maxIPictureSize = rcModeCfg.attrH265CVbr.maxBitRate * 6 / 5;
						rcModeCfg.attrH265CVbr.maxPPictureSize = rcModeCfg.attrH265CVbr.maxIPictureSize / 2;
					} else if (currRcMode == ENC_RC_MODE_AVBR) {
						rcModeCfg.rcMode = ENC_RC_MODE_AVBR;
						rcModeCfg.attrH265AVbr.maxQp = 45;
						rcModeCfg.attrH265AVbr.minQp = 15;
						rcModeCfg.attrH265AVbr.blkQpEn = 0;
						rcModeCfg.attrH265AVbr.initialQp = 35;
						rcModeCfg.attrH265AVbr.maxIQp = 45;
						rcModeCfg.attrH265AVbr.minIQp = 15;
						rcModeCfg.attrH265AVbr.maxBitRate = BITRATE_720P_Kbs;
						rcModeCfg.attrH265AVbr.iBiasLvl = 0;
						rcModeCfg.attrH265AVbr.changePos = 80;
						rcModeCfg.attrH265AVbr.staticTime = 2;
						rcModeCfg.attrH265AVbr.qualityLvl = 2;
						rcModeCfg.attrH265AVbr.maxIprop = 20;
						rcModeCfg.attrH265AVbr.minIprop = 1;
						rcModeCfg.attrH265AVbr.minStillRate = 25;
						rcModeCfg.attrH265AVbr.maxStillQp = 35;
						rcModeCfg.attrH265AVbr.maxIPictureSize = rcModeCfg.attrH265AVbr.maxBitRate * 6 / 5;
						rcModeCfg.attrH265AVbr.maxPPictureSize = rcModeCfg.attrH265AVbr.maxIPictureSize / 2;
					}
				}
				ret = IMP_Encoder_SetChnAttrRcMode(chn[j].index, &rcModeCfg);
				if (ret < 0) {
					IMP_LOG_ERR(TAG, "Set rc mode failed\n");
					return -1;
				}

				sleep(3);

				IMPEncoderCHNAttr attr;
				ret = IMP_Encoder_GetChnAttr(chn[j].index, &attr);
				if (ret < 0) {
					IMP_LOG_ERR(TAG, "IMP_Encoder_GetChnAttr(%d) failed\n", chn[j].index);
					return -1;
				}
				printf("\033[1;31m Chn(%d) curr rcMode = %d \033[0m\n", chn[j].index, attr.rcAttr.attrRcMode.rcMode);
			}
		}
	}

	return 0;
}
