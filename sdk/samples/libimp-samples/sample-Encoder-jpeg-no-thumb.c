/*
	sample-Encoder-jpeg-no-thumb.c — drops CH2 thumbnail (main JPEG only)

	Copyright (C) 2014 Ingenic Semiconductor Co.,Ltd
	The specific instructions for all API calls in this file can be found in the header file of the proj/sdk-lv3/include/api/cn/imp/

	This sample demonstrates how to capture JPEG snap: main(jpeg)
			(note：
					chn0----        chn3----        chn6----        chn0----
					chn1---|Main    chn4---|Sec     chn7---|Thr     chn1---|Four
					chn2----        chn5----        chn8----        chn2----
			)
	direct mode can be selected based on direct_switch.
	direct_switch = 1, One direct through: In this case,
		only the main stream of main camera can pass through directly, while the secondary stream of main camera can pass through non directly
	direct_switch = 2, Two direct through: In this case,
		only the main stream of main camera can pass through directly, while the secondary stream of main camera can pass through non directly;
		only the main stream of second camera can pass through directly, while the secondary stream of second camera can pass through non directly;
	direct_switch = 3, Three direct through: In this case,
		only the main stream of main camera can pass through directly, while the secondary stream of main camera can pass through non directly;
		only the main stream of second camera can pass through directly, while the secondary stream of second camera can pass through non directly;
		only the main stream of third camera can pass through directly, while the secondary stream of third camera can pass through non directly;
	direct_switch = 4, Four direct through: In this case,
		only the main stream of main camera can pass through directly, while the secondary stream of main camera can pass through non directly;
		only the main stream of second camera can pass through directly, while the secondary stream of second camera can pass through non directly;
		only the main stream of third camera can pass through directly, while the secondary stream of third camera can pass through non directly;
		only the main stream of fourth camera can pass through directly, while the secondary stream of fourth camera can pass through non directly;
*/

#include "sample-common.h"
#include <sys/stat.h>   /* mkdir("/tmp/media") */
#include <stdio.h>      /* fprintf(stderr) — bypass alog (IMP_LOG_* -> server ringbuf, invisible on serial) */

#define TAG "sample-Encoder-jpeg-no-thumb"

extern struct chn_conf chn[];
extern int direct_switch;

/* Toggle the AE-ready wait before capture.
 *   1 = wait until ae_mean converges to target (the real fix; logs trajectory
 *       + dropped-frame count).
 *   0 = capture the VERY FIRST frame with NO AE wait — to see what an
 *       un-converged frame looks like. Frame 1 is saved by get_jpeg_stream as
 *       /tmp/media/snap-12-2560x1440-0.jpg (-1/-2 = frames 2/3). */
#define WAIT_AE_READY         1

#if WAIT_AE_READY
/*
 * Wait for AE convergence by polling AE mean toward target — replaces the blind
 * sleep(SLEEP_TIME). The sensor was enabled back in sample_system_init() (Step.1),
 * so AE has already been converging during Steps 2-5; this usually returns far
 * sooner than the old fixed 5 s.
 *
 * Judgement: |ae_mean - target| < AE_MEAN_TOL sustained for AE_SETTLE_NEED
 * consecutive polls. The `stable` flag is logged but NOT used as a gate: on
 * gc4653 + this SDK it never asserts (verified: stable==0 for 5 s straight
 * while mean clearly converges). The sustained window also filters the bogus
 * first ~250 ms (a coincidental near-target reading before the AE does its
 * real exposure sweep — mean crashes to ~1 then climbs back).
 *
 * Dropped-frame count: primes the JPEG encoder (StartRecvPic before ready) so
 * frames flow during convergence; every encoded frame drained here was produced
 * before AE was ready — i.e. a frame we DROP. *dropped_out reports that count.
 * StopRecvPic before returning so get_jpeg_stream later does its own clean
 * StartRecvPic on now-converged AE.
 *
 * All diagnostics go to stderr (unbuffered -> serial) because IMP_LOG_* routes
 * to the alog server ring buffer, not the console.
 *
 * Returns ms waited until converged, or -1 on timeout.
 */
