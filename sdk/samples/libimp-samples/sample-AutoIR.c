/*
 * sample_AutoIR.c
 *
 * Copyright (C) 2014 Ingenic Semiconductor Co.,Ltd
 *
 * The specific explanations for all the APIs called in this file can be found in the header files under the directory "proj/sdk-lv3/include/api/cn/imp/".
 *
 * */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <imp/imp_log.h>
#include <imp/imp_common.h>
#include <imp/imp_system.h>
#include <imp/imp_framesource.h>
#include <imp/imp_encoder.h>

#include "sample-common.h"

#define TAG "Sample_AutoIR"
#define AUTOIR_DEBUG

// Debug macros
#ifdef AUTOIR_DEBUG
#define IR_DEBUG(fmt, ...) printf("[debug] " fmt, ##__VA_ARGS__)
#else
#define IR_DEBUG(fmt, ...)
#endif

extern struct chn_conf chn[];
static int byGetFd = 0;
static int g_soft_ps_running = 1;

/* IR CUT Control */
int sample_set_ircut(int enable)
{
	/*
	   IRCUTN = PB17
	   IRCUTP = PB18
	*/
	int fd = -1, fd0 = -1, fd1 = -1;
	char on[4], off[4];

	int gpio0 = 61;
	int gpio1 = 62;
	char tmp[128];

	if (!access("/tmp/setir",0)) {
		if (enable) {
			system("/tmp/setir 0 1");
		} else {
			system("/tmp/setir 1 0");
		}
		return 0;
	}

	fd = open("/sys/class/gpio/export", O_WRONLY);
	if(fd < 0) {
		IMP_LOG_DBG(TAG, "open /sys/class/gpio/export error !");
		return -1;
	}

	sprintf(tmp, "%d", gpio0);
	write(fd, tmp, 2);
	sprintf(tmp, "%d", gpio1);
	write(fd, tmp, 2);

	close(fd);

	sprintf(tmp, "/sys/class/gpio/gpio%d/direction", gpio0);
	fd0 = open(tmp, O_RDWR);
	if(fd0 < 0) {
		IMP_LOG_DBG(TAG, "open /sys/class/gpio/gpio%d/direction error !", gpio0);
		return -1;
	}

	sprintf(tmp, "/sys/class/gpio/gpio%d/direction", gpio1);
	fd1 = open(tmp, O_RDWR);
	if(fd1 < 0) {
		IMP_LOG_DBG(TAG, "open /sys/class/gpio/gpio%d/direction error !", gpio1);
		return -1;
	}

	write(fd0, "out", 3);
	write(fd1, "out", 3);

	close(fd0);
	close(fd1);

	sprintf(tmp, "/sys/class/gpio/gpio%d/active_low", gpio0);
	fd0 = open(tmp, O_RDWR);
	if(fd0 < 0) {
		IMP_LOG_DBG(TAG, "open /sys/class/gpio/gpio%d/active_low error !", gpio0);
		return -1;
	}

	sprintf(tmp, "/sys/class/gpio/gpio%d/active_low", gpio1);
	fd1 = open(tmp, O_RDWR);
	if(fd1 < 0) {
		IMP_LOG_DBG(TAG, "open /sys/class/gpio/gpio%d/active_low error !", gpio1);
		return -1;
	}

	write(fd0, "0", 1);
	write(fd1, "0", 1);

	close(fd0);
	close(fd1);

	sprintf(tmp, "/sys/class/gpio/gpio%d/value", gpio0);
	fd0 = open(tmp, O_RDWR);
	if(fd0 < 0) {
		IMP_LOG_DBG(TAG, "open /sys/class/gpio/gpio%d/value error !", gpio0);
		return -1;
	}

	sprintf(tmp, "/sys/class/gpio/gpio%d/value", gpio1);
	fd1 = open(tmp, O_RDWR);
	if(fd1 < 0) {
		IMP_LOG_DBG(TAG, "open /sys/class/gpio/gpio%d/value error !", gpio1);
		return -1;
	}

	sprintf(on, "%d", enable);
	sprintf(off, "%d", !enable);

	write(fd0, "0", 1);
	usleep(10*1000);

	write(fd0, on, strlen(on));
	write(fd1, off, strlen(off));

	if (!enable) {
		usleep(10*1000);
		write(fd0, off, strlen(off));
	}

	close(fd0);
	close(fd1);

	return 0;
}

