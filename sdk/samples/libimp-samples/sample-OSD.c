/*
	sample-OSD.c

	Copyright (C) 2014 Ingenic Semiconductor Co.,Ltd
	The specific instructions for all API calls in this file can be found in the header file of the proj/sdk-lv3/include/api/cn/imp/

	This sample demonstrates the operation of IPU OSD.
*/

#include "sample-common.h"
#include <stdlib.h>
#include "logodata_100x100_bgra.h"

#ifdef SUPPORT_RGB555LE
#include "bgramapinfo_rgb555le.h"
#else
#include "bitmapinfo.h"
#endif

#define TAG "sample-OSD"

#define OSD_LETTER_NUM 20

extern struct chn_conf chn[];
extern int direct_switch;
extern int gosd_enable;

static int osd_show(int grpNum);
static void *update_thread(void *p);
static int sample_osd_init(int grpNum);
static int sample_osd_exit(int grpNum);

static int grpNum[FS_CHN_NUM] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
static IMPRgnHandle prHander[FS_CHN_NUM][5];

int main(int argc, char *argv[])
{
	int i = 0;
	int ret = 0;

	direct_switch = 0;
	/* only show OSD in per sensor chn0, others need modify code */

	gosd_enable = 1;
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

	/* Step.4 OSD init */
	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			if (IMP_OSD_CreateGroup(grpNum[i]) < 0) {
				IMP_LOG_ERR(TAG, "OSD CreateGroup(%d) failed\n", grpNum[i]);
				return -1;
			}
		}
	}

	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			sample_osd_init(grpNum[i]);
		}
	}

	/* Step.5 Bind */
	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			IMPCell osd = { DEV_ID_OSD, grpNum[i], 0 };
			ret = IMP_System_Bind(&chn[i].framesource_chn, &osd);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "Bind framesource%d and osd%d failed\n", chn[i].framesource_chn.groupID, osd.groupID);
				return -1;
			}

			ret = IMP_System_Bind(&osd, &chn[i].imp_encoder);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "Bind osd%d and encoder%d failed\n", osd.groupID, chn[i].imp_encoder.groupID);
				return -1;
			}
		}
	}

	/* Step.6 Create OSD bgramap update thread */

	pthread_t tid[FS_CHN_NUM];
	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			ret = pthread_create(&tid[i], NULL, update_thread, (void *)i);
			if (ret) {
				IMP_LOG_ERR(TAG, "update_thread create failed\n");
				return -1;
			}
		}
	}

	/* Step.7 Stream On */
	ret = sample_framesource_streamon();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "FrameSource StreamOn failed\n");
		return -1;
	}

	/* Step.6 Get stream */
	ret = sample_start_get_video_stream();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "Get video stream failed\n");
		return -1;
	}
	sample_stop_get_video_stream();

	/* Step.7 Stream Off */
	ret = sample_framesource_streamoff();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "FrameSource StreamOff failed\n");
		return -1;
	}


	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			pthread_cancel(tid[i]);
			pthread_join(tid[i], NULL);
		}
	}

	/* Step.8 UnBind */

	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			IMPCell osd = { DEV_ID_OSD, grpNum[i], 0 };
			ret = IMP_System_UnBind(&osd, &chn[i].imp_encoder);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "UnBind osd%d and encoder%d failed\n", osd.groupID, chn[i].imp_encoder.groupID);
				return -1;
			}

			ret = IMP_System_UnBind(&chn[i].framesource_chn, &osd);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "UnBind framesource%d and osd%d failed\n", chn[i].framesource_chn.groupID, osd.groupID);
				return -1;
			}
		}
	}

	/* Step.9 OSD exit */
	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			ret = sample_osd_exit(i);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "osd exit failed\n");
				return -1;
			}
		}
	}

	/* Step.10 Encoder exit */
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

	/* Step.11 FrameSource exit */
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

static int osd_show(int grpNum)
{
	int ret = 0;

	ret = IMP_OSD_ShowRgn(prHander[grpNum][0], grpNum, 1);
	if (ret != 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_ShowRgn timeStamp failed\n");
		return -1;
	}

	ret = IMP_OSD_ShowRgn(prHander[grpNum][1], grpNum, 1);
	if (ret != 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_ShowRgn Logo failed\n");
		return -1;
	}

	ret = IMP_OSD_ShowRgn(prHander[grpNum][2], grpNum, 1);
	if (ret != 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_ShowRgn Logo failed\n");
		return -1;
	}

	ret = IMP_OSD_ShowRgn(prHander[grpNum][3], grpNum, 1);
	if (ret != 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_ShowRgn Logo failed\n");
		return -1;
	}

	ret = IMP_OSD_ShowRgn(prHander[grpNum][4], grpNum, 1);
	if (ret != 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_ShowRgn Logo failed\n");
		return -1;
	}

	return 0;
}

