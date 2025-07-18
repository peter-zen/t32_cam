/*
	sample-dmic-aec.c

	Copyright (C) 2014 Ingenic Semiconductor Co.,Ltd

	The specific instructions for all API calls in this file can be found in the header file of the proj/sdk-lv3/include/api/cn/imp/

	This example demonstrates how to use dmic recording aec and save the recorded file locally

	The samplerate and soundmode can be selected in this sample
	samplerate: supports 8000 16000 48000

	soundmode: only supports mono

	chncnt: supports 1, 2, and 4 channels corresponding to DMICs for mono, dual, and quad channels respectively

	numperfrm: the number of sampling points per frame is calculated as follows samplerate*40ms

	chnvol: Set the audio input/output volume, volume range: [-30 ~ 120]. -30 represents mute, 120 represents the amplification of sound to 30dB, step 0.5dB. 60 means that the volume is set to a critical point

	Again: Range [0~31], corresponding to the simulated gain value, 12 is the critical value, with a step size of 1.5dB. If it is less than 12, subtract 1, decrease by 1.5dB. If it is greater than 12, increase by 1, corresponding to an increase of 1.5dB
 */

#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/time.h>

#include "sample-common.h"

#define TAG "sample-dmic-aec"

#define DMIC0_TEST_RECORD_FILE "dmic0_record.pcm"
#define DMIC1_TEST_RECORD_FILE "dmic1_record.pcm"
#define DMIC2_TEST_RECORD_FILE "dmic2_record.pcm"
#define DMIC3_TEST_RECORD_FILE "dmic3_record.pcm"
#define DMIC_AEC_RECORD_FILE "dmic_aec.pcm"
#define AO_TEST_PLAY_FILE "play.pcm"
#define DMIC_RECORD_CNT 200

IMPDmicChnFrame g_chnFrm;
IMPDmicFrame g_refFrm;

short short_dmic_1[640] = {0};
short short_dmic_2[640] = {0};
short short_dmic_3[640] = {0};
short short_dmic_4[640] = {0};
short short_dmic_ref[640] = {0};

static void *_dmic_record_test_thread(void *argv);
static void * _ao_play_thread(void *argv);
static void *_dmic_record_test_aec_thread(void *argv);

int main()
{
	int ret = -1;
	pthread_t dmic_thread_id, play_thread_id;

	/*Record an audio segment first as the source for subsequent playback */
	printf("[INFO] Start dmic record aec test.\n");
	ret = pthread_create(&dmic_thread_id, NULL, _dmic_record_test_thread, NULL);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "Create _dmic_record_test_thread failed\n");
		return -1;
	}
	pthread_join(dmic_thread_id, NULL);
	printf("[INFO] dmic record end. Please input any key to continue.\n");
	getchar();

	/*Start AEC verification */
	ret = pthread_create(&dmic_thread_id, NULL, _dmic_record_test_aec_thread, NULL);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "Create _dmic_record_test_aec_thread failed\n");
		return -1;
	}
	ret = pthread_create(&play_thread_id, NULL, _ao_play_thread, NULL);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "Create _ao_play_thread failed\n");
		return -1;
	}

	pthread_join(dmic_thread_id, NULL);
	pthread_join(play_thread_id, NULL);

	return 0;
}

