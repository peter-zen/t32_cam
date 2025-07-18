/*
	sample-Extern-inject-yuv.c

	Copyright (C) 2014 Ingenic Semiconductor Co.,Ltd

	This sample demonstrates how to take out data from channel 0, and inject to channel 1.
*/

#include "sample-common.h"

#define TAG "sample-Extern-inject-yuv"

static int stop_flag;

static void *extern_inject_video(void *args);
static int save_stream_file(int fd, IMPEncoderStream *stream);

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

	int main_index = 0, extern_index = 1;
	IMPFSChnAttr fs_main = {
		.i2dattr.i2d_enable = 0,

		.pixFmt = PIX_FMT_NV12,
		.outFrmRateNum = FIRST_SENSOR_FRAME_RATE_NUM,
		.outFrmRateDen = FIRST_SENSOR_FRAME_RATE_DEN,
		.nrVBs = 2,
		.type = FS_PHY_CHANNEL,

		.scaler.enable = 0,
		.scaler.outwidth = FIRST_SENSOR_WIDTH,
		.scaler.outheight = FIRST_SENSOR_HEIGHT,

		.crop.enable = FIRST_CROP_EN,
		.crop.top = 0,
		.crop.left = 0,
		.crop.width = FIRST_SENSOR_WIDTH,
		.crop.height = FIRST_SENSOR_HEIGHT,

		.picWidth = FIRST_SENSOR_WIDTH,
		.picHeight = FIRST_SENSOR_HEIGHT,
	};

	IMPFSChnAttr fs_extern = {
		.i2dattr.i2d_enable = 0,

		.pixFmt = PIX_FMT_NV12,
		.outFrmRateNum = FIRST_SENSOR_FRAME_RATE_NUM,
		.outFrmRateDen = FIRST_SENSOR_FRAME_RATE_DEN,
		.nrVBs = 2,
		/* Use external yuv data */
		.type = FS_INJ_CHANNEL,

		.scaler.enable = 0,
		.scaler.outwidth = FIRST_SENSOR_WIDTH,
		.scaler.outheight = FIRST_SENSOR_HEIGHT,

		.crop.enable = 0,
		.crop.top = 0,
		.crop.left = 0,
		.crop.width = FIRST_SENSOR_WIDTH,
		.crop.height = FIRST_SENSOR_HEIGHT,

		.picWidth = FIRST_SENSOR_WIDTH,
		.picHeight = FIRST_SENSOR_HEIGHT,
	};

	/* Step.2 FrameSource init */
	ret = IMP_FrameSource_CreateChn(main_index, &fs_main);
	if(ret < 0){
		IMP_LOG_ERR(TAG, "IMP_FrameSource_CreateChn(%d) failed\n", main_index);
		return -1;
	}

	ret = IMP_FrameSource_SetChnAttr(main_index, &fs_main);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_FrameSource_SetChnAttr(%d) failed\n", main_index);
		return -1;
	}

	ret = IMP_FrameSource_ExternInject_CreateChn(extern_index, &fs_extern);
	if(ret < 0){
		IMP_LOG_ERR(TAG, "IMP_FrameSource_ExternInject_CreateChn(%d) failed\n", extern_index);
		return -1;
	}

	/* Step.3 Encoder init */
	ret = IMP_Encoder_CreateGroup(extern_index);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_Encoder_CreateGroup(%d) failed\n", extern_index);
		return -1;
	}

	IMPEncoderCHNAttr channel_attr;
	int uTargetBitRate = 1000;
	ret = IMP_Encoder_SetDefaultParam(&channel_attr, IMP_ENC_PROFILE_HEVC_MAIN, ENC_RC_MODE_CBR,
			fs_main.picWidth, fs_main.picHeight,
			fs_main.outFrmRateNum, fs_main.outFrmRateDen,
			fs_main.outFrmRateNum * 2 / fs_main.outFrmRateDen, 2,
			-1, uTargetBitRate);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_Encoder_SetDefaultParam(%d) failed\n", extern_index);
		return -1;
	}


	ret = IMP_Encoder_CreateChn(extern_index, &channel_attr);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_Encoder_CreateChn(%d) failed\n", extern_index);
		return -1;
	}

	ret = IMP_Encoder_RegisterChn(extern_index, extern_index);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_Encoder_RegisterChn(%d, %d) failed\n", extern_index, extern_index);
		return -1;
	}

	/* Step.4 Bind */
	IMPCell framesource_extern_chn = { DEV_ID_FS, extern_index, 0 };
	IMPCell encoder_extern_chn = { DEV_ID_ENC, extern_index, 0 };

	ret = IMP_System_Bind(&framesource_extern_chn, &encoder_extern_chn);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "Bind FrameSource%d and Encoder%d failed\n", framesource_extern_chn.groupID, encoder_extern_chn.groupID);
		return -1;
	}

	/* Step.5 Stream On */
	ret = IMP_FrameSource_EnableChn(main_index);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_FrameSource_EnableChn(%d) failed\n", main_index);
		return -1;
	}

	ret =  IMP_FrameSource_ExternInject_EnableChn(extern_index);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_FrameSource_ExternInject_EnableChn(%d) failed\n");
		return -1;
	}

	pthread_t tid;
	/* arg is framesource channel： src index | dst index */
	int arg = main_index | extern_index;
	ret = pthread_create(&tid, NULL, extern_inject_video, (void *)arg);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "Create thread extern_inject_video failed\n");
	}

	/* Step.6 Get stream */
	char stream_path[64];
	int stream_fd = -1, totalSaveCnt = NR_FRAMES_TO_SAVE;

	ret = IMP_Encoder_StartRecvPic(extern_index);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_Encoder_StartRecvPic(%d) failed\n", extern_index);
		return -1;
	}
	sprintf(stream_path, "%s/stream-%d.h265", STREAM_FILE_PATH_PREFIX, extern_index);

	stream_fd = open(stream_path, O_RDWR | O_CREAT | O_TRUNC, 0777);
	if (stream_fd < 0) {
		IMP_LOG_ERR(TAG, "open %s failed\n", stream_path);
		return -1;
	}

	for (i = 0; i < totalSaveCnt; i++) {
		ret = IMP_Encoder_PollingStream(extern_index, 1000);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_Encoder_PollingStream(%d) timeout\n", extern_index);
			continue;
		}

		IMPEncoderStream stream;
		ret = IMP_Encoder_GetStream(extern_index, &stream, 1);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_Encoder_GetStream(%d) failed\n", extern_index);
			return -1;
		}

		ret = save_stream_file(stream_fd, &stream);
		if (ret < 0) {
			close(stream_fd);
			IMP_LOG_ERR(TAG, "save stream failed\n");
			return -1;
		}

		IMP_Encoder_ReleaseStream(extern_index, &stream);
	}
	stop_flag = 1;

	close(stream_fd);

	ret = IMP_Encoder_StopRecvPic(extern_index);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_Encoder_StopRecvPic(%d) failed\n", extern_index);
		return -1;
	}

	pthread_join(tid, NULL);
	/* Step.7 Stream Off */

	ret = IMP_FrameSource_DisableChn(main_index);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_FrameSource_DisableChn(%d) failed\n", main_index);
		return -1;
	}

	ret = IMP_FrameSource_ExternInject_DisableChn(extern_index);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_FrameSource_DisableChn(%d) failed\n", extern_index);
		return -1;
	}

	/* Step.8 UnBind */
	ret = IMP_System_UnBind(&framesource_extern_chn, &encoder_extern_chn);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "UnBind FrameSource%d and Encoder%d failed\n", framesource_extern_chn.groupID, encoder_extern_chn.groupID);
		return -1;
	}

	/* Step.9 Encoder exit */
	ret = IMP_Encoder_UnRegisterChn(extern_index);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_Encoder_UnRegisterChn(%d) failed\n", extern_index);
		return -1;
	}

	ret = IMP_Encoder_DestroyChn(extern_index);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_Encoder_DestroyChn(%d) failed\n", extern_index);
		return -1;
	}

	ret = IMP_Encoder_DestroyGroup(extern_index);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_Encoder_DestroyGroup(%d) failed\n", extern_index);
		return -1;
	}

	/* Step.10 FrameSource exit */
	ret = IMP_FrameSource_DestroyChn(main_index);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_FrameSource_DestroyChn(%d) failed\n", main_index);
		return -1;
	}

	ret = IMP_FrameSource_ExternInject_DestroyChn(extern_index);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_FrameSource_DestroyChn(%d) failed\n", extern_index);
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

