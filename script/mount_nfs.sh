#!/bin/sh
# Company environment NFS mount — network 192.168.0.x.
# Run on the device AFTER WiFi is up:
#   ./bin/htc_net_app --ssid no_mesh_01_2.4G --pwd <WIFI_PWD>
#   . ./mount_nfs.sh
# (Home environment on 192.168.31.x uses mount_nfs_home.sh instead.)
NFS_HOST=192.168.0.206
NFS_PATH=/home/zengping/projects/hc_t32/code/t32/build
# NOTE: NFS_PATH assumes the company build host has the repo at the same path as
# the home host. If the company checkout lives elsewhere, edit NFS_PATH above.

insmod_nfs 2>/dev/null
mkdir -p /mnt/huntcam
# noac = disable NFS attribute caching so rebuilt binaries/.so are visible on the
# device immediately (without it the device runs stale code after a rebuild).
mount -o noac,nolock,vers=3 ${NFS_HOST}:${NFS_PATH} /mnt/huntcam \
  || { echo "NFS mount failed"; exit 1; }
export LD_LIBRARY_PATH=/mnt/huntcam/lib:$LD_LIBRARY_PATH
cd /mnt/huntcam
