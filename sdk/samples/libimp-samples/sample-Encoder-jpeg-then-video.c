/*
	sample-Encoder-jpeg-then-video.c

	Minimal reproducer for the cm==1 (photo=JPEG then record=H264) kernel VPU wedge.

	Hypothesis under test (see reviews/2026-06-24-wm-cm1-step1-instrument.md §8):
	  Doing a FULL JPEG encoder session (CreateChn + capture + DestroyChn) and THEN a
	  H264 encoder session (CreateChn + capture) in one process leaves the VPU (hardware
	  encoder) IRQ/DMA in a corrupted state. The H264 CreateChn re-enables the VPU IRQ
	  (kernel "enable_irq" warning at kernel/irq/manage.c:513 via vpu_open), and the
	  corrupted VPU hangs the kernel on the first H264 frame encode.

	This sample isolates that transition from all wm/scheduler/DB/upload complexity by
	reusing the sample-common helpers. It does NOT touch shared sample-common.c — all
	instrumentation is local.

	Modes (argv[1]):
	  seq         JPEG session, THEN H264 session        (the cm==1 pattern; expect WEDGE)
	  rev         H264 session, THEN JPEG session        (reverse — direction-agnostic?)
	  concurrent  both encoders created BEFORE streamon  (SDK sample-Encoder-video-jpeg flow;
	              no DestroyChn-between — the control; expect NO wedge)

	One run per boot (full sample_system_init/exit). After a wedge the device hard-hangs
	(silent, ctrl-C no recovery) — power-cycle, restart the devctl broker, run the next
	variant.

	Build: cd sdk/samples/libimp-samples && make sample-Encoder-jpeg-then-video
	Deploy: cp sample-Encoder-jpeg-then-video build/bin/
	Run:    devctl run '/mnt/huntcam/bin/sample-Encoder-jpeg-then-video seq'
*/

#include "sample-common.h"
#include <string.h>
#include <sys/stat.h>

#define TAG "jpeg-then-video"

/* TRACE: tagged, line-buffered to stdout so the host-side serial.log pinpoints a hang
 * (last "-->" with no matching "<--" = the IMP call that hung inside the kernel ioctl). */
#define TRACE(...) do { printf("[TRACE/%s] ", TAG); printf(__VA_ARGS__); fflush(stdout); } while (0)

extern struct chn_conf chn[];
extern int direct_switch;

/* Channel layout (single-sensor default config):
 *   chn[0] -> FS group 0 (2560x1440), video encoder chn 0  (payload overridden to PT_H264 below)
 *   chn[2] -> FS group 2 (640x360),   JPEG encoder chn 12  (12 + index/3, created by sample_jpeg_init)
 * sample_jpeg_init touches only enc12/FS2; sample_video_init skips index==2 and touches only enc0/FS0. */

static int bind_chn(int i)   { return IMP_System_Bind(&chn[i].framesource_chn, &chn[i].imp_encoder); }
static int unbind_chn(int i) { return IMP_System_UnBind(&chn[i].framesource_chn, &chn[i].imp_encoder); }

/* CreateGroup/DestroyGroup are group-level bookkeeping (NOT VPU hardware). Done once
 * up-front over the enabled channels so the per-session variable is exactly the encoder
 * channel lifecycle (CreateChn/DestroyChn) — the suspected VPU-poison path. */
static int create_groups(void) {
	int i, rc;
	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			rc = IMP_Encoder_CreateGroup(chn[i].index);
			TRACE("CreateGroup(%d) rc=%d\n", chn[i].index, rc);
			if (rc < 0) return -1;
		}
	}
	return 0;
}

static void destroy_groups(void) {
	int i, rc;
	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			rc = IMP_Encoder_DestroyGroup(chn[i].index);
			TRACE("DestroyGroup(%d) rc=%d\n", chn[i].index, rc);
		}
	}
}

