#! /usr/env bash

sudo cp -rf ~/t32/app/build/bin/  ~/huntcam/
sudo cp -rf ~/t32/app/build/lib/  ~/huntcam/

#bin
sudo rm -rf ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/rootfs_camera/usr/bin/htc/*
sudo cp -rf ~/t32/app/build/bin/htc_media_app  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/rootfs_camera/usr/bin/htc/
sudo cp -rf ~/t32/app/build/bin/htc_main_app  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/system/bin/htc/
sudo cp -rf ~/t32/app/build/bin/htc_daemon_app  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/system/bin/htc/
sudo cp -rf ~/t32/app/build/bin/wpa_conn  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/wifi/rtl8189fs/bin/

#res
sudo rm -rf ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/rootfs_camera/config_bak/htc/*
sudo cp -rf ~/t32/app/res/config.ini  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/rootfs_camera/config_bak/htc/
sudo cp -rf ~/t32/app/res/env.ini  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/rootfs_camera/config_bak/htc/
sudo cp -rf ~/t32/app/res/setting.json  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/rootfs_camera/config_bak/htc/

#lib
##/usr/lib/htc
sudo rm -rf ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/rootfs_camera/lib/htc/*
sudo cp -rf ~/t32/app/build/lib/libmedia_common.so  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/rootfs_camera/lib/htc/
sudo cp -rf ~/t32/app/build/lib/libmedia_snap.so  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/rootfs_camera/lib/htc/
sudo cp -rf ~/t32/app/build/lib/libjsoncpp.so  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/rootfs_camera/lib/htc/
sudo cp -rf ~/t32/app/build/lib/liblogger.so  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/rootfs_camera/lib/htc/
sudo cp -rf ~/t32/app/build/lib/libsetting.so  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/rootfs_camera/lib/htc/
sudo cp -rf ~/t32/app/build/lib/libenv.so  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/rootfs_camera/lib/htc/
sudo cp -rf ~/t32/app/build/lib/libworkmode.so  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/rootfs_camera/lib/htc/
sudo cp -rf ~/t32/app/build/lib/libgpio.so  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/rootfs_camera/lib/htc/
sudo cp -rf ~/t32/app/build/lib/libdaynight.so  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/rootfs_camera/lib/htc/
sudo cp -rf ~/t32/app/build/lib/libtime.so  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/rootfs_camera/lib/htc/
sudo cp -rf ~/t32/app/build/lib/libtimezone.so  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/rootfs_camera/lib/htc/
sudo cp -rf ~/t32/app/build/lib/libdevconf.so  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/rootfs_camera/lib/htc/
sudo cp -rf ~/t32/app/build/lib/libmcu.so  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/rootfs_camera/lib/htc/

##/system/lib/htc
sudo rm -rf ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/system/lib/htc/*
sudo cp -rf ~/t32/app/build/lib/libcommon.so  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/system/lib/htc/
sudo cp -rf ~/t32/app/build/lib/libdaemon.so  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/system/lib/htc/
sudo cp -rf ~/t32/app/build/lib/libmisc.so  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/system/lib/htc/
sudo cp -rf ~/t32/app/build/lib/libbase64.so  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/system/lib/htc/
sudo cp -rf ~/t32/app/build/lib/libcrc16.so  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/system/lib/htc/
sudo cp -rf ~/t32/app/build/lib/libdisk.so  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/system/lib/htc/
sudo cp -rf ~/t32/app/build/lib/libmd5.so  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/system/lib/htc/
sudo cp -rf ~/t32/app/build/lib/libmedia_recorder.so  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/system/lib/htc/
sudo cp -rf ~/t32/app/build/lib/libaudio_recorder.so  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/system/lib/htc/
sudo cp -rf ~/t32/app/build/lib/libmedia_rtsp.so  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/system/lib/htc/
sudo cp -rf ~/t32/app/build/lib/libminimp4.so  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/system/lib/htc/
sudo cp -rf ~/t32/app/build/lib/libnetwork.so  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/system/lib/htc/
sudo cp -rf ~/t32/app/build/lib/libpower.so  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/system/lib/htc/
sudo cp -rf ~/t32/app/build/lib/libevent.so  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/system/lib/htc/
sudo cp -rf ~/t32/app/build/lib/libevent_*  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/system/lib/htc/
sudo cp -rf ~/t32/app/build/lib/libevent-*  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/system/lib/htc/
sudo cp -rf ~/t32/app/build/lib/libsmolrtsp.so  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/system/lib/htc/
sudo cp -rf ~/t32/app/build/lib/libsmolrtsp-libevent.so  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/system/lib/htc/
sudo cp -rf ~/t32/app/build/lib/libfaac.so  ~/t32/bsp/board/Ingenic-SDK-T32/resource/rootfs/zeratul/5.4.0/uclibc/system/lib/htc/