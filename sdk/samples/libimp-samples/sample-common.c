/*
	sample-common.c

	Copyright (C) 2014 Ingenic Semiconductor Co.,Ltd

	This sample demonstrates some common functions.
*/

#include <sys/types.h>

#include "sample-common.h"

#define TAG "sample-Common"

int S_RC_METHOD = ENC_RC_MODE_CBR;
int direct_switch = 0;
int gosd_enable = 0; /* 1: ipu osd, 2: isp osd, 3: ipu osd and isp osd */
int kerenc_enable[FS_CHN_NUM] = {0};
int g_fs_nvbufs = 0; /* 0: use default, >0: override nrVBs for frame source */

static int g_fs_fps_monitor_run = 0;
static pthread_t g_fs_fps_tid[FS_CHN_NUM];

static void *fs_fps_monitor_thread(void *args)
{
	int chnNum = (int)(intptr_t)args;
	IMPFrameInfo *frame = NULL;
	int ret = 0;
	int64_t last_ts = 0;
	int frame_count = 0;
	int64_t stat_start_ts = 0;
	int log_count = 0;

	printf("[FS_CH%d] monitor thread started\n", chnNum);
	ret = IMP_FrameSource_SetFrameDepth(chnNum, 1);
	if (ret < 0) {
		printf("[FS_CH%d] monitor SetFrameDepth failed, ret=%d\n", chnNum, ret);
		return NULL;
	}

	stat_start_ts = IMP_System_GetTimeStamp();
	while (g_fs_fps_monitor_run) {
		ret = IMP_FrameSource_GetFrame(chnNum, &frame);
		if (ret < 0) {
			usleep(1000);
			continue;
		}

		int64_t now = IMP_System_GetTimeStamp();
		int64_t interval_ms = (last_ts > 0) ? (now - last_ts) / 1000 : 0;

		/* Only log anomalous intervals (>10ms deviation from expected 33ms for 30fps) */
		if (interval_ms > 43 || interval_ms < 23) {
			printf("[FS_CH%d] frame=%d ts=%lld interval=%lldms pool_idx=%d size=%d w=%d h=%d\n",
				chnNum, log_count, (long long)frame->timeStamp, (long long)interval_ms,
				frame->pool_idx, frame->size, frame->width, frame->height);
		}
		log_count++;
		last_ts = now;
		frame_count++;

		ret = IMP_FrameSource_ReleaseFrame(chnNum, frame);
		if (ret < 0) {
			printf("[FS_CH%d] monitor ReleaseFrame failed, ret=%d\n", chnNum, ret);
		}

		/* Print FPS stats every 2 seconds */
		if ((now - stat_start_ts) >= 2000000) {
			double fps = (double)frame_count * 1000000.0 / (now - stat_start_ts);
			IMPISPSensorFps sensorFps = {0};
			IMP_ISP_Tuning_GetSensorFPS(IMPVI_MAIN, &sensorFps);

			uint32_t reg_vals[6] = {0};
			uint32_t reg_addrs[6] = {0x0103, 0x0104, 0x0340, 0x0341, 0x0342, 0x0343};
			int reg_ok[6] = {0};
			for (int ri = 0; ri < 6; ri++) {
				IMPISPSensorRegister r = {0};
				r.addr = reg_addrs[ri];
				if (IMP_ISP_GetSensorRegister(IMPVI_MAIN, &r) == 0) {
					reg_vals[ri] = r.value;
					reg_ok[ri] = 1;
				}
			}
			printf("===== [FS_CH%d] FPS STAT: frames=%d, elapsed=%.1fs, fps=%.2f, sensor_fps=%d/%d, regs[0x0103]=0x%x[0x0104]=0x%x[0x0340]=0x%x[0x0341]=0x%x[0x0342]=0x%x[0x0343]=0x%x =====\n",
				chnNum, frame_count, (now - stat_start_ts) / 1000000.0, fps,
				sensorFps.num, sensorFps.den,
				reg_ok[0] ? reg_vals[0] : 0xFFFF, reg_ok[1] ? reg_vals[1] : 0xFFFF,
				reg_ok[2] ? reg_vals[2] : 0xFFFF, reg_ok[3] ? reg_vals[3] : 0xFFFF,
				reg_ok[4] ? reg_vals[4] : 0xFFFF, reg_ok[5] ? reg_vals[5] : 0xFFFF);
			frame_count = 0;
			stat_start_ts = now;
		}
	}

	IMP_FrameSource_SetFrameDepth(chnNum, 0);
	printf("[FS_CH%d] monitor thread exiting\n", chnNum);
	return NULL;
}

#define SAVE_STREAM
#define MULTI_ENCODE /* Multi process encode, Can be enabled based on your's demand */
#define SHOW_FRM_BITRATE
#ifdef SHOW_FRM_BITRATE
#define FRM_BIT_RATE_TIME 2
#define STREAM_TYPE_NUM 16
static int frmrate_sp[STREAM_TYPE_NUM] = { 0 };
static int statime_sp[STREAM_TYPE_NUM] = { 0 };
static int bitrate_sp[STREAM_TYPE_NUM] = { 0 };
#endif

