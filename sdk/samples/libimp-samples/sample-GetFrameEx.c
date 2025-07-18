/*
	sample-GetFrameEx.c

	Copyright (C) 2014 Ingenic Semiconductor Co.,Ltd
	The specific instructions for all API calls in this file can be found in the header file of the proj/sdk-lv3/include/api/cn/imp/

	This sample demonstrates how multiple processes (non main processes) retrieve nv12 data.
*/

#include "sample-common.h"

#define TAG "sample-GetFrameEx"

extern struct chn_conf chn[];

static int sample_ipc_getframeEx(void);

int main(int argc, char *argv[])
{
	int ret = 0;

	chn[0].enable = 1;
	chn[1].enable = 0;
	chn[2].enable = 0;

	ret = sample_ipc_getframeEx();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "get frameEx failed\n");
		return -1;
	}

	return 0;
}

static void *getframeex(void *arg)
{
	int i = 0;
	int ret = 0;
	int fd = -1;
	char framefilename[64] = { 0 };
	int chanNum = (int)arg;
	/*创建存储文件名*/
	sprintf(framefilename, "/tmp/shm%dfrm%d.nv12", (int)getpid(), chanNum);//后面改下这里的路径，然后只dump一张图片
	fd = open(framefilename, O_RDWR | O_CREAT, 0x644);
	if (fd < 0) {
		IMP_LOG_ERR(TAG, "open %s failed\n", framefilename);
		return NULL;
	}

	for (i = 0; i < NR_FRAMES_TO_SAVE; i++) {
		IMPFrameInfo *frame =NULL;
		ret = IMP_FrameSource_GetFrameEx(chanNum, &frame);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_FrameSource_GetFrameEx(%d) failed\n", chanNum);
			return NULL;
		}

		if (NR_FRAMES_TO_SAVE/2 == i) {
			ret = write(fd, (void *)frame->virAddr, frame->width*frame->height);
			ret = write(fd, (void *)frame->virAddr + frame->width * ((frame->height+15) & ~15), frame->width*frame->height/2);
		}

		ret = IMP_FrameSource_ReleaseFrameEx(chanNum, frame);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_FrameSource_ReleaseFrameEx(%d) failed\n", chanNum);
			return NULL;
		}
	}

	return NULL;
}

static int sample_ipc_getframeEx(void)
{
	int i = 0;
	int ret = 0;
	pthread_t tid[FS_CHN_NUM];

	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			ret = pthread_create(&tid[i], NULL, getframeex, (void *)(chn[i].index));
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "getframeex create failed\n");
				return -1;
			}
		}
	}

	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			pthread_join(tid[i], NULL);
		}
	}

	return 0;
}
