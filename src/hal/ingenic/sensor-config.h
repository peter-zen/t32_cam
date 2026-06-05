/*
    sensor-config.h

    传感器相关的配置信息（名称、I2C、分辨率、接口、MCLK、默认启动、通道开关、裁剪、帧率等）。
    从 sample-common.h 中迁移而来，供 sample-common.cpp 与其他模块统一引用。
*/

#ifndef __SENSOR_CONFIG_H__
#define __SENSOR_CONFIG_H__

#include <imp/imp_isp.h>

/* 传感器总数（one/two/thr/four） */
#define SENSOR_NUM                  IMPISP_TOTAL_ONE

/************************************ first sensor *************************************************/
#ifdef SENSOR_TYPE_SC4336P
#define FIRST_SNESOR_NAME                   "sc4336p"
#define FIRST_I2C_ADDR                      0x30
#define FIRST_I2C_ADAPTER_ID                0
#define FIRST_SENSOR_WIDTH                  2560
#define FIRST_SENSOR_HEIGHT                 1440
#define FIRST_RST_GPIO                      GPIO_PA(20)
#define FIRST_PWDN_GPIO                     -1
#define FIRST_POWER_GPIO                    -1
#define FIRST_SWITCH_GPIO                   GPIO_PA(25)
#define FIRST_SENSOR_ID                     0
#define FIRST_VIDEO_INTERFACE               IMPISP_SENSOR_VI_MIPI_CSI0
#define FIRST_MCLK                          IMPISP_SENSOR_MCLK0
#define FIRST_DEFAULT_BOOT                  0
#define CHN0_EN                             1
#define CHN1_EN                             1
#define CHN2_EN                             0
#define FIRST_CROP_EN                       0
#define FIRST_SENSOR_FRAME_RATE_NUM         15
#define FIRST_SENSOR_FRAME_RATE_DEN         1
#define FIRST_SENSOR_WIDTH_SECOND           1280
#define FIRST_SENSOR_HEIGHT_SECOND          720
#define FIRST_SENSOR_WIDTH_THIRD            1280
#define FIRST_SENSOR_HEIGHT_THIRD           720
#elif defined (SENSOR_TYPE_GC4653)
#define FIRST_SNESOR_NAME                   "gc4653"
#define FIRST_I2C_ADDR                      0x29
#define FIRST_I2C_ADAPTER_ID                0
#define FIRST_SENSOR_WIDTH                  2560
#define FIRST_SENSOR_HEIGHT                 1440
#define FIRST_RST_GPIO                      GPIO_PA(20)
#define FIRST_PWDN_GPIO                     GPIO_PA(22)
#define FIRST_POWER_GPIO                    -1
#define FIRST_SWITCH_GPIO                   -1
#define FIRST_SENSOR_ID                     0
#define FIRST_VIDEO_INTERFACE               IMPISP_SENSOR_VI_MIPI_CSI0
#define FIRST_MCLK                          IMPISP_SENSOR_MCLK0
#define FIRST_DEFAULT_BOOT                  0
#define CHN0_EN                             1
#define CHN1_EN                             1
#define CHN2_EN                             1
#define FIRST_CROP_EN                       0
#define FIRST_SENSOR_FRAME_RATE_NUM         30
#define FIRST_SENSOR_FRAME_RATE_DEN         1
#define FIRST_SENSOR_WIDTH_SECOND           1280
#define FIRST_SENSOR_HEIGHT_SECOND          720
#define FIRST_SENSOR_WIDTH_THIRD            320
#define FIRST_SENSOR_HEIGHT_THIRD           180
#else
#define FIRST_SNESOR_NAME                   "gc5613"
#define FIRST_I2C_ADDR                      0x31
#define FIRST_I2C_ADAPTER_ID                0
#define FIRST_SENSOR_WIDTH                  2880
#define FIRST_SENSOR_HEIGHT                 1620
#define FIRST_RST_GPIO                      GPIO_PA(20)
#define FIRST_PWDN_GPIO                     -1
#define FIRST_POWER_GPIO                    -1
#define FIRST_SWITCH_GPIO                   GPIO_PA(25)
#define FIRST_SENSOR_ID                     0
#define FIRST_VIDEO_INTERFACE               IMPISP_SENSOR_VI_MIPI_CSI0
#define FIRST_MCLK                          IMPISP_SENSOR_MCLK0
#define FIRST_DEFAULT_BOOT                  0
#define CHN0_EN                             1
#define CHN1_EN                             0
#define CHN2_EN                             0
#define FIRST_CROP_EN                       0
#define FIRST_SENSOR_FRAME_RATE_NUM         15
#define FIRST_SENSOR_FRAME_RATE_DEN         1
#define FIRST_SENSOR_WIDTH_SECOND           720
#define FIRST_SENSOR_HEIGHT_SECOND          576
#define FIRST_SENSOR_WIDTH_THIRD            1280
#define FIRST_SENSOR_HEIGHT_THIRD           720
#endif