/* Default threshold */
#define NIGHT_BV_THRESHOLD 759527  /* day to night bv threshold */
#define DAY_BV_THRESHOLD 898888  /* night to day bv threshold */
#define DAY_RGB_DIFF_THRESHOLD 48 /* night切day时需满足的RGB统计值阈值 */
#define DAY_RG_DIFF_THRESHOLD 30 /* night切day时需满足的R/B统计值阈值 */
#define DAY_BG_DIFF_THRESHOLD 30 /* night切day时需满足的B/G统计值阈值 */
#define DAY_SAT_THRESHOLD 23 /* night切day时需满足的sat阈值 */
#define COUNT_LIMIT 15  /*连续满足切换条件的次数阈值*/
#define AE_STABLE_TARGET_THRESHOLD  2  /* 判断AE稳定状态的Target阈值 */
#define AE_STABLE_BV_THRESHOLD  3000  /* 判断AE稳定状态的BV变化阈值 */

static unsigned int glo_rg = 256;
static unsigned int glo_bg = 256;
static unsigned int sat = 0;
static unsigned int bv = 0;

static unsigned int max_of_three(unsigned int a, unsigned int b, unsigned int c) {
        unsigned int max = a;
        if (b > max) max = b;
        if (c > max) max = c;

        return max;
}

static unsigned int min_of_three(unsigned int a, unsigned int b, unsigned int c) {
        unsigned int min = a;
        if (b < min) min = b;
        if (c < min) min = c;

        return min;
}

/* RGB 分区统计值获取 */
static int get_awb_statistics()
{
        int ret = 0;
        unsigned int avg_r=0,avg_g=0,avg_b=0;
        unsigned int sum_r=0, sum_g=0, sum_b=0;
        unsigned int drop_count=0;
        IMPISPAWBStatisInfo awb_zone;
	IMPVI_NUM vinum = IMPVI_MAIN;

        /* 获取15x15或32x32每个分区的R、G、B统计均值*/
        ret = IMP_ISP_Tuning_GetAwbStatistics(vinum, &awb_zone);
        if (ret == 0) {
                for (int i = 0; i < 15; i++) {
                        for (int j = 0; j < 15; j++) {
                                /* 丢掉过曝及过暗点，注意awb阈值参数要配合修改 */
                                if ((255 == awb_zone.awb_r.zone[i][j]) || (255 == awb_zone.awb_g.zone[i][j]) || (255 == awb_zone.awb_b.zone[i][j])
                                        || (0 == awb_zone.awb_r.zone[i][j]) || (0 == awb_zone.awb_g.zone[i][j]) || (0 == awb_zone.awb_b.zone[i][j])){
                                        sum_r += 0;
                                        sum_g += 0;
                                        sum_b += 0;
                                        drop_count += 1;
                                } else {
                                        sum_r += awb_zone.awb_r.zone[i][j];
                                        sum_g += awb_zone.awb_g.zone[i][j];
                                        sum_b += awb_zone.awb_b.zone[i][j];
                                }
                        }
                }
                if (225 == drop_count) {
                        glo_rg = 256;
                        glo_bg = 256;
                        sat = 0;
                        printf("## drop_count %d, completely overexposure or dark!! \n",drop_count);

                        return 0;
                }
        } else {
                return 0;
        }
        /* 获取 avg r g b (10bit) 及饱和度近似值  */
        avg_r = (unsigned int)((double)(sum_r)/(double)(225.0-drop_count)*4.0);
        avg_g = (unsigned int)((double)(sum_g)/(double)(225.0-drop_count)*4.0);
        avg_b = (unsigned int)((double)(sum_b)/(double)(225.0-drop_count)*4.0);
        if ((avg_r > avg_g) && (avg_b > avg_g)) {
                sat = 0; /* 强红外场景，sat置为0 */
        } else {
                sat = max_of_three(avg_r,avg_g,avg_b) - min_of_three(avg_r,avg_g,avg_b);
        }
        /* 计算r/g 和 b/g的大小，建议直接用sum计算，256为1x */
        glo_rg = (unsigned int)((double)(sum_r) / (double)(sum_g) * 256.0);
        glo_bg = (unsigned int)((double)(sum_b) / (double)(sum_g) * 256.0);
        IR_DEBUG("avg_r=%d avg_g=%d avg_b=%d\n",avg_r,avg_g,avg_b);
        IR_DEBUG("glo_r/g=%d, glo_b/g=%d, sat=%d\n",glo_rg,glo_bg,sat);
        IR_DEBUG("drop_count=%d\n",drop_count);

        return 0;
}