#define AE_POLL_INTERVAL_MS   50
#define AE_WAIT_FLOOR_MS       0   /* 0 = poll immediately after StreamOn */
#define AE_WAIT_CAP_MS      5000   /* never wait longer than the old sleep(5) */
#define AE_MEAN_TOL           20   /* |ae_mean - target| tolerance */
#define AE_SETTLE_NEED        8   /* consecutive within-tol polls to call it ready (8*50 = 400ms) */
#define JPEG_ENC_CHN         12   /* main JPEG encoder chn = 12 + chn[0].index (0) */

static int wait_ae_ready(int *dropped_out)
{
	IMPISPAeOnlyReadAttr ae;
	IMPEncoderStream stream;
	int dropped = 0, ready = 0, settle_streak = 0, waited = AE_WAIT_FLOOR_MS;

	if (dropped_out)
		*dropped_out = 0;
	if (AE_WAIT_FLOOR_MS > 0)
		usleep(AE_WAIT_FLOOR_MS * 1000);

	fprintf(stderr, "[%s] step5b: priming encoder chn=%d, polling AE mean->target...\n", TAG, JPEG_ENC_CHN);
	if (IMP_Encoder_StartRecvPic(JPEG_ENC_CHN) < 0) {
		fprintf(stderr, "[%s] AE: StartRecvPic(%d) failed -- cannot count frames\n", TAG, JPEG_ENC_CHN);
		return -1;
	}

	while (waited < AE_WAIT_CAP_MS) {
		/* Drain one encoded frame if ready: this is a pre-converged "dropped" frame. */
		if (IMP_Encoder_PollingStream(JPEG_ENC_CHN, AE_POLL_INTERVAL_MS) >= 0 &&
		    IMP_Encoder_GetStream(JPEG_ENC_CHN, &stream, 1) == 0) {
			dropped++;
			IMP_Encoder_ReleaseStream(JPEG_ENC_CHN, &stream);
		}
		waited += AE_POLL_INTERVAL_MS;

		memset(&ae, 0, sizeof(ae));
		if (IMP_ISP_Tuning_GetAeOnlyReadAttr(IMPVI_MAIN, &ae) != 0) {
			fprintf(stderr, "[%s] AE: GetAeOnlyReadAttr failed @%dms\n", TAG, waited);
			continue;
		}
		int mean_diff = (ae.target == 0) ? 0 : abs((int)ae.ae_mean - (int)ae.target);
		if (mean_diff < AE_MEAN_TOL)
			settle_streak++;
		else
			settle_streak = 0;
		fprintf(stderr, "[%s] AE: stable=%d mean=%u target=%u bv=%u dropped=%d settled=%d/%d -> %s @%dms\n",
		        TAG, ae.stable, ae.ae_mean, ae.target, ae.bv, dropped,
		        settle_streak, AE_SETTLE_NEED,
		        (settle_streak >= AE_SETTLE_NEED) ? "READY" : "...",
		        waited);
		if (settle_streak >= AE_SETTLE_NEED) {
			ready = 1;
			break;
		}
	}

	IMP_Encoder_StopRecvPic(JPEG_ENC_CHN);
	if (dropped_out)
		*dropped_out = dropped;
	if (!ready)
		fprintf(stderr, "[%s] AE: NOT converged after %dms, dropped=%d -- capturing anyway\n",
		        TAG, AE_WAIT_CAP_MS, dropped);
	return ready ? waited : -1;
}
#endif /* WAIT_AE_READY */

