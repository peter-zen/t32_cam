/*
	sample-Ai.c

	Copyright (C) 2014 Ingenic Semiconductor Co.,Ltd
	The specific instructions for all API calls in this file can be found in the header file of the proj/sdk-  lv3/include/api/cn/imp/

	This example demonstrates how to enable audio recording and save the recorded file locally

	The samplerate and soundmode can be selected in this sample
	samplerate: supports 8000 12000 16000 24000 32000 44100 48000 96000

	soundmode: Select recording single and dual channels.supports mono or stereo

	numperfrm: the number of sampling points per frame is calculated as follows samplerate*40ms

	chnvol: Set the audio input volume, volume range: [-30 ~ 120]. -30 represents mute, 120 represents the amplification of sound to 30dB, step 0.5dB. 60 means that the volume is set to a critical point

	Again: Range [0~31], corresponding to the simulated gain value, 12 is the critical value, with a step size of 1.5dB. If it is less than 12, subtract 1, decrease by 1.5dB. If it is greater than 12, increase by 1, corresponding to an increase of 1.5dB
 */

#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/time.h>

#include "sample-common.h"

#define TAG "sample-Ai"

#define AI_BASIC_TEST_RECORD_FILE "ai_record.pcm"
#define AI_BASIC_TEST_RECORD_NUM 500

static void *_ai_basic_record_test_thread(void *argv);

int main(void)
{
	int ret = -1;
	pthread_t record_thread_id;
	printf("[INFO] Test : Start audio record test.\n");
	printf("[INFO]        : Can create the %s file.\n", AI_BASIC_TEST_RECORD_FILE);
	printf("[INFO]        : Please input any key to continue.\n");
	getchar();

	/* Step.1 Start audio recording thread */
	ret = pthread_create(&record_thread_id, NULL, _ai_basic_record_test_thread, NULL);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "[ERROR] %s: pthread_create Audio Record failed\n", __func__);
		return -1;
	}
	pthread_join(record_thread_id, NULL);
	return 0;
}

static void *_ai_basic_record_test_thread(void *argv)
{
	int ret = -1;
	int record_num = 0;

	FILE *record_file = fopen(AI_BASIC_TEST_RECORD_FILE, "wb");
	if(record_file == NULL) {
		IMP_LOG_ERR(TAG, "fopen %s failed\n", AI_BASIC_TEST_RECORD_FILE);
		return NULL;
	}

	/* Step.2 set public attribute of AI device */
	int devID = 1;
	IMPAudioIOAttr attr;
	attr.samplerate = AUDIO_SAMPLE_RATE_16000;
	attr.bitwidth = AUDIO_BIT_WIDTH_16;
	attr.soundmode = AUDIO_SOUND_MODE_MONO;
	attr.frmNum = 40;
	attr.numPerFrm = 640;
	attr.chnCnt = 1;
	ret = IMP_AI_SetPubAttr(devID, &attr);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "set ai %d attr err: %d\n", devID, ret);
		return NULL;
	}
	memset(&attr, 0x0, sizeof(attr));
	ret = IMP_AI_GetPubAttr(devID, &attr);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "get ai %d attr err: %d\n", devID, ret);
		return NULL;
	}
	IMP_LOG_INFO(TAG, "Audio In GetPubAttr samplerate : %d\n", attr.samplerate);
	IMP_LOG_INFO(TAG, "Audio In GetPubAttr   bitwidth : %d\n", attr.bitwidth);
	IMP_LOG_INFO(TAG, "Audio In GetPubAttr  soundmode : %d\n", attr.soundmode);
	IMP_LOG_INFO(TAG, "Audio In GetPubAttr     frmNum : %d\n", attr.frmNum);
	IMP_LOG_INFO(TAG, "Audio In GetPubAttr  numPerFrm : %d\n", attr.numPerFrm);
	IMP_LOG_INFO(TAG, "Audio In GetPubAttr     chnCnt : %d\n", attr.chnCnt);

	/* Step.3 enable AI device */
	ret = IMP_AI_Enable(devID);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "enable ai %d err\n", devID);
		return NULL;
	}

	/* Step.4  set audio channel attribute of AI device */
	int chnID = 0;
	IMPAudioIChnParam chnParam;
	chnParam.usrFrmDepth = 40;
	chnParam.aecChn = 0;
	ret = IMP_AI_SetChnParam(devID, chnID, &chnParam);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "set ai %d channel %d attr err: %d\n", devID, chnID, ret);
		return NULL;
	}
	memset(&chnParam, 0x0, sizeof(chnParam));
	ret = IMP_AI_GetChnParam(devID, chnID, &chnParam);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "get ai %d channel %d attr err: %d\n", devID, chnID, ret);
		return NULL;
	}
	IMP_LOG_INFO(TAG, "Audio In GetChnParam usrFrmDepth : %d\n", chnParam.usrFrmDepth);

	/* Step 4: enable AI channel. */
	ret = IMP_AI_EnableChn(devID, chnID);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "Audio Record enable channel failed\n");
		return NULL;
	}

	/* Step.5 Set audio channel volume */
	int chnVol = 60;
	ret = IMP_AI_SetVol(devID, chnID, chnVol);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "Audio Record set volume failed\n");
		return NULL;
	}
	ret = IMP_AI_GetVol(devID, chnID, &chnVol);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "Audio Record get volume failed\n");
		return NULL;
	}
	IMP_LOG_INFO(TAG, "Audio In GetVol    vol : %d\n", chnVol);

	/* Step.6 Set audio channel gain */
	int aigain = 23;
	ret = IMP_AI_SetGain(devID, chnID, aigain);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "Audio Record Set Gain failed\n");
		return NULL;
	}
	ret = IMP_AI_GetGain(devID, chnID, &aigain);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "Audio Record Get Gain failed\n");
		return NULL;
	}
	IMP_LOG_INFO(TAG, "Audio In GetGain    gain : %d\n", aigain);

	/* Step.7  enable AI algorithm */
	ret = IMP_AI_EnableAlgo(devID, chnID);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "IMP_AI_EnableAlgo failed\n");
		return NULL;
	}

	while(1) {
		/* Step.8  get audio record frame */
		ret = IMP_AI_PollingFrame(devID, chnID, 1000);
		if (ret != 0 ) {
			IMP_LOG_ERR(TAG, "Audio Polling Frame Data error\n");
		}
		IMPAudioFrame frm;
		ret = IMP_AI_GetFrame(devID, chnID, &frm, BLOCK);
		if(ret != 0) {
			IMP_LOG_ERR(TAG, "Audio Get Frame Data error\n");
			return NULL;
		}

		/* Step.9 Save the recording data to a file */
		fwrite(frm.virAddr, 1, frm.len, record_file);

		/* Step.10  release the audio record frame */
		ret = IMP_AI_ReleaseFrame(devID, chnID, &frm);
		if(ret != 0) {
			IMP_LOG_ERR(TAG, "Audio release frame data error\n");
			return NULL;
		}

		if(++record_num >= AI_BASIC_TEST_RECORD_NUM)
			break;
	}

	sleep(3);
	/* Step.11 disable AI algorithm */
	ret = IMP_AI_DisableAlgo(devID, chnID);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "IMP_AI_DisableAlgo failed\n");
		return NULL;
	}

	/* Step.12 disable the audio channel */
	ret = IMP_AI_DisableChn(devID, chnID);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "Audio channel disable error\n");
		return NULL;
	}

	/* Step.13 disable the audio devices */
	ret = IMP_AI_Disable(devID);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "Audio device disable error\n");
		return NULL;
	}

	fclose(record_file);
	pthread_exit(0);
}