/* Mirror the wm's IngenicVideo::configure SetChnAttr on FS group 0: GetChnAttr -> set
 * scaler.enable + out size, picWidth/Height, crop full-sensor, outFrmRateNum/Den -> SetChnAttr.
 * The reproducer's other modes set the FS attr once (sample_framesource_init) and never
 * reconfigure; the wm reconfigures group 0 for EACH encoder (JPEG fps 15, then H264 fps 30).
 * This tests whether reconfiguring group 0 (scaler/crop/fps) AFTER a JPEG session used it is
 * what leaves the FS in a state that wedges the subsequent H264 capture. */
static int reconfigure_group(int group, int w, int h, int fr_num, int fr_den) {
	IMPFSChnAttr attr;
	int rc, sensor_w, sensor_h;
	rc = IMP_FrameSource_GetChnAttr(group, &attr);
	TRACE("--> GetChnAttr(%d) rc=%d (cur %dx%d)\n", group, rc, attr.picWidth, attr.picHeight);
	if (rc < 0) return -1;
	sensor_w = attr.picWidth;
	sensor_h = attr.picHeight;
	attr.scaler.enable = 1;
	attr.scaler.outwidth = w;
	attr.scaler.outheight = h;
	attr.picWidth = w;
	attr.picHeight = h;
	attr.crop.enable = 1;
	attr.crop.top = 0;
	attr.crop.left = 0;
	attr.crop.width = sensor_w;
	attr.crop.height = sensor_h;
	attr.outFrmRateNum = fr_num;
	attr.outFrmRateDen = fr_den;
	TRACE("--> SetChnAttr(%d) -> %dx%d fps=%d/%d crop=%dx%d\n", group, w, h, fr_num, fr_den, sensor_w, sensor_h);
	rc = IMP_FrameSource_SetChnAttr(group, &attr);
	TRACE("<-- SetChnAttr rc=%d\n", rc);
	return rc;
}

/* JPEG session = cm==1 photo phase: create enc12 -> capture -> destroy enc12. */
static int jpeg_session(void) {
	int rc;
	TRACE(">>> JPEG SESSION START\n");
	TRACE("--> sample_jpeg_init (CreateChn 12 PT_JPEG)\n");
	rc = sample_jpeg_init();
	TRACE("<-- sample_jpeg_init rc=%d\n", rc);
	if (rc < 0) return -1;

	TRACE("--> Bind FS2->enc12\n");
	rc = bind_chn(2);
	TRACE("<-- Bind rc=%d\n", rc);

	TRACE("--> framesource_streamon\n");
	rc = sample_framesource_streamon();
	TRACE("<-- streamon rc=%d\n", rc);

	TRACE("--> start_get_jpeg_stream\n");
	rc = sample_start_get_jpeg_stream();
	TRACE("<-- start_get_jpeg_stream rc=%d\n", rc);
	sleep(2);  /* get_jpeg_thread captures 20 frames (<1s @ 30fps) */
	TRACE("--> stop_get_jpeg_stream\n");
	sample_stop_get_jpeg_stream();
	TRACE("<-- stop_get_jpeg_stream\n");

	TRACE("--> framesource_streamoff\n");
	rc = sample_framesource_streamoff();
	TRACE("<-- streamoff rc=%d\n", rc);

	TRACE("--> UnBind FS2\n");
	rc = unbind_chn(2);
	TRACE("<-- UnBind rc=%d\n", rc);

	/* SUSPECTED VPU-IRQ-POISON STEP: DestroyChn(12) on the JPEG encoder. */
	TRACE("--> sample_jpeg_exit (DestroyChn 12)\n");
	rc = sample_jpeg_exit();
	TRACE("<-- sample_jpeg_exit rc=%d\n", rc);
	TRACE("<<< JPEG SESSION END\n");
	return 0;
}

/* Video session = cm==1 record phase: create enc0 (H264) -> capture -> destroy enc0.
 * THIS is where the wedge fires (if hypothesis holds): the H264 CreateChn re-arms the
 * VPU IRQ (enable_irq warning), and the first PollingStream/GetStream hangs the kernel. */