struct chn_conf chn[FS_CHN_NUM] = {
	{
		.index = CHN0_INDEX,
		.enable = CHN0_EN,
		.payloadType = PT_H265,
		.fs_chn_attr = {
			.pixFmt = PIX_FMT_NV12,
			.outFrmRateNum = FIRST_SENSOR_FRAME_RATE_NUM,
			.outFrmRateDen = FIRST_SENSOR_FRAME_RATE_DEN,
			.nrVBs = 2,
			.type = FS_PHY_CHANNEL,

			.crop.enable = FIRST_CROP_EN,
			.crop.top = 0,
			.crop.left = 0,
			.crop.width = FIRST_SENSOR_WIDTH,
			.crop.height = FIRST_SENSOR_HEIGHT,

			.scaler.enable = 1,
			.scaler.outwidth = FIRST_SENSOR_WIDTH,
			.scaler.outheight = FIRST_SENSOR_HEIGHT,

			.picWidth = FIRST_SENSOR_WIDTH,
			.picHeight = FIRST_SENSOR_HEIGHT,

		},
		.framesource_chn =	{ DEV_ID_FS, CHN0_INDEX, 0},
		.imp_encoder = { DEV_ID_ENC, CHN0_INDEX, 0},
	},
	{
		.index = CHN1_INDEX,
		.enable = CHN1_EN,
		.payloadType = PT_H265,
		.fs_chn_attr = {
			.pixFmt = PIX_FMT_NV12,
			.outFrmRateNum = FIRST_SENSOR_FRAME_RATE_NUM,
			.outFrmRateDen = FIRST_SENSOR_FRAME_RATE_DEN,
			.nrVBs = 2,
			.type = FS_PHY_CHANNEL,

			.crop.enable = 1,
			.crop.top = 0,
			.crop.left = 0,
			.crop.width = FIRST_SENSOR_WIDTH_SECOND,
			.crop.height = FIRST_SENSOR_HEIGHT_SECOND,

			.scaler.enable = 1,
			.scaler.outwidth = FIRST_SENSOR_WIDTH_SECOND,
			.scaler.outheight = FIRST_SENSOR_HEIGHT_SECOND,

			.picWidth = FIRST_SENSOR_WIDTH_SECOND,
			.picHeight = FIRST_SENSOR_HEIGHT_SECOND,
		},
		.framesource_chn =	{ DEV_ID_FS, CHN1_INDEX, 0},
		.imp_encoder = { DEV_ID_ENC, CHN1_INDEX, 0},
	},
	{
		.index = CHN2_INDEX,
		.enable = CHN2_EN,
		.payloadType = PT_H265,
		.fs_chn_attr = {
			.pixFmt = PIX_FMT_NV12,
			.outFrmRateNum = FIRST_SENSOR_FRAME_RATE_NUM,
			.outFrmRateDen = FIRST_SENSOR_FRAME_RATE_DEN,
			.nrVBs = 2,
			.type = FS_PHY_CHANNEL,

			.crop.enable = 0,
			.crop.top = 0,
			.crop.left = 0,
			.crop.width = FIRST_SENSOR_WIDTH,
			.crop.height = FIRST_SENSOR_HEIGHT,

			.scaler.enable = 1,
			.scaler.outwidth = 3840,
			.scaler.outheight = 2160,

			.picWidth = 3840,
			.picHeight = 2160,
		},
		.framesource_chn =	{ DEV_ID_FS, CHN2_INDEX, 0},
		.imp_encoder = { DEV_ID_ENC, CHN2_INDEX, 0},
	},
	{
		.index = CHN3_INDEX,
		.enable = CHN3_EN,
		.payloadType = PT_H265,
		.fs_chn_attr = {
			.pixFmt = PIX_FMT_NV12,
			.outFrmRateNum = SECOND_SENSOR_FRAME_RATE_NUM,
			.outFrmRateDen = SECOND_SENSOR_FRAME_RATE_DEN,
			.nrVBs = 2,
			.type = FS_PHY_CHANNEL,

			.crop.enable = SECOND_CROP_EN,
			.crop.top = 0,
			.crop.left = 0,
			.crop.width = SECOND_SENSOR_WIDTH,
			.crop.height = SECOND_SENSOR_HEIGHT,

			.scaler.enable = 1,
			.scaler.outwidth = SECOND_SENSOR_WIDTH,
			.scaler.outheight = SECOND_SENSOR_HEIGHT,

			.picWidth = SECOND_SENSOR_WIDTH,
			.picHeight = SECOND_SENSOR_HEIGHT,
		},
		.framesource_chn =	{ DEV_ID_FS, CHN3_INDEX, 0},
		.imp_encoder = { DEV_ID_ENC, CHN3_INDEX, 0},
	},
	{
		.index = CHN4_INDEX,
		.enable = CHN4_EN,
		.payloadType = PT_H265,
		.fs_chn_attr = {
			.pixFmt = PIX_FMT_NV12,
			.outFrmRateNum = SECOND_SENSOR_FRAME_RATE_NUM,
			.outFrmRateDen = SECOND_SENSOR_FRAME_RATE_DEN,
			.nrVBs = 2,
			.type = FS_PHY_CHANNEL,

			.crop.enable = 1,
			.crop.top = 0,
			.crop.left = 0,
			.crop.width = SECOND_SENSOR_WIDTH_SECOND,
			.crop.height = SECOND_SENSOR_HEIGHT_SECOND,

			.scaler.enable = 1,
			.scaler.outwidth = SECOND_SENSOR_WIDTH_SECOND,
			.scaler.outheight = SECOND_SENSOR_HEIGHT_SECOND,

			.picWidth = SECOND_SENSOR_WIDTH_SECOND,
			.picHeight = SECOND_SENSOR_HEIGHT_SECOND,
		},
		.framesource_chn =	{ DEV_ID_FS, CHN4_INDEX, 0},
		.imp_encoder = { DEV_ID_ENC, CHN4_INDEX, 0},
	},
	{
		.index = CHN5_INDEX,
		.enable = CHN5_EN,
		.payloadType = PT_H265,
		.fs_chn_attr = {
			.pixFmt = PIX_FMT_NV12,
			.outFrmRateNum = SECOND_SENSOR_FRAME_RATE_NUM,
			.outFrmRateDen = SECOND_SENSOR_FRAME_RATE_DEN,
			.nrVBs = 2,
			.type = FS_PHY_CHANNEL,

			.crop.enable = 0,
			.crop.top = 0,
			.crop.left = 0,
			.crop.width = SECOND_SENSOR_WIDTH_THIRD,
			.crop.height = SECOND_SENSOR_HEIGHT_THIRD,

			.scaler.enable = 1,
			.scaler.outwidth = SECOND_SENSOR_WIDTH_THIRD,
			.scaler.outheight = SECOND_SENSOR_HEIGHT_THIRD,

			.picWidth = SECOND_SENSOR_WIDTH_THIRD,
			.picHeight = SECOND_SENSOR_HEIGHT_THIRD,
		},
		.framesource_chn =	{ DEV_ID_FS, CHN5_INDEX, 0},
		.imp_encoder = { DEV_ID_ENC, CHN5_INDEX, 0},
	},
	{
		.index = CHN6_INDEX,
		.enable = CHN6_EN,
		.payloadType = PT_H265,
		.fs_chn_attr = {
			.pixFmt = PIX_FMT_NV12,
			.outFrmRateNum = THIRD_SENSOR_FRAME_RATE_NUM,
			.outFrmRateDen = THIRD_SENSOR_FRAME_RATE_DEN,
			.nrVBs = 2,
			.type = FS_PHY_CHANNEL,

			.crop.enable = THIRD_CROP_EN,
			.crop.top = 0,
			.crop.left = 0,
			.crop.width = THIRD_SENSOR_WIDTH,
			.crop.height = THIRD_SENSOR_HEIGHT,

			.scaler.enable = 1,
			.scaler.outwidth = THIRD_SENSOR_WIDTH,
			.scaler.outheight = THIRD_SENSOR_HEIGHT,

			.picWidth = THIRD_SENSOR_WIDTH,
			.picHeight = THIRD_SENSOR_HEIGHT,

		},
		.framesource_chn =	{ DEV_ID_FS, CHN6_INDEX, 0},
		.imp_encoder = { DEV_ID_ENC, CHN6_INDEX, 0},
	},
	{
		.index = CHN7_INDEX,
		.enable = CHN7_EN,
		.payloadType = PT_H265,
		.fs_chn_attr = {
			.pixFmt = PIX_FMT_NV12,
			.outFrmRateNum = THIRD_SENSOR_FRAME_RATE_NUM,
			.outFrmRateDen = THIRD_SENSOR_FRAME_RATE_DEN,
			.nrVBs = 2,
			.type = FS_PHY_CHANNEL,

			.crop.enable = 1,
			.crop.top = 0,
			.crop.left = 0,
			.crop.width = THIRD_SENSOR_WIDTH_SECOND,
			.crop.height = THIRD_SENSOR_HEIGHT_SECOND,

			.scaler.enable = 1,
			.scaler.outwidth = THIRD_SENSOR_WIDTH_SECOND,
			.scaler.outheight = THIRD_SENSOR_HEIGHT_SECOND,

			.picWidth = THIRD_SENSOR_WIDTH_SECOND,
			.picHeight = THIRD_SENSOR_HEIGHT_SECOND,
		},
		.framesource_chn =	{ DEV_ID_FS, CHN7_INDEX, 0},
		.imp_encoder = { DEV_ID_ENC, CHN7_INDEX, 0},
	},
	{
		.index = CHN8_INDEX,
		.enable = CHN8_EN,
		.payloadType = PT_H265,
		.fs_chn_attr = {
			.pixFmt = PIX_FMT_NV12,
			.outFrmRateNum = THIRD_SENSOR_FRAME_RATE_NUM,
			.outFrmRateDen = THIRD_SENSOR_FRAME_RATE_DEN,
			.nrVBs = 2,
			.type = FS_PHY_CHANNEL,

			.crop.enable = 0,
			.crop.top = 0,
			.crop.left = 0,
			.crop.width = THIRD_SENSOR_WIDTH_THIRD,
			.crop.height = THIRD_SENSOR_HEIGHT_THIRD,

			.scaler.enable = 1,
			.scaler.outwidth = THIRD_SENSOR_WIDTH_THIRD,
			.scaler.outheight = THIRD_SENSOR_HEIGHT_THIRD,

			.picWidth = THIRD_SENSOR_WIDTH_THIRD,
			.picHeight = THIRD_SENSOR_HEIGHT_THIRD,
		},
		.framesource_chn =	{ DEV_ID_FS, CHN8_INDEX, 0},
		.imp_encoder = { DEV_ID_ENC, CHN8_INDEX, 0},
	},
	{
		.index = CHN9_INDEX,
		.enable = CHN9_EN,
		.payloadType = PT_H265,
		.fs_chn_attr = {
			.pixFmt = PIX_FMT_NV12,
			.outFrmRateNum = FOURTH_SENSOR_FRAME_RATE_NUM,
			.outFrmRateDen = FOURTH_SENSOR_FRAME_RATE_DEN,
			.nrVBs = 2,
			.type = FS_PHY_CHANNEL,

			.crop.enable = FOURTH_CROP_EN,
			.crop.top = 0,
			.crop.left = 0,
			.crop.width = FOURTH_SENSOR_WIDTH,
			.crop.height = FOURTH_SENSOR_HEIGHT,

			.scaler.enable = 1,
			.scaler.outwidth = FOURTH_SENSOR_WIDTH,
			.scaler.outheight = FOURTH_SENSOR_HEIGHT,

			.picWidth = FOURTH_SENSOR_WIDTH,
			.picHeight = FOURTH_SENSOR_HEIGHT,
		},
		.framesource_chn =	{ DEV_ID_FS, CHN9_INDEX, 0},
		.imp_encoder = { DEV_ID_ENC, CHN9_INDEX, 0},
	},
	{
		.index = CHN10_INDEX,
		.enable = CHN10_EN,
		.payloadType = PT_H265,
		.fs_chn_attr = {
			.pixFmt = PIX_FMT_NV12,
			.outFrmRateNum = FOURTH_SENSOR_FRAME_RATE_NUM,
			.outFrmRateDen = FOURTH_SENSOR_FRAME_RATE_DEN,
			.nrVBs = 2,
			.type = FS_PHY_CHANNEL,

			.crop.enable = 1,
			.crop.top = 0,
			.crop.left = 0,
			.crop.width = FOURTH_SENSOR_WIDTH_SECOND,
			.crop.height = FOURTH_SENSOR_HEIGHT_SECOND,

			.scaler.enable = 1,
			.scaler.outwidth = FOURTH_SENSOR_WIDTH_SECOND,
			.scaler.outheight = FOURTH_SENSOR_HEIGHT_SECOND,

			.picWidth = FOURTH_SENSOR_WIDTH_SECOND,
			.picHeight = FOURTH_SENSOR_HEIGHT_SECOND,
		},
		.framesource_chn =	{ DEV_ID_FS, CHN10_INDEX, 0},
		.imp_encoder = { DEV_ID_ENC, CHN10_INDEX, 0},
	},
	{
		.index = CHN11_INDEX,
		.enable = CHN11_EN,
		.payloadType = PT_H265,
		.fs_chn_attr = {
			.pixFmt = PIX_FMT_NV12,
			.outFrmRateNum = FOURTH_SENSOR_FRAME_RATE_NUM,
			.outFrmRateDen = FOURTH_SENSOR_FRAME_RATE_DEN,
			.nrVBs = 2,
			.type = FS_PHY_CHANNEL,

			.crop.enable = 0,
			.crop.top = 0,
			.crop.left = 0,
			.crop.width = FOURTH_SENSOR_WIDTH_THIRD,
			.crop.height = FOURTH_SENSOR_HEIGHT_THIRD,

			.scaler.enable = 1,
			.scaler.outwidth = FOURTH_SENSOR_WIDTH_THIRD,
			.scaler.outheight = FOURTH_SENSOR_HEIGHT_THIRD,

			.picWidth = FOURTH_SENSOR_WIDTH_THIRD,
			.picHeight = FOURTH_SENSOR_HEIGHT_THIRD,
		},
		.framesource_chn =	{ DEV_ID_FS, CHN11_INDEX, 0},
		.imp_encoder = { DEV_ID_ENC, CHN11_INDEX, 0},
	},
};

IMPSensorInfo Def_Sensor_Info[4] = {
	{
		.name = FIRST_SNESOR_NAME,
		.cbus_type = TX_SENSOR_CONTROL_INTERFACE_I2C,
		.i2c = {FIRST_SNESOR_NAME, FIRST_I2C_ADDR, FIRST_I2C_ADAPTER_ID},
		.rst_gpio = FIRST_RST_GPIO,
		.pwdn_gpio = FIRST_PWDN_GPIO,
		.power_gpio = FIRST_POWER_GPIO,
		.switch_gpio = FIRST_SWITCH_GPIO,
		.switch_gpio_state = 0,
		.sensor_id = FIRST_SENSOR_ID,
		.video_interface = FIRST_VIDEO_INTERFACE,
		.mclk = FIRST_MCLK,
		.default_boot = FIRST_DEFAULT_BOOT,
		.fps = {FIRST_SENSOR_FRAME_RATE_NUM, FIRST_SENSOR_FRAME_RATE_DEN}
	},
	{
		.name = SECOND_SNESOR_NAME,
		.cbus_type = TX_SENSOR_CONTROL_INTERFACE_I2C,
		.i2c = {SECOND_SNESOR_NAME, SECOND_I2C_ADDR, SECOND_I2C_ADAPTER_ID},
		.rst_gpio = SECOND_RST_GPIO,
		.pwdn_gpio = SECOND_PWDN_GPIO,
		.power_gpio = SECOND_POWER_GPIO,
		.switch_gpio = SECOND_SWITCH_GPIO,
		.switch_gpio_state = 0,
		.sensor_id = SECOND_SENSOR_ID,
		.video_interface = SECOND_VIDEO_INTERFACE,
		.mclk = SECOND_MCLK,
		.default_boot = SECOND_DEFAULT_BOOT,
		.fps = {15, 0}
	},
	{
		.name = THIRD_SNESOR_NAME,
		.cbus_type = TX_SENSOR_CONTROL_INTERFACE_I2C,
		.i2c = {THIRD_SNESOR_NAME, THIRD_I2C_ADDR, THIRD_I2C_ADAPTER_ID},
		.rst_gpio = THIRD_RST_GPIO,
		.pwdn_gpio = THIRD_PWDN_GPIO,
		.power_gpio = THIRD_POWER_GPIO,
		.switch_gpio = THIRD_SWITCH_GPIO,
		.switch_gpio_state = 0,
		.sensor_id = THIRD_SENSOR_ID,
		.video_interface = THIRD_VIDEO_INTERFACE,
		.mclk = THIRD_MCLK,
		.default_boot = THIRD_DEFAULT_BOOT,
		.fps = {15, 0}
	},
	{
		.name = FOURTH_SNESOR_NAME,
		.cbus_type = TX_SENSOR_CONTROL_INTERFACE_I2C,
		.i2c = {FOURTH_SNESOR_NAME, FOURTH_I2C_ADDR, FOURTH_I2C_ADAPTER_ID},
		.rst_gpio = FOURTH_RST_GPIO,
		.pwdn_gpio = FOURTH_PWDN_GPIO,
		.power_gpio = FOURTH_POWER_GPIO,
		.switch_gpio = FOURTH_SWITCH_GPIO,
		.switch_gpio_state = 0,
		.sensor_id = FOURTH_SENSOR_ID,
		.video_interface = FOURTH_VIDEO_INTERFACE,
		.mclk = FOURTH_MCLK,
		.default_boot = FOURTH_DEFAULT_BOOT,
		.fps = {15, 0}
	}
};

