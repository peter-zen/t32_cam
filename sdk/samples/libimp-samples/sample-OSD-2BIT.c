/*
 * sample-IspOsd.c
 *
 * Copyright (C) 2014 Ingenic Semiconductor Co.,Ltd
 *
 * */
#include <stdio.h>
#include <stdlib.h>
#include <imp/imp_log.h>
#include <imp/isp_osd.h>
#include <imp/imp_common.h>
#include <imp/imp_system.h>
#include <imp/imp_framesource.h>
#include <imp/imp_encoder.h>
#include "sample-common.h"
#include <string.h>

#ifdef SUPPORT_RGB555LE
#include "bgramapinfo_rgb555le.h"
#else
#include "bgramapinfo.h"
#endif

#include "logodata_100x100_bgra.h"
#include "bit2data.h"
#define TAG "Sample-IspOsd"

extern struct chn_conf chn[];
extern int direct_switch;
extern int gosd_enable;

#define BOSDMODULEEABLE 1
#define OSD_LETTER_NUM 20

uint32_t *timeStampData;
IMPOSDRgnAttr rIspOsdAttr;
char path[128] = "/mnt/res/64x64_2.rgba";
char *g_pdata = NULL;
FILE *g_fp = NULL;
static int g_pichandle[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
IMPRgnHandle *pIpuHander;
int pipuhandles[FS_CHN_NUM][26];

pthread_mutex_t mutex;
static IMP_Sample_OsdParam ipuosdparam[2];
static uint32_t *timeStampData_ipu;
pthread_mutex_t mutex;
sem_t semaphore;

static void update_time(void *p)
{
	int ret;

	/*generate time*/
	char DateStr[40];
	time_t currTime;
	struct tm *currDate;
	unsigned i = 0, j = 0;
	void *dateData = NULL;
	uint32_t *data = p;

	while(1) {
		int penpos_t = 0;
		int fontadv = 0;

		time(&currTime);
		currDate = localtime(&currTime);
		memset(DateStr, 0, 40);
		strftime(DateStr, 40, "%Y-%m-%d %I:%M:%S", currDate);
		for (i = 0; i < OSD_LETTER_NUM; i++) {
			switch(DateStr[i]) {
				case '0' ... '9':
					dateData = (void *)g_2bit[DateStr[i] - '0'].pdata;
					fontadv = g_2bit[DateStr[i] - '0'].width;
					penpos_t += g_2bit[DateStr[i] - '0'].width;
					break;
				case '-':
					dateData = (void *)g_2bit[10].pdata;
					fontadv = g_2bit[10].width;
					penpos_t += g_2bit[10].width;
					break;
				case ' ':
					dateData = (void *)g_2bit[11].pdata;
					fontadv = g_2bit[11].width;
					penpos_t += g_2bit[11].width;
					break;
				case ':':
					dateData = (void *)g_2bit[12].pdata;
					fontadv = g_2bit[12].width;
					penpos_t += g_2bit[12].width;
					break;
				default:
					break;
			}
#ifdef SUPPORT_RGB555LE
			for (j = 0; j < OSD_REGION_HEIGHT; j++) {
				memcpy((void *)((uint16_t *)data + j * OSD_LETTER_NUM * 32 + penpos_t),
						(void *)((uint16_t *)dateData + j*fontadv), fontadv*sizeof(uint16_t));
			}
#else
			for (j = 0; j < 24; j++) {
				memcpy((void *)((uint8_t *)data + j * OSD_LETTER_NUM * fontadv + penpos_t - 8),
						(void *)((uint8_t *)dateData + j * fontadv), fontadv * sizeof(uint8_t));
			}
#endif
		}

		pthread_mutex_lock(&mutex);

		IMPIspOsdAttrAsm stISPOSDAsm;
		stISPOSDAsm.type = ISP_OSD_REG_PIC;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_type = IMP_ISP_PIC_ARGB_1100;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_argb_type = IMP_ISP_ARGB_TYPE_ARGB;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_pixel_alpha_disable = IMPISP_TUNING_OPS_MODE_DISABLE;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_overlap = 0;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_group = 0;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_rgb_value.osd_value_r[0] = 0;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_rgb_value.osd_value_g[0] = 0;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_rgb_value.osd_value_b[0] = 0;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_rgb_value.osd_value_r[1] = 255;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_rgb_value.osd_value_g[1] = 255;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_rgb_value.osd_value_b[1] = 255;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_rgb_value.osd_value_r[2] = 255;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_rgb_value.osd_value_g[2] = 255;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_rgb_value.osd_value_b[2] = 255;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_rgb_value.osd_value_r[3] = 255;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_rgb_value.osd_value_g[3] = 255;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_rgb_value.osd_value_b[3] = 255;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_rgb_value.osd_value_alpha[0] = 0;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_rgb_value.osd_value_alpha[1] = 1;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_rgb_value.osd_value_alpha[2] = 1;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_rgb_value.osd_value_alpha[3] = 1;
		stISPOSDAsm.stsinglepicAttr.pic.pinum =  g_pichandle[0];
		stISPOSDAsm.stsinglepicAttr.pic.osd_enable = 1;
		stISPOSDAsm.stsinglepicAttr.pic.osd_left = 10;
		stISPOSDAsm.stsinglepicAttr.pic.osd_top = 10;
		stISPOSDAsm.stsinglepicAttr.pic.osd_width = 32 * OSD_LETTER_NUM;
		stISPOSDAsm.stsinglepicAttr.pic.osd_height = 24;
		stISPOSDAsm.stsinglepicAttr.pic.osd_image = (void *)data;
		stISPOSDAsm.stsinglepicAttr.pic.osd_stride = 8 * OSD_LETTER_NUM; // 8对齐

		ret = IMP_ISP_Tuning_SetOsdRgnAttr(0, g_pichandle[0], &stISPOSDAsm);
		if(ret < 0) {
			IMP_LOG_ERR(TAG,"IMP_ISP_SetOSDAttr error\n");
			return ;
		}
		ret = IMP_ISP_Tuning_ShowOsdRgn(0, g_pichandle[0], 1);
		if(ret < 0) {
			IMP_LOG_ERR(TAG,"IMP_OSD_ShowRgn_ISP error\n");
			return ;
		}

		pthread_mutex_unlock(&mutex);

		/*Update the timestamp*/
		sleep(1);
	}

	return ;
}

void draw_pic(void)
{
	int ret = 0, i = 0;

	IMPIspOsdAttrAsm stISPOSDAsm;
	memset(&stISPOSDAsm, 0, sizeof(IMPIspOsdAttrAsm));
	for(i = 1; i < 2; i++) {
		stISPOSDAsm.type = ISP_OSD_REG_PIC;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_type = IMP_ISP_PIC_ARGB_1100;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_argb_type = IMP_ISP_ARGB_TYPE_ARGB;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_pixel_alpha_disable = IMPISP_TUNING_OPS_MODE_DISABLE;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_overlap = 0;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_group = 0;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_rgb_value.osd_value_r[0] = 0;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_rgb_value.osd_value_g[0] = 0;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_rgb_value.osd_value_b[0] = 0;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_rgb_value.osd_value_r[1] = 255;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_rgb_value.osd_value_g[1] = 255;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_rgb_value.osd_value_b[1] = 255;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_rgb_value.osd_value_r[2] = 255;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_rgb_value.osd_value_g[2] = 255;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_rgb_value.osd_value_b[2] = 255;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_rgb_value.osd_value_r[3] = 255;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_rgb_value.osd_value_g[3] = 255;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_rgb_value.osd_value_b[3] = 255;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_rgb_value.osd_value_alpha[0] = 0;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_rgb_value.osd_value_alpha[1] = 1;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_rgb_value.osd_value_alpha[2] = 1;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_rgb_value.osd_value_alpha[3] = 1;
		stISPOSDAsm.stsinglepicAttr.pic.pinum =  g_pichandle[i];
		stISPOSDAsm.stsinglepicAttr.pic.osd_enable = 1;
		stISPOSDAsm.stsinglepicAttr.pic.osd_left = 50;
		stISPOSDAsm.stsinglepicAttr.pic.osd_top = 100 + 120 * i;
		stISPOSDAsm.stsinglepicAttr.pic.osd_width = 32;
		stISPOSDAsm.stsinglepicAttr.pic.osd_height = 16;
		stISPOSDAsm.stsinglepicAttr.pic.osd_image = (void *)num0_2bit;
		stISPOSDAsm.stsinglepicAttr.pic.osd_stride = 8; // 8对齐

		// main video
		ret = IMP_ISP_Tuning_SetOsdRgnAttr(0, g_pichandle[i], &stISPOSDAsm);
		if(ret < 0) {
			IMP_LOG_ERR(TAG,"IMP_ISP_Tuning_SetOsdRgnAttr error\n");
			return ;
		}

		ret = IMP_ISP_Tuning_ShowOsdRgn(0, g_pichandle[i], 1);
		if(ret < 0) {
			IMP_LOG_ERR(TAG,"IMP_ISP_Tuning_ShowOsdRgn error\n");
			return ;
		}
	}

	for(i = 2; i < 4; i++) {
		stISPOSDAsm.type = ISP_OSD_REG_PIC;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_type = IMP_ISP_PIC_ARGB_1100;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_argb_type = IMP_ISP_ARGB_TYPE_ARGB;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_pixel_alpha_disable = IMPISP_TUNING_OPS_MODE_DISABLE;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_overlap = 0;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_group = 0;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_rgb_value.osd_value_r[0] = 0;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_rgb_value.osd_value_g[0] = 0;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_rgb_value.osd_value_b[0] = 0;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_rgb_value.osd_value_r[1] = 255;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_rgb_value.osd_value_g[1] = 255;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_rgb_value.osd_value_b[1] = 255;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_rgb_value.osd_value_r[2] = 255;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_rgb_value.osd_value_g[2] = 255;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_rgb_value.osd_value_b[2] = 255;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_rgb_value.osd_value_r[3] = 255;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_rgb_value.osd_value_g[3] = 255;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_rgb_value.osd_value_b[3] = 255;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_rgb_value.osd_value_alpha[0] = 0;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_rgb_value.osd_value_alpha[1] = 1;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_rgb_value.osd_value_alpha[2] = 1;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_rgb_value.osd_value_alpha[3] = 1;
		stISPOSDAsm.stsinglepicAttr.pic.pinum =  g_pichandle[i];
		stISPOSDAsm.stsinglepicAttr.pic.osd_enable = 1;
		stISPOSDAsm.stsinglepicAttr.pic.osd_left = 50;
		stISPOSDAsm.stsinglepicAttr.pic.osd_top = 100 + 120 * i;
		stISPOSDAsm.stsinglepicAttr.pic.osd_width = 32;
		stISPOSDAsm.stsinglepicAttr.pic.osd_height = 24;
		stISPOSDAsm.stsinglepicAttr.pic.osd_image = (void *)num2_2bit;
		stISPOSDAsm.stsinglepicAttr.pic.osd_stride = 8; // 8对齐

		// main video
		ret = IMP_ISP_Tuning_SetOsdRgnAttr(0, g_pichandle[i], &stISPOSDAsm);
		if(ret < 0) {
			IMP_LOG_ERR(TAG,"IMP_ISP_Tuning_SetOsdRgnAttr error\n");
			return ;
		}

		ret = IMP_ISP_Tuning_ShowOsdRgn(0, g_pichandle[i], 1);
		if(ret < 0) {
			IMP_LOG_ERR(TAG,"IMP_ISP_Tuning_ShowOsdRgn error\n");
			return ;
		}
	}

#if 1
	for(i = 4; i < 8; i++) {
		stISPOSDAsm.type = ISP_OSD_REG_PIC;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_type = IMP_ISP_PIC_ARGB_8888;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_argb_type = IMP_ISP_ARGB_TYPE_BGRA;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_pixel_alpha_disable = IMPISP_TUNING_OPS_MODE_DISABLE;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_overlap = 0;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_group = 1;
		stISPOSDAsm.stsinglepicAttr.pic.pinum =  g_pichandle[i];
		stISPOSDAsm.stsinglepicAttr.pic.osd_enable = 1;
		stISPOSDAsm.stsinglepicAttr.pic.osd_left = 100;
		stISPOSDAsm.stsinglepicAttr.pic.osd_top = 150 + 120 * (i - 4);
		stISPOSDAsm.stsinglepicAttr.pic.osd_width = 100;
		stISPOSDAsm.stsinglepicAttr.pic.osd_height = 100;
		stISPOSDAsm.stsinglepicAttr.pic.osd_image = (void *)logodata_100x100_bgra;
		stISPOSDAsm.stsinglepicAttr.pic.osd_stride = 100*4;

		// main video
		ret = IMP_ISP_Tuning_SetOsdRgnAttr(0, g_pichandle[i], &stISPOSDAsm);
		if(ret < 0) {
			IMP_LOG_ERR(TAG,"IMP_ISP_Tuning_SetOsdRgnAttr error\n");
			return ;
		}

		ret = IMP_ISP_Tuning_ShowOsdRgn(0, g_pichandle[i], 1);
		if(ret < 0) {
			IMP_LOG_ERR(TAG,"IMP_ISP_Tuning_ShowOsdRgn error\n");
			return ;
		}
	}
#else
	for(i = 4; i < 8; i++) {
		stISPOSDAsm.type = ISP_OSD_REG_PIC;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_type = IMP_ISP_PIC_ARGB_1100;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_argb_type = IMP_ISP_ARGB_TYPE_BGRA;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_pixel_alpha_disable = IMPISP_TUNING_OPS_MODE_DISABLE;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_overlap = 0;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_group = 1;
		stISPOSDAsm.stsinglepicAttr.pic.pinum =  g_pichandle[i];
		stISPOSDAsm.stsinglepicAttr.pic.osd_enable = 1;
		stISPOSDAsm.stsinglepicAttr.pic.osd_left = 50;
		stISPOSDAsm.stsinglepicAttr.pic.osd_top = 100 + 120 * i;
		stISPOSDAsm.stsinglepicAttr.pic.osd_width = 100;
		stISPOSDAsm.stsinglepicAttr.pic.osd_height = 100;
		stISPOSDAsm.stsinglepicAttr.pic.osd_image = (void *)logodata_100x100_bgra;
		stISPOSDAsm.stsinglepicAttr.pic.osd_stride = 100*4;

		// main video
		ret = IMP_ISP_Tuning_SetOsdRgnAttr(0, g_pichandle[i], &stISPOSDAsm);
		if(ret < 0) {
			IMP_LOG_ERR(TAG,"IMP_ISP_Tuning_SetOsdRgnAttr error\n");
			return ;
		}

		ret = IMP_ISP_Tuning_ShowOsdRgn(0, g_pichandle[i], 1);
		if(ret < 0) {
			IMP_LOG_ERR(TAG,"IMP_ISP_Tuning_ShowOsdRgn error\n");
			return ;
		}
	}
#endif

}

void draw_normal(void)
{
	int ret =0;
	int j = 0;

	for(j = 0; j < 10; j++) {
		rIspOsdAttr.type = OSD_REG_ISP_LINE_RECT;
		rIspOsdAttr.osdispdraw.stDrawAttr.pinum = j;
		rIspOsdAttr.osdispdraw.stDrawAttr.type = IMP_ISP_DRAW_LINE;
		rIspOsdAttr.osdispdraw.stDrawAttr.color_type = IMPISP_MASK_TYPE_YUV;
		rIspOsdAttr.osdispdraw.stDrawAttr.cfg.line.enable = 1;
		rIspOsdAttr.osdispdraw.stDrawAttr.cfg.line.startx = 400 + 100 * j;    /*Draw a vertical line.*/
		rIspOsdAttr.osdispdraw.stDrawAttr.cfg.line.starty = 100;
		rIspOsdAttr.osdispdraw.stDrawAttr.cfg.line.endx = 400 + 100 * j;
		rIspOsdAttr.osdispdraw.stDrawAttr.cfg.line.endy = 400;
		rIspOsdAttr.osdispdraw.stDrawAttr.cfg.line.color.ayuv.y_value = 255;
		rIspOsdAttr.osdispdraw.stDrawAttr.cfg.line.color.ayuv.u_value = 0;
		rIspOsdAttr.osdispdraw.stDrawAttr.cfg.line.color.ayuv.v_value = 0;
		rIspOsdAttr.osdispdraw.stDrawAttr.cfg.line.width = 5;
		rIspOsdAttr.osdispdraw.stDrawAttr.cfg.line.alpha = 4; /*The range is [0,4], the smaller the value, the more transparent.*/

		pthread_mutex_lock(&mutex);
		ret = IMP_OSD_SetRgnAttr_ISP(0, &rIspOsdAttr, 1);
		if(ret < 0){
			IMP_LOG_ERR(TAG, "%s, IMP_ISP_SetDrawAttr error,check param\n", __func__);
			return ;
		}
		pthread_mutex_unlock(&mutex);
	}

	for (j = 10; j < 20; j++) {
		rIspOsdAttr.type = OSD_REG_ISP_LINE_RECT;
		rIspOsdAttr.osdispdraw.stDrawAttr.pinum = j;
		rIspOsdAttr.osdispdraw.stDrawAttr.msk_size = 20;
		rIspOsdAttr.osdispdraw.stDrawAttr.type = IMP_ISP_DRAW_WIND;
		rIspOsdAttr.osdispdraw.stDrawAttr.color_type = IMPISP_MASK_TYPE_YUV;
		rIspOsdAttr.osdispdraw.stDrawAttr.cfg.wind.enable = 1;
		rIspOsdAttr.osdispdraw.stDrawAttr.cfg.wind.left = 500 + 300 * (j - 10);
		rIspOsdAttr.osdispdraw.stDrawAttr.cfg.wind.top = 500 + 40 * (j - 10);
		rIspOsdAttr.osdispdraw.stDrawAttr.cfg.wind.width = 200;
		rIspOsdAttr.osdispdraw.stDrawAttr.cfg.wind.height = 200;
		rIspOsdAttr.osdispdraw.stDrawAttr.cfg.wind.color.ayuv.y_value = 255;
		rIspOsdAttr.osdispdraw.stDrawAttr.cfg.wind.color.ayuv.u_value = 0;
		rIspOsdAttr.osdispdraw.stDrawAttr.cfg.wind.color.ayuv.v_value = 0;
		rIspOsdAttr.osdispdraw.stDrawAttr.cfg.wind.line_width = 3;
		rIspOsdAttr.osdispdraw.stDrawAttr.cfg.wind.alpha = 4; /*range:[0,4]*/
		rIspOsdAttr.osdispdraw.stDrawAttr.cfg.wind.msk_en = 0;

		pthread_mutex_lock(&mutex);
		ret = IMP_OSD_SetRgnAttr_ISP(0, &rIspOsdAttr, 1);
		if(ret < 0){
			IMP_LOG_ERR(TAG, "%s, IMP_ISP_SetDrawAttr error,check param\n", __func__);
			return ;
		}
		pthread_mutex_unlock(&mutex);
	}

		for (j = 0; j < 4; j++) {
			rIspOsdAttr.type = OSD_REG_ISP_COVER;
			rIspOsdAttr.osdispdraw.stCoverAttr.chx = 0;
			rIspOsdAttr.osdispdraw.stCoverAttr.pinum = j;
			rIspOsdAttr.osdispdraw.stCoverAttr.mask_en = 1;
			rIspOsdAttr.osdispdraw.stCoverAttr.mask_pos_top = 150 + 50 * (1 + j);
			rIspOsdAttr.osdispdraw.stCoverAttr.mask_pos_left = 150 + 50 * (1 + j);
			rIspOsdAttr.osdispdraw.stCoverAttr.mask_width = 50;
			rIspOsdAttr.osdispdraw.stCoverAttr.mask_height = 50;
			rIspOsdAttr.osdispdraw.stCoverAttr.mask_type = IMPISP_MASK_TYPE_RGB;
			rIspOsdAttr.osdispdraw.stCoverAttr.mask_value.argb.r_value = 0;
			rIspOsdAttr.osdispdraw.stCoverAttr.mask_value.argb.g_value = 0;
			rIspOsdAttr.osdispdraw.stCoverAttr.mask_value.argb.b_value = 255;

			pthread_mutex_lock(&mutex);
			ret = IMP_OSD_SetRgnAttr_ISP(0, &rIspOsdAttr, 1);
			if(ret < 0){
				IMP_LOG_ERR(TAG, "%s, IMP_ISP_Tuning_SetMask error,check param\n", __func__);
				return ;
			}
			pthread_mutex_unlock(&mutex);
	}

	return ;
}

static int sample_ipuosd_show(int grpNum)
{
	int ret, i;
	for(i = 0 ;i < 21; i++) {
		ret = IMP_OSD_ShowRgn(pipuhandles[grpNum][i], grpNum, 1);
		if(ret < 0){
			IMP_LOG_ERR(TAG,"[%d][%d] show error\n", 0, pipuhandles[grpNum][i]);
			return -1;
		}
	}

	return 0;
}

/*IPUOSD 更新时间戳线程*/
static void *update_thread(void *p)
{
	IMP_Sample_OsdParam *pipuosdpam = (IMP_Sample_OsdParam*)p;

	/*generate time*/
	char DateStr[40];
	time_t currTime;
	struct tm *currDate;
	unsigned i = 0, j = 0;
	void *dateData = NULL;
	uint32_t *data = pipuosdpam->ptimestamps;

	sample_ipuosd_show(pipuosdpam->chn);

	IMPOSDRgnAttrData rAttrData;
	while(1) {
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
		pthread_testcancel();
		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

		int penpos_t = 0;
		int fontadv = 0;

		time(&currTime);
		currDate = localtime(&currTime);
		memset(DateStr, 0, 40);
		strftime(DateStr, 40, "%Y-%m-%d %I:%M:%S", currDate);
		for (i = 0; i < OSD_LETTER_NUM; i++) {
			switch(DateStr[i]) {
				case '0' ... '9':
					dateData = (void *)g_2bit[DateStr[i] - '0'].pdata;
					fontadv = g_2bit[DateStr[i] - '0'].width;
					penpos_t += g_2bit[DateStr[i] - '0'].width;
					break;
				case '-':
					dateData = (void *)g_2bit[10].pdata;
					fontadv = g_2bit[10].width;
					penpos_t += g_2bit[10].width;
					break;
				case ' ':
					dateData = (void *)g_2bit[11].pdata;
					fontadv = g_2bit[11].width;
					penpos_t += g_2bit[11].width;
					break;
				case ':':
					dateData = (void *)g_2bit[12].pdata;
					fontadv = g_2bit[12].width;
					penpos_t += g_2bit[12].width;
					break;
				default:
					break;
			}

#ifdef SUPPORT_RGB555LE
			for (j = 0; j < OSD_REGION_HEIGHT; j++) {
				memcpy((void *)((uint16_t *)data + j*OSD_LETTER_NUM*OSD_REGION_WIDTH + penpos_t),
						(void *)((uint16_t *)dateData + j*fontadv), fontadv*sizeof(uint16_t));
			}
#else
			for (j = 0; j < 24; j++) {
				memcpy((void *)((uint8_t *)data + j*OSD_LETTER_NUM * fontadv + penpos_t),
						(void *)((uint8_t *)dateData + j*fontadv), fontadv*sizeof(uint8_t));
			}

#endif
		}
		rAttrData.picData.pData = data;
		//printf("pipuosdpam->phandles[0] = %d\n", pipuosdpam->phandles[0]);
		int handle = pipuosdpam->phandles[0];

		IMP_OSD_UpdateRgnAttrData(handle, &rAttrData);

		sleep(1);
	}

	return NULL;
}




void* isposd_thread(void *arg)
{
	/*Draw lines, boxes, rectangles occlusion*/
	draw_normal();

	/*draw pictures, note that the ISP interface that draws the picture type is different from the interface that draws lines, boxes, and rectangles*/
	draw_pic();

	/*Plot the timestamp*/
	update_time(timeStampData);

	return NULL;
}

static int sample_ipuosd_init(int grpNum)
{
	int ret, i;
	IMPRgnHandle rHanderFont = 0;
	IMPRgnHandle *rHanderLogo = NULL;
	IMPRgnHandle *rHanderCover = NULL;
	IMPRgnHandle *rHanderRect = NULL;
	IMPRgnHandle *rHanderLine = NULL;
	IMPRgnHandle *rHanderSlash = NULL;

	rHanderLogo = malloc(5 * sizeof(IMPRgnHandle));
	if (rHanderLogo <= 0) {
		IMP_LOG_ERR(TAG, "malloc() error !\n");
		return -1;
	}

	rHanderCover = malloc(5 * sizeof(IMPRgnHandle));
	if (rHanderCover <= 0) {
		IMP_LOG_ERR(TAG, "malloc() error !\n");
		return -1;
	}

	rHanderRect = malloc(5 * sizeof(IMPRgnHandle));
	if (rHanderRect <= 0) {
		IMP_LOG_ERR(TAG, "malloc() error !\n");
		return -1;
	}

	rHanderLine = malloc(5 * sizeof(IMPRgnHandle));
	if (rHanderLine <= 0) {
		IMP_LOG_ERR(TAG, "malloc() error !\n");
		return -1;
	}

	rHanderSlash = malloc(5 * sizeof(IMPRgnHandle));
	if (rHanderSlash <= 0) {
		IMP_LOG_ERR(TAG, "malloc() error !\n");
		return -1;
	}

	/* Font */
	rHanderFont = IMP_OSD_CreateRgn(NULL);
	if (rHanderFont == INVHANDLE) {
		IMP_LOG_ERR(TAG, "IMP_OSD_CreateRgn TimeStamp error !\n");
		return -1;
	}
	//query osd rgn create status
	IMPOSDRgnCreateStat stStatus;
	memset(&stStatus, 0x0, sizeof(IMPOSDRgnCreateStat));
	ret = IMP_OSD_RgnCreate_Query(rHanderFont, &stStatus);
	if(ret < 0){
		IMP_LOG_ERR(TAG, "IMP_OSD_RgnCreate_Query error !\n");
		return -1;
	}

	ret = IMP_OSD_RegisterRgn(rHanderFont, grpNum, NULL);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IVS IMP_OSD_RegisterRgn failed\n");
		return -1;
	}

	IMPOSDRgnAttr rAttrFont;
	memset(&rAttrFont, 0, sizeof(IMPOSDRgnAttr));
	rAttrFont.type = OSD_REG_PIC;
	rAttrFont.rect.p0.x = 10;
	rAttrFont.rect.p0.y = 10;
	rAttrFont.rect.p1.x = rAttrFont.rect.p0.x + 20 * 8 * 4 - 1;    //p0 is start，and p1 well be epual p0+width(or heigth)-1
	rAttrFont.rect.p1.y = rAttrFont.rect.p0.y + 24 - 1;
#ifdef SUPPORT_RGB555LE
	rAttrFont.fmt = PIX_FMT_RGB555LE;
#else
	rAttrFont.fmt = PIX_FMT_2BIT;
#endif
	rAttrFont.data.picData.pData = NULL;
	rAttrFont.bitAttr.twobit_mask = 0x0;
	rAttrFont.bitAttr.twobit_bit0_fmt = 0x00FFFFFF;
	rAttrFont.bitAttr.twobit_bit1_fmt = 0xFFFFC0CB;
	ret = IMP_OSD_SetRgnAttr(rHanderFont, &rAttrFont);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_SetRgnAttr TimeStamp error !\n");
		return -1;
	}

	IMPOSDGrpRgnAttr grAttrFont;
	if (IMP_OSD_GetGrpRgnAttr(rHanderFont, grpNum, &grAttrFont) < 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_GetGrpRgnAttr TimeStamp error !\n");
		return -1;
	}
	memset(&grAttrFont, 0, sizeof(IMPOSDGrpRgnAttr));
	grAttrFont.show = 1;
	/* Disable Font global alpha, only use pixel alpha. */
	grAttrFont.gAlphaEn = 1;
	grAttrFont.fgAlhpa = 0xff;
	grAttrFont.layer = 3;
	if (IMP_OSD_SetGrpRgnAttr(rHanderFont, grpNum, &grAttrFont) < 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_SetGrpRgnAttr TimeStamp error !\n");
		return -1;
	}

	pipuhandles[grpNum][0] = rHanderFont;

	/* Pic */
	for(i = 0; i < 5; i++) {
		rHanderLogo[i] = IMP_OSD_CreateRgn(NULL);
		if (rHanderLogo[i] == INVHANDLE) {
			IMP_LOG_ERR(TAG, "IMP_OSD_CreateRgn Logo error !\n");
			return -1;
		}

		ret = IMP_OSD_RegisterRgn(rHanderLogo[i], grpNum, NULL);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IVS IMP_OSD_RegisterRgn failed\n");
			return -1;
		}

		IMPOSDRgnAttr rAttrLogo;
		memset(&rAttrLogo, 0, sizeof(IMPOSDRgnAttr));
		int picw = 8 * 4;
		int pich = 24;
		rAttrLogo.type = OSD_REG_PIC;
		rAttrLogo.rect.p0.x = 100;
		rAttrLogo.rect.p0.y = 100 + i * 30;
		rAttrLogo.rect.p1.x = rAttrLogo.rect.p0.x + picw - 1;
		rAttrLogo.rect.p1.y = rAttrLogo.rect.p0.y + pich - 1;
		rAttrLogo.fmt = PIX_FMT_2BIT;
		rAttrLogo.data.picData.pData = num2_2bit;
		rAttrLogo.bitAttr.twobit_mask = 0x0;
		rAttrLogo.bitAttr.twobit_bit0_fmt = 0x00FFFFFF;
		rAttrLogo.bitAttr.twobit_bit1_fmt = 0xFFFFB400;
		ret = IMP_OSD_SetRgnAttr(rHanderLogo[i], &rAttrLogo);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_OSD_SetRgnAttr Logo error !\n");
			return -1;
		}

		IMPOSDGrpRgnAttr grAttrLogo;
		if (IMP_OSD_GetGrpRgnAttr(rHanderLogo[i], grpNum, &grAttrLogo) < 0) {
			IMP_LOG_ERR(TAG, "IMP_OSD_GetGrpRgnAttr Logo error !\n");
			return -1;
		}
		memset(&grAttrLogo, 0, sizeof(IMPOSDGrpRgnAttr));
		grAttrLogo.show = 1;
		/* Set Logo global alpha to 0x7f, it is semi-transparent. */
		grAttrLogo.gAlphaEn = 1;
		grAttrLogo.fgAlhpa = 0x7f;
		grAttrLogo.layer = 2;

		if (IMP_OSD_SetGrpRgnAttr(rHanderLogo[i], grpNum, &grAttrLogo) < 0) {
			IMP_LOG_ERR(TAG, "IMP_OSD_SetGrpRgnAttr Logo error !\n");
			return -1;
		}

		pipuhandles[grpNum][i+1] = rHanderLogo[i];
	}

	/* Rect */
	for(i = 0; i < 5; i++) {
		rHanderRect[i] = IMP_OSD_CreateRgn(NULL);
		if (rHanderRect[i] == INVHANDLE) {
			IMP_LOG_ERR(TAG, "IMP_OSD_CreateRgn Rect error !\n");
			return -1;
		}

		ret = IMP_OSD_RegisterRgn(rHanderRect[i], grpNum, NULL);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_OSD_RegisterRgn failed\n");
			return -1;
		}
		IMPOSDRgnAttr rAttrRect;
		memset(&rAttrRect, 0, sizeof(IMPOSDRgnAttr));

		rAttrRect.type = OSD_REG_RECT;
		rAttrRect.rect.p0.x = 10 + i * 100;
		rAttrRect.rect.p0.y = 200;
		rAttrRect.rect.p1.x = rAttrRect.rect.p0.x + 50 - 1;
		rAttrRect.rect.p1.y = rAttrRect.rect.p0.y + 40 - 1;
		rAttrRect.fmt = PIX_FMT_MONOWHITE;
		rAttrRect.data.lineRectData.color = OSD_IPU_RED;
		rAttrRect.data.lineRectData.linewidth = 5;
		ret = IMP_OSD_SetRgnAttr(rHanderRect[i], &rAttrRect);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_OSD_SetRgnAttr Rect error !\n");
			return -1;
		}
		IMPOSDGrpRgnAttr grAttrRect;

		if (IMP_OSD_GetGrpRgnAttr(rHanderRect[i], grpNum, &grAttrRect) < 0) {
			IMP_LOG_ERR(TAG, "IMP_OSD_GetGrpRgnAttr Rect error !\n");
			return -1;

		}
		memset(&grAttrRect, 0, sizeof(IMPOSDGrpRgnAttr));
		grAttrRect.show = 1;
		grAttrRect.layer = 1;
		grAttrRect.scalex = 1;
		grAttrRect.scaley = 1;
		if (IMP_OSD_SetGrpRgnAttr(rHanderRect[i], grpNum, &grAttrRect) < 0) {
			IMP_LOG_ERR(TAG, "IMP_OSD_SetGrpRgnAttr Rect error !\n");
			return -1;
		}

		pipuhandles[grpNum][i+1+5] = rHanderRect[i];
	}

	/* Line */
	for(i = 0; i < 5; i++) {
		rHanderLine[i] = IMP_OSD_CreateRgn(NULL);
		if (rHanderLine[i] == INVHANDLE) {
			IMP_LOG_ERR(TAG, "IMP_OSD_CreateRgn Line error !\n");
			return -1;
		}

		ret = IMP_OSD_RegisterRgn(rHanderLine[i], grpNum, NULL);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IVS IMP_OSD_RegisterRgn failed\n");
			return -1;
		}
		IMPOSDRgnAttr rAttrLine;
		memset(&rAttrLine, 0, sizeof(IMPOSDRgnAttr));
		rAttrLine.type = OSD_REG_HORIZONTAL_LINE;
		rAttrLine.rect.p0.x = 200;
		rAttrLine.rect.p0.y = 10 + i * 20;
		rAttrLine.rect.p1.x = rAttrLine.rect.p0.x + 100;
		rAttrLine.rect.p1.y = rAttrLine.rect.p0.y;
		rAttrLine.fmt = PIX_FMT_MONOWHITE;
		rAttrLine.data.lineRectData.color = OSD_IPU_GREEN;
		rAttrLine.data.lineRectData.linewidth = 5;

		ret = IMP_OSD_SetRgnAttr(rHanderLine[i], &rAttrLine);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_OSD_SetRgnAttr Line error !\n");
			return -1;
		}
		IMPOSDGrpRgnAttr grAttrLine;
		if (IMP_OSD_GetGrpRgnAttr(rHanderLine[i], grpNum, &grAttrLine) < 0) {
			IMP_LOG_ERR(TAG, "IMP_OSD_GetGrpRgnAttr Line error !\n");
			return -1;
		}
		memset(&grAttrLine, 0, sizeof(IMPOSDGrpRgnAttr));
		grAttrLine.show = 1;
		grAttrLine.layer = 1;
		grAttrLine.scalex = 1;
		grAttrLine.scaley = 1;
		if (IMP_OSD_SetGrpRgnAttr(rHanderLine[i], grpNum, &grAttrLine) < 0) {
			IMP_LOG_ERR(TAG, "IMP_OSD_SetGrpRgnAttr Line error !\n");
			return -1;
		}

		pipuhandles[grpNum][i+1+5+5] = rHanderLine[i];
	}

	/* Slash */
	for(i = 0; i < 5; i++) {
		rHanderSlash[i] = IMP_OSD_CreateRgn(NULL);
		if (rHanderSlash[i] == INVHANDLE) {
			IMP_LOG_ERR(TAG, "IMP_OSD_CreateRgn Slash error !\n");
			return -1;
		}

		ret = IMP_OSD_RegisterRgn(rHanderSlash[i], grpNum, NULL);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IVS IMP_OSD_RegisterRgn failed\n");
			return -1;
		}
		IMPOSDRgnAttr rAttrSlash;
		memset(&rAttrSlash, 0, sizeof(IMPOSDRgnAttr));
		rAttrSlash.type = OSD_REG_SLASH;
		rAttrSlash.rect.p0.x = 100 + i * 20;
		rAttrSlash.rect.p0.y = 300;
		rAttrSlash.rect.p1.x = rAttrSlash.rect.p0.x + 40 - 1;
		rAttrSlash.rect.p1.y = rAttrSlash.rect.p0.y + 40 - 1;
		rAttrSlash.fmt = PIX_FMT_MONOWHITE;
		rAttrSlash.data.lineRectData.color = OSD_IPU_GREEN;
		rAttrSlash.data.lineRectData.linewidth = 5;
		ret = IMP_OSD_SetRgnAttr(rHanderSlash[i], &rAttrSlash);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_OSD_SetRgnAttr Slash error !\n");
			return -1;
		}

		IMPOSDGrpRgnAttr grAttrSlash;
		if (IMP_OSD_GetGrpRgnAttr(rHanderSlash[i], grpNum, &grAttrSlash) < 0) {
			IMP_LOG_ERR(TAG, "IMP_OSD_GetGrpRgnAttr Slash error !\n");
			return -1;
		}
		memset(&grAttrSlash, 0, sizeof(IMPOSDGrpRgnAttr));
		grAttrSlash.show = 1;
		grAttrSlash.layer = 1;
		grAttrSlash.scalex = 1;
		grAttrSlash.scaley = 1;
		if (IMP_OSD_SetGrpRgnAttr(rHanderSlash[i], grpNum, &grAttrSlash) < 0) {
			IMP_LOG_ERR(TAG, "IMP_OSD_SetGrpRgnAttr Slash error !\n");
			return -1;
		}

		pipuhandles[grpNum][i+1+5+5+5] = rHanderSlash[i];
	}


	if(IMP_OSD_Start(grpNum) < 0){
		IMP_LOG_ERR(TAG, "IMP_OSD_Start error !\n");
		return -1;
	}

	IMP_LOG_INFO(TAG, "[%s]succeed!\n",__func__);

	return 0;
}

