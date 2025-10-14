#! /usr/env bash

sudo cp -rf ~/t32/t32/build/bin/  ~/huntcam/
sudo cp -rf ~/t32/t32/build/lib/  ~/huntcam/

#bin
sudo rm -rf ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/rootfs_camera/usr/bin/htc/*
sudo cp -rf ~/t32/t32/build/bin/htc_media_app  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/rootfs_camera/usr/bin/htc/
sudo cp -rf ~/t32/t32/build/bin/htc_main_app  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/system/bin/htc/
sudo cp -rf ~/t32/t32/build/bin/htc_daemon_app  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/system/bin/htc/
sudo cp -rf ~/t32/t32/build/bin/wpa_conn  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/wifi/rtl8189fs/bin/

#res
sudo rm -rf ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/rootfs_camera/config_bak/htc/*
sudo cp -rf ~/t32/t32/res/config.ini  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/rootfs_camera/config_bak/htc/
sudo cp -rf ~/t32/t32/res/env.ini  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/rootfs_camera/config_bak/htc/
sudo cp -rf ~/t32/t32/res/setting.json  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/rootfs_camera/config_bak/htc/

#lib
##/usr/lib/htc
sudo rm -rf ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/rootfs_camera/lib/htc/*
sudo cp -rf ~/t32/t32/build/lib/libmedia_common.so  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/rootfs_camera/lib/htc/
sudo cp -rf ~/t32/t32/build/lib/libmedia_snap.so  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/rootfs_camera/lib/htc/
sudo cp -rf ~/t32/t32/build/lib/libjsoncpp.so  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/rootfs_camera/lib/htc/
sudo cp -rf ~/t32/t32/build/lib/liblogger.so  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/rootfs_camera/lib/htc/
sudo cp -rf ~/t32/t32/build/lib/libsetting.so  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/rootfs_camera/lib/htc/
sudo cp -rf ~/t32/t32/build/lib/libenv.so  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/rootfs_camera/lib/htc/
sudo cp -rf ~/t32/t32/build/lib/libworkmode.so  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/rootfs_camera/lib/htc/
sudo cp -rf ~/t32/t32/build/lib/libgpio.so  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/rootfs_camera/lib/htc/
sudo cp -rf ~/t32/t32/build/lib/libdaynight.so  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/rootfs_camera/lib/htc/
sudo cp -rf ~/t32/t32/build/lib/libtime.so  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/rootfs_camera/lib/htc/
sudo cp -rf ~/t32/t32/build/lib/libdevconf.so  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/rootfs_camera/lib/htc/

##/system/lib/htc
sudo rm -rf ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/system/lib/htc/*
sudo cp -rf ~/t32/t32/build/lib/libcommon.so  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/system/lib/htc/
sudo cp -rf ~/t32/t32/build/lib/libdaemon.so  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/system/lib/htc/
sudo cp -rf ~/t32/t32/build/lib/libmisc.so  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/system/lib/htc/
sudo cp -rf ~/t32/t32/build/lib/libbase64.so  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/system/lib/htc/
sudo cp -rf ~/t32/t32/build/lib/libcrc16.so  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/system/lib/htc/
sudo cp -rf ~/t32/t32/build/lib/libdisk.so  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/system/lib/htc/
sudo cp -rf ~/t32/t32/build/lib/libmcu.so  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/system/lib/htc/
sudo cp -rf ~/t32/t32/build/lib/libmd5.so  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/system/lib/htc/
sudo cp -rf ~/t32/t32/build/lib/libmedia_recorder.so  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/system/lib/htc/
sudo cp -rf ~/t32/t32/build/lib/libmedia_rtsp.so  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/system/lib/htc/
sudo cp -rf ~/t32/t32/build/lib/libminimp4.so  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/system/lib/htc/
sudo cp -rf ~/t32/t32/build/lib/libnetwork.so  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/system/lib/htc/
sudo cp -rf ~/t32/t32/build/lib/libpower.so  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/system/lib/htc/
sudo cp -rf ~/t32/t32/build/lib/libevent.so  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/system/lib/htc/
sudo cp -rf ~/t32/t32/build/lib/libevent_*  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/system/lib/htc/
sudo cp -rf ~/t32/t32/build/lib/libevent-*  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/system/lib/htc/
sudo cp -rf ~/t32/t32/build/lib/libsmolrtsp.so  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/system/lib/htc/
sudo cp -rf ~/t32/t32/build/lib/libsmolrtsp-libevent.so  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/system/lib/htc/