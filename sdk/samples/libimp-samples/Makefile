CONFIG_UCLIBC_BUILD=y

CROSS_COMPILE ?= mips-linux-uclibc-gnu-

CC = $(CROSS_COMPILE)gcc
CPLUSPLUS = $(CROSS_COMPILE)g++
LD = $(CROSS_COMPILE)ld
AR = $(CROSS_COMPILE)ar cr
STRIP = $(CROSS_COMPILE)strip

CFLAGS = $(INCLUDES) -O2 -Wall -march=mips32r2

ifeq ($(CONFIG_UCLIBC_BUILD), y)
	CFLAGS += -muclibc
	LDFLAG += -muclibc
endif

ifeq ($(CONFIG_UCLIBC_BUILD), y)
	SDK_LIB_DIR = ../../lib/uclibc
else
	SDK_LIB_DIR = ../../lib/glibc
endif

SDK_INC_DIR = ../../include

INCLUDES = -I$(SDK_INC_DIR)

LIBS = $(SDK_LIB_DIR)/libimp.a $(SDK_LIB_DIR)/libalog.a

LDFLAG += -Wl,-gc-sections

SAMPLES = sample-Ai-AEC \
		  sample-Encoder-jpeg \
		  sample-GetFrameEx \
		  sample-IVS-unbind-move \
		  sample-Ai \
		  sample-Encoder-OSD \
		  sample-I2d \
		  sample-LCD \
		  sample-Ai-Ref \
		  sample-dmic-aec \
		  sample-Encoder-video \
		  sample-KernEnc-video \
		  sample-Snap-Kernel-yuv \
		  sample-ISP-flip \
		  sample-OSD \
		  sample-Ao \
		  sample-dmic \
		  sample-Encoder-video-jpeg \
		  sample-ISP-InternalChn \
		  sample-Audio \
		  sample-dmic-ref \
		  sample-Encoder-video-Roi \
		  sample-IspOsd \
		  sample-Setfps \
		  sample-AutoZoom \
		  sample-Encoder-changeRcMode \
		  sample-Fcrop \
		  sample-IVS-unbind-base-move \
		  sample-Encoder-changeRes \
		  sample-Framesource \
		  sample-Snap-YUV \
		  sample-Encoder-Ex \
		  sample-Extern-inject-yuv \
		  sample-Encoder-yuv \
		  sample-Temperature \
		  sample-OSD-2BIT \
		  sample-Atlas-Encoder-video \

all: 	$(SAMPLES)

sample-Ai-AEC: $(SDK_LIB_DIR)/libimp.a $(SDK_LIB_DIR)/libalog.a sample-common.o sample-Ai-AEC.o
	$(CPLUSPLUS) $(LDFLAG) -o $@ $^ $(LIBS) -lpthread -lm -lrt -ldl
	$(STRIP) $@

sample-Encoder-jpeg: $(SDK_LIB_DIR)/libimp.a $(SDK_LIB_DIR)/libalog.a sample-common.o sample-Encoder-jpeg.o
	$(CPLUSPLUS) $(LDFLAG) -o $@ $^ $(LIBS) -lpthread -lm -lrt
	$(STRIP) $@

sample-GetFrameEx: $(SDK_LIB_DIR)/libimp.a $(SDK_LIB_DIR)/libalog.a sample-common.o sample-GetFrameEx.o
	$(CPLUSPLUS) $(LDFLAG) -o $@ $^ $(LIBS) -lpthread -lm -lrt
	$(STRIP) $@

sample-IVS-unbind-move: $(SDK_LIB_DIR)/libimp.a $(SDK_LIB_DIR)/libalog.a sample-common.o sample-IVS-unbind-move.o
	$(CPLUSPLUS) $(LDFLAG) -o $@ $^ $(LIBS) -lpthread -lm -lrt
	$(STRIP) $@

sample-Ai: $(SDK_LIB_DIR)/libimp.a $(SDK_LIB_DIR)/libalog.a sample-common.o sample-Ai.o
	$(CPLUSPLUS) $(LDFLAG) -o $@ $^ $(LIBS) -lpthread -lm -lrt -ldl
	$(STRIP) $@

sample-Encoder-OSD: $(SDK_LIB_DIR)/libimp.a $(SDK_LIB_DIR)/libalog.a sample-common.o sample-Encoder-OSD.o
	$(CPLUSPLUS) $(LDFLAG) -o $@ $^ $(LIBS) -lpthread -lm -lrt -ldl
	$(STRIP) $@

sample-I2d: $(SDK_LIB_DIR)/libimp.a $(SDK_LIB_DIR)/libalog.a sample-common.o sample-I2d.o
	$(CPLUSPLUS) $(LDFLAG) -o $@ $^ $(LIBS) -lpthread -lm -lrt -ldl
	$(STRIP) $@

sample-LCD: $(SDK_LIB_DIR)/libimp.a $(SDK_LIB_DIR)/libalog.a sample-common.o sample-LCD.o
	$(CPLUSPLUS) $(LDFLAG) -o $@ $^ $(LIBS) -lpthread -lm -lrt
	$(STRIP) $@

