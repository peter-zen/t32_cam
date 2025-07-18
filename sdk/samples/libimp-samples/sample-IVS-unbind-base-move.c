/*
	sample-IVS-unbind-base-move.c

	Copyright (C) 2024 Ingenic Semiconductor Co.,Ltd
	The specific instructions for all API calls in this file can be found in the header file of the proj/sdk-lv3/include/api/cn/imp/

	This sample demonstrates the basic mobile algorithm.
*/
#include <fcntl.h>
#include <sys/types.h>
#include <imp/imp_ivs_base_move.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <imp/imp_log.h>
#include <imp/imp_common.h>
#include <imp/imp_system.h>
#include <imp/imp_framesource.h>
#include <imp/imp_ivs.h>
#include <imp/imp_ivs_base_move.h>
#include "sample-common.h"

#define TAG "sample-IVS-base-move"

#define SAD_MODE_SIZE 8  /* When sadMode is 0, it indicates that the detection area size is 8 * 8, and this macro is defined as 8 */
#define DEBUG_PRINT

extern struct chn_conf chn[];
extern int direct_switch;

static int sample_ivs_move_start(int grp_num, int chn_num, IMPIVSInterface **interface);
static int sample_ivs_move_stop(int chn_num, IMPIVSInterface *interface);

int main(int argc, char *argv[])
{
	int i = 0, j = 0;
	int ret = 0;
	unsigned char *g_sub_nv12_buf_base_move = NULL;
	IMPIVSInterface *interface = NULL;
	IMP_IVS_BaseMoveOutput *result = NULL;
	IMPFrameInfo frame;

	direct_switch = 0;

	chn[0].enable = 0;
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

	g_sub_nv12_buf_base_move = (unsigned char *)malloc(FIRST_SENSOR_WIDTH_SECOND * FIRST_SENSOR_HEIGHT_SECOND * 3 / 2);
	if (g_sub_nv12_buf_base_move == NULL) {
		IMP_LOG_ERR(TAG, "malloc failed\n");
		return -1;
	}

	/* Step.3 Stream On */
	ret = sample_framesource_streamon();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "FrameSource StreamOn failed\n");
		return -1;
	}

	/* Step.4 ivs base move start */
	ret = sample_ivs_move_start(0, IVS_CHN_ID, &interface);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "ivs move start failed\n");
		return -1;
	}

	if(interface->init && ((ret = interface->init(interface)) < 0)) {
		IMP_LOG_ERR(TAG, "interface init failed\n");
		return -1;
	}
