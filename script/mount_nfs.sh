#!/bin/sh
insmod_nfs
mkdir -p /mnt/huntcam
mount -o nolock 192.168.0.210:/home/zengping/project/huntcam/code/t32_cam/build /mnt/huntcam
if [ $? -eq 0 ]; then
    export LD_LIBRARY_PATH=/mnt/huntcam/lib:$LD_LIBRARY_PATH
else
    echo "NFS mount failed"
fi
