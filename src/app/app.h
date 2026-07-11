#ifndef APP_H
#define APP_H
#include "Common.h"
#include "GPIO.h"

#define QUICK_SNAP_DIR   "/tmp/media/"
#define QUICK_SNAP_INFO_FILE   QUICK_SNAP_DIR"info.json"

#ifdef BUILD_FOR_SIMULATION
// PC模拟模式: 使用相对路径
#define SD_CARD_PATH   "./sim_sdcard_runtime/"
#define CONFIG_FILE_PATHNAME "./res/system.sim.json"
#define PRODUCT_FILE_PATHNAME "./res/product.sim.json"
#define NETIF_NAME "eth0"
#else
// 真机模式: 使用绝对路径
#define SD_CARD_PATH   "/mnt/sdcard/"
#define CONFIG_FILE_PATHNAME "/config/htc/system.json"
#define PRODUCT_FILE_PATHNAME "/config/htc/product.json"
#define NETIF_NAME "wlan0"
#endif

#define MEDIA_TARGET_PATH   SD_CARD_PATH"media/"
#define MEDIA_UPLOAD_PATH   SD_CARD_PATH"media/upload/"
#if ALL_MEDIA_FILE_IN_ONE_FOLDER
#define MEDIA_STORE_FOLDER_PATH   MEDIA_TARGET_PATH"file/"
#endif
#define WIFI_IFNAME "wlan0"
#define ETH_IFNAME "eth0"
#define USB_DONGLE_IFNAME "usb0"
#endif /* APP_H */
