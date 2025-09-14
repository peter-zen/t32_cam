#ifndef __APP_H__
#define __APP_H__
#ifdef __cplusplus
extern "C" {
#endif

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
#endif /* __APP_H__ */