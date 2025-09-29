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
#define NETIF_NAME "wlan0"
#define ENV_FILE_PATHNAME "/config/htc/env.ini"
//#define ENV_FILE_PATHNAME "/mnt/sdcard/res/env.ini"

#ifdef __cplusplus
}
#endif
#endif /* APP_H */