int sample_ipuosd_exit()
{
	int ret = 0, i = 0, j = 0;
	for(i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable && chn[i].index == 1) {
			for(j = 0; j < 21; j++) {
				ret = IMP_OSD_UnRegisterRgn(pipuhandles[chn[i].index][j], chn[i].index);
				if (ret < 0) {
					IMP_LOG_ERR(TAG, "IMP_OSD_UnRegisterRgn Chn 0 error\n");
				}

				IMP_OSD_DestroyRgn(pipuhandles[chn[i].index][j]);
			}

			ret = IMP_OSD_DestroyGroup(chn[i].index);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "IMP_Encoder_DestrotyGroup(%d) error !\n", i);
				return -1;
			}

			IMP_OSD_Stop(chn[i].index);
		}
	}

	return 0;
}

int sample_osd_init_isp(void)
{
	int i = 0,ret = 0;

	for(i = 0; i < 8; i++) {
		g_pichandle[i] = IMP_ISP_Tuning_CreateOsdRgn(0, NULL);
	}

	return ret;
}

int sample_osd_exit_isp()
{
	int showflg = 0;
	int ret = 0, i = 0;

	for(i = 0; i < 8; i++) {
		IMP_ISP_Tuning_ShowOsdRgn(0, g_pichandle[i], showflg);
		IMP_ISP_Tuning_DestroyOsdRgn(0, g_pichandle[i]);
	}

	return ret;
}