extern int IMP_OSD_SetPoolSize(int size);

IMPSensorInfo sensor_info[4];
IMPISPCameraInputMode mode = {
	.sensor_num = SENSOR_NUM,
};

int sample_system_init()
{
	int ret = 0;

	/* isp osd and ipu osd buffer size set */
	if(1 == gosd_enable) { /* only use ipu osd */
		IMP_OSD_SetPoolSize(512*1024);
	} else if(2 == gosd_enable) { /* only use isp osd */
		IMP_ISP_Tuning_SetOsdPoolSize(512 * 1024);
	}else if(3 == gosd_enable) { /* use ipu osd and isp osd */
		IMP_OSD_SetPoolSize(512*1024);
		IMP_ISP_Tuning_SetOsdPoolSize(512 * 1024);
	} else {
		IMP_OSD_SetPoolSize(512*1024);
		IMP_ISP_Tuning_SetOsdPoolSize(512 * 1024);
	}

	ret = IMP_Encoder_SetJpegBsSize(500 * 1024);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_Encoder_SetJpegBsSize failed\n");
		return -1;
	}

	ret = IMP_Encoder_SetMultiSectionMode(1, 250, 2);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_Encoder_SetMultiSectionMode failed\n");
		return -1;
	}

#ifdef MULTI_ENCODE
	ret = IMP_Encoder_MultiProcessInit();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_Encoder_MultiProcessInit failed\n");
		return -1;
	}
#endif

	memset(&sensor_info, 0, sizeof(sensor_info));
	memcpy(&sensor_info[0], &Def_Sensor_Info[0], sizeof(IMPSensorInfo));
	memcpy(&sensor_info[1], &Def_Sensor_Info[1], sizeof(IMPSensorInfo));
	memcpy(&sensor_info[2], &Def_Sensor_Info[2], sizeof(IMPSensorInfo));
	memcpy(&sensor_info[3], &Def_Sensor_Info[3], sizeof(IMPSensorInfo));

	IMP_LOG_DBG(TAG, "sample_system_init start\n");

	ret = IMP_ISP_Open();
	if(ret < 0){
		IMP_LOG_ERR(TAG, "failed to open ISP\n");
		return -1;
	}

	if (mode.sensor_num > IMPISP_TOTAL_ONE) {
		ret = IMP_ISP_SetCameraInputMode(&mode);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_ISP_SetCameraInputMode failed\n");
			return -1;
		}
	}

	ret = IMP_ISP_AddSensor(IMPVI_MAIN, &sensor_info[0]);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_ISP_AddSensor(%d) failed\n", IMPVI_MAIN);
		return -1;
	}
	if (mode.sensor_num > IMPISP_TOTAL_ONE) {
		ret = IMP_ISP_AddSensor(IMPVI_SEC, &sensor_info[1]);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_ISP_AddSensor(%d) failed\n", IMPVI_SEC);
			return -1;
		}
	}
	if (mode.sensor_num > IMPISP_TOTAL_TWO) {
		ret = IMP_ISP_AddSensor(IMPVI_THR, &sensor_info[2]);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_ISP_AddSensor(%d) failed\n", IMPVI_SEC);
			return -1;
		}
	}
	if (mode.sensor_num > IMPISP_TOTAL_THR) {
		ret = IMP_ISP_AddSensor(IMPVI_FOUR, &sensor_info[3]);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_ISP_AddSensor(%d) failed\n", IMPVI_SEC);
			return -1;
		}
	}

	ret = IMP_ISP_EnableSensor(IMPVI_MAIN, &sensor_info[0]);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_ISP_EnableSensor(%d) failed\n", IMPVI_MAIN);
		return -1;
	}
	if (mode.sensor_num > IMPISP_TOTAL_ONE) {
		ret = IMP_ISP_EnableSensor(IMPVI_SEC, &sensor_info[1]);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_ISP_EnableSensor(%d) failed\n", IMPVI_SEC);
			return -1;
		}
	}
	if (mode.sensor_num > IMPISP_TOTAL_TWO) {
		ret = IMP_ISP_EnableSensor(IMPVI_THR, &sensor_info[2]);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_ISP_EnableSensor(%d) failed\n", IMPVI_SEC);
			return -1;
		}
	}
	if (mode.sensor_num > IMPISP_TOTAL_THR) {
		ret = IMP_ISP_EnableSensor(IMPVI_FOUR, &sensor_info[3]);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_ISP_EnableSensor(%d) failed\n", IMPVI_SEC);
			return -1;
		}
	}

	ret = IMP_System_Init();
	if(ret < 0){
		IMP_LOG_ERR(TAG, "IMP_System_Init failed\n");
		return -1;
	}

	ret = IMP_ISP_EnableTuning();
	if(ret < 0){
		IMP_LOG_ERR(TAG, "IMP_ISP_EnableTuning failed\n");
		return -1;
	}
	unsigned char value = 128;
	IMP_ISP_Tuning_SetContrast(IMPVI_MAIN, &value);
	IMP_ISP_Tuning_SetSharpness(IMPVI_MAIN, &value);
	IMP_ISP_Tuning_SetSaturation(IMPVI_MAIN, &value);
	IMP_ISP_Tuning_SetBrightness(IMPVI_MAIN, &value);
	IMPISPRunningMode mode = IMPISP_RUNNING_MODE_DAY;
	ret = IMP_ISP_Tuning_SetISPRunningMode(IMPVI_MAIN, &mode);
	if (ret < 0){
		IMP_LOG_ERR(TAG, "failed to set running mode\n");
		return -1;
	}
	/* Skip SetSensorFPS to verify if init VTS=1500 is sufficient for 30fps */
#if 0
	IMPISPSensorFps setFps = { FIRST_SENSOR_FRAME_RATE_NUM, FIRST_SENSOR_FRAME_RATE_DEN };
	ret = IMP_ISP_Tuning_SetSensorFPS(IMPVI_MAIN, &setFps);
	if (ret < 0){
		IMP_LOG_ERR(TAG, "failed to set sensor fps\n");
		return -1;
	}
	if (SENSOR_NUM > IMPISP_TOTAL_ONE) {
		setFps.num = SECOND_SENSOR_FRAME_RATE_NUM;
		setFps.den = SECOND_SENSOR_FRAME_RATE_DEN;
		ret = IMP_ISP_Tuning_SetSensorFPS(IMPVI_SEC, &setFps);
		if (ret < 0){
			IMP_LOG_ERR(TAG, "failed to set sensor fps\n");
			return -1;
		}
	}
	if (SENSOR_NUM > IMPISP_TOTAL_TWO) {
		setFps.num = THIRD_SENSOR_FRAME_RATE_NUM;
		setFps.den = THIRD_SENSOR_FRAME_RATE_DEN;
		ret = IMP_ISP_Tuning_SetSensorFPS(IMPVI_THR, &setFps);
		if (ret < 0){
			IMP_LOG_ERR(TAG, "failed to set sensor fps\n");
			return -1;
		}
	}
	if (SENSOR_NUM > IMPISP_TOTAL_THR) {
		setFps.num = FOURTH_SENSOR_FRAME_RATE_NUM;
		setFps.den = FOURTH_SENSOR_FRAME_RATE_DEN;
		ret = IMP_ISP_Tuning_SetSensorFPS(IMPVI_FOUR, &setFps);
		if (ret < 0){
			IMP_LOG_ERR(TAG, "failed to set sensor fps\n");
			return -1;
		}
	}
#endif

	IMP_LOG_INFO(TAG, "===== Sensor Config: name=%s, res=%dx%d, fps=%d/%d, nrVBs=%d =====\n",
		sensor_info[0].name, FIRST_SENSOR_WIDTH, FIRST_SENSOR_HEIGHT,
		FIRST_SENSOR_FRAME_RATE_NUM, FIRST_SENSOR_FRAME_RATE_DEN,
		chn[0].fs_chn_attr.nrVBs);

	IMP_LOG_DBG(TAG, "ImpSystemInit success\n");

	return 0;
}

int sample_system_exit()
{
	int ret = 0;

	IMP_LOG_DBG(TAG, "sample_system_exit start\n");

	IMP_System_Exit();

	ret = IMP_ISP_DisableSensor(IMPVI_MAIN);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_ISP_DisableSensor(%d) failed\n", IMPVI_MAIN);
		return -1;
	}
	if (mode.sensor_num > IMPISP_TOTAL_ONE) {
		ret = IMP_ISP_DisableSensor(IMPVI_SEC);
		if(ret < 0){
			IMP_LOG_ERR(TAG, "IMP_ISP_DisableSensor(%d) failed\n", IMPVI_SEC);
			return -1;
		}
	}
	if (mode.sensor_num > IMPISP_TOTAL_TWO) {
		ret = IMP_ISP_DisableSensor(IMPVI_THR);
		if(ret < 0){
			IMP_LOG_ERR(TAG, "IMP_ISP_DisableSensor(%d) failed\n", IMPVI_THR);
			return -1;
		}
	}
	if (mode.sensor_num > IMPISP_TOTAL_THR) {
		ret = IMP_ISP_DisableSensor(IMPVI_FOUR);
		if(ret < 0){
			IMP_LOG_ERR(TAG, "IMP_ISP_DisableSensor(%d) failed\n", IMPVI_FOUR);
			return -1;
		}
	}

	ret = IMP_ISP_DelSensor(IMPVI_MAIN, &sensor_info[0]);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_ISP_DelSensor(%d) failed\n", IMPVI_MAIN);
		return -1;
	}
	if (mode.sensor_num > IMPISP_TOTAL_ONE) {
		ret = IMP_ISP_DelSensor(IMPVI_SEC, &sensor_info[1]);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_ISP_DelSensor(%d) failed\n", IMPVI_SEC);
			return -1;
		}
	}
	if (mode.sensor_num > IMPISP_TOTAL_TWO) {
		ret = IMP_ISP_DelSensor(IMPVI_THR, &sensor_info[2]);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_ISP_DelSensor(%d) failed\n", IMPVI_THR);
			return -1;
		}
	}
	if (mode.sensor_num > IMPISP_TOTAL_THR) {
		ret = IMP_ISP_DelSensor(IMPVI_FOUR, &sensor_info[3]);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_ISP_DelSensor(%d) failed\n", IMPVI_FOUR);
			return -1;
		}
	}

	ret = IMP_ISP_DisableTuning();
	if(ret < 0){
		IMP_LOG_ERR(TAG, "IMP_ISP_DisableTuning failed\n");
		return -1;
	}

	if(IMP_ISP_Close()){
		IMP_LOG_ERR(TAG, "failed to open ISP\n");
		return -1;
	}

