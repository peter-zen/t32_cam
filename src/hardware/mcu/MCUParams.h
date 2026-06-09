#ifndef MCU_PARAMS_H
#define MCU_PARAMS_H

#define PARAM_PACK(start, bytes) ((start) << 8 | (bytes))
#define PARAM_UNPACK_START(param) ((param) >> 8) /*获取参数起始地址*/
#define PARAM_UNPACK_BYTES(param) ((param) & 0xFF) /*获取参数字节数*/

/*系统核心 (0x0000-0x000F)*/
#define PARAM_MCU_MODE PARAM_PACK(0x0, 1) /*启动模式. 1 Byte. 0:初始值, 1:单拍模式, 2:拍传模式, 3:传输模式, 4:测试模式*/
#define PARAM_MCU_FWORK_MARK PARAM_PACK(0x1, 1) /*首次启动标记. 1 Byte. 0~1*/
#define PARAM_MCU_EVENT_STATUS PARAM_PACK(0x2, 1) /*事件类型. 1 Byte. 参见《云平台接入协议》*/
#define PARAM_MCU_PType PARAM_PACK(0x3, 1) /*网络类型. 1 Byte. 0~10*/
#define PARAM_MCU_VERSION PARAM_PACK(0x4, 2) /*MCU版本号. 2 Bytes. 如10002表示V10.002*/

/*基础信息 / 运行时同步 (0x0010-0x00FF)*/
#define PARAM_MCU_BAT1 PARAM_PACK(0x10, 1) /*电池组1电压. 1 Byte. 除10后为电压值。例：126表示12.6V*/
#define PARAM_MCU_BAT2 PARAM_PACK(0x11, 1) /*电池组2电压. 1 Byte. 除10后为电压值。例：126表示12.6V*/
#define PARAM_MCU_EPWR PARAM_PACK(0x12, 1) /*外部电源电压. 1 Byte. 除10后为电压值。例：216表示21.6V*/
#define PARAM_MCU_SPWR PARAM_PACK(0x13, 1) /*太阳能板电压. 1 Byte. 除10后为电压值。例：216表示21.6V*/
#define PARAM_MCU_LOCTION_LON PARAM_PACK(0x14, 4) /*GPS经度. 4 Bytes. 除10^7，东经为+，西经为-*/
#define PARAM_MCU_LOCTION_LAT PARAM_PACK(0x18, 4) /*GPS纬度. 4 Bytes. 除10^7，北纬为+，南纬为-*/
#define PARAM_MCU_LOCTION_ELE PARAM_PACK(0x1C, 2) /*GPS高程. 2 Bytes. 除10，单位m*/
#define PARAM_MCU_PID PARAM_PACK(0x1E, 32) /*设备PID. 32 Bytes String.*/
#define PARAM_MCU_UWS PARAM_PACK(0x3E, 1) /*唤醒通信设备标记. 1 Byte. 0:无, 1:433, 2:LoRa*/
#define PARAM_MCU_UPID PARAM_PACK(0x3F, 32) /*通信设备PID(SSID). 32 Bytes String.*/
#define PARAM_MCU_UPWD PARAM_PACK(0x5F, 64) /*通信设备密码. 64 Bytes String.*/
#define PARAM_MCU_YEAR PARAM_PACK(0x9F, 2) /*RTC时钟-年. 2 Bytes*/
#define PARAM_MCU_MONTH PARAM_PACK(0xA1, 1) /*RTC时钟-月. 1 Byte*/
#define PARAM_MCU_DAY PARAM_PACK(0xA2, 1) /*RTC时钟-日. 1 Byte*/
#define PARAM_MCU_HOUR PARAM_PACK(0xA3, 1) /*RTC时钟-时. 1 Byte*/
#define PARAM_MCU_MINUTE PARAM_PACK(0xA4, 1) /*RTC时钟-分. 1 Byte*/
#define PARAM_MCU_SECOND PARAM_PACK(0xA5, 1) /*RTC时钟-秒. 1 Byte*/
#define PARAM_MCU_NUFQ PARAM_PACK(0xA6, 2) /*未上传数量. 2 Bytes. 0~65535*/
#define PARAM_MCU_TIMEOUT PARAM_PACK(0xA8, 2) /*关机倒计时. 2 Bytes. 0~120秒, 200重置*/
#define PARAM_MCU_CAM_STATUS PARAM_PACK(0xAA, 1) /*相机状态. 1 Byte. 0:已关, 1:已开机, 2:连网成功, 3:连服务器成功, 4:上传中*/
#define PARAM_MCU_AI_ALARM PARAM_PACK(0xAB, 1) /*AI预警码. 1 Byte. 0:无效*/