/* AE 稳定状态判断
 * 0：不稳定
 * 1: 开始稳定
 * 2：接近稳定
 * 3：稳定
 */
static int get_ae_stable_stage()
{
        int ret = 0;
        unsigned int bv0=0;
        unsigned int bv1=0;
        IMPISPAeOnlyReadAttr aeinfo;
	IMPVI_NUM vinum = IMPVI_MAIN;

        /* 获取两帧 aeinfo */
        ret = IMP_ISP_Tuning_GetAeOnlyReadAttr(vinum, &aeinfo);
        bv0 = aeinfo.bv;
        usleep(100000);
        ret += IMP_ISP_Tuning_GetAeOnlyReadAttr(vinum, &aeinfo);
        bv1 = aeinfo.bv;
        if (ret == 0) {
                /* 刷新当前BV值 */
                bv = bv1;
                IR_DEBUG("## stable=%d,target=%d,ae_mean=%d,bv0=%d,bv1=%d\n",aeinfo.stable,aeinfo.target,aeinfo.ae_mean,bv0,bv1);

                /* 获取AE收敛状态，1表示AE收敛结束，处于稳定状态 */
                if (1 == aeinfo.stable) {
                        return 3;
                        /* 比较target和计算亮度的差值，在一定范围内则认为接近稳定状态 */
                } else if (AE_STABLE_TARGET_THRESHOLD > (unsigned int)abs((int)aeinfo.ae_mean - (int)aeinfo.target)) {
                        return 2;
                        /* 比较前后两帧的bv差异，bv变化较小，说明环境光趋于稳定，RGB统计值可用 */
                } else if (AE_STABLE_BV_THRESHOLD > ((bv0 > bv1) ? (bv0 - bv1) : (bv1 - bv0))) {
                        return 1;
                } else {
                        return 0;
                }
        } else {
                return 0;
        }
}

