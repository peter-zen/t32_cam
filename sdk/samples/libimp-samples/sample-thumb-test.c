/*
    sample-thumb-test.c

    Dual JPEG capture test: CH0 (main photo) + CH1 (thumbnail) with IVDC.

    Pipeline:
        GC4653 Sensor (2560x1440 @ 30fps)
            |
            ├── FrameSource Ch0 (scaler: 2560x1440) --> Encoder Group 0
            |       └── Channel 12: JPEG (main photo, IVDC enabled)
            |
            └── FrameSource Ch1 (scaler: 320x180)  --> Encoder Group 1
                    └── Channel 13: JPEG (thumbnail, IVDC enabled)

    Both JPEGs are captured simultaneously and saved to the same output directory.
    This validates that IVDC works on two channels of the same sensor,
    and that hardware scaler can produce thumbnail-resolution output.

    Usage:
        ./sample-thumb-test [num_snaps] [output_dir]
        e.g. ./sample-thumb-test 5 /mnt/sdcard/DCIM
*/

#include "sample-common.h"
#include <pthread.h>
#include <sys/stat.h>
#include <errno.h>

#define TAG "sample-thumb-test"

/* Override channel config: enable CH0 + CH1, disable everything else */
extern struct chn_conf chn[];
extern int direct_switch;

/* JPEG encoder channel numbers */
#define MAIN_JPEG_ENC_CHN   12   /* CH0 -> encoder channel 12 */
#define THUMB_JPEG_ENC_CHN  13   /* CH1 -> encoder channel 13 */

/* Thumbnail dimensions */
#define THUMB_W  320
#define THUMB_H  180   /* 16:9 aspect ratio from 2560x1440 */

/* Capture parameters */
#define JPEG_QUALITY     40
#define SNAP_TIMEOUT_MS  10000

/* Output directory */
static char output_dir[256] = "/tmp";

/* Number of snaps */
static int num_snaps = 3;

/* ---- Channel override ---- */
static void override_channels(void)
{
    int i;
    for (i = 0; i < FS_CHN_NUM; i++) {
        chn[i].enable = 0;
    }

    /* CH0: main stream 2560x1440 */
    chn[0].enable = 1;
    chn[0].index = 0;
    chn[0].payloadType = PT_JPEG;
    chn[0].fs_chn_attr.scaler.outwidth = FIRST_SENSOR_WIDTH;
    chn[0].fs_chn_attr.scaler.outheight = FIRST_SENSOR_HEIGHT;
    chn[0].fs_chn_attr.picWidth = FIRST_SENSOR_WIDTH;
    chn[0].fs_chn_attr.picHeight = FIRST_SENSOR_HEIGHT;

    /* CH1: thumbnail stream 320x180 (hardware scaled) */
    chn[1].enable = 1;
    chn[1].index = 1;
    chn[1].payloadType = PT_JPEG;
    chn[1].fs_chn_attr.scaler.outwidth = THUMB_W;
    chn[1].fs_chn_attr.scaler.outheight = THUMB_H;
    chn[1].fs_chn_attr.picWidth = THUMB_W;
    chn[1].fs_chn_attr.picHeight = THUMB_H;
    chn[1].fs_chn_attr.outFrmRateNum = FIRST_SENSOR_FRAME_RATE_NUM;
    chn[1].fs_chn_attr.outFrmRateDen = FIRST_SENSOR_FRAME_RATE_DEN;

    /* Force direct_switch = 0 so sample_jpeg_init doesn't set IVDC (we do it ourselves) */
    direct_switch = 0;
}