/*信号遥测 (0x0100-0x01FF)*/
#define PARAM_MCU_SIG_TYPE PARAM_PACK(0x100, 12) /*信号制式. 12 Bytes String.*/
#define PARAM_MCU_SIG_CF PARAM_PACK(0x10C, 2) /*中心频率或频段. 2 Bytes. 0:无, 1~99:频段, 100~6000:频率(Mhz)*/
#define PARAM_MCU_SIG_TP PARAM_PACK(0x10E, 1) /*发射功率. 1 Byte. 0~50 dBm*/
#define PARAM_MCU_SIG_RSSI PARAM_PACK(0x10F, 1) /*RSSI. 1 Byte. -200~40 dBm*/
#define PARAM_MCU_SIG_RSRP PARAM_PACK(0x110, 1) /*RSRP. 1 Byte. -200~40 dBm*/
#define PARAM_MCU_SIG_RSRQ PARAM_PACK(0x111, 1) /*RSRQ. 1 Byte. -200~40 dBm*/
#define PARAM_MCU_SIG_RL PARAM_PACK(0x112, 2) /*路损. 2 Bytes. 0~32767 dBm*/
#define PARAM_MCU_SIG_SNR PARAM_PACK(0x114, 1) /*SNR. 1 Byte. -50~50*/
#define PARAM_MCU_SIG_TD PARAM_PACK(0x115, 2) /*传输距离. 2 Bytes. 0~65535 米*/

/*传感器遥测 (0x0200-0x02FF)*/
#define PARAM_MCU_CDS_DN PARAM_PACK(0x200, 1) /*日夜模式. 1 Byte. 0:夜晚, 1:白天*/
#define PARAM_MCU_CDS_VALUE PARAM_PACK(0x201, 1) /*CDS电压值. 1 Byte. 除10后为电压值*/
#define PARAM_MCU_VTS_ALARM PARAM_PACK(0x202, 1) /*振动报警. 1 Byte. 0:无振动, 1:振动报警*/
#define PARAM_MCU_VTS_SENS PARAM_PACK(0x203, 1) /*振动灵敏度. 1 Byte. 0:无, 1:低, 2:中, 3:高*/
#define PARAM_MCU_SOR_TEMPS PARAM_PACK(0x204, 1) /*温度. 1 Byte. 单位℃, 255表示无*/
#define PARAM_MCU_SOR_RHS PARAM_PACK(0x205, 1) /*湿度. 1 Byte. 单位%, 255表示无*/
#define PARAM_MCU_SOR_APS PARAM_PACK(0x206, 2) /*气压. 2 Bytes. 单位hPa, 0表示无*/
#define PARAM_MCU_SOR_CO PARAM_PACK(0x208, 2) /*一氧化碳. 2 Bytes. 单位ppm, 0表示无*/
#define PARAM_MCU_SOR_CO2 PARAM_PACK(0x20A, 2) /*二氧化碳. 2 Bytes. 单位ppm, 0表示无*/
#define PARAM_MCU_SOR_O2 PARAM_PACK(0x20C, 1) /*氧气. 1 Byte. 单位%, 0表示无*/
#define PARAM_MCU_SOR_AL PARAM_PACK(0x20D, 2) /*可见光强度. 2 Bytes. 单位lx, 65535表示无*/
#define PARAM_MCU_SOR_UVL PARAM_PACK(0x20F, 1) /*紫外光强度. 1 Byte. 0~15, 255表示无*/
#define PARAM_MCU_SOR_NOISE PARAM_PACK(0x210, 1) /*噪音. 1 Byte. 单位dBA, 0表示无*/

/*外扩无线设备 / 探测器数据 (0x0300-0x03FF)*/
#define PARAM_MCU_ESOR_WS PARAM_PACK(0x300, 1) /*唤醒外扩设备标记. 1 Byte. 0:无, 1:433, 2:LoRa*/
#define PARAM_MCU_ESOR_WID PARAM_PACK(0x301, 2) /*唤醒设备PID. 2 Bytes. 0表示无*/
#define PARAM_MCU_ESOR_ADD PARAM_PACK(0x303, 2) /*探测器通信码. 2 Bytes. 0表示无*/
#define PARAM_MCU_ESOR_ID PARAM_PACK(0x305, 2) /*探测器ID. 2 Bytes. 0表示无*/
#define PARAM_MCU_ESOR_TYPE PARAM_PACK(0x307, 1) /*探测器类型. 1 Byte. 0:无, 1~100:输入型, 101~200:输出型*/
#define PARAM_MCU_ESOR_BAT PARAM_PACK(0x308, 1) /*电池电压. 1 Byte. 除10后为电压值*/
#define PARAM_MCU_ESOR_GPSA PARAM_PACK(0x309, 4) /*坐标信息-纬度. 4 Bytes. 除10^7*/
#define PARAM_MCU_ESOR_GPSL PARAM_PACK(0x30D, 4) /*坐标信息-经度. 4 Bytes. 除10^7*/
#define PARAM_MCU_ESOR_GPSH PARAM_PACK(0x311, 2) /*坐标信息-高度. 2 Bytes. 除10*/
#define PARAM_MCU_ESOR_VALUE PARAM_PACK(0x313, 4) /*探测器值. 4 Bytes. 单位根据类型不同*/