int main(int argc, char *argv[])
{
	int i, ret;

	direct_switch = 0;

	gosd_enable = 3;

	chn[0].enable = 1;
	chn[1].enable = 1;
	chn[2].enable = 0;
	chn[3].enable = 0;
	chn[4].enable = 0;
	chn[5].enable = 0;

	pthread_mutex_init(&mutex, NULL);

	/* Step.1 System init */
	ret = sample_system_init();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_System_Init() failed\n");
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
				IMP_LOG_ERR(TAG, "IMP_Encoder_CreateGroup(%d) error !\n", chn[i].index);
				return -1;
			}
		}
	}

	ret = sample_video_init();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "Encoder init failed\n");
		return -1;
	}

	ret = IMP_OSD_CreateGroup(0);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_CreateGroup(%d) error !\n", chn[i].index);
		return -1;
	}

	/* Step.4 IPUOSD init */
	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable && chn[i].index == 1) {
			ret = IMP_OSD_CreateGroup(chn[i].index);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "IMP_OSD_CreateGroup(%d) error !\n", chn[i].index);
				return -1;
			}
			ret = sample_ipuosd_init(chn[i].index);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "OSD init failed\n");
				return -1;
			}
		}
	}

	/* Step.4 Bind */
	IMPCell osdcell[FS_CHN_NUM];
	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable && chn[i].index == 0) {
			ret = IMP_System_Bind(&chn[i].framesource_chn, &chn[i].imp_encoder);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "Bind FrameSource channel%d and Encoder failed\n",i);
				return -1;
			}
		} else if (chn[i].enable && chn[i].index == 1) {
			osdcell[chn[i].index].deviceID = DEV_ID_OSD;
			osdcell[chn[i].index].groupID  = chn[i].index;
			osdcell[chn[i].index].outputID = 0;
			ret = IMP_System_Bind(&chn[i].framesource_chn, &osdcell[chn[i].index]);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "Bind FrameSource channel0 and OSD failed\n");
				return -1;
			}
			ret = IMP_System_Bind(&osdcell[chn[i].index], &chn[i].imp_encoder);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "Bind OSD and Encoder failed\n");
				return -1;
			}
		}
	}

	ret = sample_osd_init_isp();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, " sample_osd_init_isp failed\n");
		return -1;
	}

