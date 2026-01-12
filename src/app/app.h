#ifndef APP_H
#define APP_H
#ifdef __cplusplus
extern "C" {
#endif
#include "Common.h"
#include "GPIO.h"

#define QUICK_SNAP_DIR   "/tmp/quick_snap/"
#define QUICK_SNAP_INFO_FILE   QUICK_SNAP_DIR"info.json"
#define SD_CARD_PATH   "/mnt/sdcard/"
#define MEDIA_TARGET_PATH   SD_CARD_PATH"media/"
#define MEDIA_UPLOAD_PATH   SD_CARD_PATH"media/upload/"
#if ALL_MEDIA_FILE_IN_ONE_FOLDER
#define MEDIA_STORE_FOLDER_PATH   MEDIA_TARGET_PATH"file/"
#endif
#define WIFI_IFNAME "wlan0"
#define ETH_IFNAME "eth0"
#define USB_DONGLE_IFNAME "usb0"
#define ENV_FILE_PATHNAME "/config/htc/env.ini"
//#define ENV_FILE_PATHNAME "/mnt/sdcard/res/env.ini"
#define CONFIG_FILE_PATHNAME "/config/htc/config.ini"
#define UPDATE_CONFIG_FILE_PATHNAME SD_CARD_PATH"update_config.ini"
#ifdef __cplusplus
}
#endif
#endif /* APP_H */