static void *update_thread(void *p)
{
	int j = 0;
	int ret = 0;
	int grpNum = (int)p;

	/* generate time */
	char DateStr[40];
	time_t currTime;
	struct tm *currDate;
	void *dateData = NULL;

	char *data = NULL;
        IMPOSDRgnAttr rAttr;

	ret = osd_show(grpNum);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "OSD show failed\n");
		return NULL;
	}


	while(1) {
		int penpos = 0;
		int penpos_t = 0;
		int fontadv = 0;
		unsigned int len = 0;

		ret = IMP_OSD_GetRgnAttr(prHander[grpNum][0], &rAttr);

		data = (void *)rAttr.data.bitmapData;
		time(&currTime);
		currDate = localtime(&currTime);
		memset(DateStr, 0, 40);
		strftime(DateStr, 40, "%Y-%m-%d %I:%M:%S", currDate);
		len = strlen(DateStr);
		for (int i = 0; i < len; i++) {
			switch (DateStr[i]) {
			case '0' ... '9':
#ifdef SUPPORT_COLOR_REVERSE
				if (rAttr.fontData.colType[i] == 1) {
					dateData = (void *)gBitmap_black[DateStr[i] - '0'].pdata;
				} else {
					dateData = (void *)gBitmap[DateStr[i] - '0'].pdata;
				}
#else
				dateData = (void *)gBitmap[DateStr[i] - '0'].pdata;
#endif
				fontadv = gBitmap[DateStr[i] - '0'].width;
				penpos_t += gBitmap[DateStr[i] - '0'].width;
				break;
			case '-':
#ifdef SUPPORT_COLOR_REVERSE
				if (rAttr.fontData.colType[i] == 1) {
					dateData = (void *)gBitmap_black[10].pdata;
				} else {
					dateData = (void *)gBitmap[10].pdata;
				}
#else
				dateData = (void *)gBitmap[10].pdata;
#endif
				fontadv = gBitmap[10].width;
				penpos_t += gBitmap[10].width;
				break;
			case ' ':
				dateData = (void *)gBitmap[11].pdata;
				fontadv = gBitmap[11].width;
				penpos_t += gBitmap[11].width;
				break;
			case ':':
#ifdef SUPPORT_COLOR_REVERSE
				if (rAttr.fontData.colType[i] == 1) {
					dateData = (void *)gBitmap_black[12].pdata;
				} else {
					dateData = (void *)gBitmap[12].pdata;
				}
#else
				dateData = (void *)gBitmap[12].pdata;
#endif
				fontadv = gBitmap[12].width;
				penpos_t += gBitmap[12].width;
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
			for (j = 0; j < gBitmapHight; j++) {
				memcpy((void *)(data + j*OSD_LETTER_NUM*OSD_REGION_WIDTH + penpos),
				       (void *)(dateData + j*fontadv), fontadv);
			}
			penpos = penpos_t;
#endif
		}

		sleep(1);
	}

	return NULL;
}