static void *extern_inject_video(void *args)
{
	int ret;
	int src_index, dst_index;

	src_index = ((int)args >> 16) & 0xffff;
	dst_index = (int)args & 0xffff;

	IMPFrameInfo *frame;
	IMPFrameInfo *frame0;

	ret = IMP_FrameSource_SetFrameDepth(src_index, 2);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_FrameSource_SetFrameDepth %d failed\n", src_index);
		return NULL;
	}

	while (1) {
		ret = IMP_FrameSource_GetFrame(src_index, &frame0);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_FrameSource_GetFrame %d failed\n", src_index);
			return NULL;
		}
		ret =  IMP_FrameSource_DequeueBuffer(dst_index, &frame);
		if(ret) {
			IMP_LOG_ERR(TAG, "IMP_FrameSource_DequeueBuffer %d failed\n", dst_index);
			continue;
		}

		/* printf("frame addr %p, frame->size =%d, frame0->size=%d.\n", frame->virAddr, frame->size, frame0->size); */
		memcpy((void *)frame->virAddr, (void *)frame0->virAddr, frame0->size);
		ret =  IMP_FrameSource_QueueBuffer(dst_index, frame);
		if(ret) {
			IMP_LOG_ERR(TAG, "IMP_FrameSource_QueueBuffer %d failed\n", dst_index);
			return NULL;
		}

		ret = IMP_FrameSource_ReleaseFrame(src_index, frame0);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_FrameSource_ReleaseFrame %d failed\n", src_index);
			return NULL;
		}

		if (stop_flag == 1) {
			ret = IMP_FrameSource_SetFrameDepth(src_index, 0);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "IMP_FrameSource_SetFrameDepth %d failed\n", src_index);
				return NULL;
			}
			break;
		}
	}
	return NULL;
}

static int save_stream_file(int fd, IMPEncoderStream *stream)
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
