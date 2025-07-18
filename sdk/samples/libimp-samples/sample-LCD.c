/*
	sample-LCD.c

	Copyright (C) 2014 Ingenic Semiconductor Co.,Ltd
	The specific instructions for all API calls in this file can be found in the header file of the proj/sdk-lv3/include/api/cn/imp/

	This sample demonstrates the operation of an LCD screen.
*/

#include <sys/mman.h>
#include <linux/fb.h>
#include <sys/ioctl.h>

#include "sample-common.h"

#define TAG "sample-LCD"

#define FB0DEV			"/dev/fb0"
#define MAX_DESC_NUM    2
#define MAX_LAYER_NUM 	1

extern struct chn_conf chn[];
struct jzfb_dev {
	unsigned int data_buf[MAX_DESC_NUM][MAX_LAYER_NUM];
	unsigned int num_buf;
	void *buf_addr;

	int width;
	int height;

	unsigned int vid_size;
	unsigned int fb_size;
	int bpp;
	int format;

	struct fb_fix_screeninfo fix_info;
	struct fb_var_screeninfo var_info;

	int fd;
};

static int jzfb_dev_init(struct jzfb_dev * jzfb_dev);
static int jzfb_pan_display(struct jzfb_dev *jzfb_dev, int fram_num);

int main(int argc, char *argv[])
{
	int ret = 0;
	int fram_num = 0;
	struct jzfb_dev *jzfb_dev;

	IMPFrameInfo *frame_bak;
	IMPFSChnAttr fs_chn_attr[2];

	IMPISPSensorFps fps;

	/* Step.0 init jzfb_dev */
	jzfb_dev = calloc(1, sizeof(struct jzfb_dev));
	if (jzfb_dev == NULL) {
		IMP_LOG_ERR(TAG,"jzfb_dev alloc mem for hwc dev failed!");
		return -1;
	}

	ret = jzfb_dev_init(jzfb_dev);
	if (ret < 0) {
		IMP_LOG_ERR(TAG,"jzfb_dev init error!\n");
		return -1;
	}

	/* Step.1 System init */
	ret = sample_system_init();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "System init failed\n");
		return -1;
	}

	/*Step.2 set sensror fps */
	fps.num = 25;
	fps.den = 1;
	ret = IMP_ISP_Tuning_SetSensorFPS(0, &fps);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_ISP_Tuning_SetSensorFPS failed\n");
		return -1;
	}

	/* Step.3 FrameSource init */
	if (chn[0].enable) {
		ret = IMP_FrameSource_CreateChn(chn[0].index, &chn[0].fs_chn_attr);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_FrameSource_CreateChn(%d) failed\n", chn[0].index);
			return -1;
		}
	}
	ret = IMP_FrameSource_SetChnAttr(chn[0].index, &chn[0].fs_chn_attr);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_FrameSource_SetChnAttr(%d) failed\n", chn[0].index);
		return -1;
	}

	/* Step.4 set framesource attr config */
	ret = IMP_FrameSource_GetChnAttr(0, &fs_chn_attr[0]);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_FrameSource_GetChnAttr failed\n");
		return -1;
	}
	fs_chn_attr[0].pixFmt = PIX_FMT_NV12;
	fs_chn_attr[0].crop.enable = 0;
	fs_chn_attr[0].scaler.enable = 1;
	fs_chn_attr[0].scaler.outwidth = jzfb_dev->width;
	fs_chn_attr[0].scaler.outheight = jzfb_dev->height;
	fs_chn_attr[0].picWidth = jzfb_dev->width;
	fs_chn_attr[0].picHeight = jzfb_dev->height;

	ret = IMP_FrameSource_SetChnAttr(0, &fs_chn_attr[0]);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_FrameSource_SetChnAttr failed\n");
		return -1;
	}

	/* Step.5 Stream On */
	if (chn[0].enable) {
		ret = IMP_FrameSource_EnableChn(chn[0].index);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_FrameSource_EnableChn(%d) failed\n", chn[0].index);
			return -1;
		}
	}

	ret = IMP_FrameSource_SetFrameDepth(0, 1);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_FrameSource_SetFrameDepth failed\n");
		return -1;
	}

	/* Step.6 Display  */
	while(1) {
		switch (fram_num) {
			case 0:
				ret = IMP_FrameSource_GetFrame(0, &frame_bak);
				if (ret < 0) {
					IMP_LOG_ERR(TAG, "IMP_FrameSource_GetFrame failed\n");
					return -1;
				}
				memcpy((void *)jzfb_dev->data_buf[0][0], (void *)(frame_bak->virAddr), frame_bak->size);
				break;
			case 1:
				ret = IMP_FrameSource_GetFrame(0, &frame_bak);
				if (ret < 0) {
					IMP_LOG_ERR(TAG, "IMP_FrameSource_GetFrame failed\n");
					return -1;
				}
				memcpy((void *)jzfb_dev->data_buf[1][0], (void *)(frame_bak->virAddr), frame_bak->size);
				break;
			default:
				break;
		}
		if (jzfb_pan_display(jzfb_dev, fram_num))
			break;
		fram_num ++;
		if (fram_num > 1)
			fram_num = 0;

		IMP_FrameSource_ReleaseFrame(0, frame_bak);
	}

	ret = IMP_FrameSource_SetFrameDepth(0, 0);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_FrameSource_SetFrameDepth failed\n");
		return -1;
	}

	/* Step.7 Stream Off */
	if (chn[0].enable) {
		ret = IMP_FrameSource_DisableChn(chn[0].index);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_FrameSource_DisableChn(%d) failed\n", chn[0].index);
			return -1;
		}
	}

	/* Step.8 FrameSource exit */
	if (chn[0].enable) {
		ret = IMP_FrameSource_DestroyChn(chn[0].index);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_FrameSource_DestroyChn(%d) failed\n", chn[0].index);
			return -1;
		}
	}

	/* Step.9 System exit */
	ret = sample_system_exit();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "System exit failed\n");
		return -1;
	}

	munmap(jzfb_dev->buf_addr, jzfb_dev->vid_size);
	close(jzfb_dev->fd);
	free(jzfb_dev);

	return 0;
}