#ifdef MULTI_ENCODE
	IMP_Encoder_MultiProcessExit();
#endif

	IMP_LOG_DBG(TAG, " sample_system_exit success\n");

	return 0;
}

static void *get_frame(void *args)
{
	int index = (int)args;
	int chnNum = chn[index].index;
	int i = 0;
	int ret = 0;
	IMPFrameInfo *frame = NULL;
	char framefilename[64];
	int fd = -1;

	if (PIX_FMT_NV12 == chn[index].fs_chn_attr.pixFmt) {
		sprintf(framefilename, "/mnt/sdcard/DCIM/frame%dx%d_%d.nv12", chn[index].fs_chn_attr.picWidth, chn[index].fs_chn_attr.picHeight, chnNum);
	} else {
		sprintf(framefilename, "/mnt/sdcard/DCIM/frame%dx%d_%d.raw", chn[index].fs_chn_attr.picWidth, chn[index].fs_chn_attr.picHeight, chnNum);
	}

	fd = open(framefilename, O_RDWR | O_CREAT, 0x644);
	if (fd < 0) {
		IMP_LOG_ERR(TAG, "open %s failed:%s\n", framefilename, strerror(errno));
		goto err_open_framefilename;
	}

	ret = IMP_FrameSource_SetFrameDepth(chnNum, chn[index].fs_chn_attr.nrVBs * 2);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_FrameSource_SetFrameDepth(%d,%d) failed\n", chnNum, chn[index].fs_chn_attr.nrVBs * 2);
		goto err_IMP_FrameSource_SetFrameDepth_1;
	}

	for (i = 0; i < NR_FRAMES_TO_SAVE; i++) {
		ret = IMP_FrameSource_GetFrame(chnNum, &frame);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_FrameSource_GetFrame(%d) i=%d failed\n", chnNum, i);
			goto err_IMP_FrameSource_GetFrame_i;
		}
		if (NR_FRAMES_TO_SAVE/2 == i) {
			if (write(fd, (void *)frame->virAddr, frame->size) != frame->size) {
				IMP_LOG_ERR(TAG, "chnNum=%d write frame i=%d failed\n", chnNum, i);
				goto err_write_frame;
			}
		}
		ret = IMP_FrameSource_ReleaseFrame(chnNum, frame);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_FrameSource_ReleaseFrame(%d) i=%d failed\n", chnNum, i);
			goto err_IMP_FrameSource_ReleaseFrame_i;
		}
	}

	IMP_FrameSource_SetFrameDepth(chnNum, 0);

	return (void *)0;

err_IMP_FrameSource_ReleaseFrame_i:
err_write_frame:
	IMP_FrameSource_ReleaseFrame(chnNum, frame);
err_IMP_FrameSource_GetFrame_i:
	goto err_IMP_FrameSource_SetFrameDepth_1;
	IMP_FrameSource_SetFrameDepth(chnNum, 0);
err_IMP_FrameSource_SetFrameDepth_1:
	close(fd);
err_open_framefilename:
	return (void *)-1;
}

int sample_get_frame()
{
	int i = 0;
	int ret = 0;
	pthread_t tid[FS_CHN_NUM];

	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			ret = pthread_create(&tid[i], NULL, get_frame, (void *)chn[i].index);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "Create ChnNum%d get_frame failed\n", chn[i].index);
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

int sample_start_fs_fps_monitor()
{
	int i = 0;
	int ret = 0;

	g_fs_fps_monitor_run = 1;
	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			printf("Starting FS FPS monitor for chn%d\n", chn[i].index);
			ret = pthread_create(&g_fs_fps_tid[i], NULL, fs_fps_monitor_thread, (void *)(intptr_t)chn[i].index);
			if (ret < 0) {
				printf("Create FS FPS monitor thread for chn%d failed, ret=%d\n", chn[i].index, ret);
			}
		}
	}
	return 0;
}

void sample_stop_fs_fps_monitor()
{
	int i = 0;

	printf("Stopping FS FPS monitor...\n");
	g_fs_fps_monitor_run = 0;
	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			pthread_join(g_fs_fps_tid[i], NULL);
		}
	}
	printf("FS FPS monitor stopped\n");
}

int sample_framesource_init()
{
	int i = 0;
	int ret = 0;

	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			if (g_fs_nvbufs > 0) {
				chn[i].fs_chn_attr.nrVBs = g_fs_nvbufs;
				IMP_LOG_INFO(TAG, "Override chn[%d] nrVBs to %d\n", chn[i].index, g_fs_nvbufs);
			}
			ret = IMP_FrameSource_CreateChn(chn[i].index, &chn[i].fs_chn_attr);
			if(ret < 0){
				IMP_LOG_ERR(TAG, "IMP_FrameSource_CreateChn(%d) failed\n", chn[i].index);
				return -1;
			}

			ret = IMP_FrameSource_SetChnAttr(chn[i].index, &chn[i].fs_chn_attr);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "IMP_FrameSource_SetChnAttr(%d) failed\n", chn[i].index);
				return -1;
			}
		}
	}

	return 0;
}

int sample_framesource_exit()
{
	int i = 0;
	int ret = 0;

	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			/* Destroy channel */
			ret = IMP_FrameSource_DestroyChn(chn[i].index);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "IMP_FrameSource_DestroyChn(%d) failed\n", chn[i].index);
				return -1;
			}
		}
	}
	return 0;
}


int sample_jpeg_init()
{
	int i = 0;
	int ret = 0;
	IMPEncoderAttr *enc_attr;
	IMPEncoderRcAttr *rc_attr;
	IMPEncoderCHNAttr channel_attr;
	IMPFSChnAttr *imp_chn_attr_tmp;

	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			/* channel0 run v0+v1，channel1 and channel2 run v0 */
			if (SENSOR_NUM == IMPISP_TOTAL_ONE) {
				if (chn[i].index != 2)
					continue;
			} else if (SENSOR_NUM == IMPISP_TOTAL_TWO) {
				if (chn[i].index != 0 && chn[i].index != 3)
					continue;
			} else if (SENSOR_NUM == IMPISP_TOTAL_THR) {
				if (chn[i].index != 0 && chn[i].index != 3 && chn[i].index != 6)
					continue;
			} else if (SENSOR_NUM == IMPISP_TOTAL_FOU) {
				if (chn[i].index != 0 && chn[i].index != 3 && chn[i].index != 6 && chn[i].index != 9)
					continue;
			}

			imp_chn_attr_tmp = &chn[i].fs_chn_attr;
			memset(&channel_attr, 0, sizeof(IMPEncoderCHNAttr));
			enc_attr = &channel_attr.encAttr;
			enc_attr->enType = PT_JPEG;
			enc_attr->bufSize = 0;
			enc_attr->profile = 0;
			/* Baseline test: no scale up, JPEG = FrameSource = 2560x1440
			 * Verify basic concurrent video+jpeg stability.
			 */
			enc_attr->picWidth = imp_chn_attr_tmp->picWidth;
			enc_attr->picHeight = imp_chn_attr_tmp->picHeight;
			IMP_LOG_INFO(TAG, "JPEG Channel %d: target=%dx%d (no scale, same as FrameSource)\n",
				12+chn[i].index/3, enc_attr->picWidth, enc_attr->picHeight);
			rc_attr = &channel_attr.rcAttr;
			rc_attr->attrRcMode.rcMode = ENC_RC_MODE_FIXQP;
			rc_attr->attrRcMode.attrJPEGFixQp.qp = 40;

			if(direct_switch == 1) {
				if (0 == chn[i].index)
					channel_attr.bEnableIvdc = true;
			} else if (direct_switch == 2) {
				if (0 == chn[i].index || 3 == chn[i].index)
					channel_attr.bEnableIvdc = true;
			} else if (direct_switch == 3) {
				if (0 == chn[i].index || 3 == chn[i].index || 6 == chn[i].index)
					channel_attr.bEnableIvdc = true;
			} else if (direct_switch == 4) {
				if (0 == chn[i].index || 3 == chn[i].index || 6 == chn[i].index || 9 == chn[i].index)
					channel_attr.bEnableIvdc = true;
			}

			/* Create Channel */
			ret = IMP_Encoder_CreateChn(12+chn[i].index/3, &channel_attr);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "IMP_Encoder_CreateChn(%d) failed\n", 12+chn[i].index/3);
				return -1;
			}

			/* Resigter Channel */
			ret = IMP_Encoder_RegisterChn(chn[i].index, 12+chn[i].index/3);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "IMP_Encoder_RegisterChn(group%d, chn%d) failed\n", chn[i].index, 12+chn[i].index/3);
				return -1;
			}
		}
	}

	return 0;
}

int sample_jpeg_exit(void)
{
	int i = 0;
	int ret = 0;
	int chnNum = 0;
	IMPEncoderCHNStat chn_stat;

	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			if (SENSOR_NUM == IMPISP_TOTAL_ONE) {
				if (chn[i].index != 2)
					continue;
			} else if (SENSOR_NUM == IMPISP_TOTAL_TWO) {
				if (chn[i].index != 0 && chn[i].index != 3)
					continue;
			} else if (SENSOR_NUM == IMPISP_TOTAL_THR) {
				if (chn[i].index != 0 && chn[i].index != 3 && chn[i].index != 6)
					continue;
			} else if (SENSOR_NUM == IMPISP_TOTAL_FOU) {
				if (chn[i].index != 0 && chn[i].index != 3 && chn[i].index != 6 && chn[i].index != 9)
					continue;
			}
			chnNum = 12+chn[i].index/3;

			memset(&chn_stat, 0, sizeof(IMPEncoderCHNStat));
			ret = IMP_Encoder_Query(chnNum, &chn_stat);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "IMP_Encoder_Query(%d) failed\n", chnNum);
				return -1;
			}

			if (chn_stat.registered) {
				ret = IMP_Encoder_UnRegisterChn(chnNum);
				if (ret < 0) {
					IMP_LOG_ERR(TAG, "IMP_Encoder_UnRegisterChn(%d) failed\n", chnNum);
					return -1;
				}

				ret = IMP_Encoder_DestroyChn(chnNum);
				if (ret < 0) {
					IMP_LOG_ERR(TAG, "IMP_Encoder_DestroyChn(%d) failed\n", chnNum);
					return -1;
				}
			}
		}
	}

	return 0;
}