static void *sample_autoir_thread(void *p)
{
	IMPVI_NUM vinum = IMPVI_MAIN;
        IMPISPAeOnlyReadAttr aeinfo;
        IMPISPRunningMode pmode;

        unsigned int rg_threshold=0, bg_threshold=0;
        unsigned diff_rg = 0, diff_bg = 0;
        int night_cnt = 0;
        int day_cnt = 0;
        int ae_stage = 0;
        int ret = 0;

        const char *day = "/tmp/iq/day";
        const char *night = "/tmp/iq/night";
        const char *autoir = "/tmp/iq/autoir";

	/* start from day mode */
	pmode = IMPISP_RUNNING_MODE_DAY;
	ret = IMP_ISP_Tuning_SetISPRunningMode(vinum, &pmode);
	sample_set_ircut(0);

        while (g_soft_ps_running) {
                /* AutoIR模式：创建/tmp/iq/autoir 即可 */
                if (access(autoir, F_OK) != -1) {
                        system("rm -rf /tmp/iq/day");
                        system("rm -rf /tmp/iq/night");
                        printf("## auto ir mode\n");
                        /* auto模式下，每次循环开始，获取当前的bv信息 */
                        ret = IMP_ISP_Tuning_GetAeOnlyReadAttr(vinum, &aeinfo);
                        if (0 == ret) {
                                bv = aeinfo.bv;
                                IR_DEBUG("bv=%d ev=%lld\n",aeinfo.bv,aeinfo.ExposureValue);
                        } else {
                                return NULL;
                        }

                        /* day 切 night 判断逻辑 */
                        if (pmode == IMPISP_RUNNING_MODE_DAY) {
                                printf("##night_cnt=%d\n",night_cnt);
                                if (bv < NIGHT_BV_THRESHOLD) {
                                        /* 满足切换条件时增加计数 */
                                        if ((++night_cnt > COUNT_LIMIT)) {
                                                /* 连续多次满足条件，切到night模式*/
                                                pmode = IMPISP_RUNNING_MODE_NIGHT;
                                                IMP_ISP_Tuning_SetISPRunningMode(vinum, &pmode);
                                                sample_set_ircut(1);

                                                /*切到night模式后，等待AE收敛基本稳定,
                                                 * 记录此时的状态，作为切回day模式的参考阈值
                                                 */
                                                usleep(1000000); /* 加延迟等待收敛稳定 */
                                                /* 确保AE基本稳定 */
                                                do {
                                                        usleep(100000);
                                                        ae_stage = get_ae_stable_stage();
                                                        printf("ae_stage=%d\n",ae_stage);
                                                } while (ae_stage < 2);

                                                /* 记录稳定后的RGB统计值作为night切day的参考阈值*/
                                                get_awb_statistics();
                                                rg_threshold = glo_rg;
                                                bg_threshold = glo_bg;
                                                printf("threshold r/g=%d b/g=%d\n",rg_threshold, bg_threshold);
                                                night_cnt = 0;
                                        }
                                } else {
                                        night_cnt = 0;
                                }
                        }

                        /* night 切 day 判断逻辑 */
                        if (pmode == IMPISP_RUNNING_MODE_NIGHT) {
                                printf("## day_cnt=%d\n",day_cnt);
                                get_awb_statistics();
                                /* 记录 r/g、b/g 的变化趋势 */
                                diff_rg = (glo_rg > rg_threshold) ? (glo_rg - rg_threshold) : (rg_threshold - glo_rg);
                                diff_bg = (glo_bg > bg_threshold) ? (glo_bg - bg_threshold) : (bg_threshold - glo_bg);
                                IR_DEBUG("diff r/g=%d b/g=%d\n",diff_rg, diff_bg);
                                IR_DEBUG("threshold r/g=%d b/g=%d\n",rg_threshold, bg_threshold);

                                /* 满足BV、SAT、Diff的判断条件时增加计数
                                 * 可见光增加，Diff和Sat、BV有升高趋势
                                 * 针对红外补光较弱场景，Diff变化不可信，放宽阈值，仅用BV、Sat即可
                                 */
                                if ((bv > DAY_BV_THRESHOLD && sat > DAY_SAT_THRESHOLD
                                                        && (diff_rg + diff_bg > DAY_RGB_DIFF_THRESHOLD  || diff_bg > DAY_BG_DIFF_THRESHOLD))
                                                || (sat > DAY_SAT_THRESHOLD +14  && bv > DAY_BV_THRESHOLD + 102400)) {
                                        if (++day_cnt > COUNT_LIMIT) {
                                                /* 切到day模式*/
                                                sample_set_ircut(0);
                                                pmode = IMPISP_RUNNING_MODE_DAY;
                                                IMP_ISP_Tuning_SetISPRunningMode(vinum, &pmode);
                                                day_cnt = 0;
                                        }
                                } else {
                                        day_cnt = 0;
                                }
                        }
                        /* 手动 day 模式 : /tmp/iq/下无autoir，且有day时生效 */
                } else if ((access(day, F_OK) != -1) && (pmode != IMPISP_RUNNING_MODE_DAY)) {
                        printf("## manual to day mode\n");
                        pmode = IMPISP_RUNNING_MODE_DAY;
                        ret = IMP_ISP_Tuning_SetISPRunningMode(vinum, &pmode);
                        if (ret == 0) {
                                sample_set_ircut(0);
                                system("rm -rf /tmp/iq/day");
                                /* 切至day模式之后，待AE基本稳定，记录EV和BV值 */
                                do {
                                        usleep(1000000);
                                        ae_stage = get_ae_stable_stage();
                                        printf("ae_stage=%d\n",ae_stage);
                                } while (ae_stage < 2);
                        } else {
                                return NULL;
                        }

                        /* 手动 night 模式 : /tmp/iq/下无autoir，且有night时生效 */
                } else if ((access(night, F_OK) != -1) && (pmode != IMPISP_RUNNING_MODE_NIGHT)) {
                        printf("## manual to night mode\n");
                        pmode = IMPISP_RUNNING_MODE_NIGHT;
                        ret = IMP_ISP_Tuning_SetISPRunningMode(vinum, &pmode);
                        if (ret == 0) {
                                sample_set_ircut(1);
                                system("rm -rf /tmp/iq/night");
                                /* 切至night模式之后，待AE基本稳定，记录EV、BV及RGB统计值 */
                                do {
                                        usleep(1000000);
                                        ae_stage = get_ae_stable_stage();
                                        printf("ae_stage=%d\n",ae_stage);
                                } while (ae_stage < 2);
                                get_awb_statistics();
                        } else {
                                return NULL;
                        }
                }

                usleep(40000);
        }

        return NULL;
}

