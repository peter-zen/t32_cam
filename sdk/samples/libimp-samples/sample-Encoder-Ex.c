/*
	sample-Encoder-Ex.c

	Copyright (C) 2014 Ingenic Semiconductor Co.,Ltd

	This sample demonstrates how to perform multi process coding.
*/

#include <sys/types.h>
#include <sys/stat.h>

#include "sample-common.h"

#define TAG "sample-Encoder-Ex"

int main(int argc, char *argv[])
{
	int ret = 0;
	int i = 0, frmSize = 0;
	int width = 1920, height = 1080;
	int encNum = 100;
	char path[3][32];
	FILE *inFile = NULL, *outFile = NULL, *outFile_ivpu = NULL;
	uint8_t *src_buf = NULL;
	int h = 0;
	IMPFrameInfo frame;
	IMPEncoderYuvOut stream;
	IMPEncoderYuvIn info;

	memset(&info, 0, sizeof(IMPEncoderYuvIn));

	info.type = PT_H265;
	info.mode.rcMode = ENC_RC_MODE_SMART;
	info.mode.attrH265Smart.maxBitRate = 4096;
	info.mode.attrH265Smart.initialQp = 25;
	info.mode.attrH265Smart.minQp = 20;
	info.mode.attrH265Smart.maxQp = 48;
	info.mode.attrH265Smart.minIQp = 25;
	info.mode.attrH265Smart.maxIQp = 50;
	info.outFrmRate.frmRateNum = 25;
	info.outFrmRate.frmRateDen = 1;
	info.maxGop = 25;

	sprintf(path[0], "%dx%d.nv12", width, height);
	inFile = fopen(path[0], "rb");
	if (inFile == NULL) {
		IMP_LOG_ERR(TAG, "inFile %s open failed\n", path[0]);
		goto err_encoder_yuvinit;
	}
	if (info.type == PT_H265)
		sprintf(path[1], "%s/out.h265", STREAM_FILE_PATH_PREFIX);
	else if (info.type == PT_H264)
		sprintf(path[1], "%s/out.h264", STREAM_FILE_PATH_PREFIX);

	outFile = fopen(path[1], "wb");
	if (outFile == NULL) {
		IMP_LOG_ERR(TAG, "outFile open failed\n");
		goto err_encoder_yuvinit;
	}
	ret = IMP_Encoder_YuvInit_Ex(&h, width, height, &info);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_Encoder_YuvInit_Ex failed\n");
		goto err_encoder_yuvinit;
	}

	frmSize = width*height*3/2;
	src_buf = (uint8_t*)IMP_Encoder_VbmAlloc_Ex(frmSize, 256);
	if(src_buf == NULL) {
		IMP_LOG_ERR(TAG, "IMP_Encoder_VbmAlloc_Ex failed\n");
		goto err_encoder_vbmalloc;
	}

	frame.width = width;
	frame.height = height;
	frame.size = frmSize;
	frame.phyAddr = (uint32_t)IMP_Encoder_VbmV2P((intptr_t)src_buf);
	frame.virAddr = (uint32_t)src_buf;
	stream.outAddr = (uint8_t*)IMP_Encoder_VbmAlloc_Ex(frmSize, 256);

	/**************** Encoder with jpeg ****************/
	int stream_length;
	fread(src_buf, 1, width*height*3/2, inFile);

	ret = IMP_Encoder_InputJpege_Ex((uint8_t *)frame.virAddr, stream.outAddr, frame.width, frame.height, 25, &stream_length);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_Encoder_InputJpege_Ex failed\n");
		goto err_encoder_yuvencode;
	} else {
		printf("stream_length %d\n", stream_length);
	}
	sprintf(path[2], "%s/out.jpg", STREAM_FILE_PATH_PREFIX);
	outFile_ivpu = fopen(path[2], "wb");
	fwrite(stream.outAddr, 1, stream_length, outFile_ivpu);
	fclose(outFile_ivpu);
	/**************** Encoder with jpeg ****************/

	memset((void *)frame.virAddr, 0, frmSize);
	memset(stream.outAddr, 0, frmSize);
	fseek(inFile, 0, SEEK_SET);
	for (i = 0; i < encNum; i++) {
		fread(src_buf, 1, width*height*3/2, inFile);

		stream.outLen = frmSize;
		ret = IMP_Encoder_YuvEncode_Ex(h, frame, &stream);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_Encoder_YuvEncode failed\n");
			goto err_encoder_yuvencode;
		}
		printf("\r%d encode success", i);
		fflush(stdout);
		fwrite(stream.outAddr, 1, stream.outLen, outFile);
	}
	puts("");

err_encoder_yuvencode:
	IMP_Encoder_VbmFree_Ex(stream.outAddr);
	IMP_Encoder_VbmFree_Ex(src_buf);
err_encoder_vbmalloc:
	IMP_Encoder_YuvExit_Ex(h);
	if (info.type != PT_JPEG)
		fclose(outFile);
	fclose(inFile);
err_encoder_yuvinit:

	return ret;
}