int sample_video_init()
{
	int i = 0;
	int ret = 0;
	int chnNum = 0;
	int s32picWidth = 0, s32picHeight = 0;
	IMPEncoderAttr *enc_attr;
	IMPEncoderRcAttr *rc_attr;
	IMPFSChnAttr *imp_chn_attr_tmp;
	IMPEncoderCHNAttr channel_attr;
	IMPFSI2DAttr i2d_attr;

	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			/* CH2 is dedicated to JPEG capture, skip video encoder */
			if (chn[i].index == 2)
				continue;
			imp_chn_attr_tmp = &chn[i].fs_chn_attr;
			memset(&channel_attr, 0, sizeof(IMPEncoderCHNAttr));
			memset(&i2d_attr, 0, sizeof(IMPFSI2DAttr));

			ret = IMP_FrameSource_GetI2dAttr(chn[i].index, &i2d_attr);
			if(ret < 0){
				IMP_LOG_ERR(TAG, "IMP_FrameSource_GetI2dAttr(%d) failed\n", chn[i].index);
				return -1;
			}

			if((1 == i2d_attr.i2d_enable) &&
					((i2d_attr.rotate_enable) && (i2d_attr.rotate_angle == 90 || i2d_attr.rotate_angle == 270))){
				/* this depend on your sensor or channels */
				s32picWidth  = chn[i].fs_chn_attr.picHeight;
				s32picHeight = chn[i].fs_chn_attr.picWidth;
			} else {
				s32picWidth  = chn[i].fs_chn_attr.picWidth;
				s32picHeight = chn[i].fs_chn_attr.picHeight;
			}

			enc_attr = &channel_attr.encAttr;
			enc_attr->enType = chn[i].payloadType;
			if ((s32picHeight > 1920) || (s32picHeight == 1920)) {
				enc_attr->bufSize = s32picWidth * s32picHeight * 3 / 2;
			} else if ((s32picHeight > 1520) || (s32picHeight == 1520)) {
				enc_attr->bufSize = s32picWidth * s32picHeight * 3 / 8;
			} else if ((s32picHeight > 1080) || (s32picHeight == 1080)) {
				enc_attr->bufSize = s32picWidth * s32picHeight / 2;
			} else {
				enc_attr->bufSize = s32picWidth * s32picHeight * 3 / 4;
			}
			enc_attr->profile   = 1;
			enc_attr->picWidth  = s32picWidth;
			enc_attr->picHeight = s32picHeight;
			rc_attr = &channel_attr.rcAttr;
			rc_attr->attrHSkip.hSkipAttr.skipType = IMP_Encoder_STYPE_N1X;
			rc_attr->attrHSkip.hSkipAttr.m = 3;
			rc_attr->attrHSkip.hSkipAttr.n = 4;
			rc_attr->attrHSkip.hSkipAttr.maxSameSceneCnt = 0;
			rc_attr->attrHSkip.hSkipAttr.bEnableScenecut = 0;
			rc_attr->attrHSkip.hSkipAttr.bBlackEnhance = 0;
			rc_attr->attrHSkip.maxHSkipType = IMP_Encoder_STYPE_N1X;
			if (chn[i].payloadType == PT_H264) {
				chnNum = chn[i].index;
				rc_attr->outFrmRate.frmRateNum = imp_chn_attr_tmp->outFrmRateNum;
				rc_attr->outFrmRate.frmRateDen = imp_chn_attr_tmp->outFrmRateDen;
				rc_attr->maxGop = 2 * rc_attr->outFrmRate.frmRateNum / rc_attr->outFrmRate.frmRateDen;
				if (S_RC_METHOD == ENC_RC_MODE_FIXQP) {
					rc_attr->attrRcMode.rcMode = ENC_RC_MODE_FIXQP;
					rc_attr->attrRcMode.attrH264FixQp.IQp = 35;
					rc_attr->attrRcMode.attrH264FixQp.PQp = 35;
					rc_attr->attrRcMode.attrH264FixQp.blkQpEn = 0;
				} else if (S_RC_METHOD == ENC_RC_MODE_CBR) {
					rc_attr->attrRcMode.rcMode = ENC_RC_MODE_CBR;
					rc_attr->attrRcMode.attrH264Cbr.maxQp = 45;
					rc_attr->attrRcMode.attrH264Cbr.minQp = 15;
					rc_attr->attrRcMode.attrH264Cbr.blkQpEn = 0;
					rc_attr->attrRcMode.attrH264Cbr.initialQp = 35;
					rc_attr->attrRcMode.attrH264Cbr.maxIQp = 45;
					rc_attr->attrRcMode.attrH264Cbr.minIQp = 15;
					rc_attr->attrRcMode.attrH264Cbr.outBitRate = BITRATE_720P_Kbs;
					rc_attr->attrRcMode.attrH264Cbr.iBiasLvl = 0;
					rc_attr->attrRcMode.attrH264Cbr.IPfrmQPDelta = 8;
					rc_attr->attrRcMode.attrH264Cbr.PPfrmQPDelta = 8;
					rc_attr->attrRcMode.attrH264Cbr.staticTime = 2;
					rc_attr->attrRcMode.attrH264Cbr.flucLvl = 2;
					rc_attr->attrRcMode.attrH264Cbr.qualityLvl = 3;
					rc_attr->attrRcMode.attrH264Cbr.maxIprop = 100;
					rc_attr->attrRcMode.attrH264Cbr.minIprop = 1;
					rc_attr->attrRcMode.attrH264Cbr.maxIPictureSize = rc_attr->attrRcMode.attrH264Cbr.outBitRate * 6 / 5;
					rc_attr->attrRcMode.attrH264Cbr.maxPPictureSize = rc_attr->attrRcMode.attrH264Cbr.maxIPictureSize * 3 / 5;
				} else if (S_RC_METHOD == ENC_RC_MODE_VBR) {
					rc_attr->attrRcMode.rcMode = ENC_RC_MODE_VBR;
					rc_attr->attrRcMode.attrH264Vbr.maxQp = 45;
					rc_attr->attrRcMode.attrH264Vbr.minQp = 15;
					rc_attr->attrRcMode.attrH264Vbr.blkQpEn = 0;
					rc_attr->attrRcMode.attrH264Vbr.initialQp = 35;
					rc_attr->attrRcMode.attrH264Vbr.maxIQp = 45;
					rc_attr->attrRcMode.attrH264Vbr.minIQp = 15;
					rc_attr->attrRcMode.attrH264Vbr.maxBitRate = BITRATE_720P_Kbs;
					rc_attr->attrRcMode.attrH264Vbr.iBiasLvl = 0;
					rc_attr->attrRcMode.attrH264Vbr.changePos = 80;
					rc_attr->attrRcMode.attrH264Vbr.staticTime = 2;
					rc_attr->attrRcMode.attrH264Vbr.qualityLvl = 6;
					rc_attr->attrRcMode.attrH264Vbr.maxIprop = 100;
					rc_attr->attrRcMode.attrH264Vbr.minIprop = 1;
					rc_attr->attrRcMode.attrH264Vbr.maxIPictureSize = rc_attr->attrRcMode.attrH264Vbr.maxBitRate * 6 / 5;
					rc_attr->attrRcMode.attrH264Vbr.maxPPictureSize = rc_attr->attrRcMode.attrH264Vbr.maxIPictureSize * 3 / 5;
				} else if (S_RC_METHOD == ENC_RC_MODE_SMART) {
					rc_attr->attrRcMode.rcMode = ENC_RC_MODE_SMART;
					rc_attr->attrRcMode.attrH264Smart.maxQp = 45;
					rc_attr->attrRcMode.attrH264Smart.minQp = 15;
					rc_attr->attrRcMode.attrH264Smart.blkQpEn = 0;
					rc_attr->attrRcMode.attrH264Smart.initialQp = 35;
					rc_attr->attrRcMode.attrH264Smart.maxIQp = 45;
					rc_attr->attrRcMode.attrH264Smart.minIQp = 15;
					rc_attr->attrRcMode.attrH264Smart.maxBitRate = BITRATE_720P_Kbs;
					rc_attr->attrRcMode.attrH264Smart.iBiasLvl = 0;
					rc_attr->attrRcMode.attrH264Smart.changePos = 80;
					rc_attr->attrRcMode.attrH264Smart.staticTime = 2;
					rc_attr->attrRcMode.attrH264Smart.qualityLvl = 6;
					rc_attr->attrRcMode.attrH264Smart.maxIprop = 100;
					rc_attr->attrRcMode.attrH264Smart.minIprop = 1;
					rc_attr->attrRcMode.attrH264Smart.minStillRate = 25;
					rc_attr->attrRcMode.attrH264Smart.maxStillQp = 35;
					rc_attr->attrRcMode.attrH264Smart.superSmartEn = 0;
					rc_attr->attrRcMode.attrH264Smart.supSmtStillLvl = 5;
					rc_attr->attrRcMode.attrH264Smart.supSmtStillRateLvl = 2;
					rc_attr->attrRcMode.attrH264Smart.maxSupSmtStillRate = 20;
					rc_attr->attrRcMode.attrH264Smart.maxIPictureSize = rc_attr->attrRcMode.attrH264Smart.maxBitRate * 6 / 5;
					rc_attr->attrRcMode.attrH264Smart.maxPPictureSize = rc_attr->attrRcMode.attrH264Smart.maxIPictureSize * 3 / 5;
				} else if (S_RC_METHOD == ENC_RC_MODE_CVBR) {
					rc_attr->attrRcMode.rcMode = ENC_RC_MODE_CVBR;
					rc_attr->attrRcMode.attrH264CVbr.maxQp = 45;
					rc_attr->attrRcMode.attrH264CVbr.minQp = 15;
					rc_attr->attrRcMode.attrH264CVbr.blkQpEn = 0;
					rc_attr->attrRcMode.attrH264CVbr.initialQp = 35;
					rc_attr->attrRcMode.attrH264CVbr.maxIQp = 45;
					rc_attr->attrRcMode.attrH264CVbr.minIQp = 15;
					rc_attr->attrRcMode.attrH264CVbr.maxBitRate = BITRATE_720P_Kbs;
					rc_attr->attrRcMode.attrH264CVbr.longMaxBitRate = rc_attr->attrRcMode.attrH264CVbr.maxBitRate * 4 / 5;
					rc_attr->attrRcMode.attrH264CVbr.longMinBitRate = rc_attr->attrRcMode.attrH264CVbr.maxBitRate * 3 / 5;
					rc_attr->attrRcMode.attrH264CVbr.iBiasLvl = 0;
					rc_attr->attrRcMode.attrH264CVbr.changePos = 80;
					rc_attr->attrRcMode.attrH264CVbr.shortStatTime = 2;
					rc_attr->attrRcMode.attrH264CVbr.longStatTime = 60;
					rc_attr->attrRcMode.attrH264CVbr.qualityLvl = 6;
					rc_attr->attrRcMode.attrH264CVbr.maxIprop = 100;
					rc_attr->attrRcMode.attrH264CVbr.minIprop = 1;
					rc_attr->attrRcMode.attrH264CVbr.extraBitRate = 5;
					rc_attr->attrRcMode.attrH264CVbr.maxIPictureSize = rc_attr->attrRcMode.attrH264CVbr.maxBitRate * 6 / 5;
					rc_attr->attrRcMode.attrH264CVbr.maxPPictureSize = rc_attr->attrRcMode.attrH264CVbr.maxIPictureSize * 3 / 5;
				} else if (S_RC_METHOD == ENC_RC_MODE_AVBR){
					rc_attr->attrRcMode.rcMode = ENC_RC_MODE_AVBR;
					rc_attr->attrRcMode.attrH264AVbr.maxQp = 45;
					rc_attr->attrRcMode.attrH264AVbr.minQp = 15;
					rc_attr->attrRcMode.attrH264AVbr.blkQpEn = 0;
					rc_attr->attrRcMode.attrH264AVbr.initialQp = 35;
					rc_attr->attrRcMode.attrH264AVbr.maxIQp = 45;
					rc_attr->attrRcMode.attrH264AVbr.minIQp = 15;
					rc_attr->attrRcMode.attrH264AVbr.maxBitRate = BITRATE_720P_Kbs;
					rc_attr->attrRcMode.attrH264AVbr.iBiasLvl = 0;
					rc_attr->attrRcMode.attrH264AVbr.changePos = 80;
					rc_attr->attrRcMode.attrH264AVbr.staticTime = 2;
					rc_attr->attrRcMode.attrH264AVbr.qualityLvl = 6;
					rc_attr->attrRcMode.attrH264AVbr.maxIprop = 100;
					rc_attr->attrRcMode.attrH264AVbr.minIprop = 1;
					rc_attr->attrRcMode.attrH264AVbr.minStillRate = 25;
					rc_attr->attrRcMode.attrH264AVbr.maxStillQp = 35;
					rc_attr->attrRcMode.attrH264AVbr.maxIPictureSize = rc_attr->attrRcMode.attrH264AVbr.maxBitRate * 6 / 5;
					rc_attr->attrRcMode.attrH264AVbr.maxPPictureSize = rc_attr->attrRcMode.attrH264AVbr.maxIPictureSize * 3 / 5;
				} else {
					IMP_LOG_ERR(TAG, "S_RC_METHOD error!\n");
					return -1;
				}
			} else if (chn[i].payloadType == PT_H265) { /* PT_H265 */
				chnNum = chn[i].index;
				rc_attr = &channel_attr.rcAttr;
				rc_attr->outFrmRate.frmRateNum = imp_chn_attr_tmp->outFrmRateNum;
				rc_attr->outFrmRate.frmRateDen = imp_chn_attr_tmp->outFrmRateDen;
				rc_attr->maxGop = 2 * rc_attr->outFrmRate.frmRateNum / rc_attr->outFrmRate.frmRateDen;
				if (S_RC_METHOD == ENC_RC_MODE_FIXQP) {
					rc_attr->attrRcMode.rcMode = ENC_RC_MODE_FIXQP;
					rc_attr->attrRcMode.attrH265FixQp.IQp = 35;
					rc_attr->attrRcMode.attrH265FixQp.PQp = 35;
					rc_attr->attrRcMode.attrH265FixQp.blkQpEn = 0;
				} else if (S_RC_METHOD == ENC_RC_MODE_CBR) {
					rc_attr->attrRcMode.rcMode = ENC_RC_MODE_CBR;
					rc_attr->attrRcMode.attrH265Cbr.maxQp = 45;
					rc_attr->attrRcMode.attrH265Cbr.minQp = 15;
					rc_attr->attrRcMode.attrH265Cbr.blkQpEn = 0;
					rc_attr->attrRcMode.attrH265Cbr.initialQp = 35;
					rc_attr->attrRcMode.attrH265Cbr.maxIQp = 45;
					rc_attr->attrRcMode.attrH265Cbr.minIQp = 15;
					rc_attr->attrRcMode.attrH265Cbr.outBitRate = BITRATE_720P_Kbs;
					rc_attr->attrRcMode.attrH265Cbr.iBiasLvl = 0;
					rc_attr->attrRcMode.attrH265Cbr.IPfrmQPDelta = 8;
					rc_attr->attrRcMode.attrH265Cbr.PPfrmQPDelta = 8;
					rc_attr->attrRcMode.attrH265Cbr.staticTime = 2;
					rc_attr->attrRcMode.attrH265Cbr.flucLvl = 2;
					rc_attr->attrRcMode.attrH265Cbr.qualityLvl = 3;
					rc_attr->attrRcMode.attrH265Cbr.maxIprop = 100;
					rc_attr->attrRcMode.attrH265Cbr.minIprop = 1;
					rc_attr->attrRcMode.attrH265Cbr.maxIPictureSize = rc_attr->attrRcMode.attrH265Cbr.outBitRate * 6 / 5;
					rc_attr->attrRcMode.attrH265Cbr.maxPPictureSize = rc_attr->attrRcMode.attrH265Cbr.maxIPictureSize * 3 / 5;
				} else if (S_RC_METHOD == ENC_RC_MODE_VBR) {
					rc_attr->attrRcMode.rcMode = ENC_RC_MODE_VBR;
					rc_attr->attrRcMode.attrH265Vbr.maxQp = 45;
					rc_attr->attrRcMode.attrH265Vbr.minQp = 15;
					rc_attr->attrRcMode.attrH265Vbr.blkQpEn = 0;
					rc_attr->attrRcMode.attrH265Vbr.initialQp = 35;
					rc_attr->attrRcMode.attrH265Vbr.maxIQp = 45;
					rc_attr->attrRcMode.attrH265Vbr.minIQp = 15;
					rc_attr->attrRcMode.attrH265Vbr.maxBitRate = BITRATE_720P_Kbs;
					rc_attr->attrRcMode.attrH265Vbr.iBiasLvl = 0;
					rc_attr->attrRcMode.attrH265Vbr.changePos = 80;
					rc_attr->attrRcMode.attrH265Vbr.staticTime = 2;
					rc_attr->attrRcMode.attrH265Vbr.qualityLvl = 6;
					rc_attr->attrRcMode.attrH265Vbr.maxIprop = 100;
					rc_attr->attrRcMode.attrH265Vbr.minIprop = 1;
					rc_attr->attrRcMode.attrH265Vbr.maxIPictureSize = rc_attr->attrRcMode.attrH265Vbr.maxBitRate * 6 / 5;
					rc_attr->attrRcMode.attrH265Vbr.maxPPictureSize = rc_attr->attrRcMode.attrH265Vbr.maxIPictureSize * 3 / 5;
				} else if (S_RC_METHOD == ENC_RC_MODE_SMART) {
					rc_attr->attrRcMode.rcMode = ENC_RC_MODE_SMART;
					rc_attr->attrRcMode.attrH265Smart.maxQp = 45;
					rc_attr->attrRcMode.attrH265Smart.minQp = 15;
					rc_attr->attrRcMode.attrH265Smart.blkQpEn = 0;
					rc_attr->attrRcMode.attrH265Smart.initialQp = 35;
					rc_attr->attrRcMode.attrH265Smart.maxIQp = 45;
					rc_attr->attrRcMode.attrH265Smart.minIQp = 15;
					rc_attr->attrRcMode.attrH265Smart.maxBitRate = BITRATE_720P_Kbs;
					rc_attr->attrRcMode.attrH265Smart.iBiasLvl = 0;
					rc_attr->attrRcMode.attrH265Smart.changePos = 80;
					rc_attr->attrRcMode.attrH265Smart.staticTime = 2;
					rc_attr->attrRcMode.attrH265Smart.qualityLvl = 6;
					rc_attr->attrRcMode.attrH265Smart.maxIprop = 100;
					rc_attr->attrRcMode.attrH265Smart.minIprop = 1;
					rc_attr->attrRcMode.attrH265Smart.minStillRate = 25;
					rc_attr->attrRcMode.attrH265Smart.maxStillQp = 35;
					rc_attr->attrRcMode.attrH265Smart.superSmartEn = 0;
					rc_attr->attrRcMode.attrH265Smart.supSmtStillLvl = 5;
					rc_attr->attrRcMode.attrH265Smart.supSmtStillRateLvl = 2;
					rc_attr->attrRcMode.attrH265Smart.maxSupSmtStillRate = 20;
					rc_attr->attrRcMode.attrH265Smart.maxIPictureSize = rc_attr->attrRcMode.attrH265Smart.maxBitRate * 6 / 5;
					rc_attr->attrRcMode.attrH265Smart.maxPPictureSize = rc_attr->attrRcMode.attrH265Smart.maxIPictureSize * 3 / 5;
				} else if (S_RC_METHOD == ENC_RC_MODE_CVBR) {
					rc_attr->attrRcMode.rcMode = ENC_RC_MODE_CVBR;
					rc_attr->attrRcMode.attrH265CVbr.maxQp = 45;
					rc_attr->attrRcMode.attrH265CVbr.minQp = 15;
					rc_attr->attrRcMode.attrH265CVbr.blkQpEn = 0;
					rc_attr->attrRcMode.attrH265CVbr.initialQp = 35;
					rc_attr->attrRcMode.attrH265CVbr.maxIQp = 45;
					rc_attr->attrRcMode.attrH265CVbr.minIQp = 15;
					rc_attr->attrRcMode.attrH265CVbr.maxBitRate = BITRATE_720P_Kbs;
					rc_attr->attrRcMode.attrH265CVbr.longMaxBitRate = rc_attr->attrRcMode.attrH265CVbr.maxBitRate * 4 / 5;
					rc_attr->attrRcMode.attrH265CVbr.longMinBitRate = rc_attr->attrRcMode.attrH265CVbr.maxBitRate * 3 / 5;
					rc_attr->attrRcMode.attrH265CVbr.iBiasLvl = 0;
					rc_attr->attrRcMode.attrH265CVbr.changePos = 80;
					rc_attr->attrRcMode.attrH265CVbr.shortStatTime = 2;
					rc_attr->attrRcMode.attrH265CVbr.longStatTime = 60;
					rc_attr->attrRcMode.attrH265CVbr.qualityLvl = 6;
					rc_attr->attrRcMode.attrH265CVbr.maxIprop = 100;
					rc_attr->attrRcMode.attrH265CVbr.minIprop = 1;
					rc_attr->attrRcMode.attrH265CVbr.extraBitRate = 5;
					rc_attr->attrRcMode.attrH265CVbr.maxIPictureSize = rc_attr->attrRcMode.attrH265CVbr.maxBitRate * 6 / 5;
					rc_attr->attrRcMode.attrH265CVbr.maxPPictureSize = rc_attr->attrRcMode.attrH265CVbr.maxIPictureSize * 3 / 5;
				} else if (S_RC_METHOD == ENC_RC_MODE_AVBR) {
					rc_attr->attrRcMode.rcMode = ENC_RC_MODE_AVBR;
					rc_attr->attrRcMode.attrH265AVbr.maxQp = 45;
					rc_attr->attrRcMode.attrH265AVbr.minQp = 15;
					rc_attr->attrRcMode.attrH265AVbr.blkQpEn = 0;
					rc_attr->attrRcMode.attrH265AVbr.initialQp = 35;
					rc_attr->attrRcMode.attrH265AVbr.maxIQp = 45;
					rc_attr->attrRcMode.attrH265AVbr.minIQp = 15;
					rc_attr->attrRcMode.attrH265AVbr.maxBitRate = BITRATE_720P_Kbs;
					rc_attr->attrRcMode.attrH265AVbr.iBiasLvl = 0;
					rc_attr->attrRcMode.attrH265AVbr.changePos = 80;
					rc_attr->attrRcMode.attrH265AVbr.staticTime = 2;
					rc_attr->attrRcMode.attrH265AVbr.qualityLvl = 6;
					rc_attr->attrRcMode.attrH265AVbr.maxIprop = 100;
					rc_attr->attrRcMode.attrH265AVbr.minIprop = 1;
					rc_attr->attrRcMode.attrH265AVbr.minStillRate = 25;
					rc_attr->attrRcMode.attrH265AVbr.maxStillQp = 35;
					rc_attr->attrRcMode.attrH265AVbr.maxIPictureSize = rc_attr->attrRcMode.attrH265AVbr.maxBitRate * 6 / 5;
					rc_attr->attrRcMode.attrH265AVbr.maxPPictureSize = rc_attr->attrRcMode.attrH265AVbr.maxIPictureSize * 3 / 5;
				} else {
					IMP_LOG_ERR(TAG, "S_RC_METHOD error!\n");
					return -1;
				}
			}

			if(direct_switch == 1) {
				if (0 == chn[i].index)
					channel_attr.bEnableIvdc = true;
			} else if (direct_switch == 2) {
				if (0 == chn[i].index || 3 == chn[i].index)
					channel_attr.bEnableIvdc = true;
			} else if (direct_switch == 3) {
				if (0 == chn[i].index || 3 == chn[i].index || 6 == chn[i].index)
					channel_attr.bEnableIvdc = true;
			} else if (direct_switch == 4) {
				if (0 == chn[i].index || 3 == chn[i].index || 6 == chn[i].index || 9 == chn[i].index)
					channel_attr.bEnableIvdc = true;
			}

			ret = IMP_Encoder_CreateChn(chnNum, &channel_attr);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "IMP_Encoder_CreateChn(%d) failed\n", chnNum);
				return -1;
			}

			ret = IMP_Encoder_RegisterChn(chn[i].index, chnNum);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "IMP_Encoder_RegisterChn(group%d, chn%d) failed\n", chn[i].index, chnNum);
				return -1;
			}
		}
	}

	return 0;
}