static void *_dmic_record_test_thread(void *argv)
{
	int ret = -1;

	FILE *dmic0_record = fopen(AO_TEST_PLAY_FILE, "wb");
	if (NULL == dmic0_record) {
		IMP_LOG_ERR(TAG, "fopen:%s failed.\n",AO_TEST_PLAY_FILE);
		return NULL;
	}

	/* Step.1 set dmic user info:if need aec function */
	ret = IMP_DMIC_SetUserInfo(0, 1, 0);
	if (ret != 0) {
		IMP_LOG_ERR(TAG, "dmic set user info error.\n");
		return NULL;
	}

	/* Step.2 set dmic audio attr */
	IMPDmicAttr attr;
	attr.samplerate = DMIC_SAMPLE_RATE_16000;
	attr.bitwidth = DMIC_BIT_WIDTH_16;
	attr.soundmode = DMIC_SOUND_MODE_MONO;
	attr.chnCnt = 1;  //chnCnt=1(1 dmic),2(2 dmic),4(4 dmic)
	attr.frmNum = 40;
	attr.numPerFrm = 640;

	ret = IMP_DMIC_SetPubAttr(0, &attr);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "DMIC_SetPubAttr failed.\n");
		return NULL;
	}

	/* Step.3 enable DMIC device */
	ret = IMP_DMIC_Enable(0);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "DMIC Enable failed.\n");
		return NULL;
	}

	/* Step.4 set dmic channel attr */
	IMPDmicChnParam chnParam;
	chnParam.usrFrmDepth = 40;
	ret = IMP_DMIC_SetChnParam(0, 0, &chnParam);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "DMIC SetChnParam failed.\n");
		return NULL;
	}

	/* Step.5 enable dmic channel */
	ret = IMP_DMIC_EnableChn(0, 0);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "DMIC Enable Channel failed.\n");
		return NULL;
	}

	/* Step.6 set dmic volume */
	ret = IMP_DMIC_SetVol(0, 0, 60);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "DMIC Set vol failed.\n");
		return NULL;
	}

	/*Step.7 set dmic gain */
	ret = IMP_DMIC_SetGain(0, 0, 24);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "DMIC Set Gain failed.\n");
		return NULL;
	}

	short *pdata = NULL;
	int k = 0;
	int record_cnt = 0;

	while(1){
		/* Step.8 get dmic record frame */
		ret = IMP_DMIC_PollingFrame(0, 0, 1000);
		if (ret != 0) {
			IMP_LOG_ERR(TAG, "dmic polling frame data error.\n");
		}
		ret = IMP_DMIC_GetFrame(0, 0, &g_chnFrm, BLOCK);
		if(ret < 0) {
			printf("IMP_DMIC_GetFrame failed\n");
			break;
		}
		pdata = (short*)(g_chnFrm.rawFrame.virAddr);

		/* Step.9 Save the dmic recording data to file */
		for(k = 0; k < 640; k++) {
			/* 4dmic get data */
			/*short_dmic_1[k] = pdata[k*4];
			  short_dmic_2[k] = pdata[k*4+1];
			  short_dmic_3[k] = pdata[k*4+2];
			  short_dmic_4[k] = pdata[k*4+3];*/

			/* 1dmic get data */
			short_dmic_1[k] = pdata[k];

			/* 2dmic get data */
			/*short_dmic_1[k] = pdata[k * 2];
			  short_dmic_1[k] = pdata[k * 2 + 1];*/
		}
		fwrite(short_dmic_1, 2, 640, dmic0_record);
		/*fwrite(short_dmic_2, 2, 640, dmic1_record);
		  fwrite(short_dmic_3, 2, 640, dmic2_record);
		  fwrite(short_dmic_4, 2, 640, dmic3_record);*/

		/* Step.10 release the dmic record frame */
		ret = IMP_DMIC_ReleaseFrame(0, 0, &g_chnFrm) ;
		if (ret < 0) {
			printf("IMP_DMIC_ReleaseFrame failed.\n");
			break;
		}
		if(++record_cnt > DMIC_RECORD_CNT) break;
	}

	/* Step.11 disable the dmic channel */
	ret = IMP_DMIC_DisableChn(0, 0);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "DMIC DisableChn error.\n");
		return NULL;
	}

	/* Step.12 disable the dmic devices */
	ret = IMP_DMIC_Disable(0);
	if (ret != 0){
		IMP_LOG_ERR(TAG, "DMIC Disable error.\n");
		return NULL;
	}

	fclose(dmic0_record);
	return NULL;
}

