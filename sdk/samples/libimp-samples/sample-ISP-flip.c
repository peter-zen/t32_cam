/*
	sample-ISP-flip.c

	Copyright (C) 2014 Ingenic Semiconductor Co.,Ltd
	The specific instructions for all API calls in this file can be found in the header file of the proj/sdk-lv3/include/api/cn/imp/

	This sample demonstrates the mirroring and flipping of the ISP module.
*/

#include "sample-common.h"

#define TAG "sample-ISP-flip"

extern struct chn_conf chn[];
extern int direct_switch;

int main(int argc, char *argv[])
{
	int i = 0;
	int ret = 0;

	direct_switch = 0;
	chn[0].enable = 1;
	chn[1].enable = 1;
	chn[2].enable = 0;

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

	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			ret = IMP_Encoder_CreateGroup(chn[i].index);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "Encoder CreateGroup(%d) failed\n", chn[i].index);
				return -1;
			}
		}
	}

	/* Step.3 Encoder init */
	ret = sample_video_init();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "Video init failed\n");
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

	/* Step.6 Get stream */
	ret = sample_start_get_video_stream();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "Get video stream failed\n");
		return -1;
	}

	/* normal image */
	usleep(500000);
	IMPISPHVFLIPAttr attr;
	IMP_ISP_Tuning_GetHVFLIP(IMPVI_MAIN, &attr);
	attr.isp_mode[0] = IMPISP_FLIP_NORMAL_MODE;
	IMP_ISP_Tuning_SetHVFLIP(IMPVI_MAIN, &attr);

	/* horizonital flip */
	usleep(500000);
	IMP_ISP_Tuning_GetHVFLIP(IMPVI_MAIN, &attr);
	attr.isp_mode[0] = IMPISP_FLIP_H_MODE;
	IMP_ISP_Tuning_SetHVFLIP(IMPVI_MAIN, &attr);

	/* normal image */
	usleep(500000);
	IMP_ISP_Tuning_GetHVFLIP(IMPVI_MAIN, &attr);
	attr.isp_mode[0] = IMPISP_FLIP_NORMAL_MODE;
	IMP_ISP_Tuning_SetHVFLIP(IMPVI_MAIN, &attr);

	/* vertical flip */
	usleep(500000);
	IMP_ISP_Tuning_GetHVFLIP(IMPVI_MAIN, &attr);
	attr.isp_mode[0] = IMPISP_FLIP_V_MODE;
	IMP_ISP_Tuning_SetHVFLIP(IMPVI_MAIN, &attr);

	/* normal image */
	usleep(500000);
	IMP_ISP_Tuning_GetHVFLIP(IMPVI_MAIN, &attr);
	attr.isp_mode[0] = IMPISP_FLIP_NORMAL_MODE;
	IMP_ISP_Tuning_SetHVFLIP(IMPVI_MAIN, &attr);

	/* horizonital and vertical flip */
	usleep(500000);
	IMP_ISP_Tuning_GetHVFLIP(IMPVI_MAIN, &attr);
	attr.isp_mode[0] = IMPISP_FLIP_HV_MODE;
	IMP_ISP_Tuning_SetHVFLIP(IMPVI_MAIN, &attr);

	/* normal image */
	usleep(500000);
	IMP_ISP_Tuning_GetHVFLIP(IMPVI_MAIN, &attr);
	attr.isp_mode[0] = IMPISP_FLIP_NORMAL_MODE;
	IMP_ISP_Tuning_SetHVFLIP(IMPVI_MAIN, &attr);

	/* Step.8 Stop Get stream */
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
