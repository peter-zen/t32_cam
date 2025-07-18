/*
	sample-Encoder-video-Roi.c

	Copyright (C) 2014 Ingenic Semiconductor Co.,Ltd
	The specific instructions for all API calls in this file can be found in the header file of the proj/sdk-lv3/include/api/cn/imp/

	This sample demonstrates how to use rect-roi and map-roi: main(h264/h265)
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

#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>

#include "sample-common.h"

#define TAG "sample-Encoder-video-Roi"
/**
 * 此例程包括ROI和MapROI的使用介绍，注意ROI和MapROI不可同时使用！！
 */

/**
 * 定义Map ROI的Map构成，实现QP Mode和QP Value的封装
 */
#define IMP_PACKING_MAP(mode, qp) \
    ((unsigned char)(((mode & 0x03) << 6) + \
                     (qp & 0x3f)))

int enc_Roi = 1;

/* 配合Ingenic人车宠等识别类算法使用，定义一组目标分别对应何种编码质量。数组下标：0 -- 背景 、1 -- 人、2 -- 机动车、3 -- 非机动车、4 -- 宠物、... 规定算法最大识别目标种类16种 */
/* 不同目标设定不同的QP值，用户可根据需求自主设定。如例程 0:<相对QP 5>、1:<相对QP -5>、2:<相对QP -8>、3:<绝对QP 20> 【注意】：若未指定某类目标对应的QP值，那么算法识别出该类时按常规编码流程处理 */
/* 相对QP取值范围:-26~25，绝对QP取值范围:1~51 */
static uint8_t data[16] = { IMP_PACKING_MAP(IMP_ROI_QPMODE_DELTA, 5), IMP_PACKING_MAP(IMP_ROI_QPMODE_DELTA, -5), IMP_PACKING_MAP(IMP_ROI_QPMODE_DELTA, -8), IMP_PACKING_MAP(IMP_ROI_QPMODE_FIXED_QP, 20) };
static IMPEncoderMappingList list = {data, 16};	/** < 上面的QP数据封装成一张映射表 */

extern struct chn_conf chn[];
extern int direct_switch;

static int sample_get_Roi_stream();

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

	/* Step.6 Get stream */
	ret = sample_get_Roi_stream();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "Get video stream failed\n");
		return -1;
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
				IMP_LOG_ERR(TAG, "UnBind framesource%d and encoder%d failed\n", chn[i].framesource_chn.groupID, chn[i].imp_encoder.groupID);
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

static int save_Roi_stream(int fd, IMPEncoderStream *stream)
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