pthread_t tid[FS_CHN_NUM];
int main(int argc, char *argv[])
{
        int i, ret;

        if (argc >= 2) {
                byGetFd = atoi(argv[1]);
        }

        /* Step.1 System init */
        ret = sample_system_init();
        if (ret < 0) {
                IMP_LOG_ERR(TAG, "IMP_System_Init() failed\n");
                return -1;
        }

        /* Step.2 FrameSource init */
        ret = sample_framesource_init();
        if (ret < 0) {
                IMP_LOG_ERR(TAG, "FrameSource init failed\n");
                return -1;
        }

        /* Step.3 Encoder init */
        for (i = 0; i < FS_CHN_NUM; i++) {
                if (chn[i].enable) {
                        ret = IMP_Encoder_CreateGroup(chn[i].index);
                        if (ret < 0) {
                                IMP_LOG_ERR(TAG, "IMP_Encoder_CreateGroup(%d) error !\n", chn[i].index);
                                return -1;
                        }
                }
        }

        ret = sample_video_init();
        if (ret < 0) {
                IMP_LOG_ERR(TAG, "Video init failed\n");
                return -1;
        }

        /* Step.4 Bind */
        for (i = 0; i < FS_CHN_NUM; i++) {
                if (chn[i].enable) {
                        ret = IMP_System_Bind(&chn[i].framesource_chn, &chn[i].imp_encoder);
                        if (ret < 0) {
                                IMP_LOG_ERR(TAG, "Bind FrameSource channel%d and Encoder failed\n",i);
                                return -1;
                        }
                }
        }

        /* Step.5 Stream On */
        ret = sample_framesource_streamon();
        if (ret < 0) {
                IMP_LOG_ERR(TAG, "ImpStreamOn failed\n");
                return -1;
        }

        /* Step.6 Create autoir thread */
        ret = pthread_create(&tid[i], NULL, sample_autoir_thread, (void *)argv);
        if (ret < 0) {
                IMP_LOG_ERR(TAG, "Create sample_autoir_thread failed\n");
        }

        /* Step.7 Get stream */
        if (byGetFd) {
                ret = sample_get_video_stream_byfd();
                if (ret < 0) {
                        IMP_LOG_ERR(TAG, "Get video stream byfd failed\n");
                        return -1;
                }
        } else {
                ret = sample_start_get_video_stream();
                if (ret < 0) {
                        IMP_LOG_ERR(TAG, "Get video stream failed\n");
                        return -1;
                }
                sample_stop_get_video_stream();
        }

        /* Exit sequence as follow */
        /* Step.a Stream Off */
        ret = sample_framesource_streamoff();
        if (ret < 0) {
                IMP_LOG_ERR(TAG, "FrameSource StreamOff failed\n");
                return -1;
        }

        /* Step.b UnBind */
        for (i = 0; i < FS_CHN_NUM; i++) {
                if (chn[i].enable) {
                        ret = IMP_System_UnBind(&chn[i].framesource_chn, &chn[i].imp_encoder);
                        if (ret < 0) {
                                IMP_LOG_ERR(TAG, "UnBind FrameSource channel%d and Encoder failed\n",i);
                                return -1;
                        }
                }
        }

        /* Step.c Encoder exit */
        ret = sample_video_exit();
        if (ret < 0) {
                IMP_LOG_ERR(TAG, "Video exit failed\n");
                return -1;
        }

        for (i = 0; i < FS_CHN_NUM; i++) {
                if (chn[i].enable) {
                        ret = IMP_Encoder_DestroyGroup(chn[i].index);
                        if (ret < 0) {
                                IMP_LOG_ERR(TAG, "Encoder DestroyGroup(%d) failed\n", chn[i].index);
                                return -1;
                        }
                }
        }

        /* Step.d FrameSource exit */
        ret = sample_framesource_exit();
        if (ret < 0) {
                IMP_LOG_ERR(TAG, "FrameSource exit failed\n");
                return -1;
        }

        /* Step.e System exit */
        ret = sample_system_exit();
        if (ret < 0) {
                IMP_LOG_ERR(TAG, "sample_system_exit() failed\n");
                return -1;
        }

        return 0;
}
