/*
	sample-Encoder-changeRes.c

	Copyright (C) 2014 Ingenic Semiconductor Co.,Ltd
	The specific instructions for all API calls in this file can be found in the header file of the proj/sdk-lv3/include/api/cn/imp/

	This sample demonstrates how to switch resolution: main(h264/h265 + jpeg), sec(h265/h264)
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

#define TAG "sample-Encoder-changeRes"

extern struct chn_conf chn[];
extern int direct_switch;

int changeWidth[] = {640, 704, 720, 1280, 1920, 2304, 2560, 2688, 2592, 2880};
int changeHeight[] = {254, 484, 576, 720, 1080, 1296, 1440, 1520, 1944, 1620};

static int sample_changres(int ChangeCnt);

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

	/* Step.7 Change Resolution */
	ret = sample_changres(10);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "Change Resolution failed\n");
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

	/* Step.12 System exit */
	ret = sample_system_exit();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "System exit failed\n");
		return -1;
	}

	return 0;
}

static int sample_changres(int ChangeCnt)
{
	int i = 0, j = 0;
	int ret = 0;

	int changeSize = sizeof(changeWidth) / sizeof(int);
	srand((unsigned int)time(NULL));
	for (i = 0; i < ChangeCnt; i++) {
		/* 1. stop get stream */
		sample_stop_get_jpeg_stream();
		sample_stop_get_video_stream();

		/* 2. framesource off */
		sample_framesource_streamoff();

		/* 3. unbind */
		for (j = 0; j < FS_CHN_NUM; j++) {
			if (chn[j].enable) {
				ret = IMP_System_UnBind(&chn[j].framesource_chn, &chn[j].imp_encoder);
				if (ret < 0) {
					IMP_LOG_ERR(TAG, "UnBind framesource%d and encoder%d failed\n", chn[j].framesource_chn.groupID, chn[j].imp_encoder.groupID);
					return -1;
				}
			}
		}

		/* 4. encoder exit */
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

		for (j = 0; j < FS_CHN_NUM; j++) {
			if (chn[j].enable) {
				ret = IMP_Encoder_DestroyGroup(chn[j].index);
				if (ret < 0) {
					IMP_LOG_ERR(TAG, "Encoder DestroyGroup(%d) failed\n", chn[j].index);
					return -1;
				}
			}
		}

		/* 5. framesource exit */
		ret = sample_framesource_exit();
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "FrameSource exit failed\n");
			return -1;
		}

		/* 6. framesource init */
		int c = rand() % changeSize;
		while ((chn[0].fs_chn_attr.scaler.outwidth == changeWidth[c]) && (chn[0].fs_chn_attr.scaler.outheight = changeHeight[c])) {
			c = rand() % changeSize;
		}
		chn[0].fs_chn_attr.scaler.enable = 1;
		chn[0].fs_chn_attr.scaler.outwidth = changeWidth[c];
		chn[0].fs_chn_attr.scaler.outheight = changeHeight[c];
		chn[0].fs_chn_attr.picWidth = changeWidth[c];
		chn[0].fs_chn_attr.picHeight = changeHeight[c];
		ret = sample_framesource_init();
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "FrameSource init failed\n");
			return -1;
		}
		printf("\033[1;31;40m change cnt:%d, next=[w:%d, h:%d] \033[0m\n", i, changeWidth[c], changeHeight[c]);

		/* 7. encode init */
		for (j = 0; j < FS_CHN_NUM; j++) {
			if (chn[j].enable) {
				ret = IMP_Encoder_CreateGroup(chn[j].index);
				if (ret < 0) {
					IMP_LOG_ERR(TAG, "Encoder CreateGroup(%d) failed\n", chn[j].index);
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

		/* 8. bind */
		for (j = 0; j < FS_CHN_NUM; j++) {
			if (chn[j].enable) {
				ret = IMP_System_Bind(&chn[j].framesource_chn, &chn[j].imp_encoder);
				if (ret < 0) {
					IMP_LOG_ERR(TAG, "Bind framesource%d and encoder%d failed\n", chn[j].framesource_chn.groupID, chn[j].imp_encoder.groupID);
					return -1;
				}
			}
		}

		/* 9. framesource streamon */
		ret = sample_framesource_streamon();
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "FrameSource StreamOn failed\n");
			return -1;
		}

		/* 10. get stream */
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
	}

	return 0;
}
