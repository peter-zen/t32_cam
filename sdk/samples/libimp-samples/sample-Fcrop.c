/*
	sample-Fcrop.c

	Copyright (C) 2014 Ingenic Semiconductor Co.,Ltd
	The specific instructions for all API calls in this file can be found in the header file of the proj/sdk-lv3/include/api/cn/imp/

	The main purpose of this example is to implement the electronic scaling function using Fcrop. Currently, there are limitations in scaling due to encoding resolution limitations.
*/

#include "sample-common.h"

#define TAG "sample-Fcrop"

extern struct chn_conf chn[];
extern int direct_switch;

static int sample_res_get_video_stream();
static int sample_res_init();
static int sample_res_deinit();

int main(int argc, char *argv[])
{
	int i = 0;
	int ret = 0;

	direct_switch = 0;
	chn[0].enable = 1;
	chn[1].enable = 0;
	chn[2].enable = 0;

	float zoom_test[] = {3, 3.5, 4, 5, 6, 7, 8, 9, 10};

	ret = sample_system_init();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "System init failed\n");
		return -1;
	}

	for (i = 0; i <= 6; i++) {
		/* init original resolution 1920x1080 & 1280x720 */
		chn[0].fs_chn_attr.fcrop.enable = 1;
		chn[0].fs_chn_attr.fcrop.top = 0;
		chn[0].fs_chn_attr.fcrop.left = 0;
		chn[0].fs_chn_attr.fcrop.width = ((int)(((1920/zoom_test[i]))+31) & ~31);
		chn[0].fs_chn_attr.fcrop.height = (((int)(1080/zoom_test[i])) + 1) & ~1;
		chn[0].fs_chn_attr.scaler.enable = 0;
		chn[0].fs_chn_attr.picWidth = ((int)(((1920/zoom_test[i])) +31) & ~31);
		chn[0].fs_chn_attr.picHeight = (((int)(1080/zoom_test[i])) + 1) & ~1;

		printf("picWidth = %d picHeight = %d\n", chn[0].fs_chn_attr.picWidth, chn[0].fs_chn_attr.picHeight);
		ret = sample_res_init();
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "res init failed\n");
			return -1;
		}

		ret = sample_res_get_video_stream();
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "res get video stream failed\n");
			return -1;
		}

		ret = sample_res_deinit();
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "res deinit failed\n");
			return -1;
		}
	}

	ret = sample_system_exit();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "System exit failed\n");
		return -1;
	}

	return 0;
}

static int save_stream(int fd, IMPEncoderStream *stream)
{
	int i = 0;
	int ret = 0;
	int nr_pack = stream->packCount;

	for (i = 0; i < nr_pack; i++) {
		ret = write(fd, (void *)stream->pack[i].virAddr, stream->pack[i].length);
		if (ret != stream->pack[i].length) {
			IMP_LOG_ERR(TAG, "stream write failed\n");
			return -1;
		}
	}
	return 0;
}

static void *res_get_video_stream(void *args)
{
	int i = 0;
	int ret = 0;
	int stream_fd = -1;
	int totalSaveCnt = 0;
	char stream_path[64] = { 0 };

	int val = (int)args;
	int chnNum = val & 0xffff;
	IMPPayloadType encType = (val >> 16) & 0xffff;

	ret = IMP_Encoder_StartRecvPic(chnNum);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_Encoder_StartRecvPic(%d) failed\n", chnNum);
		return NULL;
	}

	sprintf(stream_path, "%s/stream-%d-%dx%d.%s", STREAM_FILE_PATH_PREFIX, chnNum,
			chn[chnNum].fs_chn_attr.picWidth, chn[chnNum].fs_chn_attr.picHeight,
			(encType == PT_H264) ? "h264" : "h265");

	IMP_LOG_DBG(TAG, "Video ChnNum=%d Open Stream file %s\n", chnNum, stream_path);
	stream_fd = open(stream_path, O_RDWR | O_CREAT | O_TRUNC, 0777);
	if (stream_fd < 0) {
		IMP_LOG_ERR(TAG, "open %s failed\n", stream_path);
		return NULL;
	}
	IMP_LOG_DBG(TAG, "OK\n");
	totalSaveCnt = NR_FRAMES_TO_SAVE;

	for (i = 0; i < totalSaveCnt; i++) {
		ret = IMP_Encoder_PollingStream(chnNum, 1000);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_Encoder_PollingStream(%d) i=%d timeout\n", chnNum, i);
			continue;
		}

		IMPEncoderStream stream;
		/* Get H264 or H265 Stream */
		ret = IMP_Encoder_GetStream(chnNum, &stream, 1);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_Encoder_GetStream(%d) failed\n", chnNum);
			return NULL;
		}

		ret = save_stream(stream_fd, &stream);
		if (ret < 0) {
			close(stream_fd);
			return NULL;
		}

		IMP_Encoder_ReleaseStream(chnNum, &stream);
	}
	close(stream_fd);

	ret = IMP_Encoder_StopRecvPic(chnNum);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_Encoder_StopRecvPic(%d) failed\n", chnNum);
		return NULL;
	}

	return NULL;
}

static int sample_res_get_video_stream()
{
	int i = 0;
	int ret = 0;
	pthread_t tid[FS_CHN_NUM];

	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			int arg = ((chn[i].payloadType << 16) | chn[i].index);
			ret = pthread_create(&tid[i], NULL, res_get_video_stream, (void *)arg);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "Create Chn%d res_get_video_stream failed\n", chn[i].index);
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

static int sample_res_init()
{
	int i = 0;
	int ret = 0;

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

	ret = sample_video_init();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "Video init failed\n");
		return -1;
	}

	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			ret = IMP_System_Bind(&chn[i].framesource_chn, &chn[i].imp_encoder);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "Bind framesource%d and encoder%d failed\n", chn[i].framesource_chn.groupID, chn[i].imp_encoder.groupID);
				return -1;
			}
		}
	}

	ret = sample_framesource_streamon();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "FrameSource StreamOn failed\n");
		return -1;
	}

	return 0;
}

static int sample_res_deinit()
{
	int i = 0;
	int ret = 0;

	ret = sample_framesource_streamoff();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "FrameSource StreamOff failed\n");
		return -1;
	}

	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			ret = IMP_System_UnBind(&chn[i].framesource_chn, &chn[i].imp_encoder);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "UnBind framesource%d and encoder%d failed\n", chn[i].framesource_chn.groupID, chn[i].imp_encoder.groupID);
				return -1;
			}
		}
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

	ret = sample_framesource_exit();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "FrameSource exit failed\n");
		return -1;
	}

	return 0;
}