static void *get_Roi_stream(void *args)
{
	int val, i, chnNum, ret;
	char stream_path[64];
	IMPPayloadType payloadType;
	IMPEncoderMapRoiCfg map_roi;
	int stream_fd = -1;
	int mb_length = 0;
	int width_cnt = 0;
	int height_cnt = 0;

	val = (int)args;
	chnNum = val & 0xffff;
	payloadType = (val >> 16) & 0xffff;

	memset(&map_roi, 0, sizeof(IMPEncoderMapRoiCfg));

	ret = IMP_Encoder_StartRecvPic(chnNum);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_Encoder_StartRecvPic(%d) failed\n", chnNum);
		return ((void *)-1);
	}
	sprintf(stream_path, "%s/stream-%d.%s", STREAM_FILE_PATH_PREFIX, chnNum,
			(payloadType == PT_H264) ? "h264" : "h265");

	IMP_LOG_DBG(TAG, "Video ChnNum=%d Open Stream file %s ", chnNum, stream_path);
	stream_fd = open(stream_path, O_RDWR | O_CREAT | O_TRUNC, 0777);
	if (stream_fd < 0) {
		IMP_LOG_ERR(TAG, "open stream_fd failed\n");
		return ((void *)-1);
	}
	IMP_LOG_DBG(TAG, "OK\n");

	if (1 == enc_Roi) {
		mb_length = 16;

		if (chn[chnNum].fs_chn_attr.picWidth % mb_length == 0) {
			width_cnt = chn[chnNum].fs_chn_attr.picWidth / mb_length;
		} else {
			width_cnt = chn[chnNum].fs_chn_attr.picWidth / mb_length + 1;
		}

		if (chn[chnNum].fs_chn_attr.picHeight % mb_length == 0) {
			height_cnt = chn[chnNum].fs_chn_attr.picHeight / mb_length;
		} else {
			height_cnt = chn[chnNum].fs_chn_attr.picHeight / mb_length + 1;
		}

		map_roi.mapSize = width_cnt * height_cnt;
		map_roi.map = (uint8_t *)malloc(map_roi.mapSize);
		if (NULL == map_roi.map) {
			IMP_LOG_ERR(TAG, "map_roi malloc failed\n");
			return ((void *)-1);
		}
		memset(map_roi.map, 0, map_roi.mapSize);
	}

	for (i = 0; i < NR_FRAMES_TO_SAVE; i++) {
		ret = IMP_Encoder_PollingStream(chnNum, 1000);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_Encoder_PollingStream(%d) timeout\n", chnNum);
			continue;
		}

		IMPEncoderStream stream;
		/* Get H264 or H265 Stream */
		ret = IMP_Encoder_GetStream(chnNum, &stream, 1);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_Encoder_GetStream(%d) failed\n", chnNum);
			return ((void *)-1);
		}

		ret = save_Roi_stream(stream_fd, &stream);
		if (ret < 0) {
			close(stream_fd);
			return ((void *)ret);
		}

		IMP_Encoder_ReleaseStream(chnNum, &stream);

		if (1 == enc_Roi) {
#if 1
			/*Rect Roi*/
			if (i < 40 && 0 == chnNum) {
				IMPEncoderROIAttr roi_attr;
				memset(&roi_attr, 0, sizeof(IMPEncoderROIAttr));

				roi_attr.roi[0].u32Index = 0;
				roi_attr.roi[0].bEnable = 1;
				roi_attr.roi[0].bRelatedQp = 1;
				roi_attr.roi[0].s32Qp = -10;
				roi_attr.roi[0].rect.p0.x = 16;
				roi_attr.roi[0].rect.p0.y = 16;
				roi_attr.roi[0].rect.p1.x = 200;
				roi_attr.roi[0].rect.p1.y = 200;

				roi_attr.roi[1].u32Index = 1;
				roi_attr.roi[1].bEnable = 1;
				roi_attr.roi[1].bRelatedQp = 1;
				roi_attr.roi[1].s32Qp = -10;
				roi_attr.roi[1].rect.p0.x = 0;
				roi_attr.roi[1].rect.p0.y = 300;
				roi_attr.roi[1].rect.p1.x = 300;
				roi_attr.roi[1].rect.p1.y = 600;

				roi_attr.roi[4].u32Index = 15;
				roi_attr.roi[4].bEnable = 1;
				roi_attr.roi[4].bRelatedQp = 1;
				roi_attr.roi[4].s32Qp = -20;
				roi_attr.roi[4].rect.p0.x = 600;
				roi_attr.roi[4].rect.p0.y = 600;
				roi_attr.roi[4].rect.p1.x = 900;
				roi_attr.roi[4].rect.p1.y = 900;

				ret = IMP_Encoder_SetChnROI(chnNum, &roi_attr);
				if (ret < 0) {
					IMP_LOG_ERR(TAG, "IMP_Encoder_SetChnROI(%d) error: %d\n", chnNum, ret);
					return ((void *)-1);
				}
			}

			if (i >= 60 && 0 == chnNum) {
				IMPEncoderROIAttr roi_attr;
				memset(&roi_attr, 0, sizeof(IMPEncoderROIAttr));

				roi_attr.roi[0].u32Index = 0;
				roi_attr.roi[0].bEnable = 1;
				roi_attr.roi[0].bRelatedQp = 1;
				roi_attr.roi[0].s32Qp = -10;
				roi_attr.roi[0].rect.p0.x = 0;
				roi_attr.roi[0].rect.p0.y = 0;
				roi_attr.roi[0].rect.p1.x = 300;
				roi_attr.roi[0].rect.p1.y = 300;

				roi_attr.roi[3].u32Index = 1;
				roi_attr.roi[3].bEnable = 1;
				roi_attr.roi[3].bRelatedQp = 1;
				roi_attr.roi[3].s32Qp = -20;
				roi_attr.roi[3].rect.p0.x = 400;
				roi_attr.roi[3].rect.p0.y = 400;
				roi_attr.roi[3].rect.p1.x = 800;
				roi_attr.roi[3].rect.p1.y = 800;

				ret = IMP_Encoder_SetChnROI(chnNum, &roi_attr);
				if (ret < 0) {
					IMP_LOG_ERR(TAG, "IMP_Encoder_SetChnROI(%d) error: %d\n", chnNum, ret);
					return ((void *)-1);
				}
			}
#else
			/*Map Roi*/
			int m = 0, n = 0;
			if (i >= 20 && 0 == chnNum) {
				memset(map_roi.map, 0, map_roi.mapSize);
				for (m = 0; m < height_cnt/3; m++) {
					for (n = 0; n < width_cnt/10; n++) {
						/* 第一种使用方式 ==> 使用直接模式配置MapROI,可以直接配置每一个宏块。step1:指定QP模式,step2:指定QP值,step3:格式化封装 */
						IMPEncoderQPMode mode = IMP_ROI_QPMODE_DELTA;									/**< step1 */
						int qp = -10;																	/**< step2 */
						map_roi.map[m * width_cnt + width_cnt / 2 + n] = IMP_PACKING_MAP(mode, qp);		/**< step3 */
					}
				}
				map_roi.type = IMP_MAPPINGTYPE_DIRECT;
				ret = IMP_Encoder_SetChnMapRoi(chnNum, &map_roi, NULL);
				if (ret < 0) {
					IMP_LOG_ERR(TAG, "IMP_Encoder_SetChnMapRoi(%d) error: %d\n", chnNum, ret);
					return ((void *)-1);
				}
			}	/**< 此处的循环体表示在第20帧以后,手动绘制了一个矩形区域并且配置矩形区域内的宏块QP为帧级QP基础上减10,即<相对QP -10> */


			/* 第二种使用方式 ==> 结合Ingenic提供的算法.算法实时检测一帧时会输出一张热图数据,假设热图为:hot_map(unsigend char*) */
			/* memcpy(map_roi.map, hot_map, map_roi.mapSize); */
			/* map_roi.type = IMP_MAPPINGTYPE_DEVIOUS; */
			/* ret = IMP_Encoder_SetChnMapRoi(chnNum, &map_roi, &list); */
			/* if (ret < 0) { */
			/* 	IMP_LOG_ERR(TAG, "IMP_Encoder_SetChnMapRoi(%d) error: %d\n", chnNum, ret); */
			/* 	return ((void *)-1); */
			/* } */

#endif
		}
	}

	if(1 == enc_Roi) {
		free(map_roi.map);
		map_roi.map = NULL;
	}

	close(stream_fd);

	ret = IMP_Encoder_StopRecvPic(chnNum);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_Encoder_StopRecvPic(%d) failed\n", chnNum);
		return ((void *)-1);
	}

	return ((void *)0);
}

static int sample_get_Roi_stream()
{
	int i = 0;
	int ret = 0;
	pthread_t tid[FS_CHN_NUM];

	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			int arg = ((chn[i].payloadType << 16) | chn[i].index);
			ret = pthread_create(&tid[i], NULL, get_Roi_stream, (void *)arg);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "Create ChnNum%d get_Roi_stream failed\n", chn[i].index);
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
