/*
	sample-Encoder-ldc-video.c

	Copyright (C) 2014 Ingenic Semiconductor Co.,Ltd
	The specific instructions for all API calls in this file can be found in the header file of the proj/sdk-lv3/include/api/cn/imp/

	This sample demonstrates how to capture the H264/H265 stream: main(h264/h265), sec(h265/h264)
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
#include "ldclut.h"

#define TAG "sample-Encoder-ldc-video"

extern struct chn_conf chn[];
extern int direct_switch;

static int byGetFd = 0;

int main(int argc, char *argv[])
{
	int i = 0;
	int ret = 0;

	if (argc >= 2) {
		byGetFd = atoi(argv[1]);
	}

#if	1  //Method for using LDC on the main stream and sub-stream.
	   //The "ldclut.h" header file contains LDC parameters for different resolutions, as the LDC module relies on these parameters for distortion correction. These parameters can be generated using dynamic debugging tools.
	   //Currently, the sample uses a few sets of parameters for testing, as shown in the header file.
	IMPISPLDCInitAttr ldc;
	memset(&ldc, 0, sizeof(IMPISPLDCInitAttr));

	ldc.cattr[0].mode = IMPISP_OPS_MODE_ENABLE;
	ldc.cattr[0].enable = IMPISP_OPS_MODE_ENABLE;
	ldc.cattr[0].prioy = IMPISP_LDC_PRIOY_FIRST;
	memcpy(&ldc.cattr[0].params, &ldc_default_params[0], sizeof(IMPISPLDCParams));

	ldc.cattr[1].mode = IMPISP_OPS_MODE_ENABLE;
	ldc.cattr[1].enable = IMPISP_OPS_MODE_ENABLE;
	ldc.cattr[1].prioy = IMPISP_LDC_PRIOY_FIRST;
	memcpy(&ldc.cattr[1].params, &ldc_default_params[2], sizeof(IMPISPLDCParams));

	IMP_ISP_LDC_INIT(IMPVI_MAIN, &ldc); //Be sure to call it before IMP_ISP_Open. After the call is successful, a set of parameters in ldc_default_params[] will be passed down.
#endif

	/* Step.1 System init */
	ret = sample_system_init();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "System init failed\n");
		return -1;
	}

#if 0
	{
		//The IMP_ISP_LDC_INIT initialization can be further configured and accessed through the SET and GET interfaces for LDC-related parameters.
		IMPISPLDCAttr ldc;
		memset(&ldc, 0, sizeof(IMPISPLDCAttr));
		IMP_ISP_LDC_GetAttr(IMPVI_MAIN, &ldc);

		ldc.cattr[0].enable = IMPISP_OPS_MODE_ENABLE;
		ldc.cattr[0].prioy = IMPISP_LDC_PRIOY_FIRST;
		memcpy(&ldc.cattr[0].params, &ldc_default_params[1], sizeof(IMPISPLDCParams));

		ldc.cattr[1].enable = IMPISP_OPS_MODE_ENABLE;
		ldc.cattr[1].prioy = IMPISP_LDC_PRIOY_FIRST;
		memcpy(&ldc.cattr[1].params, &ldc_default_params[2], sizeof(IMPISPLDCParams));
		IMP_ISP_LDC_SetAttr(IMPVI_MAIN, &ldc);
	}
#endif

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

	/* Step.4 Bind */
	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			ret = IMP_System_Bind(&chn[i].framesource_chn, &chn[i].imp_encoder);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "Bind FrameSource%d and Encoder%d failed\n", chn[i].framesource_chn.groupID, chn[i].imp_encoder.groupID);
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

	sleep(SLEEP_TIME);

	/* Step.6 Get stream */
	if (byGetFd) {
		ret = sample_get_video_stream_byfd();
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "Get video stream byfd failed\n");
			return -1;
		}
	} else {
		ret = sample_start_get_video_stream();
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "Get video stream failed\n");
			return -1;
		}
		sample_stop_get_video_stream();
	}

	/* Step.7 Stream Off */
	ret = sample_framesource_streamoff();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "FrameSource StreamOff failed\n");
		return -1;
	}

	/* Step.8 UnBind */
	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			ret = IMP_System_UnBind(&chn[i].framesource_chn, &chn[i].imp_encoder);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "UnBind FrameSource%d and Encoder%d failed\n", chn[i].framesource_chn.groupID, chn[i].imp_encoder.groupID);
				return -1;
			}
		}
	}

	/* Step.9 Encoder exit */
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

	/* Step.10 FrameSource exit */
	ret = sample_framesource_exit();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "FrameSource exit failed\n");
		return -1;
	}

	/* Step.11 System exit */
	ret = sample_system_exit();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "System exit failed\n");
		return -1;
	}

	return 0;
}
