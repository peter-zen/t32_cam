#!/bin/sh
# Home environment NFS mount — build host sits on 192.168.31.x.
# Run on the device AFTER WiFi is up:
#   ./bin/net --ssid no_mesh_02_2.4G --pwd <WIFI_PWD>
#   . ./mount_nfs_home.sh
# (Company environment on 192.168.0.x uses mount_nfs.sh instead.)
NFS_HOST=192.168.31.200
NFS_PATH=/home/zengping/projects/hc_t32/code/t32/build

insmod_nfs 2>/dev/null
mkdir -p /mnt/huntcam
# noac = disable NFS attribute caching so rebuilt binaries/.so are visible on the
# device immediately (without it the device runs stale code after a rebuild).
mount -o noac,nolock,vers=3 ${NFS_HOST}:${NFS_PATH} /mnt/huntcam \
  || { echo "NFS mount failed"; exit 1; }
export LD_LIBRARY_PATH=/mnt/huntcam/lib:$LD_LIBRARY_PATH
cd /mnt/huntcam
