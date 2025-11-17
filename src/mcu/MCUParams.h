#ifndef MCU_PARAMS_H
#define MCU_PARAMS_H

#define PARAM_PACK(start, bytes) ((start) << 8 | (bytes))
#define PARAM_UNPACK_START(param) ((param) >> 8) /*获取参数起始地址*/
#define PARAM_UNPACK_BYTES(param) ((param) & 0xFF) /*获取参数字节数*/

#define PARAM_MCU_WORK_MODE PARAM_PACK(0x0, 0x1) /*工作模式： 0-仅拍照；1-拍照并上传；2-仅上传；3-仅测试；4-UVC*/
#define PARAM_MCU_VERSION PARAM_PACK(0x1, 2) /*MCU版本号. 2 Bytes, 数据类型: Number. e.g:10002表示V10.002*/
#define PARAM_MCU_BATTERY1 PARAM_PACK(0x3, 1) /*电池组1电压值 1 Bytes, 数据类型: Number. 除10后为电压值。e.g:126表示12.6V*/
#define PARAM_MCU_BATTERY2 PARAM_PACK(0x4, 1) /*电池组2电压值 1 Bytes, 数据类型: Number. 除10后为电压值。e.g:126表示12.6V*/
#define PARAM_MCU_EPOWER PARAM_PACK(0x5, 1) /*外部电源电压， 1 Bytes, 数据类型: Number. 除10后为电压值。e.g:126表示12.6V*/
#define PARAM_MCU_SPOWER PARAM_PACK(0x6, 1) /*太阳能板电压， 1 Bytes, 数据类型: Number. 除10后为电压值。e.g:126表示12.6V*/
#define PARAM_MCU_TEMPER PARAM_PACK(0x7, 1) /*温度值， 1 Bytes, 数据类型: Number. 减125后为温度值。e.g:125表示0℃*/
#define PARAM_MCU_RHS PARAM_PACK(0x8, 1) /*湿度值， 1 Bytes, 数据类型: Number. 单位%。e.g:25表示25%*/
#define PARAM_MCU_APS PARAM_PACK(0x9, 2) /*气压值， 2 Bytes, 数据类型: Number. 单位hpa。e.g:1000表示100hpa*/
#define PARAM_MCU_GPSA PARAM_PACK(0xB, 4) /*GPS定位值-维度， 4 Bytes, 数据类型: Number. 维度缩放为整数，需除10^7后为实际维度值。e.g:40430000表示40.43度*/
#define PARAM_MCU_GPSL PARAM_PACK(0xF, 4) /*GPS定位值-经度， 4 Bytes, 数据类型: Number. 经度缩放为整数，需除10^7后为实际经度值。e.g:114123000表示114.123度*/
#define PARAM_MCU_GPSH PARAM_PACK(0x13, 2) /*GPS定位值-高度， 2 Bytes, 数据类型: Number. 高度缩放为整数，需除10后为实际高度值。单位m。e.g:100表示10m*/
#define PARAM_MCU_PID PARAM_PACK(0x15, 32) /*设备PID. 32 Bytes, 数据类型: String. e.g:C154E001M4500046*/
#define PARAM_MCU_UPID PARAM_PACK(0x35, 32) /*通讯设备PID（SSID）. 32 Bytes, 数据类型: String. e.g:CKVISON-2.4G*/
#define PARAM_MCU_UPWD PARAM_PACK(0x55, 64) /*通讯设备密码（Password）. 64 Bytes, 数据类型: String.*/
#define PARAM_MCU_YEAR PARAM_PACK(0x95, 2) /*RTC时钟-年. 2 Bytes, 数据类型: Number. e.g:2025表示2025年*/
#define PARAM_MCU_MONTH PARAM_PACK(0x97, 1) /*RTC时钟-月. 1 Bytes, 数据类型: Number. e.g:1表示1月*/
#define PARAM_MCU_DAY PARAM_PACK(0x98, 1) /*RTC时钟-日. 1 Bytes, 数据类型: Number. e.g:1表示1日*/
#define PARAM_MCU_HOUR PARAM_PACK(0x99, 1) /*RTC时钟-时. 1 Bytes, 数据类型: Number. e.g:1表示1时*/
#define PARAM_MCU_MINUTE PARAM_PACK(0x9A, 1) /*RTC时钟-分. 1 Bytes, 数据类型: Number. e.g:1表示1分*/
#define PARAM_MCU_SECOND PARAM_PACK(0x9B, 1) /*RTC时钟-秒. 1 Bytes, 数据类型: Number. e.g:1表示1秒*/
#endif // MCU_PARAMS_H