static int sample_osd_init(int grpNum)
{
	int ret = 0;
	IMPRgnHandle rHanderFont = 0;
	IMPRgnHandle rHanderLogo = 0;
	IMPRgnHandle rHanderCover = 0;
	IMPRgnHandle rHanderRect = 0;
	IMPRgnHandle rHanderLine = 0;

	rHanderFont = IMP_OSD_CreateRgn(NULL);
	if (rHanderFont == INVHANDLE) {
		IMP_LOG_ERR(TAG, "IMP_OSD_CreateRgn TimeStamp failed\n");
		return -1;
	}
	/* query osd rgn create status */
	IMPOSDRgnCreateStat stStatus;
	memset(&stStatus, 0x0, sizeof(IMPOSDRgnCreateStat));
	ret = IMP_OSD_RgnCreate_Query(rHanderFont, &stStatus);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_RgnCreate_Query failed\n");
		return -1;
	}

	rHanderLogo = IMP_OSD_CreateRgn(NULL);
	if (rHanderLogo == INVHANDLE) {
		IMP_LOG_ERR(TAG, "IMP_OSD_CreateRgn Logo failed\n");
		return -1;
	}

	rHanderCover = IMP_OSD_CreateRgn(NULL);
	if (rHanderCover == INVHANDLE) {
		IMP_LOG_ERR(TAG, "IMP_OSD_CreateRgn Cover failed\n");
		return -1;
	}

	rHanderRect = IMP_OSD_CreateRgn(NULL);
	if (rHanderRect == INVHANDLE) {
		IMP_LOG_ERR(TAG, "IMP_OSD_CreateRgn Rect failed\n");
		return -1;
	}

	rHanderLine = IMP_OSD_CreateRgn(NULL);
	if (rHanderLine == INVHANDLE) {
		IMP_LOG_ERR(TAG, "IMP_OSD_CreateRgn Line failed\n");
		return -1;
	}

	ret = IMP_OSD_RegisterRgn(rHanderFont, grpNum, NULL);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IVS IMP_OSD_RegisterRgn failed\n");
		return -1;
	}

	/* query osd rgn register status */
	IMPOSDRgnRegisterStat stRigStatus;
	memset(&stRigStatus, 0x0, sizeof(IMPOSDRgnRegisterStat));
	ret = IMP_OSD_RgnRegister_Query(rHanderFont, grpNum,&stRigStatus);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_RgnRegister_Query failed\n");
		return -1;
	}

	ret = IMP_OSD_RegisterRgn(rHanderLogo, grpNum, NULL);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IVS IMP_OSD_RegisterRgn failed\n");
		return -1;
	}

	ret = IMP_OSD_RegisterRgn(rHanderCover, grpNum, NULL);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IVS IMP_OSD_RegisterRgn failed\n");
		return -1;
	}

	ret = IMP_OSD_RegisterRgn(rHanderRect, grpNum, NULL);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IVS IMP_OSD_RegisterRgn failed\n");
		return -1;
	}

	ret = IMP_OSD_RegisterRgn(rHanderLine, grpNum, NULL);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IVS IMP_OSD_RegisterRgn failed\n");
		return -1;
	}

	/* Font */
	IMPOSDRgnAttr rAttrFont;
	memset(&rAttrFont, 0, sizeof(IMPOSDRgnAttr));
	rAttrFont.type = OSD_REG_BITMAP;
	rAttrFont.rect.p0.x = 10;
	rAttrFont.rect.p0.y = 10;
	rAttrFont.rect.p1.x = rAttrFont.rect.p0.x + 20 * OSD_REGION_WIDTH- 1; /* p0 is start，and p1 well be epual p0+width(or heigth)-1 */
	rAttrFont.rect.p1.y = rAttrFont.rect.p0.y + OSD_REGION_HEIGHT - 1;
#ifdef SUPPORT_RGB555LE
	rAttrFont.fmt = PIX_FMT_RGB555LE;
#else
	rAttrFont.fmt = PIX_FMT_MONOWHITE;