int sample_video_exit(void)
{
	int i = 0;
	int ret = 0;
	int chnNum = 0;
	IMPEncoderCHNStat chn_stat;

	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			/* CH2 is dedicated to JPEG capture, skip video encoder */
			if (chn[i].index == 2)
				continue;
			chnNum = chn[i].index;
			memset(&chn_stat, 0, sizeof(IMPEncoderCHNStat));
			ret = IMP_Encoder_Query(chnNum, &chn_stat);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "IMP_Encoder_Query(%d) failed\n", chnNum);
				return -1;
			}

			if (chn_stat.registered) {
				ret = IMP_Encoder_UnRegisterChn(chnNum);
				if (ret < 0) {
					IMP_LOG_ERR(TAG, "IMP_Encoder_UnRegisterChn(%d) failed\n", chnNum);
					return -1;
				}

				ret = IMP_Encoder_DestroyChn(chnNum);
				if (ret < 0) {
					IMP_LOG_ERR(TAG, "IMP_Encoder_DestroyChn(%d) failed\n", chnNum);
					return -1;
				}
			}
		}
	}

	return 0;
}

int sample_framesource_streamon()
{
	int i = 0;
	int ret = 0;
	/* Enable channels */
	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			ret = IMP_FrameSource_EnableChn(chn[i].index);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "IMP_FrameSource_EnableChn(%d) failed\n", chn[i].index);
				return -1;
			}
		}
	}
	return 0;
}

