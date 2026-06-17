# T2 — Bug Report (raw)

## 现象(用户描述)

第二次启动 `htc_main_app -m --force-day`,日志**停在** x264 encoder 初始化行
(`i264e[info]: profile Main, level 3.1`),**没有**出现第一次启动里有的
"RTSP 监听端口" 日志。第一次启动是正常的(能跑到监听端口)。

## 关键线索

1. **第二次启动才复现** —— 强烈指向硬件/全局资源在第一次进程退出时没被正确释放,
   第二次进程重新 init HAL/编码器时被阻塞或失败。
2. WiFi 相关 `insmod ... File exists`、`ctrl_iface exists and seems to be in use`
   是第一次启动残留,非本次卡死点(卡死点在 RTSP/编码器初始化之后)。
3. 日志最后几行顺序:
   - `RTSP initialize: BUILD_FOR_SIMULATION=OFF ...` (RtspServer.cpp:179)
   - `OSDController: setPoolSize(2) done`
   - `IspOsdManager: init done ...` (IspOsdManager.cpp:28)
   - `IngenicVideo VTS corrected to 0x0690 (1680)`
   - `rtsp config: sensor=0 stream=1 ...` (RtspServer.cpp:457)
   - `i264e[info]: profile Main, level 3.1` ← **x264 编码器初始化,日志到此中断**
   - (期望但缺失)RTSP server 开始监听端口的日志

## 完整日志(第二次启动)

```
[root@Zeratul:huntcam]# ./bin/htc_main_app -m --force-day
I/elog            [1970-01-01 01:07:03.631] EasyLogger V2.2.99 is initialize success.
I/LEGACY          [1970-01-01 01:07:03.632] Log File: /mnt/sdcard/logs/app.log
I/LEGACY          [1970-01-01 01:07:03.639] program type 1
I/Scanner         [1970-01-01 01:07:03.640] Start scanning mode=pending_thumb media=/mnt/sdcard/DCIM pending_thumb=/mnt/sdcard/data/thumb_pending
I/Scanner         [1970-01-01 01:07:03.646] Scan finished mode=pending_thumb
exec: mount | grep -q /dev/mmcblk0p1
I/LEGACY          [1970-01-01 01:07:03.671] SD card already mounted, skip
I/LEGACY          [1970-01-01 01:07:03.672] Set timezone to UTC+8
I/LEGACY          [1970-01-01 09:07:03.673] CMD_MOBILE: force DAY mode from --force-day
exec: insmod /system/bin/wifi/8189fs.ko
insmod: can't insert '/system/bin/wifi/8189fs.ko': File exists
I/LEGACY          [1970-01-01 09:07:05.993] Connecting to WiFi: CKV
exec: /system/bin/wifi/wpa_conn wlan0 CKV ckvison6688 15 1
exec: mkdir -p /config/profiles
exec: /system/bin/wifi/wpa_passphrase CKV ckvison6688 > /config/profiles/wpa_supplicant.conf
WiFi configuration generated successfully
Interface wlan0 found after 0 seconds
exec: ifconfig wlan0 up
exec: /system/bin/wifi/wpa_supplicant -Dnl80211,wext -i wlan0 -c /config/profiles/wpa_supplicant.conf -C /tmp/wpa_supplicant &
WiFi connection successfully initiated
Waiting for WiFi connection to be established...
exec: /system/bin/wifi/wpa_cli -i "wlan0" -p /tmp/wpa_supplicant status 2>/dev/null
Successfully initialized wpa_supplicant
WiFi successfully connected
exec: /system/bin/wifi/wpa_cli -i "wlan0" -p /tmp/wpa_supplicant status 2>/dev/null
SystemCall_Dbus_ReadWrite_Thread 520 read socket data failed exit this thread, ret:0 errno:0 (Success)
SystemCall_Dbus_ReadWrite_Thread 521 maybe client is close
I/LEGACY          [1970-01-01 09:07:08.864] Connecting to WiFi: CKV done
exec: udhcpc -i wlan0 -t 10
udhcpc (v1.22.1) started
ctrl_iface exists and seems to be in use - cannot override it
Delete '/tmp/wpa_supplicant/wlan0' manually if it is not used anymore
Failed to initialize control interface '/tmp/wpa_supplicant'.
You may have another wpa_supplicant process already running or the file was
left by an unclean termination of wpa_supplicant in which case you will need to
manually remove this file before starting wpa_supplicant again.

nl80211: deinit ifname=wlan0 disabled_11b_rates=0
Sending discover...
Sending select for 192.168.0.102...
Lease of 192.168.0.102 obtained, lease time 43200
deleting routers
adding dns 192.168.0.246
I/MDNS            [1970-01-01 09:07:10.086] Started mDNS service _t32cam._tcp.local on wlan0 (192.168.0.102)
[HTTP] Initialized with port=80, threads=2
[HTTP] CivetWeb library initialized with features: 0x0
I/HttpApiV1       [1970-01-01 09:07:10.088] Registering V1 APIs
[HTTP] Server started on port 80
I/EVENT           [1970-01-01 09:07:10.089] tcp event server started on port 5000
I/MDNS            [1970-01-01 09:07:10.089] HTTP server started on port 80 for interface wlan0 (192.168.0.102)
I/LEGACY          [1970-01-01 09:07:10.434] RTSP initialize: BUILD_FOR_SIMULATION=OFF (HAL provider expected: IngenicVideo/IngenicAudio)
I/LEGACY          [1970-01-01 09:07:10.481] OSDController: setPoolSize(2) done
I/LEGACY          [1970-01-01 09:07:10.657] IspOsdManager: init done, region will be created on stream start
I/LEGACY          [1970-01-01 09:07:10.658] IngenicVideo VTS corrected to 0x0690 (1680)
I/LEGACY          [1970-01-01 09:07:10.658] rtsp config: sensor=0 stream=1 requested_size=1280x720 requested_fps=30/1 codec=0 rc=1
i264e[info]: profile Main, level 3.1
```