static int video_session(int reconf) {
	int rc;
	TRACE(">>> VIDEO SESSION START (reconf=%d)\n", reconf);
	if (reconf) {
		TRACE("--> reconfigure_group0 for H264 (2560x1440 fps 30) — wm's SetChnAttr\n");
		reconfigure_group(0, 2560, 1440, 30, 1);
	}
	TRACE("--> sample_video_init (CreateChn 0 H264 — enable_irq warning expected)\n");
	rc = sample_video_init();
	TRACE("<-- sample_video_init rc=%d\n", rc);
	if (rc < 0) return -1;

	TRACE("--> Bind FS0->enc0\n");
	rc = bind_chn(0);
	TRACE("<-- Bind rc=%d\n", rc);

	TRACE("--> framesource_streamon\n");
	rc = sample_framesource_streamon();
	TRACE("<-- streamon rc=%d\n", rc);

	/* WEDGE POINT: the get_video_thread's first IMP_Encoder_PollingStream(0,1000). */
	TRACE("--> start_get_video_stream (FIRST POLL = WEDGE POINT)\n");
	rc = sample_start_get_video_stream();
	TRACE("<-- start_get_video_stream rc=%d\n", rc);
	sleep(3);  /* let it capture a few frames; if wedged, the thread is already hung here */
	TRACE("--> stop_get_video_stream\n");
	sample_stop_get_video_stream();  /* joins; blocks forever if the thread hung in PollingStream */
	TRACE("<-- stop_get_video_stream\n");

	TRACE("--> framesource_streamoff\n");
	rc = sample_framesource_streamoff();
	TRACE("<-- streamoff rc=%d\n", rc);

	TRACE("--> UnBind FS0\n");
	rc = unbind_chn(0);
	TRACE("<-- UnBind rc=%d\n", rc);

	TRACE("--> sample_video_exit\n");
	rc = sample_video_exit();
	TRACE("<-- sample_video_exit rc=%d\n", rc);
	TRACE("<<< VIDEO SESSION END\n");
	return 0;
}

/* JPEG-G0 session = FAITHFUL cm==1 photo main-path: a FULL-RES (2560x1440) JPEG encoder on
 * FS GROUP 0 (chn 12 registered to group 0, not group 2), capture, then destroy. This mirrors
 * the wm's ImageSnap photo: configure payload=JPEG sensor=0 stream=0 group_id=0 channel_id=12
 * 2560x1440 (per hal_trace.log). The bare `seq` mode puts JPEG on group 2 (low-res) and does NOT
 * wedge; this mode tests whether the wm's group-0 full-res-JPEG→H264 reuse is the real trigger. */
