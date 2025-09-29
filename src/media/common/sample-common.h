/*
	sample-common.h

	Copyright (C) 2014 Ingenic Semiconductor Co.,Ltd

	This sample demonstrates the declaration of some common functions.
*/

#ifndef __SAMPLE_COMMON_H__
#define __SAMPLE_COMMON_H__

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <math.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <semaphore.h>

#include <imp/imp_system.h>
#include <imp/imp_isp.h>
#include <imp/imp_framesource.h>
#include <imp/imp_osd.h>
#include <imp/imp_ivs.h>
#include <imp/imp_encoder.h>
#include <imp/imp_audio.h>
#include <imp/imp_dmic.h>
#include <imp/imp_log.h>
#include <imp/imp_common.h>
#include <imp/imp_utils.h>

#ifdef __cplusplus
#if __cplusplus
extern "C"
{
#endif
#endif /* __cplusplus */
/******************************************** Sensor Attribute Table *********************************************/
/* 		NAME		            I2C_ADDR		RESOLUTION		Default_Boot			        				*/
/* 		gc2063/gc2063s1		      0x37 			1920*1080		0:30fps_mipi 1:15fps_mipi						*/
/* 		gc2063s2/gc2063s3		  0x3f 			1920*1080		0:30fps_mipi 1:15fps_mipi						*/
/* 		gc3003		              0x37 			2304*1296		0:30fps_mipi						            */
/* 		gc4023		              0x29 			2560*1440		0:30fps_mipi						            */
/* 		gc4653		              0x29 			2560*1440		0:30fps_mipi						            */
/* 		gc5603		              0x31 			2880*1620		0:30fps_mipi						            */
/* 		gc5613		              0x31 			2880*1620		0:30fps_mipi 1:20fps_mipi_hdr 2:25fps_mipi      */
/* 		gc8613		              0x31 			3840*2160		0:25fps_mipi 1:15fps_mipi                       */
/* 		gc8613a		              0x31 			3840*2160		0:20fps_mipi                                    */
/* 		sc200ai/sc200ais1         0x30 			1920*1080		0:30fps_mipi_1lane 1:60fps_mipi_2lane			*/
/* 		sc231hai/sc231hais1       0x30 			1920*1080		0:30fps_mipi								    */
/* 		sc401ai		              0x30 			2560*1440		0:30fps_mipi								    */
/* 		sc500ai		              0x32 			2880*1620		0:40fps_mipi								    */
/* 		sc8238		              0x30 			3840*2160		0:30fps_mipi_4lane 								*/
/******************************************** Sensor Attribute Table *********************************************/


#define SENSOR_NUM                  IMPISP_TOTAL_ONE            //sensor total number (one/two/thr/four)

/************************************ first sensor *************************************************/
#ifdef SENSOR_TYPE_SC4336P
#define FIRST_SNESOR_NAME           		"sc4336p"            //sensor name (match with snesor driver name)
#define FIRST_I2C_ADDR              		0x30                //sensor i2c address
#define FIRST_I2C_ADAPTER_ID                0                           //sensor controller number used (0/1/2/3)
#define FIRST_SENSOR_WIDTH                  2560                      //sensor width
#define FIRST_SENSOR_HEIGHT                 1440                     //sensor height
#define FIRST_RST_GPIO                      GPIO_PA(20)                 //sensor reset gpio
#define FIRST_PWDN_GPIO                     -1                          //sensor pwdn gpio
#define FIRST_POWER_GPIO                    -1                          //sensor power gpio
#define FIRST_SWITCH_GPIO                   GPIO_PA(25)                 //sensor switch gpio
#define FIRST_SENSOR_ID                     0                           //sensor index
#define FIRST_VIDEO_INTERFACE               IMPISP_SENSOR_VI_MIPI_CSI0  //sensor interface type (dvp/csi0/csi1)
#define FIRST_MCLK                          IMPISP_SENSOR_MCLK0         //sensor clk source (mclk0/mclk1/mclk2)
#define FIRST_DEFAULT_BOOT                  0                           //sensor default mode(0/1/2/3/4)
#define CHN0_EN                             1                           //sensor0，output0
#define CHN1_EN                             1                           //sensor0，output1
#define CHN2_EN                             0                           //sensor0, output2
#define FIRST_CROP_EN                       0
#define FIRST_SENSOR_FRAME_RATE_NUM         15
#define FIRST_SENSOR_FRAME_RATE_DEN         1
#define FIRST_SENSOR_WIDTH_SECOND           1280
#define FIRST_SENSOR_HEIGHT_SECOND          720
#define FIRST_SENSOR_WIDTH_THIRD            1280
#define FIRST_SENSOR_HEIGHT_THIRD           720
#elif defined (SENSOR_TYPE_GC4653)
#define FIRST_SNESOR_NAME                   "gc4653"                    //sensor name (match with snesor driver name)
#define FIRST_I2C_ADDR                      0x29                        //sensor i2c address
#define FIRST_I2C_ADAPTER_ID                0                           //sensor controller number used (0/1/2/3)
#define FIRST_SENSOR_WIDTH                  2560                        //sensor width
#define FIRST_SENSOR_HEIGHT                 1440                        //sensor height
#define FIRST_RST_GPIO                      -1                 //sensor reset gpio
#define FIRST_PWDN_GPIO                     -1                          //sensor pwdn gpio
#define FIRST_POWER_GPIO                    -1                          //sensor power gpio
#define FIRST_SWITCH_GPIO                   -1                 //sensor switch gpio
#define FIRST_SENSOR_ID                     0                           //sensor index
#define FIRST_VIDEO_INTERFACE               IMPISP_SENSOR_VI_MIPI_CSI0  //sensor interface type (dvp/csi0/csi1)
#define FIRST_MCLK                          IMPISP_SENSOR_MCLK0         //sensor clk source (mclk0/mclk1/mclk2)
#define FIRST_DEFAULT_BOOT                  0                           //sensor default mode(0/1/2/3/4)
#define CHN0_EN                             1                           //sensor0，output0
#define CHN1_EN                             0                           //sensor0，output1
#define CHN2_EN                             0                           //sensor0, output2
#define FIRST_CROP_EN                       0
#define FIRST_SENSOR_FRAME_RATE_NUM         15
#define FIRST_SENSOR_FRAME_RATE_DEN         1
#define FIRST_SENSOR_WIDTH_SECOND           720
#define FIRST_SENSOR_HEIGHT_SECOND          576
#define FIRST_SENSOR_WIDTH_THIRD            1280
#define FIRST_SENSOR_HEIGHT_THIRD           720
#else
#define FIRST_SNESOR_NAME                   "gc5613"                    //sensor name (match with snesor driver name)
#define FIRST_I2C_ADDR                      0x31                        //sensor i2c address
#define FIRST_I2C_ADAPTER_ID                0                           //sensor controller number used (0/1/2/3)
#define FIRST_SENSOR_WIDTH                  2880                        //sensor width
#define FIRST_SENSOR_HEIGHT                 1620                        //sensor height
#define FIRST_RST_GPIO                      GPIO_PA(20)                 //sensor reset gpio
#define FIRST_PWDN_GPIO                     -1                          //sensor pwdn gpio
#define FIRST_POWER_GPIO                    -1                          //sensor power gpio
#define FIRST_SWITCH_GPIO                   GPIO_PA(25)                 //sensor switch gpio
#define FIRST_SENSOR_ID                     0                           //sensor index
#define FIRST_VIDEO_INTERFACE               IMPISP_SENSOR_VI_MIPI_CSI0  //sensor interface type (dvp/csi0/csi1)
#define FIRST_MCLK                          IMPISP_SENSOR_MCLK0         //sensor clk source (mclk0/mclk1/mclk2)
#define FIRST_DEFAULT_BOOT                  0                           //sensor default mode(0/1/2/3/4)
#define CHN0_EN                             1                           //sensor0，output0
#define CHN1_EN                             0                           //sensor0，output1
#define CHN2_EN                             0                           //sensor0, output2
#define FIRST_CROP_EN                       0
#define FIRST_SENSOR_FRAME_RATE_NUM         15
#define FIRST_SENSOR_FRAME_RATE_DEN         1
#define FIRST_SENSOR_WIDTH_SECOND           720
#define FIRST_SENSOR_HEIGHT_SECOND          576
#define FIRST_SENSOR_WIDTH_THIRD            1280
#define FIRST_SENSOR_HEIGHT_THIRD           720
#endif

/************************************ second sensor *************************************************/
#define SECOND_SNESOR_NAME                  "gc2063s1"                  //sensor name (match with snesor driver name)
#define SECOND_I2C_ADDR                     0x37                        //sensor i2c address
#define SECOND_I2C_ADAPTER_ID               1                           //sensor controller number used (0/1/2/3)
#define SECOND_SENSOR_WIDTH                 1920                        //sensor width
#define SECOND_SENSOR_HEIGHT                1080                        //sensor height
#define SECOND_RST_GPIO                     GPIO_PA(21)                 //sensor reset gpio
#define SECOND_PWDN_GPIO                    -1                          //sensor pwdn gpio
#define SECOND_POWER_GPIO                   -1                          //sensor power gpio
#define SECOND_SWITCH_GPIO                  GPIO_PA(26)                 //sensor switch gpio
#define SECOND_SENSOR_ID                    1                           //sensor index
#define SECOND_VIDEO_INTERFACE              IMPISP_SENSOR_VI_MIPI_CSI1  //sensor interface type (dvp/csi0/csi1)
#define SECOND_MCLK                         IMPISP_SENSOR_MCLK1         //sensor clk source (mclk0/mclk1/mclk2)
#define SECOND_DEFAULT_BOOT                 0                           //sensor default mode(0/1/2/3/4)
#define CHN3_EN                             0                           //sensor1，output0
#define CHN4_EN                             0                           //sensor1，output1
#define CHN5_EN                             0                           //sensor1，output2
#define SECOND_CROP_EN                      0
#define SECOND_SENSOR_FRAME_RATE_NUM        15
#define SECOND_SENSOR_FRAME_RATE_DEN        1
#define SECOND_SENSOR_WIDTH_SECOND          720
#define SECOND_SENSOR_HEIGHT_SECOND         576
#define SECOND_SENSOR_WIDTH_THIRD           1280
#define SECOND_SENSOR_HEIGHT_THIRD          720

/************************************ third sensor *************************************************/
#define THIRD_SNESOR_NAME                   "gc2063s2"                  //sensor name (match with snesor driver name)
#define THIRD_I2C_ADDR                      0x3f                        //sensor i2c address
#define THIRD_I2C_ADAPTER_ID                0                           //sensor controller number used (0/1/2/3)
#define THIRD_SENSOR_WIDTH                  1920                        //sensor width
#define THIRD_SENSOR_HEIGHT                 1080                        //sensor height
#define THIRD_RST_GPIO                      -1                          //sensor reset gpio
#define THIRD_PWDN_GPIO                     -1                          //sensor pwdn gpio
#define THIRD_POWER_GPIO                    -1                          //sensor power gpio
#define THIRD_SWITCH_GPIO                  GPIO_PA(25)                  //sensor switch gpio
#define THIRD_SENSOR_ID                     2                           //sensor index
#define THIRD_VIDEO_INTERFACE               IMPISP_SENSOR_VI_MIPI_CSI0  //sensor interface type (dvp/csi0/csi1)
#define THIRD_MCLK                          IMPISP_SENSOR_MCLK0         //sensor clk source (mclk0/mclk1/mclk2)
#define THIRD_DEFAULT_BOOT                  0                           //sensor default mode(0/1/2/3/4)
#define CHN6_EN                             0                           //sensor3，output0
#define CHN7_EN                             0                           //sensor3，output1
#define CHN8_EN                             0                           //sensor3，output2
#define THIRD_CROP_EN                       0
#define THIRD_SENSOR_FRAME_RATE_NUM         15
#define THIRD_SENSOR_FRAME_RATE_DEN         1
#define THIRD_SENSOR_WIDTH_SECOND           720
#define THIRD_SENSOR_HEIGHT_SECOND          576
#define THIRD_SENSOR_WIDTH_THIRD            1280
#define THIRD_SENSOR_HEIGHT_THIRD           720

/************************************ fourth sensor *************************************************/
#define FOURTH_SNESOR_NAME                  "gc2063s3"                  //sensor name (match with snesor driver name)
#define FOURTH_I2C_ADDR                     0x3f                        //sensor i2c address
#define FOURTH_I2C_ADAPTER_ID               1                           //sensor controller number used (0/1/2/3)
#define FOURTH_SENSOR_WIDTH                 1920                        //sensor width
#define FOURTH_SENSOR_HEIGHT                1080                        //sensor height
#define FOURTH_RST_GPIO                     -1                          //sensor reset gpio
#define FOURTH_PWDN_GPIO                    -1                          //sensor pwdn gpio
#define FOURTH_POWER_GPIO                   -1                          //sensor power gpio
#define FOURTH_SWITCH_GPIO                  GPIO_PA(26)                 //sensor switch gpio
#define FOURTH_SENSOR_ID                    3                           //sensor index
#define FOURTH_VIDEO_INTERFACE              IMPISP_SENSOR_VI_MIPI_CSI1  //sensor interface type (dvp/csi0/csi1)
#define FOURTH_MCLK                         IMPISP_SENSOR_MCLK1         //sensor clk source (mclk0/mclk1/mclk2)
#define FOURTH_DEFAULT_BOOT                 0                           //sensor default mode(0/1/2/3/4)
#define CHN9_EN                             0                           //sensor4，output0
#define CHN10_EN                            0                           //sensor4，output1
#define CHN11_EN                            0                           //sensor4，output2
#define FOURTH_CROP_EN                      0
#define FOURTH_SENSOR_FRAME_RATE_NUM        15
#define FOURTH_SENSOR_FRAME_RATE_DEN        1
#define FOURTH_SENSOR_WIDTH_SECOND          720
#define FOURTH_SENSOR_HEIGHT_SECOND         576
#define FOURTH_SENSOR_WIDTH_THIRD           1280
#define FOURTH_SENSOR_HEIGHT_THIRD          720



#define BITRATE_720P_Kbs        1000

#define NR_FRAMES_TO_SAVE       200
#define NR_JPEG_TO_SAVE         20
#define STREAM_BUFFER_SIZE      (1 * 1024 * 1024)

#define ENC_VIDEO_CHANNEL       0
#define ENC_JPEG_CHANNEL        1

#define STREAM_FILE_PATH_PREFIX     "/tmp"
#define SNAP_FILE_PATH_PREFIX       "/tmp"

#define OSD_REGION_WIDTH            16
#define OSD_REGION_HEIGHT           34
#define OSD_REGION_WIDTH_SEC        8
#define OSD_REGION_HEIGHT_SEC       18


#define SLEEP_TIME          5

#define FS_CHN_NUM          12  /* MIN 1, MAX 4 */
#define IVS_CHN_ID          1

#define CHN0_INDEX  0
#define CHN1_INDEX  1
#define CHN2_INDEX  2
#define CHN3_INDEX  3
#define CHN4_INDEX  4
#define CHN5_INDEX  5
#define CHN6_INDEX  6
#define CHN7_INDEX  7
#define CHN8_INDEX  8
#define CHN9_INDEX  9
#define CHN10_INDEX  10
#define CHN11_INDEX  11

#define CHN_ENABLE 1
#define CHN_DISABLE 0

/* #define SUPPORT_RGB555LE */

struct chn_conf{
	unsigned int index; /* 0 for main channel, 1 for second channel */
	unsigned int enable;
	IMPPayloadType  payloadType;
	IMPFSChnAttr fs_chn_attr;
	IMPCell framesource_chn;
	IMPCell imp_encoder;
};

#define  CHN_NUM  ARRAY_SIZE(chn)

int sample_system_init();
int sample_system_exit();

int sample_framesource_init();
int sample_framesource_exit();

int sample_jpeg_init();
int sample_jpeg_exit();
int sample_video_init();
int sample_video_exit(void);

int sample_framesource_streamon();
int sample_framesource_streamoff();

int sample_get_frame();
int sample_start_get_video_stream();
void sample_stop_get_video_stream();
int sample_get_kernvideo_stream();
void sample_stop_kernvideo_stream();
int sample_start_get_jpeg_stream();
void sample_stop_get_jpeg_stream();
int sample_get_video_stream_byfd();

extern int IMP_Encoder_KernEnc_Stop();
extern int IMP_Encoder_KernEnc_GetStream(int encChn, IMPEncoderKernEncOut *encOut);
extern int IMP_Encoder_KernEnc_Release(int encChn);

typedef struct sample_osd_param{
	int chn;
	int *phandles;
	uint32_t *ptimestamps;
}IMP_Sample_OsdParam;

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif /* __SAMPLE_COMMON_H__ */
