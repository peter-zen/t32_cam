/*
	sample-Ao.c

	Copyright (C) 2014 Ingenic Semiconductor Co.,Ltd
	The specific instructions for all API calls in this file can be found in the header file of the proj/sdk-lv3/include/api/cn/imp/

	This example demonstrates how to use audio playback

	The samplerate and soundmode can be selected in this sample
	samplerate: supports 8000 12000 16000 24000 32000 44100 48000 96000

	soundmode: Select recording single and dual channels.supports mono or stereo

	numperfrm: the number of sampling points per frame is calculated as follows samplerate*40ms

	chnvol: Set the audio input/output volume, volume range: [-30 ~ 120]. -30 represents mute, 120 represents the amplification of sound to 30dB, step 0.5dB. 60 means that the volume is set to a critical point

	Again: Range [0~31], corresponding to the simulated gain value, 12 is the critical value, with a step size of 1.5dB. If it is less than 12, subtract 1, decrease by 1.5dB. If it is greater than 12, increase by 1, corresponding to an increase of 1.5dB
 */

#include <sys/prctl.h>

#include "sample-common.h"

#define TAG "sample-Ao"

#define AO_TEST_SAMPLE_RATE 16000
#define AO_TEST_SAMPLE_TIME 20
#define AO_TEST_BUF_SIZE (AO_TEST_SAMPLE_RATE * sizeof(short) * AO_TEST_SAMPLE_TIME / 1000)
#define AO_BASIC_TEST_PLAY_FILE  "./ao_play.pcm"

int _ao_basic_test(void);
static void *_ao_test_play_thread(void *argv);

int main(void)
{
	int ret = -1;

	ret = _ao_basic_test();
	if (0 != ret) {
		printf("err: _ao_basic_test\n");
	}

	return ret;
}

int _ao_basic_test(void)
{
	int ret = -1;
	pthread_t play_thread_id;

	/* Check if there are playback files present */
	printf("[INFO] Test Ao basic:\n");
	if (!access(AO_BASIC_TEST_PLAY_FILE, F_OK)) {
		printf("[INFO]  : Ao play file: %s is exist.\n", AO_BASIC_TEST_PLAY_FILE);
	} else {
		printf("[INFO]  : Ao play file: %s is not exist!\n", AO_BASIC_TEST_PLAY_FILE);
		return -1;
	}
	printf("[INFO]	: Please input any key to continue:\n");
	getchar();

	/* Create audio playback thread */
	ret = pthread_create(&play_thread_id, NULL, _ao_test_play_thread, NULL);
	if (ret != 0) {
		IMP_LOG_ERR(TAG, "[ERROR] %s: pthread_create Audio Record failed\n", __func__);
		return -1;
	}
	pthread_join(play_thread_id, NULL);

	return ret;
}