static int jpeg_session_g0(int use_ivdc, int reconf) {
	IMPEncoderCHNAttr channel_attr;
	IMPEncoderAttr *enc_attr;
	IMPEncoderRcAttr *rc_attr;
	IMPEncoderStream stream;
	int rc, f;

	TRACE(">>> JPEG-G0 SESSION (FS group 0, full-res, ivdc=%d, reconf=%d) START\n", use_ivdc, reconf);
	if (reconf) {
		TRACE("--> reconfigure_group0 for JPEG (2560x1440 fps 15) — wm's SetChnAttr\n");
		reconfigure_group(0, 2560, 1440, 15, 1);
	}
	memset(&channel_attr, 0, sizeof(channel_attr));
	enc_attr = &channel_attr.encAttr;
	enc_attr->enType = PT_JPEG;
	enc_attr->bufSize = 0;
	enc_attr->profile = 0;
	enc_attr->picWidth  = chn[0].fs_chn_attr.picWidth;   /* 2560 — same as wm photo */
	enc_attr->picHeight = chn[0].fs_chn_attr.picHeight;  /* 1440 */
	rc_attr = &channel_attr.rcAttr;
	rc_attr->attrRcMode.rcMode = ENC_RC_MODE_FIXQP;
	rc_attr->attrRcMode.attrJPEGFixQp.qp = 40;
	channel_attr.bEnableIvdc = use_ivdc ? true : false;   /* wm sets ivdc=1 on photo */

	TRACE("--> CreateChn(12, JPEG %dx%d on group 0, ivdc=%d)\n", enc_attr->picWidth, enc_attr->picHeight, use_ivdc);
	rc = IMP_Encoder_CreateChn(12, &channel_attr);
	TRACE("<-- CreateChn(12) rc=%d\n", rc);
	if (rc < 0) return -1;

	TRACE("--> RegisterChn(group 0, chn 12)   [wm photo registers JPEG to group 0]\n");
	rc = IMP_Encoder_RegisterChn(0, 12);
	TRACE("<-- RegisterChn rc=%d\n", rc);

	TRACE("--> Bind FS0->enc0\n");
	rc = bind_chn(0);
	TRACE("<-- Bind rc=%d\n", rc);

	TRACE("--> IMP_FrameSource_EnableChn(0)\n");
	rc = IMP_FrameSource_EnableChn(0);
	TRACE("<-- EnableChn rc=%d\n", rc);

	/* Capture a few full-res JPEG frames to exercise the VPU for JPEG on group 0. */
	for (f = 0; f < 3; f++) {
		TRACE("--> StartRecvPic(12) frame %d\n", f);
		IMP_Encoder_StartRecvPic(12);
		TRACE("--> PollingStream(12) frame %d\n", f);
		rc = IMP_Encoder_PollingStream(12, 1000);
		TRACE("<-- PollingStream rc=%d\n", rc);
		if (rc < 0) { IMP_Encoder_StopRecvPic(12); break; }
		rc = IMP_Encoder_GetStream(12, &stream, 1);
		TRACE("<-- GetStream rc=%d pack=%d\n", rc, rc >= 0 ? (int)stream.packCount : -1);
		if (rc >= 0) IMP_Encoder_ReleaseStream(12, &stream);
		IMP_Encoder_StopRecvPic(12);
	}

	TRACE("--> IMP_FrameSource_DisableChn(0)\n");
	IMP_FrameSource_DisableChn(0);
	TRACE("<-- DisableChn\n");

	TRACE("--> UnBind FS0\n");
	unbind_chn(0);
	TRACE("<-- UnBind\n");

	TRACE("--> UnRegisterChn(12) + DestroyChn(12)  [wm ~ImageSnap: JPEG teardown]\n");
	IMP_Encoder_UnRegisterChn(12);
	IMP_Encoder_DestroyChn(12);
	TRACE("<-- JPEG-G0 encoder destroyed\n");
	TRACE("<<< JPEG-G0 SESSION END\n");
	return 0;
}

/* PHOTO-WM = faithful wm photo phase (per the wedge hal_trace.log lines 0-79): JPEG main on
 * group 0 (chn 12, 2560x1440, qp40) + JPEG thumbnail on group 2 (chn 14, 320x180, qp60)
 * CONCURRENTLY, capture one of each, then destroy both. This is the ONE remaining difference
 * vs seq-g0-full (which omits the concurrent group-2 thumbnail). If this wedges where
 * seq-g0-full did not, the concurrent group-2 thumbnail (extra VPU encoder) is the residue
 * source that poisons the subsequent H264. */