sample-Ai-Ref: $(SDK_LIB_DIR)/libimp.a $(SDK_LIB_DIR)/libalog.a sample-common.o sample-Ai-Ref.o
	$(CPLUSPLUS) $(LDFLAG) -o $@ $^ $(LIBS) -lpthread -lm -lrt -ldl
	$(STRIP) $@

sample-dmic-aec: $(SDK_LIB_DIR)/libimp.a $(SDK_LIB_DIR)/libalog.a sample-common.o sample-dmic-aec.o
	$(CPLUSPLUS) $(LDFLAG) -o $@ $^ $(LIBS) -lpthread -lm -lrt -ldl
	$(STRIP) $@

sample-Encoder-video: $(SDK_LIB_DIR)/libimp.a $(SDK_LIB_DIR)/libalog.a sample-common.o sample-Encoder-video.o
	$(CPLUSPLUS) $(LDFLAG) -o $@ $^ $(LIBS) -lpthread -lm -lrt
	$(STRIP) $@

sample-KernEnc-video: $(SDK_LIB_DIR)/libimp.a $(SDK_LIB_DIR)/libalog.a sample-common.o sample-KernEnc-video.o
	$(CPLUSPLUS) $(LDFLAG) -o $@ $^ $(LIBS) -lpthread -lm -lrt
	$(STRIP) $@

sample-Snap-Kernel-yuv: $(SDK_LIB_DIR)/libimp.a $(SDK_LIB_DIR)/libalog.a sample-common.o sample-Snap-Kernel-yuv.o
	$(CPLUSPLUS) $(LDFLAG) -o $@ $^ $(LIBS) -lpthread -lm -lrt
	$(STRIP) $@

sample-ISP-flip: $(SDK_LIB_DIR)/libimp.a $(SDK_LIB_DIR)/libalog.a sample-common.o sample-ISP-flip.o
	$(CPLUSPLUS) $(LDFLAG) -o $@ $^ $(LIBS) -lpthread -lm -lrt
	$(STRIP) $@

sample-OSD: $(SDK_LIB_DIR)/libimp.a $(SDK_LIB_DIR)/libalog.a sample-common.o sample-OSD.o
	$(CPLUSPLUS) $(LDFLAG) -o $@ $^ $(LIBS) -lpthread -lm -lrt
	$(STRIP) $@

sample-Ao: $(SDK_LIB_DIR)/libimp.a $(SDK_LIB_DIR)/libalog.a sample-common.o sample-Ao.o
	$(CPLUSPLUS) $(LDFLAG) -o $@ $^ $(LIBS) -lpthread -lm -lrt -ldl
	$(STRIP) $@

sample-dmic: $(SDK_LIB_DIR)/libimp.a $(SDK_LIB_DIR)/libalog.a sample-common.o sample-dmic.o
	$(CPLUSPLUS) $(LDFLAG) -o $@ $^ $(LIBS) -lpthread -lm -lrt -ldl
	$(STRIP) $@

sample-Encoder-video-jpeg: $(SDK_LIB_DIR)/libimp.a $(SDK_LIB_DIR)/libalog.a sample-common.o sample-Encoder-video-jpeg.o
	$(CPLUSPLUS) $(LDFLAG) -o $@ $^ $(LIBS) -lpthread -lm -lrt
	$(STRIP) $@

sample-ISP-InternalChn: $(SDK_LIB_DIR)/libimp.a $(SDK_LIB_DIR)/libalog.a sample-common.o sample-ISP-InternalChn.o
	$(CPLUSPLUS) $(LDFLAG) -o $@ $^ $(LIBS) -lpthread -lm -lrt
	$(STRIP) $@

sample-Audio: $(SDK_LIB_DIR)/libimp.a $(SDK_LIB_DIR)/libalog.a sample-common.o sample-Audio.o
	$(CPLUSPLUS) $(LDFLAG) -o $@ $^ $(LIBS) -lpthread -lm -lrt -ldl
	$(STRIP) $@

sample-dmic-ref: $(SDK_LIB_DIR)/libimp.a $(SDK_LIB_DIR)/libalog.a sample-common.o sample-dmic-ref.o
	$(CPLUSPLUS) $(LDFLAG) -o $@ $^ $(LIBS) -lpthread -lm -lrt -ldl
	$(STRIP) $@

sample-Encoder-video-Roi: $(SDK_LIB_DIR)/libimp.a $(SDK_LIB_DIR)/libalog.a sample-common.o sample-Encoder-video-Roi.o
	$(CPLUSPLUS) $(LDFLAG) -o $@ $^ $(LIBS) -lpthread -lm -lrt
	$(STRIP) $@

sample-IspOsd: $(SDK_LIB_DIR)/libimp.a $(SDK_LIB_DIR)/libalog.a sample-common.o sample-IspOsd.o
	$(CPLUSPLUS) $(LDFLAG) -o $@ $^ $(LIBS) -lpthread -lm -lrt
	$(STRIP) $@