static void * _ao_play_thread(void *argv)
{
	int ret = -1;
	unsigned char *buf = NULL;
	int size = 0;
	buf = (unsigned char *)malloc(1280);
	if (buf == NULL) {
		IMP_LOG_ERR(TAG, "[ERROR] %s: malloc audio buf error\n", __func__);
		return NULL;
	}

	FILE *play_file = fopen(AO_TEST_PLAY_FILE, "rb");
	if (play_file == NULL) {
		IMP_LOG_ERR(TAG, "[ERROR] %s: fopen %s failed\n", __func__, AO_TEST_PLAY_FILE);
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
	int chnVol = 60;
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

	while (1) {
		size = fread(buf, 1, 1280, play_file);
		if (size < 1280)
			break;

		/* Step.6 send frame data */
		IMPAudioFrame frm;
		frm.virAddr = (uint32_t *)buf;
		frm.len = size;
		ret = IMP_AO_SendFrame(devID, chnID, &frm, BLOCK);
		if (ret != 0) {
			IMP_LOG_ERR(TAG, "send Frame Data error\n");
			return NULL;
		}
	}
	/* Step.7 waiting for the last audio data to finish playing */
	ret = IMP_AO_FlushChnBuf(devID, chnID);
	if (ret != 0) {
		IMP_LOG_ERR(TAG, "IMP_AO_FlushChnBuf error\n");
		return NULL;
	}

	/* Step.8 disable the audio channel */
	ret = IMP_AO_DisableChn(devID, chnID);
	if (ret != 0) {
		IMP_LOG_ERR(TAG, "Audio channel disable error\n");
		return NULL;
	}

	/* Step.9 disable the audio devices */
	ret = IMP_AO_Disable(devID);
	if (ret != 0) {
		IMP_LOG_ERR(TAG, "Audio device disable error\n");
		return NULL;
	}

	fclose(play_file);
	free(buf);
	pthread_exit(0);
}

static void *_dmic_record_test_aec_thread(void *argv)
{
	int ret = -1;

	FILE *dmic0_record = fopen(DMIC0_TEST_RECORD_FILE, "wb");
	if (NULL == dmic0_record) {
		IMP_LOG_ERR(TAG, "fopen:%s failed.\n",DMIC0_TEST_RECORD_FILE);
		return NULL;
	}
	FILE *dmic1_record = fopen(DMIC1_TEST_RECORD_FILE, "wb");
	if (NULL == dmic1_record) {
		IMP_LOG_ERR(TAG, "fopen:%s failed.\n",DMIC1_TEST_RECORD_FILE);
		return NULL;
	}
	FILE *dmic2_record = fopen(DMIC2_TEST_RECORD_FILE, "wb");
	if (NULL == dmic2_record) {
		IMP_LOG_ERR(TAG, "fopen:%s failed.\n",DMIC2_TEST_RECORD_FILE);
		return NULL;
	}
	FILE *dmic3_record = fopen(DMIC3_TEST_RECORD_FILE, "wb");
	if (NULL == dmic3_record) {
		IMP_LOG_ERR(TAG, "fopen:%s failed.\n",DMIC3_TEST_RECORD_FILE);
		return NULL;
	}
	FILE *dmic_aec_record = fopen(DMIC_AEC_RECORD_FILE, "wb");
	if (NULL == dmic_aec_record) {
		IMP_LOG_ERR(TAG, "fopen:%s failed.\n",DMIC_AEC_RECORD_FILE);
		return NULL;
	}

	/* Step.1 set dmic user info:if need aec function */
	ret = IMP_DMIC_SetUserInfo(0, 1, 1);
	if (ret != 0) {
		IMP_LOG_ERR(TAG, "dmic set user info error.\n");
		return NULL;
	}

	/* Step.2 set dmic audio attr */
	IMPDmicAttr attr;
	attr.samplerate = DMIC_SAMPLE_RATE_16000;
	attr.bitwidth = DMIC_BIT_WIDTH_16;
	attr.soundmode = DMIC_SOUND_MODE_MONO;
	attr.chnCnt = 4;  //chnCnt=1(1 dmic),2(2 dmic),4(4 dmic)
	attr.frmNum = 40;
	attr.numPerFrm = 640;

	ret = IMP_DMIC_SetPubAttr(0, &attr);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "DMIC_SetPubAttr failed.\n");
		return NULL;
	}

	/*Step.3 enable DMIC device */
	ret = IMP_DMIC_Enable(0);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "DMIC Enable failed.\n");
		return NULL;
	}

	/*Step.4 set dmic channel attr */
	IMPDmicChnParam chnParam;
	chnParam.usrFrmDepth = 40;
	ret = IMP_DMIC_SetChnParam(0, 0, &chnParam);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "DMIC SetChnParam failed.\n");
		return NULL;
	}

	/*Step.5 enable dmic channel */
	ret = IMP_DMIC_EnableChn(0, 0);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "DMIC Enable Channel failed.\n");
		return NULL;
	}

	/*Step.6 set dmic volume */
	ret = IMP_DMIC_SetVol(0, 0, 60);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "DMIC Set vol failed.\n");
		return NULL;
	}

	/*Step.7 set dmic gain */
	ret = IMP_DMIC_SetGain(0, 0, 22);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "DMIC Set Gain failed.\n");
		return NULL;
	}

	/*Step.8 enable dmic aec funcion */
	ret = IMP_DMIC_EnableAec(0, 0, 0, 0);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "DMIC Enable Aec.\n");
		return NULL;
	}

	short *pdata = NULL;
	int k = 0;
	int record_cnt = 0;

	while(1){
		/* Step.9 get dmic record frame */
		ret = IMP_DMIC_PollingFrame(0, 0, 1000);
		if (ret != 0) {
			IMP_LOG_ERR(TAG, "dmic polling frame data error.\n");
		}
		ret = IMP_DMIC_GetFrame(0, 0, &g_chnFrm, BLOCK);
		if(ret < 0) {
			printf("IMP_DMIC_GetFrame failed\n");
			break;
		}
		pdata = (short*)(g_chnFrm.rawFrame.virAddr);

		for(k = 0; k < 640; k++) {
			/* 4dmic get data */
			short_dmic_1[k] = pdata[k*4];
			short_dmic_2[k] = pdata[k*4+1];
			short_dmic_3[k] = pdata[k*4+2];
			short_dmic_4[k] = pdata[k*4+3];

			/* 1dmic get data */
			/*short_dmic_1[k] = pdata[k];*/

			/* 2dmic get data */
			/*short_dmic_1[k] = pdata[k * 2];
			  short_dmic_1[k] = pdata[k * 2 + 1];*/
		}
		fwrite(short_dmic_1, 2, 640, dmic0_record);
		fwrite(short_dmic_2, 2, 640, dmic1_record);
		fwrite(short_dmic_3, 2, 640, dmic2_record);
		fwrite(short_dmic_4, 2, 640, dmic3_record);

		fwrite((char*)g_chnFrm.aecFrame.virAddr, 1, g_chnFrm.aecFrame.len, dmic_aec_record);

		/* Step.10 release the dmic record frame */
		ret = IMP_DMIC_ReleaseFrame(0, 0, &g_chnFrm) ;
		if (ret < 0) {
			printf("IMP_DMIC_ReleaseFrame failed.\n");
			break;
		}
		if(++record_cnt > DMIC_RECORD_CNT) break;
	}

	/* Step.11 disable dmic aec */
	ret = IMP_DMIC_DisableAec(0, 0);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "DMIC DisableAec error.\n");
		return NULL;
	}

	/* Step.12 disable the dmic channel */
	ret = IMP_DMIC_DisableChn(0, 0);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "DMIC DisableChn error.\n");
		return NULL;
	}

	/* Step.13 disable the dmic devices */
	ret = IMP_DMIC_Disable(0);
	if (ret != 0){
		IMP_LOG_ERR(TAG, "DMIC Disable error.\n");
		return NULL;
	}

	fclose(dmic0_record);
	fclose(dmic1_record);
	fclose(dmic2_record);
	fclose(dmic3_record);
	fclose(dmic_aec_record);

	return NULL;

}