static int photo_session_wm(int use_ivdc, int reconf, int with_osd) {
	IMPEncoderCHNAttr attr;
	IMPEncoderStream stream;
	int rc;
	int osdH = -1;  /* OSD region handle on group 0 (wm IspOsdManager equivalent) */

	TRACE(">>> PHOTO-WM SESSION (g0 main + g2 thumb concurrent) ivdc=%d reconf=%d osd=%d START\n", use_ivdc, reconf, with_osd);

	/* group 0 main JPEG (chn 12) */
	if (reconf) reconfigure_group(0, 2560, 1440, 15, 1);
	memset(&attr, 0, sizeof(attr));
	attr.encAttr.enType = PT_JPEG; attr.encAttr.bufSize = 0; attr.encAttr.profile = 0;
	attr.encAttr.picWidth = 2560; attr.encAttr.picHeight = 1440;
	attr.rcAttr.attrRcMode.rcMode = ENC_RC_MODE_FIXQP; attr.rcAttr.attrRcMode.attrJPEGFixQp.qp = 40;
	attr.bEnableIvdc = use_ivdc ? true : false;
	TRACE("--> CreateChn(12, JPEG 2560x1440 g0, ivdc=%d)\n", use_ivdc);
	rc = IMP_Encoder_CreateChn(12, &attr); TRACE("<-- CreateChn(12) rc=%d\n", rc); if (rc < 0) return -1;
	TRACE("--> RegisterChn(g0, 12) + Bind g0\n");
	IMP_Encoder_RegisterChn(0, 12);
	bind_chn(0);

	/* group 2 thumbnail JPEG (chn 14) */
	if (reconf) reconfigure_group(2, 320, 180, 15, 1);
	memset(&attr, 0, sizeof(attr));
	attr.encAttr.enType = PT_JPEG; attr.encAttr.bufSize = 0; attr.encAttr.profile = 0;
	attr.encAttr.picWidth = 320; attr.encAttr.picHeight = 180;
	attr.rcAttr.attrRcMode.rcMode = ENC_RC_MODE_FIXQP; attr.rcAttr.attrRcMode.attrJPEGFixQp.qp = 60;
	attr.bEnableIvdc = use_ivdc ? true : false;
	TRACE("--> CreateChn(14, JPEG 320x180 g2, ivdc=%d)\n", use_ivdc);
	rc = IMP_Encoder_CreateChn(14, &attr); TRACE("<-- CreateChn(14) rc=%d\n", rc); if (rc < 0) return -1;
	TRACE("--> RegisterChn(g2, 14) + Bind g2\n");
	IMP_Encoder_RegisterChn(2, 14);
	bind_chn(2);

	/* stream on + start recv both (mirrors wm order) */
	TRACE("--> EnableChn(0) + StartRecvPic(12)\n");
	IMP_FrameSource_EnableChn(0);
	IMP_Encoder_StartRecvPic(12);
	TRACE("--> EnableChn(2) + StartRecvPic(14)\n");
	IMP_FrameSource_EnableChn(2);
	IMP_Encoder_StartRecvPic(14);

	/* OSD region on group 0 (mirrors wm IspOsdManager::ensureRegion — the one wm-photo
	 * element the reproducer still omits). If this arms the VPU IRQ / wedges the later
	 * H264, OSD is the trigger. */
	if (with_osd) {
		TRACE("--> IMP_ISP_Tuning_CreateOsdRgn(0)  [wm OSD engagement]\n");
		osdH = IMP_ISP_Tuning_CreateOsdRgn(0, NULL);
		TRACE("<-- CreateOsdRgn handle=%d\n", osdH);
	}

	/* capture one main + one thumbnail (wm burst=1) */
	TRACE("--> PollingStream(12)\n"); rc = IMP_Encoder_PollingStream(12, 1000); TRACE("<-- rc=%d\n", rc);
	if (rc >= 0) { IMP_Encoder_GetStream(12, &stream, 1); IMP_Encoder_ReleaseStream(12, &stream); }
	TRACE("--> PollingStream(14)\n"); rc = IMP_Encoder_PollingStream(14, 1000); TRACE("<-- rc=%d\n", rc);
	if (rc >= 0) { IMP_Encoder_GetStream(14, &stream, 1); IMP_Encoder_ReleaseStream(14, &stream); }

	/* teardown both (mirrors wm ~ImageSnap order: g2 first, then g0) */
	if (with_osd && osdH >= 0) {
		TRACE("--> IMP_ISP_Tuning_DestroyOsdRgn(0, %d)\n", osdH);
		IMP_ISP_Tuning_DestroyOsdRgn(0, osdH);
		TRACE("<-- DestroyOsdRgn\n");
	}
	TRACE("--> DisableChn(2) + DisableChn(0)\n");
	IMP_FrameSource_DisableChn(2);
	IMP_FrameSource_DisableChn(0);
	TRACE("--> UnRegister+Destroy chn14 + UnBind g2\n");
	IMP_Encoder_UnRegisterChn(14); unbind_chn(2); IMP_Encoder_DestroyChn(14);
	TRACE("--> UnRegister+Destroy chn12 + UnBind g0  [wm ~ImageSnap main teardown]\n");
	IMP_Encoder_UnRegisterChn(12); unbind_chn(0); IMP_Encoder_DestroyChn(12);
	TRACE("<<< PHOTO-WM SESSION END\n");
	return 0;
}