//#define JPEGQUAILTY_CHANGE
int main(int argc, char *argv[])
{
	int i = 0;
	int ret = 0;

	fprintf(stderr, "[%s] start (WAIT_AE_READY=%d)\n", TAG, WAIT_AE_READY);

	/* 8M up-scale test #2: relative to stock sample, ONLY change CH0
	 * scaler.outwidth/outheight to 3840x2160. Keep nrVBs=2 (NOT 1 — earlier
	 * nrVBs=1 run gave size=0 even at native 2560x1440: single buffer starves
	 * the FS<->encoder handshake, dropped=1 then stuck). CH0/CH1 stay enabled
	 * as stock. picWidth stays sensor-native 2560x1440. */
	chn[2].enable = 0;
	chn[0].fs_chn_attr.scaler.outwidth = 3840;
	chn[0].fs_chn_attr.scaler.outheight = 2160;

	/* Ensure output dir exists — open() creates files, not dirs. */
	mkdir("/tmp/media", 0777);

	/* Step.1 System init */
	ret = sample_system_init();
	if (ret < 0) {
		fprintf(stderr, "[%s] System init failed\n", TAG);
		return -1;
	}

	/* Step.2 FrameSource init */
	ret = sample_framesource_init();
	if (ret < 0) {
		fprintf(stderr, "[%s] FrameSource init failed\n", TAG);
		return -1;
	}

	/* Step.3 Encoder init */
	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			ret = IMP_Encoder_CreateGroup(chn[i].index);
			if (ret < 0) {
				fprintf(stderr, "[%s] Encoder CreateGroup(%d) failed\n", TAG, chn[i].index);
				return -1;
			}
		}
	}

	ret = sample_jpeg_init();
	if (ret < 0) {
		fprintf(stderr, "[%s] Jpeg init failed\n", TAG);
		return -1;
	}

#ifdef JPEGQUAILTY_CHANGE
	int jpegQp = 20;
	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			IMP_Encoder_SetJpegQp(12+chn[i].index/3, jpegQp);
		}
	}
#endif

	/* Step.4 Bind */
	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			ret = IMP_System_Bind(&chn[i].framesource_chn, &chn[i].imp_encoder);
			if (ret < 0) {
				fprintf(stderr, "[%s] Bind FrameSource%d and Encoder%d failed\n", TAG, chn[i].framesource_chn.groupID, chn[i].imp_encoder.groupID);
				return -1;
			}
		}
	}

	/* Step.5 Stream On */
	ret = sample_framesource_streamon();
	if (ret < 0) {
		fprintf(stderr, "[%s] FrameSource StreamOn failed\n", TAG);
		return -1;
	}

	/* Step.5b AE-ready wait — only when WAIT_AE_READY==1. When 0, fall straight
	 * through to Step.6 and capture the first frame raw (no wait). */
#if WAIT_AE_READY
	{
		int dropped = 0;
		int ae_ms = wait_ae_ready(&dropped);
		fprintf(stderr, "[%s] AE %s in %d ms, dropped %d frame(s) before ready (old fixed wait was %d s)\n",
		        TAG, ae_ms >= 0 ? "ready" : "TIMEOUT", ae_ms, dropped, SLEEP_TIME);
	}
#else
	fprintf(stderr, "[%s] step5b: WAIT_AE_READY=0 -- capturing frame 1 with NO AE wait\n", TAG);
#endif

	/* Step.6 Get Snap (saves NR_JPEG_TO_SAVE=3 frames to /tmp/media/snap-12-2560x1440-{0,1,2}.jpg) */
	ret = sample_start_get_jpeg_stream();
	if (ret < 0) {
		fprintf(stderr, "[%s] Get Jpeg stream failed\n", TAG);
		return -1;
	}
	sample_stop_get_jpeg_stream();

	/* Step.7 Stream Off */
	ret = sample_framesource_streamoff();
	if (ret < 0) {
		fprintf(stderr, "[%s] FrameSource StreamOff failed\n", TAG);
		return -1;
	}

	/* Step.8 UnBind */
	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			ret = IMP_System_UnBind(&chn[i].framesource_chn, &chn[i].imp_encoder);
			if (ret < 0) {
				fprintf(stderr, "[%s] UnBind FrameSource%d and Encoder%d failed\n", TAG, chn[i].framesource_chn.groupID, chn[i].imp_encoder.groupID);
				return -1;
			}
		}
	}

	/* Step.9 Encoder exit */
	ret = sample_jpeg_exit();
	if (ret < 0) {
		fprintf(stderr, "[%s] Jpeg exit failed\n", TAG);
		return -1;
	}

	/* Step.10 FrameSource exit */
	ret = sample_framesource_exit();
	if (ret < 0) {
		fprintf(stderr, "[%s] FrameSource exit failed\n", TAG);
		return -1;
	}

	/* Step.11 System exit */
	ret = sample_system_exit();
	if (ret < 0) {
		fprintf(stderr, "[%s] System exit failed\n", TAG);
		return -1;
	}

	fprintf(stderr, "[%s] done\n", TAG);
	return 0;
}
