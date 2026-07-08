/*
	sample-Encoder-video.c

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

#define TAG "sample-Encoder-video"

extern struct chn_conf chn[];
extern int direct_switch;
extern int g_fs_nvbufs;

static int byGetFd = 0;
static int sleep_sec = SLEEP_TIME;

int main(int argc, char *argv[])
{
	int i = 0;
	int ret = 0;

	if (argc >= 2) {
		byGetFd = atoi(argv[1]);
	}
	if (argc >= 3) {
		g_fs_nvbufs = atoi(argv[2]);
		IMP_LOG_INFO(TAG, "Command line set g_fs_nvbufs=%d\n", g_fs_nvbufs);
	}
	if (argc >= 4) {
		sleep_sec = atoi(argv[3]);
	}
	if (argc >= 5) {
		int codec = atoi(argv[4]);
		if (codec == 264) {
			chn[0].payloadType = PT_H264;
			IMP_LOG_INFO(TAG, "Command line override CHN0 codec -> H264\n");
		} else if (codec == 265) {
			chn[0].payloadType = PT_H265;
			IMP_LOG_INFO(TAG, "Command line override CHN0 codec -> H265\n");
		}
	}

	IMP_LOG_INFO(TAG, "Usage: %s [byGetFd=0] [nvbufs=0] [sleep_sec=%d] [codec=265|264]\n", argv[0], SLEEP_TIME);

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

	/*
	 * VTS workaround removed for root-cause verification.
	 * If SetSensorFPS is skipped, init VTS=1500 should yield ~30fps.
	 */
#if 0
	{
		IMPISPSensorRegister reg = {0};

		reg.addr = 0x0340;
		reg.value = 0x06;
		ret = IMP_ISP_SetSensorRegister(IMPVI_MAIN, &reg);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "Set VTS high byte failed\n");
		}

		reg.addr = 0x0341;
		reg.value = 0x90;
		ret = IMP_ISP_SetSensorRegister(IMPVI_MAIN, &reg);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "Set VTS low byte failed\n");
		}

		reg.addr = 0x0340; reg.value = 0;
		IMP_ISP_GetSensorRegister(IMPVI_MAIN, &reg);
		uint32_t vts_high = reg.value;

		reg.addr = 0x0341; reg.value = 0;
		IMP_ISP_GetSensorRegister(IMPVI_MAIN, &reg);
		uint32_t vts_low = reg.value;

		uint32_t vts = (vts_high << 8) | vts_low;
		IMP_LOG_INFO(TAG, "===== VTS forced to 1680, readback: high=0x%x low=0x%x vts=%d =====\n",
			vts_high, vts_low, vts);
	}
#endif

	/* Start FrameSource FPS monitor before sleep/get stream */
	ret = sample_start_fs_fps_monitor();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "Start FS FPS monitor failed\n");
		return -1;
	}

	IMP_LOG_INFO(TAG, "Stream on, sleep %d seconds before capture...\n", sleep_sec);
	sleep(sleep_sec);

	/* Stop FrameSource FPS monitor before encoder capture to avoid frame competition */
	sample_stop_fs_fps_monitor();

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