/************************************ second sensor *************************************************/
#define SECOND_SNESOR_NAME                  "gc2063s1"
#define SECOND_I2C_ADDR                     0x37
#define SECOND_I2C_ADAPTER_ID               1
#define SECOND_SENSOR_WIDTH                 1920
#define SECOND_SENSOR_HEIGHT                1080
#define SECOND_RST_GPIO                     GPIO_PA(21)
#define SECOND_PWDN_GPIO                    -1
#define SECOND_POWER_GPIO                   -1
#define SECOND_SWITCH_GPIO                  GPIO_PA(26)
#define SECOND_SENSOR_ID                    1
#define SECOND_VIDEO_INTERFACE              IMPISP_SENSOR_VI_MIPI_CSI1
#define SECOND_MCLK                         IMPISP_SENSOR_MCLK1
#define SECOND_DEFAULT_BOOT                 0
#define CHN3_EN                             0
#define CHN4_EN                             0
#define CHN5_EN                             0
#define SECOND_CROP_EN                      0
#define SECOND_SENSOR_FRAME_RATE_NUM        15
#define SECOND_SENSOR_FRAME_RATE_DEN        1
#define SECOND_SENSOR_WIDTH_SECOND          720
#define SECOND_SENSOR_HEIGHT_SECOND         576
#define SECOND_SENSOR_WIDTH_THIRD           1280
#define SECOND_SENSOR_HEIGHT_THIRD          720

/************************************ third sensor *************************************************/
#define THIRD_SNESOR_NAME                   "gc2063s2"
#define THIRD_I2C_ADDR                      0x3f
#define THIRD_I2C_ADAPTER_ID                0
#define THIRD_SENSOR_WIDTH                  1920
#define THIRD_SENSOR_HEIGHT                 1080
#define THIRD_RST_GPIO                      -1
#define THIRD_PWDN_GPIO                     -1
#define THIRD_POWER_GPIO                    -1
#define THIRD_SWITCH_GPIO                   GPIO_PA(25)
#define THIRD_SENSOR_ID                     2
#define THIRD_VIDEO_INTERFACE               IMPISP_SENSOR_VI_MIPI_CSI0
#define THIRD_MCLK                          IMPISP_SENSOR_MCLK0
#define THIRD_DEFAULT_BOOT                  0
#define CHN6_EN                             0
#define CHN7_EN                             0
#define CHN8_EN                             0
#define THIRD_CROP_EN                       0
#define THIRD_SENSOR_FRAME_RATE_NUM         15
#define THIRD_SENSOR_FRAME_RATE_DEN         1
#define THIRD_SENSOR_WIDTH_SECOND           720
#define THIRD_SENSOR_HEIGHT_SECOND          576
#define THIRD_SENSOR_WIDTH_THIRD            1280
#define THIRD_SENSOR_HEIGHT_THIRD           720

/************************************ fourth sensor *************************************************/
#define FOURTH_SNESOR_NAME                  "gc2063s3"
#define FOURTH_I2C_ADDR                     0x3f
#define FOURTH_I2C_ADAPTER_ID               1
#define FOURTH_SENSOR_WIDTH                 1920
#define FOURTH_SENSOR_HEIGHT                1080
#define FOURTH_RST_GPIO                     -1
#define FOURTH_PWDN_GPIO                    -1
#define FOURTH_POWER_GPIO                   -1
#define FOURTH_SWITCH_GPIO                  GPIO_PA(26)
#define FOURTH_SENSOR_ID                    3
#define FOURTH_VIDEO_INTERFACE              IMPISP_SENSOR_VI_MIPI_CSI1
#define FOURTH_MCLK                         IMPISP_SENSOR_MCLK1
#define FOURTH_DEFAULT_BOOT                 0
#define CHN9_EN                             0
#define CHN10_EN                            0
#define CHN11_EN                            0
#define FOURTH_CROP_EN                      0
#define FOURTH_SENSOR_FRAME_RATE_NUM        15
#define FOURTH_SENSOR_FRAME_RATE_DEN        1
#define FOURTH_SENSOR_WIDTH_SECOND          720
#define FOURTH_SENSOR_HEIGHT_SECOND         576
#define FOURTH_SENSOR_WIDTH_THIRD           1280
#define FOURTH_SENSOR_HEIGHT_THIRD          720

#endif /* __SENSOR_CONFIG_H__ */