static void *_ao_test_play_thread(void *argv)
{
	unsigned char *buf = NULL;
	int size = 0;
	int ret = -1;

	buf = (unsigned char *)malloc(AO_TEST_BUF_SIZE);
	if (buf == NULL) {
		IMP_LOG_ERR(TAG, "[ERROR] %s: malloc audio buf error\n", __func__);
		return NULL;
	}

	FILE *play_file = fopen(AO_BASIC_TEST_PLAY_FILE, "rb");
	if (play_file == NULL) {
		IMP_LOG_ERR(TAG, "[ERROR] %s: fopen %s failed\n", __func__, AO_BASIC_TEST_PLAY_FILE);
		return NULL;
	}

	/* Step.1 set public attribute of AO device */
	int devID = 0;
	IMPAudioIOAttr attr;
	attr.samplerate = AUDIO_SAMPLE_RATE_16000;
	attr.bitwidth = AUDIO_BIT_WIDTH_16;
	attr.soundmode = AUDIO_SOUND_MODE_MONO;
	attr.frmNum = 20;
	attr.numPerFrm = 640;
	attr.chnCnt = 1;
	ret = IMP_AO_SetPubAttr(devID, &attr);
	if (ret != 0) {
		IMP_LOG_ERR(TAG, "set ao %d attr err: %d\n", devID, ret);
		return NULL;
	}
	memset(&attr, 0x0, sizeof(attr));
	ret = IMP_AO_GetPubAttr(devID, &attr);
	if (ret != 0) {
		IMP_LOG_ERR(TAG, "get ao %d attr err: %d\n", devID, ret);
		return NULL;
	}
	IMP_LOG_INFO(TAG, "Audio Out GetPubAttr samplerate:%d\n", attr.samplerate);
	IMP_LOG_INFO(TAG, "Audio Out GetPubAttr   bitwidth:%d\n", attr.bitwidth);
	IMP_LOG_INFO(TAG, "Audio Out GetPubAttr  soundmode:%d\n", attr.soundmode);
	IMP_LOG_INFO(TAG, "Audio Out GetPubAttr     frmNum:%d\n", attr.frmNum);
	IMP_LOG_INFO(TAG, "Audio Out GetPubAttr  numPerFrm:%d\n", attr.numPerFrm);
	IMP_LOG_INFO(TAG, "Audio Out GetPubAttr     chnCnt:%d\n", attr.chnCnt);

	/* Step.2 enable AO device */
	ret = IMP_AO_Enable(devID);
	if (ret != 0) {
		IMP_LOG_ERR(TAG, "enable ao %d err\n", devID);
		return NULL;
	}

	/* Step.3 enable AO channel */
	int chnID = 0;
	ret = IMP_AO_EnableChn(devID, chnID);
	if (ret != 0) {
		IMP_LOG_ERR(TAG, "Audio play enable channel failed\n");
		return NULL;
	}

	/* Step.4 Set audio channel volume */
	int chnVol = 80;
	ret = IMP_AO_SetVol(devID, chnID, chnVol);
	if (ret != 0) {
		IMP_LOG_ERR(TAG, "Audio Play set volume failed\n");
		return NULL;
	}
	ret = IMP_AO_GetVol(devID, chnID, &chnVol);
	if (ret != 0) {
		IMP_LOG_ERR(TAG, "Audio Play get volume failed\n");
		return NULL;
	}
	IMP_LOG_INFO(TAG, "Audio Out GetVol    vol:%d\n", chnVol);

	/* Step.5 Set audio channel gain */
	int aogain = 31;
	ret = IMP_AO_SetGain(devID, chnID, aogain);
	if (ret != 0) {
		IMP_LOG_ERR(TAG, "Audio Play Set Gain failed\n");
		return NULL;
	}
	ret = IMP_AO_GetGain(devID, chnID, &aogain);
	if (ret != 0) {
		IMP_LOG_ERR(TAG, "Audio Play Get Gain failed\n");
		return NULL;
	}
	IMP_LOG_INFO(TAG, "Audio Out GetGain    gain : %d\n", aogain);

	/* Step.6 enable AO algorithm */
	ret = IMP_AO_EnableAlgo(devID, chnID);
	if (ret != 0) {
		IMP_LOG_ERR(TAG, "IMP_AO_EnableAlgo failed\n");
		return NULL;
	}

	int i = 0;
	while (1) {
		size = fread(buf, 1, AO_TEST_BUF_SIZE, play_file);
		if (size < AO_TEST_BUF_SIZE)
			break;

		/* Step.7 send frame data */
		IMPAudioFrame frm;
		frm.virAddr = (uint32_t *)buf;
		frm.len = size;
		ret = IMP_AO_SendFrame(devID, chnID, &frm, BLOCK);
		if (ret != 0) {
			IMP_LOG_ERR(TAG, "send Frame Data error!\n");
			return NULL;
		}

		/* Step.8 view playback frame status */
		IMPAudioOChnState play_status;
		ret = IMP_AO_QueryChnStat(devID, chnID, &play_status);
		if (ret != 0) {
			IMP_LOG_ERR(TAG, "IMP_AO_QueryChnStat error!\n");
			return NULL;
		}

		IMP_LOG_INFO(TAG, "Play: TotalNum %d, FreeNum %d, BusyNum %d\n",
				play_status.chnTotalNum, play_status.chnFreeNum, play_status.chnBusyNum);

		/* Pause API validation */
		if (++i == 40) {
			ret = IMP_AO_PauseChn(devID, chnID);
			if (ret != 0) {
				IMP_LOG_ERR(TAG, "IMP_AO_PauseChn error!\n");
				return NULL;
			}
			printf("[INFO] Test : Audio Play Pause test.\n");
			printf("[INFO]		: Please input any key to continue:\n");
			getchar();

			/* Clear audio frame data */
			ret = IMP_AO_ClearChnBuf(devID, chnID);
			if (ret != 0) {
				IMP_LOG_ERR(TAG, "IMP_AO_ClearChnBuf error!\n");
				return NULL;
			}

			/* Restore audio playback */
			ret = IMP_AO_ResumeChn(devID, chnID);
			if (ret != 0) {
				IMP_LOG_ERR(TAG, "IMP_AO_ResumeChn error!\n");
				return NULL;
			}
		}
	}

	/* Step.9 waiting for the last audio data to finish playing */
	ret = IMP_AO_FlushChnBuf(devID, chnID);
	if (ret != 0) {
		IMP_LOG_ERR(TAG, "IMP_AO_FlushChnBuf error\n");
		return NULL;
	}

	/* Step.10 disable AO algorithm */
	ret = IMP_AO_DisableAlgo(devID, chnID);
	if (ret != 0) {
		IMP_LOG_ERR(TAG, "IMP_AO_DisableAlgo error\n");
		return NULL;
	}

	/* Step.11 disable the audio channel */
	ret = IMP_AO_DisableChn(devID, chnID);
	if (ret != 0) {
		IMP_LOG_ERR(TAG, "Audio channel disable error\n");
		return NULL;
	}

	/* Step.12 disable the audio devices */
	ret = IMP_AO_Disable(devID);
	if (ret != 0) {
		IMP_LOG_ERR(TAG, "Audio device disable error\n");
		return NULL;
	}

	fclose(play_file);
	free(buf);
	pthread_exit(0);
}


