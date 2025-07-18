/*
	sample-Encoder-video.c

	Copyright (C) 2014 Ingenic Semiconductor Co.,Ltd
	The specific instructions for all API calls in this file can be found in the header file of the proj/sdk-lv3/include/api/cn/imp/
*/

#include "sample-common.h"
#include <sys/mman.h>
#include <sys/ioctl.h>
#define TAG "sample-Snap-Kernel-yuv"


enum{
	PRO_ZRT_IOCTL_DMA_GET_FRAME = 1,
	PRO_ZRT_IOCTL_FREE_DMA_FRAME,
	PRO_ZRT_IOCTL_INVALUE,
};
#define MEM_DEV "/dev/mem"
#define SNAP_DEV	"/proc/jz/zeratul/dma_frame"
#define ZRT_IOCTL_TYPE 'Z'
#define ZRT_IOCTL_GET_DMA_FRAME 	_IOWR(ZRT_IOCTL_TYPE, PRO_ZRT_IOCTL_DMA_GET_FRAME, struct fast_frame_info)
#define ZRT_IOCTL_FREE_DMA_FRAME 	_IOWR(ZRT_IOCTL_TYPE, PRO_ZRT_IOCTL_FREE_DMA_FRAME, struct fast_frame_info)


struct fast_frame_info {
       int vinum;
       int channel;
       int frame_num;
       unsigned int alloc_size;
       unsigned int width;
       unsigned int height;
       unsigned int kpaddr;
       void *kvaddr;
       void *src_kvaddr;
};
int main(int argc, char *argv[])
{
	void *vaddr = NULL;
	struct fast_frame_info info;
	int fd,fd_yuv,mem_fd,ret;
	char write_path[512] = {0};
	ssize_t bytes_written;
	//get frame fd
	fd = open(SNAP_DEV, O_RDWR);
	if (fd == -1) {
		IMP_LOG_ERR(TAG,"Failed to open device");
		return EXIT_FAILURE;
	}
	info.vinum = 0;
	info.channel = 0;
	info.frame_num = 0;
	ret = ioctl(fd, ZRT_IOCTL_GET_DMA_FRAME, &info);
	if (ret == -1) {
		IMP_LOG_ERR(TAG,"ioctl error: %s\n", strerror(errno));
		close(fd);
		return EXIT_FAILURE;
	}
	IMP_LOG_INFO(TAG,"channel:%d,frame_num:%d,alloc_size:%d,width:%d,height:%d,paddr%#x\n",info.channel,
		     info.frame_num,info.alloc_size,info.width,info.height,info.kpaddr);

	mem_fd = open(MEM_DEV, O_RDWR);
	if(mem_fd <= 0) {
		IMP_LOG_ERR(TAG, "open %s failed\n", MEM_DEV);
		goto open_failed_io;
	}
	vaddr = mmap(NULL, info.alloc_size, PROT_READ | PROT_WRITE, MAP_SHARED,
					 mem_fd, info.kpaddr);
	if(!vaddr) {
		IMP_LOG_ERR(TAG, "mmap failed\n");
		goto open_failed_mem;
	}

	//write snap yuv file
	sprintf(write_path,"/tmp/snap-yuv-%dx%d.nv12",info.width,info.height);
	fd_yuv = open(write_path, O_RDWR|O_CREAT);
	if (fd_yuv == -1) {
		IMP_LOG_ERR(TAG,"Failed to open %s device", write_path);
		goto open_failed_mem;
	}
	bytes_written = write(fd_yuv, vaddr, info.alloc_size);
	if (bytes_written == -1) {
		IMP_LOG_ERR(TAG,"Failed to write %s device",write_path);
		goto open_failed_yuv;
	}
	//release
	ret = ioctl(fd, ZRT_IOCTL_FREE_DMA_FRAME, &info);
	if (ret == -1) {
		IMP_LOG_ERR(TAG, "ioctl error: %s\n", strerror(errno));
		goto open_failed_yuv;
	}
	munmap(vaddr, info.alloc_size);
	close(fd);
	close(mem_fd);
	close(fd_yuv);
	printf("Written %zd bytes: %s\n", bytes_written, write_path);
	return 0;

open_failed_yuv:
	munmap(vaddr, info.alloc_size);
	close(fd_yuv);
open_failed_mem:
	close(mem_fd);
open_failed_io:
	close(fd);
	return EXIT_FAILURE;
}