#endif
	rAttrFont.data.bitmapData = valloc(20 * OSD_REGION_WIDTH * OSD_REGION_HEIGHT * sizeof(uint32_t));
	if (rAttrFont.data.bitmapData == NULL) {
		IMP_LOG_ERR(TAG, "alloc rAttr.data.bitmapData TimeStamp failed\n");
		return -1;
	}
	ret = IMP_OSD_SetRgnAttr(rHanderFont, &rAttrFont);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_SetRgnAttr TimeStamp failed\n");
		return -1;
	}
	IMPOSDGrpRgnAttr grAttrFont;
	if (IMP_OSD_GetGrpRgnAttr(rHanderFont, grpNum, &grAttrFont) < 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_GetGrpRgnAttr Logo failed\n");
		return -1;
	}
	memset(&grAttrFont, 0, sizeof(IMPOSDGrpRgnAttr));
	grAttrFont.show = 0;
	/* Disable Font global alpha, only use pixel alpha. */
	grAttrFont.gAlphaEn = 0;
	grAttrFont.fgAlhpa = 0xff;
	grAttrFont.layer = 3;
	if (IMP_OSD_SetGrpRgnAttr(rHanderFont, grpNum, &grAttrFont) < 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_SetGrpRgnAttr Logo failed\n");
		return -1;
	}

	int j = 0;
	for(j = 0; j < 20; j++) {
		memset(bitdata_100x100 + j * 200, 0xff, 100);
	}

	/* Logo */
	IMPOSDRgnAttr rAttrLogo;
	memset(&rAttrLogo, 0, sizeof(IMPOSDRgnAttr));
	int picw = 100;
	int pich = 100;
	rAttrLogo.type = OSD_REG_PIC;
	rAttrLogo.rect.p0.x = 100;
	rAttrLogo.rect.p0.y = 100;
	/* p0 is start，and p1 well be epual p0+width(or heigth)-1 */
	rAttrLogo.rect.p1.x = rAttrLogo.rect.p0.x+picw-1;
	rAttrLogo.rect.p1.y = rAttrLogo.rect.p0.y+pich-1;
	rAttrLogo.fmt = PIX_FMT_2BIT;
	rAttrLogo.data.picData.pData = bitdata_100x100;
	//rAttrLogo.data.picData.pData = g_pdata;
	rAttrLogo.bitAttr.twobit_mask = 0x0;
	rAttrLogo.bitAttr.twobit_bit0_fmt = 0x7F0000FF;
	rAttrLogo.bitAttr.twobit_bit1_fmt = 0x7F00FF00;

	ret = IMP_OSD_SetRgnAttr(rHanderLogo, &rAttrLogo);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_SetRgnAttr Logo failed\n");
		return -1;
	}
	IMPOSDGrpRgnAttr grAttrLogo;
	if (IMP_OSD_GetGrpRgnAttr(rHanderLogo, grpNum, &grAttrLogo) < 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_GetGrpRgnAttr Logo failed\n");
		return -1;
	}
	memset(&grAttrLogo, 0, sizeof(IMPOSDGrpRgnAttr));
	grAttrLogo.show = 0;
	/* Set Logo global alpha to 0x7f, it is semi-transparent. */
	grAttrLogo.gAlphaEn = 1;
	grAttrLogo.fgAlhpa = 0x7f;
	grAttrLogo.layer = 2;
	if (IMP_OSD_SetGrpRgnAttr(rHanderLogo, grpNum, &grAttrLogo) < 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_SetGrpRgnAttr Logo failed\n");
		return -1;
	}

	/* Cover */
	IMPOSDRgnAttr rAttrCover;
	memset(&rAttrCover, 0, sizeof(IMPOSDRgnAttr));
	rAttrCover.type = OSD_REG_COVER;
	rAttrCover.rect.p0.x = 300;
	rAttrCover.rect.p0.y = 300;
	rAttrCover.rect.p1.x = rAttrCover.rect.p0.x+300 -1;
	rAttrCover.rect.p1.y = rAttrCover.rect.p0.y+280 -1 ;
	rAttrCover.fmt = PIX_FMT_BGRA;
	rAttrCover.data.coverData.color = OSD_IPU_RED;
	ret = IMP_OSD_SetRgnAttr(rHanderCover, &rAttrCover);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_SetRgnAttr Cover failed\n");
		return -1;
	}
	IMPOSDGrpRgnAttr grAttrCover;
	if (IMP_OSD_GetGrpRgnAttr(rHanderCover, grpNum, &grAttrCover) < 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_GetGrpRgnAttr Cover failed\n");
		return -1;
	}
	memset(&grAttrCover, 0, sizeof(IMPOSDGrpRgnAttr));
	grAttrCover.show = 0;
	/* Disable Cover global alpha, it is absolutely no transparent. */
	grAttrCover.gAlphaEn = 1;
	grAttrCover.fgAlhpa = 0x7f;
	grAttrCover.layer = 2;
	if (IMP_OSD_SetGrpRgnAttr(rHanderCover, grpNum, &grAttrCover) < 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_SetGrpRgnAttr Cover failed\n");
		return -1;
	}

	/* Rect */
	IMPOSDRgnAttr rAttrRect;
	memset(&rAttrRect, 0, sizeof(IMPOSDRgnAttr));
	rAttrRect.type = OSD_REG_RECT;
	rAttrRect.rect.p0.x = 400;
	rAttrRect.rect.p0.y = 400;
	rAttrRect.rect.p1.x = rAttrRect.rect.p0.x + 300 - 1;
	rAttrRect.rect.p1.y = rAttrRect.rect.p0.y + 300 - 1;
	rAttrRect.fmt = PIX_FMT_MONOWHITE;
	rAttrRect.data.lineRectData.color = OSD_IPU_RED;
	rAttrRect.data.lineRectData.linewidth = 5;
	ret = IMP_OSD_SetRgnAttr(rHanderRect, &rAttrRect);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_SetRgnAttr Rect failed\n");
		return -1;
	}
	IMPOSDGrpRgnAttr grAttrRect;
	if (IMP_OSD_GetGrpRgnAttr(rHanderRect, grpNum, &grAttrRect) < 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_GetGrpRgnAttr Rect failed\n");
		return -1;
	}
	memset(&grAttrRect, 0, sizeof(IMPOSDGrpRgnAttr));
	grAttrRect.show = 0;
	grAttrRect.layer = 1;
	grAttrRect.scalex = 1;
	grAttrRect.scaley = 1;
	if (IMP_OSD_SetGrpRgnAttr(rHanderRect, grpNum, &grAttrRect) < 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_SetGrpRgnAttr Rect failed\n");
		return -1;
	}

	/* Line */
	IMPOSDRgnAttr rAttrLine;
	memset(&rAttrLine, 0, sizeof(IMPOSDRgnAttr));
	rAttrLine.type = OSD_REG_HORIZONTAL_LINE;
	rAttrLine.rect.p0.x = 300;
	rAttrLine.rect.p0.y = 200;
	rAttrLine.rect.p1.x = rAttrLine.rect.p0.x + 300 - 1;
	rAttrLine.rect.p1.y = rAttrLine.rect.p0.y;
	rAttrLine.fmt = PIX_FMT_MONOWHITE;
	rAttrLine.data.lineRectData.color = OSD_IPU_GREEN;
	rAttrLine.data.lineRectData.linewidth = 5;
	ret = IMP_OSD_SetRgnAttr(rHanderLine, &rAttrLine);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_SetRgnAttr Line failed\n");
		return -1;
	}
	IMPOSDGrpRgnAttr grAttrLine;
	if (IMP_OSD_GetGrpRgnAttr(rHanderLine, grpNum, &grAttrLine) < 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_GetGrpRgnAttr Line failed\n");
		return -1;
	}
	memset(&grAttrLine, 0, sizeof(IMPOSDGrpRgnAttr));
	grAttrLine.show = 0;
	grAttrLine.layer = 1;
	grAttrLine.scalex = 1;
	grAttrLine.scaley = 1;
	if (IMP_OSD_SetGrpRgnAttr(rHanderLine, grpNum, &grAttrLine) < 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_SetGrpRgnAttr Line failed\n");
		return -1;
	}

	ret = IMP_OSD_Start(grpNum);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_Start TimeStamp, Logo, Cover and Rect failed\n");
		return -1;
	}

	prHander[grpNum][0] = rHanderFont;
	prHander[grpNum][1] = rHanderLogo;
	prHander[grpNum][2] = rHanderCover;
	prHander[grpNum][3] = rHanderRect;
	prHander[grpNum][4] = rHanderLine;

	return 0;
}

