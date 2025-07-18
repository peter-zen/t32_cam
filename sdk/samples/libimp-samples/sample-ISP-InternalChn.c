/*
	sample-ISP-InternalChn.c

	Copyright (C) 2014 Ingenic Semiconductor Co.,Ltd
	The specific instructions for all API calls in this file can be found in the header file of the proj/sdk-lv3/include/api/cn/imp/
*/

#include "sample-common.h"

#define TAG "sample-Isp-InternalChn"

extern struct chn_conf chn[];

int main(int argc, char *argv[])
{
	int ret = 0;
	IMPISPInternalChnAttr attr;

	/* Step.1 System init */
	ret = sample_system_init();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "System init failed\n");
		return -1;
	}

	IMP_ISP_GetInternalChnAttr(IMPVI_MAIN, &attr);

#if 0
	/* Before using the IR channel, check whether the Sensor supports it */
	attr.ch[1].type = TX_ISP_INTERNAL_CHANNEL_ISP_IR;
	chn[1].fs_chn_attr.pixFmt = PIX_FMT_RAW8;
	chn[1].fs_chn_attr.nrVBs = 2;
	chn[1].enable = 1;
#endif
#if 0
	attr.ch[1].type = TX_ISP_INTERNAL_CHANNEL_VIC_DMA0;
	attr.ch[1].vc_index = 0;
	chn[1].fs_chn_attr.pixFmt = PIX_FMT_RAW16;
	chn[1].fs_chn_attr.nrVBs = 2;
	chn[1].enable = 1;
#endif

#if 0
	attr.ch[2].type = TX_ISP_INTERNAL_CHANNEL_VIC_DMA1;
	attr.ch[2].vc_index = 1;
	chn[2].fs_chn_attr.pixFmt = PIX_FMT_RAW16;
	chn[2].fs_chn_attr.nrVBs = 2;
	chn[2].enable = 1;
#endif

	IMP_ISP_SetInternalChnAttr(IMPVI_MAIN, &attr);

	/* Step.2 FrameSource init */
	ret = sample_framesource_init();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "FrameSource init failed\n");
		return -1;
	}

	/* Step.3 Stream On */
	ret = sample_framesource_streamon();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "FrameSource StreamOn failed\n");
		return -1;
	}

	/* Step.4 Get frame */
	ret = sample_get_frame();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "get frame failed\n");
		return -1;
	}

	/* Step.5 Stream Off */
	ret = sample_framesource_streamoff();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "FrameSource StreamOff failed\n");
		return -1;
	}

	/* Step.6 FrameSource exit */
	ret = sample_framesource_exit();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "FrameSource exit failed\n");
		return -1;
	}

	/* Step.7 System exit */
	ret = sample_system_exit();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "System exit failed\n");
		return -1;
	}

	return 0;
}