/* ---- Custom JPEG init for CH0 and CH1 with IVDC ---- */
static int dual_jpeg_init(void)
{
    int ret;
    IMPEncoderCHNAttr chn_attr;
    IMPEncoderAttr *enc_attr;
    IMPEncoderRcAttr *rc_attr;

    /* --- CH0: main JPEG encoder (channel 12) --- */
    memset(&chn_attr, 0, sizeof(chn_attr));
    enc_attr = &chn_attr.encAttr;
    enc_attr->enType = PT_JPEG;
    enc_attr->bufSize = 0;
    enc_attr->picWidth = FIRST_SENSOR_WIDTH;
    enc_attr->picHeight = FIRST_SENSOR_HEIGHT;
    rc_attr = &chn_attr.rcAttr;
    rc_attr->attrRcMode.rcMode = ENC_RC_MODE_FIXQP;
    rc_attr->attrRcMode.attrJPEGFixQp.qp = JPEG_QUALITY;
    chn_attr.bEnableIvdc = true;

    IMP_LOG_INFO(TAG, "Creating main JPEG encoder: chn=%d size=%dx%d ivdc=1\n",
                 MAIN_JPEG_ENC_CHN, enc_attr->picWidth, enc_attr->picHeight);

    ret = IMP_Encoder_CreateChn(MAIN_JPEG_ENC_CHN, &chn_attr);
    if (ret < 0) {
        IMP_LOG_ERR(TAG, "IMP_Encoder_CreateChn(%d) failed: %d\n", MAIN_JPEG_ENC_CHN, ret);
        return -1;
    }
    ret = IMP_Encoder_RegisterChn(0, MAIN_JPEG_ENC_CHN);
    if (ret < 0) {
        IMP_LOG_ERR(TAG, "IMP_Encoder_RegisterChn(group0, chn%d) failed: %d\n", MAIN_JPEG_ENC_CHN, ret);
        return -1;
    }

    /* --- CH1: thumbnail JPEG encoder (channel 13) --- */
    memset(&chn_attr, 0, sizeof(chn_attr));
    enc_attr = &chn_attr.encAttr;
    enc_attr->enType = PT_JPEG;
    enc_attr->bufSize = 0;
    enc_attr->picWidth = THUMB_W;
    enc_attr->picHeight = THUMB_H;
    rc_attr = &chn_attr.rcAttr;
    rc_attr->attrRcMode.rcMode = ENC_RC_MODE_FIXQP;
    rc_attr->attrRcMode.attrJPEGFixQp.qp = JPEG_QUALITY;
    chn_attr.bEnableIvdc = true;

    IMP_LOG_INFO(TAG, "Creating thumb JPEG encoder: chn=%d size=%dx%d ivdc=1\n",
                 THUMB_JPEG_ENC_CHN, enc_attr->picWidth, enc_attr->picHeight);

    ret = IMP_Encoder_CreateChn(THUMB_JPEG_ENC_CHN, &chn_attr);
    if (ret < 0) {
        IMP_LOG_ERR(TAG, "IMP_Encoder_CreateChn(%d) failed: %d\n", THUMB_JPEG_ENC_CHN, ret);
        return -1;
    }
    ret = IMP_Encoder_RegisterChn(1, THUMB_JPEG_ENC_CHN);
    if (ret < 0) {
        IMP_LOG_ERR(TAG, "IMP_Encoder_RegisterChn(group1, chn%d) failed: %d\n", THUMB_JPEG_ENC_CHN, ret);
        return -1;
    }

    return 0;
}

static void dual_jpeg_exit(void)
{
    IMP_Encoder_UnRegisterChn(MAIN_JPEG_ENC_CHN);
    IMP_Encoder_DestroyChn(MAIN_JPEG_ENC_CHN);
    IMP_Encoder_UnRegisterChn(THUMB_JPEG_ENC_CHN);
    IMP_Encoder_DestroyChn(THUMB_JPEG_ENC_CHN);
}

/* ---- Save encoder stream to file ---- */
static int save_stream_to_file(const char *path, IMPEncoderStream *stream)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        IMP_LOG_ERR(TAG, "open %s failed: %s\n", path, strerror(errno));
        return -1;
    }
    int i;
    for (i = 0; i < stream->packCount; i++) {
        write(fd, (void *)stream->pack[i].virAddr, stream->pack[i].length);
    }
    close(fd);
    return 0;
}

/* ---- Capture one JPEG frame from an encoder channel ---- */
static int capture_jpeg(int enc_chn, const char *filepath)
{
    int ret;

    ret = IMP_Encoder_StartRecvPic(enc_chn);
    if (ret < 0) {
        IMP_LOG_ERR(TAG, "StartRecvPic(%d) failed: %d\n", enc_chn, ret);
        return -1;
    }

    ret = IMP_Encoder_PollingStream(enc_chn, SNAP_TIMEOUT_MS);
    if (ret < 0) {
        IMP_LOG_ERR(TAG, "PollingStream(%d) timeout\n", enc_chn);
        IMP_Encoder_StopRecvPic(enc_chn);
        return -1;
    }

    IMPEncoderStream stream;
    ret = IMP_Encoder_GetStream(enc_chn, &stream, 1);
    if (ret < 0) {
        IMP_LOG_ERR(TAG, "GetStream(%d) failed: %d\n", enc_chn, ret);
        IMP_Encoder_StopRecvPic(enc_chn);
        return -1;
    }

    save_stream_to_file(filepath, &stream);
    IMP_LOG_INFO(TAG, "Saved chn%d -> %s (%d packs)\n", enc_chn, filepath, stream.packCount);

    IMP_Encoder_ReleaseStream(enc_chn, &stream);
    IMP_Encoder_StopRecvPic(enc_chn);

    return 0;
}

/* ---- Thread args ---- */
struct snap_args {
    int enc_chn;
    int snap_index;
    const char *suffix;   /* "main" or "thumb" */
};

