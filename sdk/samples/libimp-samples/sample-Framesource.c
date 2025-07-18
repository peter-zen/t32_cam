/*
	sample-Framesource.c

	Copyright (C) 2014 Ingenic Semiconductor Co.,Ltd
	The specific instructions for all API calls in this file can be found in the header file of the proj/sdk-lv3/include/api/cn/imp/

	This sample demonstrates how to capture nv12 data.
			(note：
					chn0----        chn3----        chn6----        chn0----
					chn1---|Main    chn4---|Sec     chn7---|Thr     chn1---|Four
					chn2----        chn5----        chn8----        chn2----
			)
*/

#include "sample-common.h"

#define TAG "sample-FrameSource"

extern struct chn_conf chn[];

#ifdef GET_FRAME_EX
static int sample_mainpro_getframeex();
#endif

int main(int argc, char *argv[])
{
	int ret = 0;
	chn[0].enable = 1;
	chn[1].enable = 1;
	chn[2].enable = 0;
	if (SENSOR_NUM > IMPISP_TOTAL_ONE) {
		chn[3].enable = 1;
		chn[4].enable = 1;
		chn[5].enable = 0;
	}
	if (SENSOR_NUM > IMPISP_TOTAL_TWO) {
		chn[6].enable = 1;
		chn[7].enable = 1;
		chn[8].enable = 0;
	}
	if (SENSOR_NUM > IMPISP_TOTAL_THR) {
		chn[9].enable = 1;
		chn[10].enable = 1;
		chn[11].enable = 0;
	}

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

	/* Step.3 Stream On */
	ret = sample_framesource_streamon();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "FrameSource StreamOn failed\n");
		return -1;
	}

	/* Step.4 Get frame */
#ifdef GET_FRAME_EX
	ret = sample_mainpro_getframeex();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "get frameex failed\n");
		return -1;
	}
#else
	ret = sample_get_frame();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "get frame failed\n");
		return -1;
	}
#endif

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

#ifdef GET_FRAME_EX
static void *mainpro_getframeex_thread(void *args)
{
	int i = 0;
	int ret = 0;
	int fd = -1;
	char framefilename[64] = { 0 };
	int index = (int)args;
	int chnNum = chn[index].index;
	IMPFrameInfo *frame = NULL;

	if (PIX_FMT_NV12 == chn[index].fs_chn_attr.pixFmt) {
		sprintf(framefilename, "frame%dx%d_%d.nv12", chn[index].fs_chn_attr.picWidth, chn[index].fs_chn_attr.picHeight, index);
	} else {
		sprintf(framefilename, "frame%dx%d_%d.raw", chn[index].fs_chn_attr.picWidth, chn[index].fs_chn_attr.picHeight, index);
	}

	fd = open(framefilename, O_RDWR | O_CREAT, 0x644);
	if (fd < 0) {
		IMP_LOG_ERR(TAG, "open %s failed\n", framefilename);
		return NULL;
	}

	for (i = 0; i < NR_FRAMES_TO_SAVE; i++) {
		printf("IMP_FrameSource_GetFrameEx%d i=%d\n",chnNum,i);
		ret = IMP_FrameSource_GetFrameEx(chnNum, &frame);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_FrameSource_GetFrameEx(%d) failed\n", chnNum);
			return NULL;
		}

		if (NR_FRAMES_TO_SAVE/2 == i) {
			write(fd, (void *)frame->virAddr, frame->width * frame->height);
			write(fd, (void *)frame->virAddr + frame->width * ((frame->height + 15) & ~15),frame->width * frame->height / 2);
		}


		ret = IMP_FrameSource_ReleaseFrameEx(chnNum, frame);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_FrameSource_ReleaseFrameEx(%d) failed\n", chnNum);
			return NULL;
		}
	}

	close(fd);

	return NULL;
}

static int sample_mainpro_getframeex(void)
{
	int i = 0;
	int ret = 0;
	pthread_t tid[FS_CHN_NUM];

	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			ret = pthread_create(&tid[i], NULL, mainpro_getframeex_thread, (void *)(chn[i].index));
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "Create Chn%d mainpro_getframeex_thread failed\n", chn[i].index);
				return -1;
			}
		}
	}

	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			pthread_join(tid[i],NULL);
		}
	}

	return 0;
}
#endif