static int jzfb_dev_init(struct jzfb_dev * jzfb_dev)
{
	int i = 0, j = 0;
	int ret = 0;

	jzfb_dev->fd = open(FB0DEV, O_RDWR);
	if (jzfb_dev->fd <= 2) {
		perror("fb0 open error");
		return jzfb_dev->fd;
	}

	/* get framebuffer's var_info */
	if ((ret = ioctl(jzfb_dev->fd, FBIOGET_VSCREENINFO, &jzfb_dev->var_info)) < 0) {
		perror("FBIOGET_VSCREENINFO failed");
		goto err_getinfo;
	}

	/* get framebuffer's fix_info */
	if ((ret = ioctl(jzfb_dev->fd, FBIOGET_FSCREENINFO, &jzfb_dev->fix_info)) < 0) {
		perror("FBIOGET_FSCREENINFO failed");
		goto err_getinfo;
	}

	jzfb_dev->var_info.width = jzfb_dev->var_info.xres;
	jzfb_dev->var_info.height = jzfb_dev->var_info.yres;
	jzfb_dev->bpp = jzfb_dev->var_info.bits_per_pixel >> 3;

	jzfb_dev->width = jzfb_dev->var_info.xres;
	jzfb_dev->height = jzfb_dev->var_info.yres;
	/* format rgb888 use 4 word ; format nv12/nv21 user 2 word */
	jzfb_dev->fb_size = jzfb_dev->var_info.xres * jzfb_dev->var_info.yres * jzfb_dev->bpp;
	jzfb_dev->num_buf = jzfb_dev->var_info.yres_virtual / jzfb_dev->var_info.yres;
	jzfb_dev->vid_size = jzfb_dev->fb_size * jzfb_dev->num_buf;

	jzfb_dev->buf_addr = mmap(0, jzfb_dev->vid_size, PROT_READ | PROT_WRITE, MAP_SHARED, jzfb_dev->fd, 0);
	if (jzfb_dev->buf_addr == 0) {
		perror("Map failed");
		ret = -1;
		goto err_getinfo;
	}

	for (i = 0; i < MAX_DESC_NUM; i++) {
		for (j = 0; j < MAX_LAYER_NUM; j++) {
			jzfb_dev->data_buf[i][j] = (unsigned int)(jzfb_dev->buf_addr +
					j * jzfb_dev->fb_size +
					i * jzfb_dev->fb_size * MAX_LAYER_NUM);
		}
	}
	printf("xres = %d, yres = %d line_length = %d fbsize = %d, num_buf = %d, vidSize = %d\n",
			jzfb_dev->var_info.xres, jzfb_dev->var_info.yres,
			jzfb_dev->fix_info.line_length, jzfb_dev->fb_size,
			jzfb_dev->num_buf, jzfb_dev->vid_size);
	return ret;

err_getinfo:
	close(jzfb_dev->fd);
	return ret;
}

static int jzfb_pan_display(struct jzfb_dev *jzfb_dev, int fram_num)
{
	int ret = 0;
	switch (fram_num) {
		case 0:
			jzfb_dev->var_info.yoffset = jzfb_dev->height * 0;
			break;
		case 1:
			jzfb_dev->var_info.yoffset = jzfb_dev->height * 1;
			break;
		case 2:
			jzfb_dev->var_info.yoffset = jzfb_dev->height * 2;
			break;
	}

	jzfb_dev->var_info.activate = FB_ACTIVATE_NOW;
	ret = ioctl(jzfb_dev->fd, FBIOPAN_DISPLAY, &jzfb_dev->var_info);
	if (ret < 0) {
		printf("pan display error!");
		return ret;
	}
	return 0;
}
