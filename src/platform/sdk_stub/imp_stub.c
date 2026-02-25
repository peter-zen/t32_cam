/**
 * @file imp_stub.c
 * @brief IMP库的PC模拟实现
 * 
 * 在PC环境下提供IMP库的空实现，
 * 所有函数返回成功或合理的默认值。
 */

#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include "hal_encoder.h"
#include "hal_framesource.h"
#include "hal_system.h"
#include <imp/imp_audio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * IMP System Stub
 * ============================================================================ */

typedef struct {
    char aVersion[64];
} IMPVersion;

typedef struct {
    int deviceID;
    int groupID;
    int outputID;
} IMPCell;

int IMP_System_Init(void)
{
    printf("[IMP_STUB] IMP_System_Init: PC simulation mode\n");
    return hal_system_init();
}

int IMP_System_Exit(void)
{
    printf("[IMP_STUB] IMP_System_Exit: PC simulation mode\n");
    return hal_system_exit();
}

int64_t IMP_System_GetTimeStamp(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

int IMP_System_RebaseTimeStamp(int64_t basets)
{
    (void)basets;
    return 0;
}

uint32_t IMP_System_ReadReg32(uint32_t regAddr)
{
    (void)regAddr;
    return 0;
}

void IMP_System_WriteReg32(uint32_t regAddr, uint32_t value)
{
    (void)regAddr;
    (void)value;
}

int IMP_System_GetVersion(IMPVersion *pstVersion)
{
    if (pstVersion) {
        snprintf(pstVersion->aVersion, sizeof(pstVersion->aVersion), 
                 "IMP_STUB_1.0.0_PC_SIM");
    }
    return 0;
}

const char* IMP_System_GetCPUInfo(void)
{
    return "PC_SIMULATION";
}

int IMP_System_Bind(IMPCell *srcCell, IMPCell *dstCell)
{
    (void)srcCell;
    (void)dstCell;
    return 0;
}

int IMP_System_UnBind(IMPCell *srcCell, IMPCell *dstCell)
{
    (void)srcCell;
    (void)dstCell;
    return 0;
}

int IMP_System_GetBindbyDest(IMPCell *dstCell, IMPCell *srcCell)
{
    (void)dstCell;
    (void)srcCell;
    return 0;
}

int IMP_System_MemPoolRequest(int poolId, size_t size, char *name)
{
    (void)poolId;
    (void)size;
    (void)name;
    return 0;
}

/* ============================================================================
 * IMP FrameSource Stub - 调用 HAL 层实现
 * ============================================================================ */

int IMP_FrameSource_CreateChn(int chnNum, void *chnAttr)
{
    (void)chnAttr;
    /* 使用默认属性创建通道 */
    HalFSChnAttr attr = {
        .width = 1920,
        .height = 1080,
        .pix_fmt = HAL_PIX_FMT_NV12,
        .fps_num = 25,
        .fps_den = 1,
        .buf_count = 3,
        .crop_enable = 0,
        .scale_enable = 0
    };
    printf("[IMP_STUB] IMP_FrameSource_CreateChn(%d): PC simulation\n", chnNum);
    return hal_fs_create_channel(chnNum, &attr);
}

int IMP_FrameSource_DestroyChn(int chnNum)
{
    return hal_fs_destroy_channel(chnNum);
}

int IMP_FrameSource_EnableChn(int chnNum)
{
    printf("[IMP_STUB] IMP_FrameSource_EnableChn(%d): PC simulation\n", chnNum);
    return hal_fs_enable_channel(chnNum);
}

int IMP_FrameSource_DisableChn(int chnNum)
{
    return hal_fs_disable_channel(chnNum);
}

int IMP_FrameSource_SetChnAttr(int chnNum, void *chnAttr)
{
    (void)chnNum;
    (void)chnAttr;
    return 0;
}

int IMP_FrameSource_GetChnAttr(int chnNum, void *chnAttr)
{
    (void)chnNum;
    (void)chnAttr;
    return 0;
}

int IMP_FrameSource_GetFrame(int chnNum, void *frame, int blockFlag)
{
    (void)chnNum;
    (void)frame;
    (void)blockFlag;
    return -1;  // PC模拟下无法获取真实帧
}

int IMP_FrameSource_ReleaseFrame(int chnNum, void *frame)
{
    (void)chnNum;
    (void)frame;
    return 0;
}

/* ============================================================================
 * IMP Encoder Stub - 调用 HAL 层实现
 * ============================================================================ */

int IMP_Encoder_CreateGroup(int grpNum)
{
    printf("[IMP_STUB] IMP_Encoder_CreateGroup(%d): PC simulation\n", grpNum);
    return hal_enc_create_group(grpNum);
}

int IMP_Encoder_DestroyGroup(int grpNum)
{
    return hal_enc_destroy_group(grpNum);
}

int IMP_Encoder_CreateChn(int chnNum, void *chnAttr)
{
    (void)chnAttr;
    /* 使用默认属性创建通道 */
    HalEncoderChnAttr attr = {
        .payload_type = HAL_PT_H264,
        .width = 1920,
        .height = 1080,
        .fps_num = 25,
        .fps_den = 1,
        .gop = 50,
        .rc_mode = HAL_RC_MODE_CBR,
        .bitrate = 2000,
        .max_bitrate = 3000,
        .qp = 30
    };
    printf("[IMP_STUB] IMP_Encoder_CreateChn(%d): PC simulation\n", chnNum);
    return hal_enc_create_channel(chnNum, &attr);
}

int IMP_Encoder_DestroyChn(int chnNum)
{
    return hal_enc_destroy_channel(chnNum);
}

int IMP_Encoder_RegisterChn(int grpNum, int chnNum)
{
    printf("[IMP_STUB] IMP_Encoder_RegisterChn(%d, %d): PC simulation\n", grpNum, chnNum);
    return hal_enc_register_channel(grpNum, chnNum);
}

int IMP_Encoder_UnRegisterChn(int chnNum)
{
    /* HAL 层需要 group_id，这里假设 group_id == chnNum */
    return hal_enc_unregister_channel(chnNum, chnNum);
}

int IMP_Encoder_StartRecvPic(int chnNum)
{
    return hal_enc_start_recv_pic(chnNum);
}

int IMP_Encoder_StopRecvPic(int chnNum)
{
    return hal_enc_stop_recv_pic(chnNum);
}

int IMP_Encoder_PollingStream(int chnNum, int timeoutMsec)
{
    (void)chnNum;
    // PC模拟: 模拟等待一段时间后返回成功，表示有数据可用
    // 使用 usleep 模拟帧间隔 (约30fps = 33ms)
    usleep(33000);
    return 0;  // 返回0表示有数据可用
}

int IMP_Encoder_GetStream(int chnNum, void *stream, int blockFlag)
{
    (void)chnNum;
    (void)stream;
    (void)blockFlag;
    // PC模拟: 返回成功，但不填充实际数据
    // 调用者需要处理空数据的情况
    return 0;
}

int IMP_Encoder_ReleaseStream(int chnNum, void *stream)
{
    (void)chnNum;
    (void)stream;
    return 0;
}

int IMP_Encoder_RequestIDR(int chnNum)
{
    (void)chnNum;
    return 0;
}

int IMP_Encoder_FlushStream(int chnNum)
{
    (void)chnNum;
    return 0;
}

/* ============================================================================
 * IMP ISP Stub
 * ============================================================================ */

int IMP_ISP_Open(void)
{
    printf("[IMP_STUB] IMP_ISP_Open: PC simulation\n");
    return 0;
}

int IMP_ISP_Close(void)
{
    return 0;
}

int IMP_ISP_AddSensor(void *sensor)
{
    (void)sensor;
    return 0;
}

int IMP_ISP_DelSensor(void *sensor)
{
    (void)sensor;
    return 0;
}

int IMP_ISP_EnableSensor(void)
{
    return 0;
}

int IMP_ISP_DisableSensor(void)
{
    return 0;
}

int IMP_ISP_EnableTuning(void)
{
    return 0;
}

int IMP_ISP_DisableTuning(void)
{
    return 0;
}

int IMP_ISP_SetCameraInputMode(void *mode)
{
    (void)mode;
    return 0;
}

/* ============================================================================
 * IMP ISP Tuning Stub - 图像调节功能
 * ============================================================================ */

int IMP_ISP_Tuning_SetSharpness(int num, unsigned char *sharpness)
{
    (void)num;
    (void)sharpness;
    return 0;
}

int IMP_ISP_Tuning_GetSharpness(int num, unsigned char *sharpness)
{
    (void)num;
    if (sharpness) *sharpness = 128;
    return 0;
}

int IMP_ISP_Tuning_SetBrightness(int num, unsigned char *bright)
{
    (void)num;
    (void)bright;
    return 0;
}

int IMP_ISP_Tuning_GetBrightness(int num, unsigned char *bright)
{
    (void)num;
    if (bright) *bright = 128;
    return 0;
}

int IMP_ISP_Tuning_SetSaturation(int num, unsigned char *saturation)
{
    (void)num;
    (void)saturation;
    return 0;
}

int IMP_ISP_Tuning_GetSaturation(int num, unsigned char *saturation)
{
    (void)num;
    if (saturation) *saturation = 128;
    return 0;
}

int IMP_ISP_Tuning_SetContrast(int num, unsigned char *contrast)
{
    (void)num;
    (void)contrast;
    return 0;
}

int IMP_ISP_Tuning_GetContrast(int num, unsigned char *contrast)
{
    (void)num;
    if (contrast) *contrast = 128;
    return 0;
}

int IMP_ISP_Tuning_SetSensorFPS(int num, void *fps)
{
    (void)num;
    (void)fps;
    return 0;
}

int IMP_ISP_Tuning_GetSensorFPS(int num, void *fps)
{
    (void)num;
    (void)fps;
    return 0;
}

int IMP_ISP_Tuning_SetISPRunningMode(int num, void *mode)
{
    (void)num;
    (void)mode;
    return 0;
}

int IMP_ISP_Tuning_GetISPRunningMode(int num, void *mode)
{
    (void)num;
    (void)mode;
    return 0;
}

int IMP_ISP_Tuning_SetAntiFlickerAttr(int num, void *attr)
{
    (void)num;
    (void)attr;
    return 0;
}

int IMP_ISP_Tuning_GetAntiFlickerAttr(int num, void *attr)
{
    (void)num;
    (void)attr;
    return 0;
}

int IMP_ISP_Tuning_SetHVFLIP(int num, void *attr)
{
    (void)num;
    (void)attr;
    return 0;
}

int IMP_ISP_Tuning_GetHVFLIP(int num, void *attr)
{
    (void)num;
    (void)attr;
    return 0;
}

int IMP_ISP_Tuning_GetSensorAttr(int num, void *attr)
{
    (void)num;
    (void)attr;
    return 0;
}

int IMP_ISP_Tuning_SetBcshHue(int num, unsigned char *hue)
{
    (void)num;
    (void)hue;
    return 0;
}

int IMP_ISP_Tuning_GetBcshHue(int num, unsigned char *hue)
{
    (void)num;
    if (hue) *hue = 128;
    return 0;
}

int IMP_ISP_Tuning_WaitFrameDone(int num, void *attr)
{
    (void)num;
    (void)attr;
    return 0;
}

/* ============================================================================
 * IMP FrameSource Extended Stub
 * ============================================================================ */

int IMP_FrameSource_SetFrameDepth(int chnNum, int depth)
{
    (void)chnNum;
    (void)depth;
    return 0;
}

int IMP_FrameSource_GetFrameDepth(int chnNum, int *depth)
{
    (void)chnNum;
    if (depth) *depth = 0;
    return 0;
}

int IMP_FrameSource_GetI2dAttr(int chnNum, void *attr)
{
    (void)chnNum;
    (void)attr;
    return 0;
}

int IMP_FrameSource_SetI2dAttr(int chnNum, void *attr)
{
    (void)chnNum;
    (void)attr;
    return 0;
}

int IMP_FrameSource_SetPool(int chnNum, int poolID)
{
    (void)chnNum;
    (void)poolID;
    return 0;
}

int IMP_FrameSource_GetPool(int chnNum)
{
    (void)chnNum;
    return 0;
}

int IMP_FrameSource_SetChnFifoAttr(int chnNum, void *attr)
{
    (void)chnNum;
    (void)attr;
    return 0;
}

int IMP_FrameSource_GetChnFifoAttr(int chnNum, void *attr)
{
    (void)chnNum;
    (void)attr;
    return 0;
}

int IMP_FrameSource_SnapFrame(int chnNum, int fmt, int width, int height, void *framedata, void *frame)
{
    (void)chnNum;
    (void)fmt;
    (void)width;
    (void)height;
    (void)framedata;
    (void)frame;
    return -1;
}

/* ============================================================================
 * IMP Encoder Extended Stub
 * ============================================================================ */

int IMP_Encoder_Query(int encChn, void *stat)
{
    (void)encChn;
    (void)stat;
    return 0;
}

int IMP_Encoder_GetFd(int encChn)
{
    (void)encChn;
    return -1;
}

int IMP_Encoder_SetJpegBsSize(uint32_t jpegBsSize)
{
    (void)jpegBsSize;
    return 0;
}

int IMP_Encoder_MultiProcessInit(void)
{
    return 0;
}

void IMP_Encoder_MultiProcessExit(void)
{
}

int IMP_Encoder_SetMultiSectionMode(int mode, int size, int cnt)
{
    (void)mode;
    (void)size;
    (void)cnt;
    return 0;
}

int IMP_Encoder_KernEnc_Stop(void)
{
    return 0;
}

int IMP_Encoder_KernEnc_GetStream(int encChn, void *encOut)
{
    (void)encChn;
    (void)encOut;
    return -1;
}

int IMP_Encoder_KernEnc_Release(int encChn)
{
    (void)encChn;
    return 0;
}

int IMP_Encoder_KernEnc_GetStatus(int encChn, int *enable)
{
    (void)encChn;
    if (enable) *enable = 0;
    return 0;
}

int IMP_Encoder_SetChnAttr(int encChn, void *attr)
{
    (void)encChn;
    (void)attr;
    return 0;
}

int IMP_Encoder_GetChnAttr(int encChn, void *attr)
{
    (void)encChn;
    (void)attr;
    return 0;
}

int IMP_Encoder_SetChnRcAttr(int encChn, void *attr)
{
    (void)encChn;
    (void)attr;
    return 0;
}

int IMP_Encoder_GetChnRcAttr(int encChn, void *attr)
{
    (void)encChn;
    (void)attr;
    return 0;
}

int IMP_Encoder_GetChnFrmRate(int encChn, void *frmRate)
{
    (void)encChn;
    (void)frmRate;
    return 0;
}

int IMP_Encoder_SetChnFrmRate(int encChn, void *frmRate)
{
    (void)encChn;
    (void)frmRate;
    return 0;
}

/* ============================================================================
 * IMP OSD Stub
 * ============================================================================ */

int IMP_OSD_SetPoolSize(int size)
{
    (void)size;
    return 0;
}

int IMP_OSD_CreateGroup(int grpNum)
{
    (void)grpNum;
    return 0;
}

int IMP_OSD_DestroyGroup(int grpNum)
{
    (void)grpNum;
    return 0;
}

void* IMP_OSD_CreateRgn(void *attr)
{
    (void)attr;
    return (void*)1;  // 返回非空指针表示成功
}

int IMP_OSD_DestroyRgn(void *handle)
{
    (void)handle;
    return 0;
}

int IMP_OSD_RegisterRgn(void *handle, int grpNum, void *attr)
{
    (void)handle;
    (void)grpNum;
    (void)attr;
    return 0;
}

int IMP_OSD_UnRegisterRgn(void *handle, int grpNum)
{
    (void)handle;
    (void)grpNum;
    return 0;
}

int IMP_OSD_SetRgnAttr(void *handle, void *attr)
{
    (void)handle;
    (void)attr;
    return 0;
}

int IMP_OSD_GetRgnAttr(void *handle, void *attr)
{
    (void)handle;
    (void)attr;
    return 0;
}

int IMP_OSD_UpdateRgnAttrData(void *handle, void *data)
{
    (void)handle;
    (void)data;
    return 0;
}

int IMP_OSD_ShowRgn(void *handle, int grpNum, int show)
{
    (void)handle;
    (void)grpNum;
    (void)show;
    return 0;
}

int IMP_OSD_Start(int grpNum)
{
    (void)grpNum;
    return 0;
}

int IMP_OSD_Stop(int grpNum)
{
    (void)grpNum;
    return 0;
}

/* ============================================================================
 * IMP ISP Tuning Extended Stub
 * ============================================================================ */

int IMP_ISP_Tuning_SetOsdPoolSize(int size)
{
    (void)size;
    return 0;
}

int IMP_ISP_Tuning_GetOsdPoolSize(int *size)
{
    (void)size;
    if (size) *size = 0;
    return 0;
}

/* ============================================================================
 * IMP Audio Stub
 * ============================================================================ */

int IMP_AI_SetPubAttr(int audioDevId, IMPAudioIOAttr *attr)
{
    (void)audioDevId;
    (void)attr;
    return 0;
}

int IMP_AI_Enable(int audioDevId)
{
    (void)audioDevId;
    return 0;
}

int IMP_AI_Disable(int audioDevId)
{
    (void)audioDevId;
    return 0;
}

int IMP_AI_EnableChn(int audioDevId, int aiChn)
{
    (void)audioDevId;
    (void)aiChn;
    return 0;
}

int IMP_AI_DisableChn(int audioDevId, int aiChn)
{
    (void)audioDevId;
    (void)aiChn;
    return 0;
}

int IMP_AI_SetVol(int audioDevId, int aiChn, int vol)
{
    (void)audioDevId;
    (void)aiChn;
    (void)vol;
    return 0;
}

int IMP_AI_PollingFrame(int audioDevId, int aiChn, unsigned int timeout_ms)
{
    (void)audioDevId;
    (void)aiChn;
    (void)timeout_ms;
    // Simulate delay
    usleep(20000); // 20ms
    return 0;
}

int IMP_AI_GetFrame(int audioDevId, int aiChn, IMPAudioFrame *frm, IMPBlock block)
{
    (void)audioDevId;
    (void)aiChn;
    (void)block;
    
    if (frm) {
        // Generate dummy audio data (silence or noise)
        static uint8_t dummy_buf[640]; // 320 samples * 2 bytes
        memset(dummy_buf, 0, sizeof(dummy_buf));
        
        frm->virAddr = (uint32_t*)(uintptr_t)dummy_buf; // Cast to satisfy type
        frm->phyAddr = 0;
        frm->len = sizeof(dummy_buf);
        frm->timeStamp = 0; // Should probably update timestamp
        frm->seq = 0;
    }
    return 0;
}

int IMP_AI_ReleaseFrame(int audioDevId, int aiChn, IMPAudioFrame *frm)
{
    (void)audioDevId;
    (void)aiChn;
    (void)frm;
    return 0;
}

#ifdef __cplusplus
}
#endif