/*
	ret = sample_ivs_set_senser(1, 3, interface);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "ivs set senser failed\n");
		return -1;
	}
*/
	/* Step.5 start to get ivs move result */
	for (i = 0; i < NR_FRAMES_TO_SAVE * 1000; i++) {

		ret = IMP_FrameSource_SnapFrame(1, PIX_FMT_NV12, FIRST_SENSOR_WIDTH_SECOND, FIRST_SENSOR_HEIGHT_SECOND, g_sub_nv12_buf_base_move, &frame);
		if (ret < 0) {
			printf("%d get frame failed try again\n", 1);
			usleep(30*1000);
		}

		static int write_flag = 1;
		if(1 == write_flag) {
			int fd = -1;
			char framefilename[64];
			if (PIX_FMT_NV12 == chn[1].fs_chn_attr.pixFmt) {
				sprintf(framefilename, "/tmp/frame%dx%d_%d.nv12", chn[1].fs_chn_attr.picWidth, chn[1].fs_chn_attr.picHeight, i);
			} else {
				sprintf(framefilename, "/tmp/frame%dx%d_%d.raw", chn[1].fs_chn_attr.picWidth, chn[1].fs_chn_attr.picHeight, i);
			}
			fd = open(framefilename, O_RDWR | O_CREAT, 0x644);
			if (fd < 0) {
				IMP_LOG_ERR(TAG, "open %s failed:%s\n", framefilename, strerror(errno));
				return -1;
			}
			if (write(fd, (void *)g_sub_nv12_buf_base_move, frame.size) != frame.size) {
				IMP_LOG_ERR(TAG, "chnNum=%d write frame i=%d failed\n", 1, i);
			}
			write_flag = 0;
			close(fd);
		}

		frame.virAddr = (unsigned int)g_sub_nv12_buf_base_move;
		if (interface->preProcessSync && ((ret = interface->preProcessSync(interface, &frame)) < 0)) {
			IMP_LOG_ERR(TAG, "interface preProcessSync failed\n");
			return -1;
		}
		if (interface->processAsync && ((ret = interface->processAsync(interface, &frame)) < 0)) {
			IMP_LOG_ERR(TAG, "interface processAsync failed\n");
			return -1;
		}
		if (interface->getResult && ((ret = interface->getResult(interface, (void **)&result)) < 0)) {
			IMP_LOG_ERR(TAG, "interface getResult failed\n");
			return -1;
		}
#ifdef DEBUG_PRINT
		for(j = 0; j < result->datalen; j ++) {
			printf("%4d ",*(result->data + j));
			if(j%(FIRST_SENSOR_WIDTH_SECOND/SAD_MODE_SIZE) == 0) printf("\n");
		}
#endif
		//printf("frame[%d], result->retRoi(%d,%d,%d,%d)\n", i, result->retRoi[0], result->retRoi[1], result->retRoi[2], result->retRoi[3]);
		//release moveresult
		if (interface->releaseResult && ((ret = interface->releaseResult(interface, (void *)result)) < 0)) {
			IMP_LOG_ERR(TAG, "interface releaseResult failed\n");
			return -1;
		}
	}

	interface->exit(interface);

	free(g_sub_nv12_buf_base_move);

	/* Step.6 ivs base move stop */
	ret = sample_ivs_move_stop(IVS_CHN_ID, interface);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "ivs move stop failed\n");
		return -1;
	}

	/* Step.7 Stream Off */
	ret = sample_framesource_streamoff();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "FrameSource StreamOff failed\n");
		return -1;
	}

	/* Step.8 FrameSource exit */
	ret = sample_framesource_exit();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "FrameSource exit failed\n");
		return -1;
	}

	/* Step.17 System exit */
	ret = sample_system_exit();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "System exit failed\n");
		return -1;
	}

	return 0;
}

static int sample_ivs_move_start(int grp_num, int chn_num, IMPIVSInterface **interface)
{
	int ret = 0;
	IMP_IVS_BaseMoveParam param;

	memset(&param, 0, sizeof(IMP_IVS_BaseMoveParam));
	param.skipFrameCnt = 3;
	param.referenceNum = 4;
	param.sadMode = 0;
	param.sense = 3;
	param.frameInfo.width = FIRST_SENSOR_WIDTH_SECOND;
	param.frameInfo.height = FIRST_SENSOR_HEIGHT_SECOND;

	*interface = IMP_IVS_CreateBaseMoveInterface(&param);
	if (*interface == NULL) {
		IMP_LOG_ERR(TAG, "IMP_IVS_CreateBaseMoveInterface(%d) failed\n", grp_num);
		return -1;
	}

	ret = IMP_IVS_CreateChn(chn_num, *interface);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_IVS_CreateChn(%d) failed\n", chn_num);
		return -1;
	}

	return 0;
}

static int sample_ivs_move_stop(int chn_num, IMPIVSInterface *interface)
{
	int ret = 0;

	ret = IMP_IVS_DestroyChn(chn_num);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_IVS_DestroyChn(%d) failed\n", chn_num);
		return -1;
	}

	IMP_IVS_DestroyBaseMoveInterface(interface);

	return 0;
}

#if 0
static int sample_ivs_set_sense(int chn_num, int sensor)
{
	int ret = 0;
	IMP_IVS_MoveParam param;
	int i = 0;
	ret = IMP_IVS_GetParam(chn_num, &param);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_IVS_GetParam(%d) failed\n", chn_num);
		return -1;
	}
	for( i = 0 ; i < param.roiRectCnt ; i++){
		param.sense[i] = sensor;
	}
	ret = IMP_IVS_SetParam(chn_num, &param);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_IVS_SetParam(%d) failed\n", chn_num);
		return -1;
	}
	return 0;
}
#endif
