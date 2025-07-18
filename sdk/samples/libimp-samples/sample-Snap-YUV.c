/*
	sample-Snap-YUV.c

	Copyright (C) 2014 Ingenic Semiconductor Co.,Ltd
	The specific instructions for all API calls in this file can be found in the header file of the proj/sdk-lv3/include/api/cn/imp/

	This sample demonstrates the operation of grabbing YUV.
*/

#include "sample-common.h"

#define TAG "sample-Snap-YUV"

extern struct chn_conf chn[];

int main(int argc, char *argv[])
{
	int ret = 0;

	IMPFrameInfo *frame_bak;
	IMPFSChnAttr fs_chn_attr[2];
	FILE *fp;

	fp = fopen("/tmp/snap.yuv", "wb");
	if (fp == NULL) {
		IMP_LOG_ERR(TAG, "open failed\n");
		return -1;
	}

	/* Step.1 System init */
	ret = sample_system_init();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "System_init failed\n");
		return -1;
	}

	/* Step.2 FrameSource init */
	if (chn[0].enable) {
		ret = IMP_FrameSource_CreateChn(chn[0].index, &chn[0].fs_chn_attr);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_FrameSource_CreateChn(%d) failed\n", chn[0].index);
			return -1;
		}

		ret = IMP_FrameSource_SetChnAttr(chn[0].index, &chn[0].fs_chn_attr);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_FrameSource_SetChnAttr(%d) failed\n", chn[0].index);
			return -1;
		}
	}

	/* Step.3 FrameSource channel config */
	ret = IMP_FrameSource_GetChnAttr(0, &fs_chn_attr[0]);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_FrameSource_GetChnAttr failed\n");
		return -1;
	}

	fs_chn_attr[0].pixFmt = PIX_FMT_NV12;
	ret = IMP_FrameSource_SetChnAttr(0, &fs_chn_attr[0]);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_FrameSource_SetChnAttr failed\n");
		return -1;
	}

	/* Step.4 Enable Encoder channel,Stream On */
	if (chn[0].enable) {
		ret = IMP_FrameSource_EnableChn(chn[0].index);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_FrameSource_EnableChn(%d) failed\n", chn[0].index);
			return -1;
		}
	}

	/* Step.5 Snap yuv */
	{
		/* Set the number of channel cache frames */
		ret = IMP_FrameSource_SetFrameDepth(0, 1);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_FrameSource_SetFrameDepth failed\n");
			return -1;
		}

		int m = 0;
		for (m = 1; m <= 51; m++) {
			/* Get YUV data */
			ret = IMP_FrameSource_GetFrame(0, &frame_bak);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "IMP_FrameSource_GetFrame failed\n");
				return -1;
			}
			if (m % 50 == 0) {
				fwrite((void *)frame_bak->virAddr, frame_bak->size, 1, fp);
				fclose(fp);
			}
			/* Release the acquired YUV data */
			IMP_FrameSource_ReleaseFrame(0, frame_bak);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "IMP_FrameSource_ReleaseFrame failed\n");
				return -1;
			}
		}
		/* To stop getting YUV data, you need to set the channel cache to 0 */
		ret = IMP_FrameSource_SetFrameDepth(0, 0);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_FrameSource_SetFrameDepth failed\n");
			return -1;
		}
	}

	/* Step.6 Stream Off */
	if (chn[0].enable) {
		ret = IMP_FrameSource_DisableChn(chn[0].index);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_FrameSource_DisableChn(%d) failed\n", chn[0].index);
			return -1;
		}
	}

	/* Step.7 FrameSource exit */
	if (chn[0].enable) {
		/* Destroy channel */
		ret = IMP_FrameSource_DestroyChn(0);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_FrameSource_DestroyChn(%d) failed\n", 0);
			return -1;
		}
	}

	/* Step.8 System exit */
	ret = sample_system_exit();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "System exit failed\n");
		return -1;
	}

	return 0;
}
