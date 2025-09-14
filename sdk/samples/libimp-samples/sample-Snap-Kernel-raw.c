#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/fcntl.h>
#include <sys/mman.h>



int main()
{
	int fd = -1;
	int kmem_fd = -1;
	FILE *fout = NULL;
	unsigned int kmem_paddr;
	unsigned int kmem_vaddr;
	unsigned int kmem_length;
	int w,h;
	w=1920;h=1080;

	kmem_paddr = 0x6800000;//ispmem
	kmem_length = w * h * 2;;//raw size: w * h * 2

	kmem_fd = open("/dev/rmem", O_RDWR);
	if(kmem_fd <= 0) {
		printf("open %s failed\n", "/dev/rmem");
		return -1;
	}

	kmem_vaddr = (unsigned int)mmap(NULL, kmem_length, PROT_READ | PROT_WRITE, MAP_SHARED, kmem_fd, kmem_paddr);
	if(kmem_vaddr <= 0) {
		printf("mmap failed\n");
		return -1;
	}

	fout = fopen("/tmp/kern_isp.raw", "w+");
	if (!fout) {
		printf("fopen failed\n");
		return -1;
	}
	for(int i=0;i<1;i++){
		fwrite((void *)(kmem_vaddr + i * kmem_length), kmem_length, 1, fout);

	}
	munmap((void *)kmem_vaddr, kmem_length);
	close(kmem_fd);
	fclose(fout);
	close(fd);
	return 0;
}
