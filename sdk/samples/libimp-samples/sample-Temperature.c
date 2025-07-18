#include <stdio.h>
#include <sys/ioctl.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define TEMP_IOC_MAGIC 'T'
#define TSENSOR_GET_TEMP _IOWR(TEMP_IOC_MAGIC,1,int)

int main()
{
	int fd = 0,ret = 0,temp = 0;
	fd = open("/dev/temp",O_CREAT | O_RDWR);
	if(fd < 0){
		printf("open err\n");
		return -1;
	}

	ret = ioctl(fd,TSENSOR_GET_TEMP,&temp);
	if(ret < 0){
		printf("ioctl err,ret:%d\n",ret);
		return -1;
	}

	printf("temp:%d.%d,ret:%d\n",temp/1000,temp%1000,ret);

	close(fd);

	return 0;
}