/* Concurrent = SDK sample-Encoder-video-jpeg flow: both encoders created BEFORE streamon,
 * no DestroyChn-between. The control — should NOT wedge if the hypothesis is right. */
static int concurrent_session(void) {
	int rc;
	TRACE(">>> CONCURRENT SESSION START\n");
	TRACE("--> sample_video_init (CreateChn 0)\n");
	rc = sample_video_init();
	TRACE("<-- sample_video_init rc=%d\n", rc);
	TRACE("--> sample_jpeg_init (CreateChn 12)\n");
	rc = sample_jpeg_init();
	TRACE("<-- sample_jpeg_init rc=%d\n", rc);

	TRACE("--> Bind FS0 + FS2\n");
	bind_chn(0);
	bind_chn(2);

	TRACE("--> framesource_streamon\n");
	rc = sample_framesource_streamon();
	TRACE("<-- streamon rc=%d\n", rc);

	TRACE("--> start_get_video_stream + start_get_jpeg_stream\n");
	sample_start_get_video_stream();
	sample_start_get_jpeg_stream();
	sleep(5);
	sample_stop_get_jpeg_stream();
	sample_stop_get_video_stream();

	TRACE("--> framesource_streamoff\n");
	sample_framesource_streamoff();

	unbind_chn(0);
	unbind_chn(2);
	sample_jpeg_exit();
	sample_video_exit();
	TRACE("<<< CONCURRENT SESSION END\n");
	return 0;
}

/* SEQ-SPLIT = anti-singleton: photo and record in SEPARATE IMP sessions within one
 * process (init→photo→exit, then init→record→exit). The SharedVideo singleton (handoff §2)
 * was introduced specifically to AVOID IMP_System_Exit→re-Init wedging per-photo. This mode
 * tests whether that exit→re-Init wedge reproduces in the sample, OR whether fresh-IMP-per-
 * session is viable — a potential "correct flow" that sidesteps persistent-state residue. */
static int run_seq_split(void) {
	int rc;
	direct_switch = 1;  /* IVDC, matching the wm */

	TRACE("--- SESSION 1 (photo): init -> photo -> exit ---\n");
	TRACE("--> sample_system_init\n"); rc = sample_system_init(); TRACE("<-- rc=%d\n", rc); if (rc < 0) return -1;
	if (sample_framesource_init() < 0) { sample_system_exit(); return -1; }
	chn[0].payloadType = PT_H264;
	if (create_groups() < 0) { sample_framesource_exit(); sample_system_exit(); return -1; }
	photo_session_wm(1, 1, 0);
	destroy_groups();
	sample_framesource_exit();
	TRACE("--> sample_system_exit (end session 1) — then RE-INIT for record\n");
	sample_system_exit();

	TRACE("--- SESSION 2 (record): init -> record -> exit ---\n");
	TRACE("--> sample_system_init (RE-INIT — the exit→re-Init wedge point)\n");
	rc = sample_system_init();
	TRACE("<-- RE-INIT rc=%d\n", rc);
	if (rc < 0) { TRACE("!!! RE-INIT FAILED — exit→re-Init wedge CONFIRMED\n"); return -1; }
	if (sample_framesource_init() < 0) { sample_system_exit(); return -1; }
	chn[0].payloadType = PT_H264;
	if (create_groups() < 0) { sample_framesource_exit(); sample_system_exit(); return -1; }
	video_session(1);
	destroy_groups();
	sample_framesource_exit();
	sample_system_exit();
	TRACE("=== SEQ-SPLIT DONE (exit→re-Init was clean) ===\n");
	return 0;
}

