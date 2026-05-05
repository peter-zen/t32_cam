#ifndef __USER_RESIZE_SIMD_H__
#define __USER_RESIZE_SIMD_H__

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <mxu2.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#define NUM_ALIGN(val, T) ((((val) + T - 1) / T) * T)

static inline int opencv_resize_crop_simd(uint8_t *src_base, int src_w, int src_h, uint8_t *dst_base_ptr, int dst_w, int dst_h);

#endif