/*设置 / 策略 (0x0400-0x04FF)*/
#define PARAM_MCU_CAM_MAXS PARAM_PACK(0x400, 1) /*每日拍摄次数限制. 1 Byte. 0:不限制, 1~255*/
#define PARAM_MCU_PIR_MODE PARAM_PACK(0x401, 1) /*PIR模式. 1 Byte. 0:关闭, 1:主副同触发, 2:主探测*/
#define PARAM_MCU_PIR_SENS PARAM_PACK(0x402, 1) /*PIR灵敏度. 1 Byte. 0:自动, 1:低, 2:中, 3:高*/
#define PARAM_MCU_PIR_INT PARAM_PACK(0x403, 2) /*PIR触发间隔. 2 Bytes. 单位秒*/
#define PARAM_MCU_TIMER PARAM_PACK(0x405, 1) /*定时开关. 1 Byte. 0:关闭, 1:开启*/
#define PARAM_MCU_PIR_EN PARAM_PACK(0x406, 1) /*PIR开关. 1 Byte. 0:关闭, 1:开启*/
#define PARAM_MCU_TIMER_INT PARAM_PACK(0x407, 2) /*循环拍摄间隔. 2 Bytes. 单位分钟*/
#define PARAM_MCU_TIMER_1START PARAM_PACK(0x409, 2) /*时段1开始. 2 Bytes. 单位分钟*/
#define PARAM_MCU_TIMER_1END PARAM_PACK(0x40B, 2) /*时段1结束. 2 Bytes. 单位分钟*/
#define PARAM_MCU_TIMER_2START PARAM_PACK(0x40D, 2) /*时段2开始. 2 Bytes. 单位分钟*/
#define PARAM_MCU_TIMER_2END PARAM_PACK(0x40F, 2) /*时段2结束. 2 Bytes. 单位分钟*/
#define PARAM_MCU_TIMER_3START PARAM_PACK(0x411, 2) /*时段3开始. 2 Bytes. 单位分钟*/
#define PARAM_MCU_TIMER_3END PARAM_PACK(0x413, 2) /*时段3结束. 2 Bytes. 单位分钟*/
#define PARAM_MCU_TIMER_4START PARAM_PACK(0x415, 2) /*时段4开始. 2 Bytes. 单位分钟*/
#define PARAM_MCU_TIMER_4END PARAM_PACK(0x417, 2) /*时段4结束. 2 Bytes. 单位分钟*/
#define PARAM_MCU_TIMER_5START PARAM_PACK(0x419, 2) /*时段5开始. 2 Bytes. 单位分钟*/
#define PARAM_MCU_TIMER_5END PARAM_PACK(0x41B, 2) /*时段5结束. 2 Bytes. 单位分钟*/
#define PARAM_MCU_TIMER_REPEATS PARAM_PACK(0x41D, 1) /*重复. 1 Byte. 0000000~1111111, 日/六/五/四/三/二/一*/
#define PARAM_MCU_DEVICE_NAME PARAM_PACK(0x41E, 24) /*设备名称. 24 Bytes String.*/
#define PARAM_MCU_HEARTRATE PARAM_PACK(0x436, 3) /*状态上报周期. 3 Bytes. 单位秒, 0无心跳*/
#define PARAM_MCU_UP_MODE PARAM_PACK(0x439, 1) /*文件上传模式. 1 Byte. 0:无, 1:实时, 2:心跳周期, 3:累计*/
#define PARAM_MCU_UP_NUFQ PARAM_PACK(0x43A, 1) /*累计数量阈值. 1 Byte.*/
#define PARAM_MCU_TDS_CF PARAM_PACK(0x43B, 2) /*发射频率设置. 2 Bytes.*/
#define PARAM_MCU_TDS_TP PARAM_PACK(0x43D, 1) /*发射功率设置. 1 Byte.*/
#define PARAM_MCU_TDS_BW PARAM_PACK(0x43E, 1) /*带宽设置. 1 Byte.*/

#endif // MCU_PARAMS_H