sample-Setfps: $(SDK_LIB_DIR)/libimp.a $(SDK_LIB_DIR)/libalog.a sample-common.o sample-Setfps.o
	$(CPLUSPLUS) $(LDFLAG) -o $@ $^ $(LIBS) -lpthread -lm -lrt
	$(STRIP) $@

sample-AutoZoom: $(SDK_LIB_DIR)/libimp.a $(SDK_LIB_DIR)/libalog.a sample-common.o sample-AutoZoom.o
	$(CPLUSPLUS) $(LDFLAG) -o $@ $^ $(LIBS) -lpthread -lm -lrt
	$(STRIP) $@

sample-Encoder-changeRcMode: $(SDK_LIB_DIR)/libimp.a $(SDK_LIB_DIR)/libalog.a sample-common.o sample-Encoder-changeRcMode.o
	$(CPLUSPLUS) $(LDFLAG) -o $@ $^ $(LIBS) -lpthread -lm -lrt
	$(STRIP) $@

sample-Fcrop: $(SDK_LIB_DIR)/libimp.a $(SDK_LIB_DIR)/libalog.a sample-common.o sample-Fcrop.o
	$(CPLUSPLUS) $(LDFLAG) -o $@ $^ $(LIBS) -lpthread -lm -lrt
	$(STRIP) $@

sample-IVS-unbind-base-move: $(SDK_LIB_DIR)/libimp.a $(SDK_LIB_DIR)/libalog.a sample-common.o sample-IVS-unbind-base-move.o
	$(CPLUSPLUS) $(LDFLAG) -o $@ $^ $(LIBS) -lpthread -lm -lrt
	$(STRIP) $@

sample-Encoder-changeRes: $(SDK_LIB_DIR)/libimp.a $(SDK_LIB_DIR)/libalog.a sample-common.o sample-Encoder-changeRes.o
	$(CPLUSPLUS) $(LDFLAG) -o $@ $^ $(LIBS) -lpthread -lm -lrt
	$(STRIP) $@

sample-Framesource: $(SDK_LIB_DIR)/libimp.a $(SDK_LIB_DIR)/libalog.a sample-common.o sample-Framesource.o
	$(CPLUSPLUS) $(LDFLAG) -o $@ $^ $(LIBS) -lpthread -lm -lrt
	$(STRIP) $@

sample-Snap-YUV: $(SDK_LIB_DIR)/libimp.a $(SDK_LIB_DIR)/libalog.a sample-common.o sample-Snap-YUV.o
	$(CPLUSPLUS) $(LDFLAG) -o $@ $^ $(LIBS) -lpthread -lm -lrt
	$(STRIP) $@

sample-Encoder-Ex: $(SDK_LIB_DIR)/libimp.a $(SDK_LIB_DIR)/libalog.a sample-common.o sample-Encoder-Ex.o
	$(CPLUSPLUS) $(LDFLAG) -o $@ $^ $(LIBS) -lpthread -lm -lrt
	$(STRIP) $@

sample-Extern-inject-yuv: $(SDK_LIB_DIR)/libimp.a $(SDK_LIB_DIR)/libalog.a sample-common.o sample-Extern-inject-yuv.o
	$(CPLUSPLUS) $(LDFLAG) -o $@ $^ $(LIBS) -lpthread -lm -lrt
	$(STRIP) $@

sample-Encoder-yuv: $(SDK_LIB_DIR)/libimp.a $(SDK_LIB_DIR)/libalog.a sample-common.o sample-Encoder-yuv.o
	$(CPLUSPLUS) $(LDFLAG) -o $@ $^ $(LIBS) -lpthread -lm -lrt
	$(STRIP) $@

sample-Temperature: $(SDK_LIB_DIR)/libimp.a $(SDK_LIB_DIR)/libalog.a sample-common.o sample-Temperature.o
	$(CPLUSPLUS) $(LDFLAG) -o $@ $^ $(LIBS) -lpthread -lm -lrt
	$(STRIP) $@

sample-OSD-2BIT: $(SDK_LIB_DIR)/libimp.a $(SDK_LIB_DIR)/libalog.a sample-common.o sample-OSD-2BIT.o
	$(CPLUSPLUS) $(LDFLAG) -o $@ $^ $(LIBS) -lpthread -lm -lrt
	$(STRIP) $@

sample-Atlas-Encoder-video: $(SDK_LIB_DIR)/libimp.a $(SDK_LIB_DIR)/libalog.a $(SDK_LIB_DIR)/libsysutils.a sample-common.o sample-Atlas-Encoder-video.o
	$(CPLUSPLUS) $(LDFLAG) -o $@ $^ $(LIBS) $(SDK_LIB_DIR)/libsysutils.a  -lpthread -lm -lrt
	$(STRIP) $@

%.o:%.c sample-common.h
	$(CC) -c $(CFLAGS) $< -o $@

clean:
	rm -f *.o *~

distclean: clean
	rm -f $(SAMPLES)