int sample_framesource_streamoff()
{
	int i = 0;
	int ret = 0;
	/* Disble channels */
	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable){
			ret = IMP_FrameSource_DisableChn(chn[i].index);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "IMP_FrameSource_DisableChn(%d) failed\n", chn[i].index);
				return -1;
			}
		}
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

#if 0
static int save_stream_by_name(char *stream_prefix, int idx, IMPEncoderStream *stream)
{
	int i = 0;
	int ret = 0;
	int stream_fd = -1;
	char stream_path[128];
	int nr_pack = stream->packCount;

	sprintf(stream_path, "%s_%d", stream_prefix, idx);

	IMP_LOG_DBG(TAG, "Open Stream file %s ", stream_path);
	stream_fd = open(stream_path, O_RDWR | O_CREAT | O_TRUNC, 0777);
	if (stream_fd < 0) {
		IMP_LOG_ERR(TAG, "open %s failed\n", stream_path);
		return -1;
	}
	IMP_LOG_DBG(TAG, "OK\n");

	for (i = 0; i < nr_pack; i++) {
		ret = write(stream_fd, (void *)stream->pack[i].virAddr, stream->pack[i].length);
		if (ret != stream->pack[i].length) {
			close(stream_fd);
			IMP_LOG_ERR(TAG, "stream write failed\n");
			return -1;
		}
	}

	close(stream_fd);

	return 0;
}
#endif

pthread_t tid[FS_CHN_NUM*2];
pthread_t tid_kernenc[FS_CHN_NUM*2];
static void *get_kernvideo_stream(void *args)
{
	int ret = 0;
	int val = 0, chnNum = 0;
	FILE  *fd = NULL;
	char ker_stream_path[64];
	IMPEncoderKernEncOut encOut;

	val = (int)args;
	chnNum = val & 0xffff;
	IMPPayloadType payloadType;
	payloadType = (val >> 16) & 0xffff;

	sprintf(ker_stream_path, "%s/kern_enc-%d.%s", STREAM_FILE_PATH_PREFIX, chnNum,
			(payloadType == PT_H264) ? "h264" : "h265");
	fd = fopen(ker_stream_path, "a+");
	if (fd < 0) {
		IMP_LOG_ERR(TAG, "KernEnc open file failed\n");
		return NULL;
	}
	while (ret == 0) {
		ret = IMP_Encoder_KernEnc_GetStream(chnNum, &encOut);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "KernEnc get stream failed\n");
		} else {
#if 0
			printf("index=%d\n", encOut.index);
			printf("type=%d\n", encOut.type);
			printf("bufAddr=0x%08x\n", encOut.bufAddr);
			printf("bufLen=%d\n", encOut.bufLen);
			printf("strmCnt=%d\n", encOut.strmCnt);
			printf("strmAddr=0x%08x\n", encOut.strmAddr);
			printf("strmLen=%d\n", encOut.strmLen);
			printf("timestamp=%d\n", encOut.timestamp);
			printf("\n");
#endif
			fwrite((void *)encOut.strmAddr, 1, encOut.strmLen, fd);
		}
	}
    ret = IMP_Encoder_KernEnc_Release(chnNum);
    if (ret < 0) {
        IMP_LOG_ERR(TAG, "KernEnc release failed\n");
        return NULL;
    }
	fclose(fd);
    return NULL;

}
static void *get_video_stream(void *args)
{
	int i = 0;
	int ret = 0;
	int val = 0, chnNum = 0;

	val = (int)args;
	chnNum = val & 0xffff;
#ifdef SAVE_STREAM
	char stream_path[64];
	IMPPayloadType payloadType;
	int stream_fd = -1;
	IMPFSI2DAttr i2d_attr;
	int s32picWidth = 0, s32picHeight = 0;
	payloadType = (val >> 16) & 0xffff;
#endif

	ret = IMP_Encoder_StartRecvPic(chnNum);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_Encoder_StartRecvPic(%d) failed\n", chnNum);
		return NULL;
	}

#ifdef SAVE_STREAM
	memset(&i2d_attr, 0, sizeof(IMPFSI2DAttr));

	ret = IMP_FrameSource_GetI2dAttr(chnNum, &i2d_attr);
	if(ret < 0){
		IMP_LOG_ERR(TAG, "IMP_FrameSource_GetI2dAttr(%d) failed\n", chnNum);
		return NULL;
	}

	if((1 == i2d_attr.i2d_enable) &&
			((i2d_attr.rotate_enable) && (i2d_attr.rotate_angle == 90 || i2d_attr.rotate_angle == 270))){
		/* this depend on your sensor or channels */
		s32picWidth  = chn[chnNum].fs_chn_attr.picHeight;
		s32picHeight = chn[chnNum].fs_chn_attr.picWidth;
	} else {
		s32picWidth  = chn[chnNum].fs_chn_attr.picWidth;
		s32picHeight = chn[chnNum].fs_chn_attr.picHeight;
	}
	sprintf(stream_path, "%s/stream-%d-%dx%d.%s", STREAM_FILE_PATH_PREFIX, chnNum,
			s32picWidth, s32picHeight, (payloadType == PT_H264) ? "h264" : "h265");

	IMP_LOG_INFO(TAG, "Video ChnNum=%d Open Stream file %s\n", chnNum, stream_path);
	stream_fd = open(stream_path, O_RDWR | O_CREAT | O_TRUNC, 0777);
	if (stream_fd < 0) {
		IMP_LOG_ERR(TAG, "open %s failed\n", stream_path);
		return NULL;
	}
	IMP_LOG_DBG(TAG, "OK\n");
#endif

	int totalSaveStreamCnt = NR_FRAMES_TO_SAVE;
	int64_t enc_last_ts = 0;
	for (i = 0; i < totalSaveStreamCnt; i++) {
		ret = IMP_Encoder_PollingStream(chnNum, 1000);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_Encoder_PollingStream(%d) timeout\n", chnNum);
			continue;
		}

		IMPEncoderStream stream;
		/* Get H264 or H265 Stream */
		ret = IMP_Encoder_GetStream(chnNum, &stream, 1);
#ifdef SHOW_FRM_BITRATE
		int j, len = 0;
		for (j = 0; j < stream.packCount; j++) {
			len += stream.pack[j].length;
		}
		bitrate_sp[chnNum] += len;
		frmrate_sp[chnNum]++;

		int64_t now = IMP_System_GetTimeStamp() / 1000;
		if(((int)(now - statime_sp[chnNum]) / 1000) >= FRM_BIT_RATE_TIME){
			double fps = (double)frmrate_sp[chnNum] / ((double)(now - statime_sp[chnNum]) / 1000);
			double kbr = (double)bitrate_sp[chnNum] * 8 / (double)(now - statime_sp[chnNum]);

			printf("streamNum[%d]:FPS: %0.2f,Bitrate: %0.2f(kbps)\n", chnNum, fps, kbr);
			//fflush(stdout);

			frmrate_sp[chnNum] = 0;
			bitrate_sp[chnNum] = 0;
			statime_sp[chnNum] = now;
		}
#endif
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_Encoder_GetStream(%d) failed\n", chnNum);
			return NULL;
		}

		/* Log encoder timestamp and interval only for anomalous frames */
		{
			int64_t enc_now = IMP_System_GetTimeStamp();
			int64_t enc_interval_ms = (enc_last_ts > 0) ? (enc_now - enc_last_ts) / 1000 : 0;
			if (enc_interval_ms > 43 || enc_interval_ms < 23) {
				IMP_LOG_INFO(TAG, "[ENC_CH%d] frame=%d packCount=%d interval=%lldms\n",
					chnNum, i, stream.packCount, (long long)enc_interval_ms);
			}
			enc_last_ts = enc_now;
		}

#ifdef SAVE_STREAM
		ret = save_stream(stream_fd, &stream);
		if (ret < 0) {
			close(stream_fd);
			return NULL;
		}
#endif

		IMP_Encoder_ReleaseStream(chnNum, &stream);
	}