/* ---- Main ---- */
int main(int argc, char *argv[])
{
    int ret, i;

    if (argc >= 2) num_snaps = atoi(argv[1]);
    if (num_snaps < 1) num_snaps = 1;
    if (argc >= 3) strncpy(output_dir, argv[2], sizeof(output_dir) - 1);

    IMP_LOG_INFO(TAG, "=== Dual JPEG (CH0 main + CH1 thumb) with IVDC ===\n");
    IMP_LOG_INFO(TAG, "snaps=%d, output=%s\n", num_snaps, output_dir);

    /* Create output directory */
    mkdir(output_dir, 0777);

    /* Override channel config */
    override_channels();

    /* Force direct_switch for IVDC */
    direct_switch = 2;  /* Enable IVDC for CH0 and CH1 */

    /* Step 1: System init */
    ret = sample_system_init();
    if (ret < 0) {
        IMP_LOG_ERR(TAG, "System init failed\n");
        return -1;
    }

    /* Step 2: FrameSource init (creates + sets attr for CH0 and CH1) */
    ret = sample_framesource_init();
    if (ret < 0) {
        IMP_LOG_ERR(TAG, "FrameSource init failed\n");
        return -1;
    }

    /* Step 3: Create encoder groups for CH0 and CH1 */
    ret = IMP_Encoder_CreateGroup(0);
    if (ret < 0) {
        IMP_LOG_ERR(TAG, "CreateGroup(0) failed\n");
        return -1;
    }
    ret = IMP_Encoder_CreateGroup(1);
    if (ret < 0) {
        IMP_LOG_ERR(TAG, "CreateGroup(1) failed\n");
        return -1;
    }

    /* Step 4: Custom dual JPEG init (skip sample_video_init and sample_jpeg_init) */
    ret = dual_jpeg_init();
    if (ret < 0) {
        IMP_LOG_ERR(TAG, "Dual JPEG init failed\n");
        return -1;
    }

    /* Step 5: Bind FrameSource -> Encoder */
    /* CH0 -> encoder group 0 */
    IMPCell fs0 = { DEV_ID_FS, 0, 0 };
    IMPCell enc0 = { DEV_ID_ENC, 0, 0 };
    ret = IMP_System_Bind(&fs0, &enc0);
    if (ret < 0) {
        IMP_LOG_ERR(TAG, "Bind FS0 -> ENC0 failed\n");
        return -1;
    }
    /* CH1 -> encoder group 1 */
    IMPCell fs1 = { DEV_ID_FS, 1, 0 };
    IMPCell enc1 = { DEV_ID_ENC, 1, 0 };
    ret = IMP_System_Bind(&fs1, &enc1);
    if (ret < 0) {
        IMP_LOG_ERR(TAG, "Bind FS1 -> ENC1 failed\n");
        return -1;
    }

    /* Step 6: Stream on */
    ret = sample_framesource_streamon();
    if (ret < 0) {
        IMP_LOG_ERR(TAG, "StreamOn failed\n");
        return -1;
    }

    /* Step 7: Capture loop */
    IMP_LOG_INFO(TAG, "=== Starting capture: %d snaps ===\n", num_snaps);

    for (i = 0; i < num_snaps; i++) {
        char main_path[512], thumb_path[512];
        snprintf(main_path, sizeof(main_path), "%s/IMG_%04d.jpg", output_dir, i);
        snprintf(thumb_path, sizeof(thumb_path), "%s/IMG_%04d_thumb.jpg", output_dir, i);

        IMP_LOG_INFO(TAG, "--- Snap %d/%d ---\n", i + 1, num_snaps);

        /* Capture main JPEG (CH0, encoder channel 12) */
        ret = capture_jpeg(MAIN_JPEG_ENC_CHN, main_path);
        if (ret < 0) {
            IMP_LOG_ERR(TAG, "Main JPEG capture failed\n");
        }

        /* Capture thumbnail JPEG (CH1, encoder channel 13) */
        ret = capture_jpeg(THUMB_JPEG_ENC_CHN, thumb_path);
        if (ret < 0) {
            IMP_LOG_ERR(TAG, "Thumb JPEG capture failed\n");
        }
    }

    IMP_LOG_INFO(TAG, "=== Capture complete ===\n");

    /* Step 8: Stream off */
    ret = sample_framesource_streamoff();
    if (ret < 0) {
        IMP_LOG_ERR(TAG, "StreamOff failed\n");
    }

    /* Step 9: Unbind */
    IMP_System_UnBind(&fs0, &enc0);
    IMP_System_UnBind(&fs1, &enc1);

    /* Step 10: Cleanup */
    dual_jpeg_exit();
    IMP_Encoder_DestroyGroup(0);
    IMP_Encoder_DestroyGroup(1);
    sample_framesource_exit();
    sample_system_exit();

    IMP_LOG_INFO(TAG, "=== Done ===\n");
    return 0;
}