#ifdef SUPPORT_RGB555LE
	timeStampData = malloc(OSD_LETTER_NUM * 32 * 24 * sizeof(uint16_t));
	timeStampData_ipu = malloc(OSD_LETTER_NUM * OSD_REGION_HEIGHT * OSD_REGION_WIDTH * sizeof(uint16_t));
#else
	timeStampData = malloc(OSD_LETTER_NUM * 16 * 24 * sizeof(uint32_t));
	timeStampData_ipu = malloc(OSD_LETTER_NUM * 8 * 24 * 4 * sizeof(uint32_t));
#endif

#if 1
	/* ipu osd */
	pthread_t ipuosd_tid[FS_CHN_NUM];
	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable && chn[i].index == 1) {
			ipuosdparam[chn[i].index].chn = chn[i].index;
			ipuosdparam[chn[i].index].phandles = pipuhandles[chn[i].index];
			ipuosdparam[chn[i].index].ptimestamps = timeStampData_ipu;
			ret = pthread_create(&ipuosd_tid[chn[i].index], NULL, update_thread, &ipuosdparam[chn[i].index]);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "ipu thread create error %d\n", __LINE__);
				return -1;
			}
		}
	}
#endif
	/* Step.5 Stream On */
	ret = sample_framesource_streamon();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "ImpStreamOn failed\n");
		return -1;
	}

	pthread_t tid;
	pthread_create(&tid, NULL, isposd_thread, NULL);

	/* Step.6 Get stream */
	ret = sample_start_get_video_stream();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "Get video stream failed\n");
		return -1;
	}

	sample_stop_get_video_stream();

	ret = sample_osd_exit_isp();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, " sample_osd_exit_isp failed\n");
		return -1;
	}

	pthread_cancel(tid);
	pthread_join(tid, NULL);
	free(timeStampData);

	/* Exit sequence as follow */
	/* Step.7 Stream Off */
	ret = sample_framesource_streamoff();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "FrameSource StreamOff failed\n");
		return -1;
	}

	/* Step.8 UnBind */
	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable && chn[i].index == 0) {
			ret = IMP_System_UnBind(&chn[i].framesource_chn, &chn[i].imp_encoder);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "UnBind FrameSource channel%d and Encoder failed\n",i);
				return -1;
			}
		} else if(chn[i].enable && chn[i].index == 1) {
			ret = IMP_System_UnBind(&chn[i].framesource_chn, &osdcell[chn[i].index]);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "UnBind FrameSource channel0 and OSD failed\n");
				return -1;
			}
			ret = IMP_System_UnBind(&osdcell[chn[i].index], &chn[i].imp_encoder);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "UnBind OSD and Encoder failed\n");
				return -1;
			}
		}
	}

	ret = sample_ipuosd_exit();
	if (ret) {
		IMP_LOG_ERR(TAG, "sample ipuosd exit error\n");
		return -1;
	}

	/* Step.9 Encoder exit */
	ret = sample_video_exit();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "Encoder exit failed\n");
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
		IMP_LOG_ERR(TAG, "sample_system_exit() failed\n");
		return -1;
	}

	free(timeStampData_ipu);

	sem_destroy(&semaphore);
	pthread_mutex_destroy(&mutex);

	return 0;
}