#ifdef SAVE_STREAM
	close(stream_fd);
#endif

	ret = IMP_Encoder_StopRecvPic(chnNum);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_Encoder_StopRecvPic(%d) failed\n", chnNum);
		return NULL;
	}

	return NULL;
}

int sample_start_get_video_stream()
{
	int i = 0;
	int ret = 0;

	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			/* CH2 is dedicated to JPEG capture, skip video stream */
			if (chn[i].index == 2)
				continue;
			int arg = ((chn[i].payloadType << 16) | chn[i].index);
			ret = pthread_create(&tid[chn[i].index], NULL, get_video_stream, (void *)arg);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "Create ChnNum%d get_video_stream failed\n", chn[i].index);
			}
		}
	}

	return 0;
}

void sample_stop_get_video_stream()
{
	int i = 0;

	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			/* CH2 is dedicated to JPEG capture, skip video stream */
			if (chn[i].index == 2)
				continue;
			pthread_join(tid[chn[i].index], NULL);
		}
	}

	return;
}
void sample_stop_kernvideo_stream()
{
	int i = 0;

	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			pthread_join(tid[chn[i].index], NULL);
            if(kerenc_enable[i] == 1)
                pthread_join(tid_kernenc[chn[i].index], NULL);
		}
	}

	return;
}

int sample_get_kernvideo_stream()
{
	int i = 0;
	int ret = 0;
	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
            ret = IMP_Encoder_KernEnc_GetStatus(chn[i].index, &kerenc_enable[i]);
            if (ret < 0) {
                IMP_LOG_ERR(TAG, "IMP_Encoder_KernEnc_GetStatus failed\n");
                return -1;
            }
            if(kerenc_enable[i] == 1){
                int arg = ((chn[i].payloadType << 16) | chn[i].index);
                ret = pthread_create(&tid_kernenc[chn[i].index], NULL,get_kernvideo_stream, (void *)arg);
                if (ret < 0) {
                    IMP_LOG_ERR(TAG, "ChnNum%d get_video_stream failed\n", chn[i].index);
                }
            }
		}
	}

	return 0;
}

void *get_jpeg_stream(void *args)
{
	int i = 0;
	int ret = 0;
	int val = 0, chnNum = 0;

	val = (int)args;
	chnNum = val & 0xffff;
#ifdef SAVE_STREAM
	char snap_path[64];
	IMPFSI2DAttr i2d_attr;
	int s32picWidth = 0, s32picHeight = 0;
#endif

	srand((unsigned int)time(NULL));
	int totalSaveStreamCnt = NR_JPEG_TO_SAVE;
	for (i = 0; i < totalSaveStreamCnt; i++) {
		ret = IMP_Encoder_StartRecvPic(chnNum);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_Encoder_StartRecvPic(%d) failed\n", chnNum);
			return NULL;
		}
#ifdef SAVE_STREAM
		int snap_fd;

		memset(&i2d_attr, 0, sizeof(IMPFSI2DAttr));

		ret = IMP_FrameSource_GetI2dAttr((chnNum-12)*3, &i2d_attr);
		if(ret < 0){
			IMP_LOG_ERR(TAG, "IMP_FrameSource_GetI2dAttr(%d) failed\n", (chnNum-12)*3);
			return NULL;
		}

		if((1 == i2d_attr.i2d_enable) &&
				((i2d_attr.rotate_enable) && (i2d_attr.rotate_angle == 90 || i2d_attr.rotate_angle == 270))){
			/* this depend on your sensor or channels */
			s32picWidth  = chn[(chnNum-12)*3].fs_chn_attr.picHeight;
			s32picHeight = chn[(chnNum-12)*3].fs_chn_attr.picWidth;
		} else {
			s32picWidth  = chn[(chnNum-12)*3].fs_chn_attr.picWidth;
			s32picHeight = chn[(chnNum-12)*3].fs_chn_attr.picHeight;
		}
		sprintf(snap_path, "%s/snap-%d-%dx%d-%d.jpg", SNAP_FILE_PATH_PREFIX,
				chnNum, s32picWidth, s32picHeight, i);

		IMP_LOG_INFO(TAG, "Open Snap file %s\n", snap_path);
		snap_fd = open(snap_path, O_RDWR | O_CREAT | O_TRUNC, 0777);
		if (snap_fd < 0) {
			IMP_LOG_ERR(TAG, "open %s failed\n", snap_path);
			return NULL;
		}
		IMP_LOG_DBG(TAG, "OK\n");
#endif

		/* Polling JPEG Snap, set timeout as 10000msec */
		ret = IMP_Encoder_PollingStream(chnNum, 10000);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_Encoder_PollingStream(%d) timeout\n", chnNum);
			continue;
		}

		IMPEncoderStream stream;
		/* Get JPEG Snap */
		ret = IMP_Encoder_GetStream(chnNum, &stream, 1);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_Encoder_GetStream(%d) failed\n", chnNum);
			return NULL;
		}

#ifdef SAVE_STREAM
		ret = save_stream(snap_fd, &stream);
		if (ret < 0) {
			close(snap_fd);
			return NULL;
		}
#endif
		IMP_Encoder_ReleaseStream(chnNum, &stream);

#ifdef SAVE_STREAM
		close(snap_fd);
#endif

		ret = IMP_Encoder_StopRecvPic(chnNum);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_Encoder_StopRecvPic(%d) failed\n", chnNum);
			return NULL;
		}
	}

	return NULL;
}

int sample_start_get_jpeg_stream()
{
	int i = 0;
	int ret = 0;

	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			if (SENSOR_NUM == IMPISP_TOTAL_ONE) {
				if (chn[i].index != 2)
					continue;
			} else if (SENSOR_NUM == IMPISP_TOTAL_TWO) {
				if (chn[i].index != 0 && chn[i].index != 3)
					continue;
			} else if (SENSOR_NUM == IMPISP_TOTAL_THR) {
				if (chn[i].index != 0 && chn[i].index != 3 && chn[i].index != 6)
					continue;
			} else if (SENSOR_NUM == IMPISP_TOTAL_FOU) {
				if (chn[i].index != 0 && chn[i].index != 3 && chn[i].index != 6 && chn[i].index != 9)
					continue;
			}

			int arg = ((PT_JPEG << 16) | (12+chn[i].index/3));
			ret = pthread_create(&tid[12+chn[i].index/3], NULL, get_jpeg_stream, (void *)arg);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "Create ChnNum%d get_jpeg_stream failed\n", 12+chn[i].index/3);
			}
		}
	}

	return 0;
}

void sample_stop_get_jpeg_stream()
{
	int i = 0;

	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			if (SENSOR_NUM == IMPISP_TOTAL_ONE) {
				if (chn[i].index != 2)
					continue;
			} else if (SENSOR_NUM == IMPISP_TOTAL_TWO) {
				if (chn[i].index != 0 && chn[i].index != 3)
					continue;
			} else if (SENSOR_NUM == IMPISP_TOTAL_THR) {
				if (chn[i].index != 0 && chn[i].index != 3 && chn[i].index != 6)
					continue;
			} else if (SENSOR_NUM == IMPISP_TOTAL_FOU) {
				if (chn[i].index != 0 && chn[i].index != 3 && chn[i].index != 6 && chn[i].index != 9)
					continue;
			}

			pthread_join(tid[12+chn[i].index/3], NULL);
		}
	}
	return;
}

int sample_get_video_stream_byfd()
{
	int i = 0;
	int ret = 0;
	int chnNum = 0;
	int streamFd[FS_CHN_NUM], vencFd[FS_CHN_NUM], maxVencFd = 0;
	char stream_path[FS_CHN_NUM][128];
	fd_set readfds;
	struct timeval selectTimeout;
	int saveStreamCnt[FS_CHN_NUM], totalSaveStreamCnt[FS_CHN_NUM];
	memset(streamFd, 0, sizeof(streamFd));
	memset(vencFd, 0, sizeof(vencFd));
	memset(stream_path, 0, sizeof(stream_path));
	memset(saveStreamCnt, 0, sizeof(saveStreamCnt));
	memset(totalSaveStreamCnt, 0, sizeof(totalSaveStreamCnt));

	for (i = 0; i < FS_CHN_NUM; i++) {
		streamFd[i] = -1;
		vencFd[i] = -1;
		saveStreamCnt[i] = 0;
		if (chn[i].enable) {
			chnNum = chn[i].index;
			totalSaveStreamCnt[i] = NR_FRAMES_TO_SAVE;
			sprintf(stream_path[i], "%s/stream-%d.%s", STREAM_FILE_PATH_PREFIX, chnNum,
					(chn[i].payloadType == PT_H264) ? "h264" : "h265");

			streamFd[i] = open(stream_path[i], O_RDWR | O_CREAT | O_TRUNC, 0777);
			if (streamFd[i] < 0) {
				IMP_LOG_ERR(TAG, "open %s failed\n", stream_path[i]);
				return -1;
			}

			vencFd[i] = IMP_Encoder_GetFd(chnNum);
			if (vencFd[i] < 0) {
				IMP_LOG_ERR(TAG, "IMP_Encoder_GetFd(%d) failed\n", chnNum);
				return -1;
			}

			if (maxVencFd < vencFd[i]) {
				maxVencFd = vencFd[i];
			}

			ret = IMP_Encoder_StartRecvPic(chnNum);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "IMP_Encoder_StartRecvPic(%d) failed\n", chnNum);
				return -1;
			}
		}
	}

	while(1) {
		int breakFlag = 1;
		for (i = 0; i < FS_CHN_NUM; i++) {
			breakFlag &= (saveStreamCnt[i] >= totalSaveStreamCnt[i]);
		}
		if (breakFlag) {
			break;  /* save frame enough */
		}

		FD_ZERO(&readfds);
		for (i = 0; i < FS_CHN_NUM; i++) {
			if (chn[i].enable && saveStreamCnt[i] < totalSaveStreamCnt[i]) {
				FD_SET(vencFd[i], &readfds);
			}
		}
		selectTimeout.tv_sec = 2;
		selectTimeout.tv_usec = 0;

		ret = select(maxVencFd + 1, &readfds, NULL, NULL, &selectTimeout);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "select failed\n");
			return -1;
		} else if (ret == 0) {
			continue;
		} else {
			for (i = 0; i < FS_CHN_NUM; i++) {
				if (chn[i].enable && FD_ISSET(vencFd[i], &readfds)) {
					IMPEncoderStream stream;

					chnNum = chn[i].index;
					/* Get H264 or H265 Stream */
					ret = IMP_Encoder_GetStream(chnNum, &stream, 1);
					if (ret < 0) {
						IMP_LOG_ERR(TAG, "IMP_Encoder_GetStream(%d) failed\n", chnNum);
						return -1;
					}

					ret = save_stream(streamFd[i], &stream);
					if (ret < 0) {
						close(streamFd[i]);
						return -1;
					}

					IMP_Encoder_ReleaseStream(chnNum, &stream);
					saveStreamCnt[i]++;
				}
			}
		}
	}

	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			chnNum = chn[i].index;
			IMP_Encoder_StopRecvPic(chnNum);
			close(streamFd[i]);
		}
	}

	return 0;
}
