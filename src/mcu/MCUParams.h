#ifndef MCU_PARAMS_H
#define MCU_PARAMS_H

#define PARAM_PACK(start, bytes) ((start) << 8 | (bytes))
#define PARAM_UNPACK_START(param) ((param) >> 8) /*获取参数起始地址*/
#define PARAM_UNPACK_BYTES(param) ((param) & 0xFF) /*获取参数字节数*/

#define PARAM_WORK_MODE PARAM_PACK(0x0, 0x1) /*工作模式*/
#define PARAM_MCU_VERSION PARAM_PACK(0x0, 2) /*MCU版本号. 2 Bytes, 数据类型: Number. e.g:10002表示V10.002*/
#define PARAM_MCU_PID PARAM_PACK(0x2, 24) /*设备PID. 24 Bytes, 数据类型: String. e.g:C154E001M4500046*/
#define PARAM_MCU_UPID PARAM_PACK(0x1A, 32) /*通讯设备PID（SSID）. 32 Bytes, 数据类型: String. e.g:CKVISON-2.4G*/
#define PARAM_MCU_UPWD PARAM_PACK(0x3A, 64) /*通讯设备密码（Password）. 64 Bytes, 数据类型: String.*/
#define PARAM_MCU_BATTERY1 PARAM_PACK(0x7A, 1) /*电池组1电压值 1 Bytes, 数据类型: Number. 除10后为电压值。e.g:126表示12.6V*/
#define PARAM_MCU_BATTERY2 PARAM_PACK(0x7B, 1) /*电池组2电压值 1 Bytes, 数据类型: Number. 除10后为电压值。e.g:126表示12.6V*/
#define PARAM_SPOWER PARAM_PACK(0x7C, 1) /*太阳能板电压， 1 Bytes, 数据类型: Number. 除10后为电压值。e.g:126表示12.6V*/

#endif // MCU_PARAMS_H