int main(int argc, char *argv[]) {
	const char *mode = (argc > 1) ? argv[1] : "seq";
	int rc;

	printf("\n========== sample-Encoder-jpeg-then-video  mode=%s ==========\n", mode);
	fflush(stdout);

	/* sample-common save_stream writes to /mnt/sdcard/DCIM; ensure it exists. */
	mkdir(STREAM_FILE_PATH_PREFIX, 0777);

	/* seq-split does its own per-session init/exit (two IMP sessions); skip the common path. */
	if (strcmp(mode, "seq-split") == 0) {
		return run_seq_split();
	}

	TRACE("--> sample_system_init\n");
	rc = sample_system_init();
	TRACE("<-- sample_system_init rc=%d\n", rc);
	if (rc < 0) { TRACE("system_init FAILED\n"); return -1; }

	TRACE("--> sample_framesource_init\n");
	rc = sample_framesource_init();
	TRACE("<-- sample_framesource_init rc=%d\n", rc);
	if (rc < 0) { TRACE("framesource_init FAILED\n"); sample_system_exit(); return -1; }

	/* Match the wm cm==1 record payload: H264 (sample default is PT_H265). */
	chn[0].payloadType = PT_H264;
	TRACE("chn[0].payloadType overridden to PT_H264\n");

	if (create_groups() < 0) { TRACE("create_groups FAILED\n"); goto teardown; }

	if (strcmp(mode, "seq") == 0) {
		jpeg_session();
		video_session(0);
	} else if (strcmp(mode, "seq-g0") == 0) {
		jpeg_session_g0(0, 0);
		video_session(0);
	} else if (strcmp(mode, "seq-g0-ivdc") == 0) {
		/* IVDC (ISP-VPU direct connect) on BOTH JPEG (inline attr) and H264
		 * (sample_video_init honors direct_switch=1, sample-common.c:1373). */
		direct_switch = 1;
		jpeg_session_g0(1, 0);
		video_session(0);
	} else if (strcmp(mode, "seq-g0-full") == 0) {
		/* Most faithful to wm: group 0 + IVDC + SetChnAttr reconfigure (scaler/crop/fps)
		 * on BOTH the JPEG and the H264, exactly as IngenicVideo::configure does.
		 * seq-g0-ivdc (no SetChnAttr) was clean; this isolates the wm's SetChnAttr. */
		direct_switch = 1;
		jpeg_session_g0(1, 1);
		video_session(1);
	} else if (strcmp(mode, "seq-g0-wm") == 0) {
		/* FULL wm photo: group-0 main JPEG + group-2 thumbnail JPEG CONCURRENT (chn 12 + 14),
		 * + IVDC + SetChnAttr, exactly per the wedge hal_trace.log. seq-g0-full (no thumbnail)
		 * was clean; this adds the one remaining wm-photo element (concurrent g2 thumbnail). */
		direct_switch = 1;
		photo_session_wm(1, 1, 0);
		video_session(1);
	} else if (strcmp(mode, "seq-g0-wm-osd") == 0) {
		/* seq-g0-wm + OSD region on group 0 (IMP_ISP_Tuning_CreateOsdRgn, mirroring wm
		 * IspOsdManager). The last untested wm-photo element. Tests whether OSD engagement
		 * is what arms the VPU IRQ / wedges the subsequent H264. */
		direct_switch = 1;
		photo_session_wm(1, 1, 1);
		video_session(1);
	} else if (strcmp(mode, "rev") == 0) {
		video_session(0);
		jpeg_session();
	} else if (strcmp(mode, "concurrent") == 0) {
		concurrent_session();
	} else {
		printf("usage: %s [seq|seq-g0|seq-g0-ivdc|seq-g0-full|seq-g0-wm|seq-g0-wm-osd|seq-split|rev|concurrent]\n", argv[0]);
		fflush(stdout);
	}

teardown:
	TRACE("--> destroy_groups\n");
	destroy_groups();
	TRACE("--> sample_framesource_exit\n");
	sample_framesource_exit();
	TRACE("--> sample_system_exit\n");
	rc = sample_system_exit();
	TRACE("<-- sample_system_exit rc=%d\n", rc);
	TRACE("========== DONE ==========\n");
	return 0;
}
