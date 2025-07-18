/*
	sample-I2d.c

	Copyright (C) 2014 Ingenic Semiconductor Co.,Ltd
	The specific instructions for all API calls in this file can be found in the header file of the proj/sdk-lv3/include/api/cn/imp/

	This sample demonstrates flipping 90 degrees, 180 degrees, and 270 degrees.
*/

#include "sample-common.h"

#define TAG "sample-I2d"

extern struct chn_conf chn[];
extern int direct_switch;

static int sample_framesource_i2dopr(void);

int main(int argc, char *argv[])
{
	int i = 0;
	int ret = 0;

	direct_switch = 0;
	chn[0].enable = 1;
	chn[1].enable = 0;
	chn[2].enable = 0;

	/* Step.1 System init */
	ret = sample_system_init();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "System init failed\n");
		return -1;
	}

	/* Step.2 framesource init*/
	ret = sample_framesource_init();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "FrameSource init failed\n");
		return -1;
	}

	/* Step.3 I2dAttr init */
	ret = sample_framesource_i2dopr();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "i2d process failed\n");
		return -1;
	}

	/* Step.4 Encoder init */
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

	/* Step.5 Bind */
	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			ret = IMP_System_Bind(&chn[i].framesource_chn, &chn[i].imp_encoder);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "Bind framesource%d and encoder%d failed\n", chn[i].framesource_chn.groupID, chn[i].imp_encoder.groupID);
				return -1;
			}
		}
	}

	/* Step.6 Stream On */
	ret = sample_framesource_streamon();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "FrameSource StreamOn failed\n");
		return -1;
	}

	sleep(SLEEP_TIME);

	/* Step.7 Start Get stream */
	ret = sample_start_get_video_stream();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "Get video stream failed\n");
		return -1;
	}

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

static int sample_framesource_i2dopr(void)
{
	int i = 0;
	int ret = 0;
	static int s32cnt = 0;
	IMPFSI2DAttr sti2dattr;
	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			if (s32cnt++ % 2 == 0) {
				/* i2d enable */
				ret = IMP_FrameSource_GetI2dAttr(chn[i].index, &sti2dattr);
				if (ret < 0) {
					IMP_LOG_ERR(TAG, "IMP_FrameSource_GetI2dAttr(%d) failed\n", chn[i].index);
					return -1;
				}
				memset(&sti2dattr, 0x0, sizeof(IMPFSI2DAttr));
				sti2dattr.i2d_enable = 1;
				sti2dattr.flip_enable = 0;
				sti2dattr.mirr_enable = 0;
				sti2dattr.rotate_enable = 1;
				sti2dattr.rotate_angle = 90;
				ret = IMP_FrameSource_SetI2dAttr(chn[i].index, &sti2dattr);
				if (ret < 0) {
					IMP_LOG_ERR(TAG, "IMP_FrameSource_SetI2dAttr(%d) failed\n", chn[i].index);
					return -1;
				}

			} else {
				/* i2d disable */
				ret = IMP_FrameSource_GetI2dAttr(chn[i].index, &sti2dattr);
				if (ret < 0) {
					IMP_LOG_ERR(TAG, "IMP_FrameSource_GetI2dAttr(%d) failed\n", chn[i].index);
					return -1;
				}
				memset(&sti2dattr, 0x0, sizeof(IMPFSI2DAttr));
				sti2dattr.i2d_enable = 0;/* if this off,all i2d off */
				sti2dattr.flip_enable = 0;
				sti2dattr.mirr_enable = 0;
				sti2dattr.rotate_enable = 1;
				sti2dattr.rotate_angle = 90;
				ret = IMP_FrameSource_SetI2dAttr(chn[i].index, &sti2dattr);
				if (ret < 0) {
					IMP_LOG_ERR(TAG, "IMP_FrameSource_SetI2dAttr(%d) failed\n", chn[i].index);
					return -1;
				}
			}
		}
	}

	return 0;
}