static int sample_osd_exit(int grpNum)
{
	int ret = 0;

	ret = IMP_OSD_ShowRgn(prHander[grpNum][0], grpNum, 0);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_ShowRgn close timeStamp failed\n");
	}

	ret = IMP_OSD_ShowRgn(prHander[grpNum][1], grpNum, 0);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_ShowRgn close Logo failed\n");
	}

	ret = IMP_OSD_ShowRgn(prHander[grpNum][2], grpNum, 0);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_ShowRgn close cover failed\n");
	}

	ret = IMP_OSD_ShowRgn(prHander[grpNum][3], grpNum, 0);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_ShowRgn close Rect failed\n");
	}

	ret = IMP_OSD_ShowRgn(prHander[grpNum][4], grpNum, 0);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_ShowRgn close Rect failed\n");
	}

	ret = IMP_OSD_UnRegisterRgn(prHander[grpNum][0], grpNum);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_UnRegisterRgn timeStamp failed\n");
	}

	ret = IMP_OSD_UnRegisterRgn(prHander[grpNum][1], grpNum);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_UnRegisterRgn logo failed\n");
	}

	ret = IMP_OSD_UnRegisterRgn(prHander[grpNum][2], grpNum);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_UnRegisterRgn Cover failed\n");
	}

	ret = IMP_OSD_UnRegisterRgn(prHander[grpNum][3], grpNum);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_UnRegisterRgn Rect failed\n");
	}

	ret = IMP_OSD_UnRegisterRgn(prHander[grpNum][4], grpNum);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_UnRegisterRgn Rect failed\n");
	}

	IMP_OSD_DestroyRgn(prHander[grpNum][0]);
	IMP_OSD_DestroyRgn(prHander[grpNum][1]);
	IMP_OSD_DestroyRgn(prHander[grpNum][2]);
	IMP_OSD_DestroyRgn(prHander[grpNum][3]);
	IMP_OSD_DestroyRgn(prHander[grpNum][4]);

	ret = IMP_OSD_DestroyGroup(grpNum);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_DestroyGroup failed\n");
		return -1;
	}

	return 0;
}
