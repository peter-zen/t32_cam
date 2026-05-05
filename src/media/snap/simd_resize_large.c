#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <mxu2.h>
#define NUM_ALIGN(val, T) ((((val) + T - 1) / T) * T)
static inline int opencv_resize_crop_simd(uint8_t *src_base, int src_w, int src_h, uint8_t *dst_base_ptr, int dst_w, int dst_h) {

    int box_w = src_w;
    int box_h = src_h;
    uint8_t *src_ybase;
    uint8_t *dst_ybase;
    int iline_size = src_w;
    int oline_size = dst_w;

    int valid_dst_w = dst_w;
    int valid_dst_h = dst_h;
    float inv_scale_x = (float)box_w / (float)valid_dst_w;
    float inv_scale_y = (float)box_h / (float)valid_dst_h;
    int32_t trans_x = (int32_t)((inv_scale_x * 0.5 - 0.5) * 65536);
    int32_t trans_y = (int32_t)((inv_scale_y * 0.5 - 0.5) * 65536);
    int32_t scale_x = inv_scale_x * 65536;
    int32_t scale_y = inv_scale_y * 65536;
    int32_t src_fx = trans_x; // src float x
    int32_t src_fy = trans_y; // src float y
    uint8_t *dst_base;
    int width_cycs = (int)(16 / inv_scale_x); /*cal pixel num one time(odd number)*/
    if (width_cycs < 4) {
        return -1;
    } else {
        width_cycs = 8;
    }

    int malloc_size = ((valid_dst_w + width_cycs - 1) / width_cycs) * width_cycs;
    uint32_t *w1lambda = (uint32_t *)malloc((malloc_size * 10) * sizeof(uint32_t));
    uint32_t *w0lambda = w1lambda + malloc_size;
    uint32_t *w1lambda_uv = w0lambda + malloc_size;
    uint32_t *w0lambda_uv = w1lambda_uv + malloc_size;
    uint32_t *h1lambda = w0lambda_uv + malloc_size;
    uint32_t *h0lambda = h1lambda + malloc_size;
    uint8_t *w_pixel_0 = (uint8_t *)(h0lambda + malloc_size);
    uint8_t *w_pixel_1 = w_pixel_0 + malloc_size;
    uint8_t *w_pixel_uv_0 = w_pixel_1 + malloc_size;
    uint8_t *w_pixel_uv_1 = w_pixel_uv_0 + malloc_size;
    if (w1lambda == NULL || w0lambda == NULL || w1lambda_uv == NULL || w0lambda_uv == NULL ||
            h1lambda == NULL || h0lambda == NULL || w_pixel_0 == NULL || w_pixel_1 == NULL ||
            w_pixel_uv_0 == NULL || w_pixel_uv_1 == NULL) {
        return -1;
    }

    int dst_x = 0;
    for (int cycs_i = 0; cycs_i < ((valid_dst_w + width_cycs - 1) / width_cycs); cycs_i++) {
        for (int i = 0; i < width_cycs; i++) {
            dst_x = cycs_i * width_cycs + i;
            int src_fx_round = src_fx + 16 + dst_x * scale_x;
            int src_x = (src_fx_round) >> 16;
            int bias = (src_x < box_w - 1) ? 1 : 0;
            int w_x1 = (src_fx_round & 0xFFFF) >> 5;
            int w_x0 = 2048 - w_x1;
            w1lambda[dst_x] = w_x1;
            w0lambda[dst_x] = w_x0;
            int start_idx = (cycs_i * width_cycs * scale_x) >> 16;
            /*(2x + 1)<<8 + 2x = 514x + 256*/
            w_pixel_0[dst_x] = (src_x - start_idx) * 2 > 0 ? (src_x - start_idx) * 2 : 0; /*updata shufvb idx*/
            w_pixel_1[dst_x] = (src_x - start_idx + bias) * 2;
            if (dst_x % 2 == 0) {
                start_idx = start_idx & 0xfffe;
                int index = src_x - start_idx > 0 ? src_x - start_idx : 0;
                index = index & 0xfffe;
                index = index >= 0 ? index * 2 : 0;
                w_pixel_uv_0[dst_x] = index;
                w_pixel_uv_0[dst_x + 1] = w_pixel_uv_0[dst_x] + 2;
                w_pixel_uv_1[dst_x] = ((src_x - start_idx + bias) & 0xfffe) * 2;
                w_pixel_uv_1[dst_x + 1] = w_pixel_uv_1[dst_x] + 2;

                w1lambda_uv[dst_x] = w1lambda[dst_x];
                w1lambda_uv[dst_x + 1] = w1lambda[dst_x];
                w0lambda_uv[dst_x] = w0lambda[dst_x];
                w0lambda_uv[dst_x + 1] = w0lambda[dst_x];
            }
        }
    }

    // nv12
    //int src_stride = src_w;
    int dst_stride = dst_w;
    src_ybase = src_base + (int)(src_w * src_h);
    src_base = src_base;
    dst_base = dst_base_ptr;
    dst_ybase = dst_base_ptr + dst_stride * (dst_h);


    uint32_t index = 2;
    v4i32 bis = _mx128_mfcpu_w(index);
    int dst_y = 0;
    //float h_lambda1[2] = {0};
    uint8_t tmp[16];
    v16i8 zero = _mx128_li_b(0);
    v16i8 b_to_w_vr0 = {0, 1, 1, 1, 2, 3, 3, 3, 4, 5, 5, 5, 6, 7, 7, 7};
    v16i8 b_to_w_vr1 = {8, 1, 1, 1, 10, 3, 3, 3, 12, 5, 5, 5, 14, 7, 7, 7};
    v16i8 w_to_b_vr0 = {0, 8, 16, 24, 1, 9, 17, 25, 1, 1, 1, 1, 1, 1, 1, 1};
    for (; dst_y < valid_dst_h; dst_y++) {
        int src_fy_round = src_fy + 16;
        int src_y = (src_fy_round) >> 16;
        src_y = src_y > 0 ? src_y : 0;
        int w_y1 = (src_fy_round & 0xFFFF) >> 5;
        int w_y0 = 2048 - w_y1;
        int bias = (src_y < box_h - 1) ? 1 : 0;
        int src_y1 = src_y + bias;
        v4i32 h0lambda_1 = _mx128_mfcpu_w(w_y0);
        v4i32 h1lambda_1 = _mx128_mfcpu_w(w_y1);
        uint8_t *ptr_pixel_in_h0 = src_base + src_y * iline_size;
        uint8_t *ptr_pixel_in_h1 = src_base + src_y1 * iline_size;
        uint8_t *ptr_pixel_out_h = dst_base + dst_y * oline_size;
        uint8_t *ptr_pixel_in_uv_h0 = src_ybase + (src_y / 2) * iline_size;
        uint8_t *ptr_pixel_in_uv_h1 = src_ybase + (src_y1 / 2) * iline_size;
        uint8_t *ptr_pixel_out_uv_h = dst_ybase + (dst_y / 2) * oline_size;

        int dst_x = 0;
        for (; dst_x <= (valid_dst_w - 16); dst_x += 8) {
            uint8_t *ptr_pixel_out = ptr_pixel_out_h + dst_x;
            v4i32 w1lambda_1 = _mx128_lu1q(&w1lambda[dst_x], 0);
            v4i32 w1lambda_11 = _mx128_lu1q(&w1lambda[dst_x + 4], 0);
            v4i32 w0lambda_1 = _mx128_lu1q(&w0lambda[dst_x], 0);
            v4i32 w0lambda_11 = _mx128_lu1q(&w0lambda[dst_x + 4], 0);

            int src_fx_round = src_fx + 16 + dst_x * scale_x;
            int w1 = (src_fx_round) >> 16;

            w1 = w1 > 0 ? w1 : 0;

            uint8_t *ptr_pixel_11 = ptr_pixel_in_h0 + w1;  /*idx0*/
            uint8_t *ptr_pixel_12 = ptr_pixel_in_h1 + w1;  /*idx0*/

            v16i8 pixel11 = _mx128_lu1q(&ptr_pixel_11[0], 0);
            v16i8 pixel12 = _mx128_lu1q(&ptr_pixel_12[0], 0);
            v16u8 idx_0 = _mx128_lu1q(&w_pixel_0[dst_x], 0);
            v16u8 idx_1 = _mx128_lu1q(&w_pixel_1[dst_x], 0);

            v16u8 dst_w_0 = _mx128_shufv(pixel11, pixel11, idx_0);
            v16u8 dst_w_1 = _mx128_shufv(pixel11, pixel11, idx_1);
            v16u8 dst_h_0 = _mx128_shufv(pixel12, pixel12, idx_0);
            v16u8 dst_h_1 = _mx128_shufv(pixel12, pixel12, idx_1);

            v4i32 in_w_vr0 = (v4i32)_mx128_shufv(zero, dst_w_0, b_to_w_vr0);
            v4i32 in_w_vr01 = (v4i32)_mx128_shufv(zero, dst_w_0, b_to_w_vr1);
            v4i32 in_w_vr1 = (v4i32)_mx128_shufv(zero, dst_w_1, b_to_w_vr0);
            v4i32 in_w_vr11 = (v4i32)_mx128_shufv(zero, dst_w_1, b_to_w_vr1);
            v4i32 in_h_vr0 = (v4i32)_mx128_shufv(zero, dst_h_0, b_to_w_vr0);
            v4i32 in_h_vr01 = (v4i32)_mx128_shufv(zero, dst_h_0, b_to_w_vr1);
            v4i32 in_h_vr1 = (v4i32)_mx128_shufv(zero, dst_h_1, b_to_w_vr0);
            v4i32 in_h_vr11 = (v4i32)_mx128_shufv(zero, dst_h_1, b_to_w_vr1);

            in_w_vr0 = _mx128_mul_w(in_w_vr0, w0lambda_1);
            in_w_vr1 = _mx128_mul_w(in_w_vr1, w1lambda_1);
            in_h_vr0 = _mx128_mul_w(in_h_vr0, w0lambda_1);
            in_h_vr1 = _mx128_mul_w(in_h_vr1, w1lambda_1);

            in_w_vr01 = _mx128_mul_w(in_w_vr01, w0lambda_11);
            in_w_vr11 = _mx128_mul_w(in_w_vr11, w1lambda_11);
            in_h_vr01 = _mx128_mul_w(in_h_vr01, w0lambda_11);
            in_h_vr11 = _mx128_mul_w(in_h_vr11, w1lambda_11);

            in_w_vr0 = _mx128_add_w(in_w_vr0, in_w_vr1);
            in_h_vr0 = _mx128_add_w(in_h_vr0, in_h_vr1);
            in_w_vr01 = _mx128_add_w(in_w_vr01, in_w_vr11);
            in_h_vr01 = _mx128_add_w(in_h_vr01, in_h_vr11);

            in_w_vr0 = _mx128_srli_w(in_w_vr0, 4);
            in_h_vr0 = _mx128_srli_w(in_h_vr0, 4);
            in_w_vr01 = _mx128_srli_w(in_w_vr01, 4);
            in_h_vr01 = _mx128_srli_w(in_h_vr01, 4);

            in_w_vr0 = _mx128_mul_w(in_w_vr0, h0lambda_1);
            in_h_vr0 = _mx128_mul_w(in_h_vr0, h1lambda_1);
            in_w_vr01 = _mx128_mul_w(in_w_vr01, h0lambda_1);
            in_h_vr01 = _mx128_mul_w(in_h_vr01, h1lambda_1);

            in_w_vr0 = _mx128_srli_w(in_w_vr0, 16);
            in_h_vr0 = _mx128_srli_w(in_h_vr0, 16);
            in_w_vr01 = _mx128_srli_w(in_w_vr01, 16);
            in_h_vr01 = _mx128_srli_w(in_h_vr01, 16);

            in_w_vr0 = _mx128_add_w(in_w_vr0, in_h_vr0);
            in_w_vr01 = _mx128_add_w(in_w_vr01, in_h_vr01);
            in_w_vr0 = _mx128_add_w(in_w_vr0, bis);
            in_w_vr01 = _mx128_add_w(in_w_vr01, bis);
            in_w_vr0 = _mx128_srli_w(in_w_vr0, 2);
            in_w_vr01 = _mx128_srli_w(in_w_vr01, 2);

            v16i8 out_value = _mx128_shufv(in_w_vr01, in_w_vr0, w_to_b_vr0);

            _mx128_su1q(out_value, ptr_pixel_out, 0);

            if (dst_y % 2 == 0) {
                w1 = w1 & 0xfffe;
                uint8_t *ptr_pixel_uv_out = ptr_pixel_out_uv_h + dst_x;
                uint8_t *ptr_pixel_uv_11 = ptr_pixel_in_uv_h0 + w1;  /*idx0*/
                uint8_t *ptr_pixel_uv_12 = ptr_pixel_in_uv_h1 + w1;  /*idx0*/

                w1lambda_1 = _mx128_lu1q(&w1lambda_uv[dst_x], 0);
                w0lambda_1 = _mx128_lu1q(&w0lambda_uv[dst_x], 0);
                w1lambda_11 = _mx128_lu1q(&w1lambda_uv[dst_x + 4], 0);
                w0lambda_11 = _mx128_lu1q(&w0lambda_uv[dst_x + 4], 0);

                pixel11 = _mx128_lu1q(&ptr_pixel_uv_11[0], 0);
                pixel12 = _mx128_lu1q(&ptr_pixel_uv_12[0], 0);
                idx_0 = _mx128_lu1q(&w_pixel_uv_0[dst_x], 0);
                idx_1 = _mx128_lu1q(&w_pixel_uv_1[dst_x], 0);

                dst_w_0 = _mx128_shufv(pixel11, pixel11, idx_0);
                dst_h_0 = _mx128_shufv(pixel12, pixel12, idx_0);
                dst_w_1 = _mx128_shufv(pixel11, pixel11, idx_1);
                dst_h_1 = _mx128_shufv(pixel12, pixel12, idx_1);

                in_w_vr0 = (v4i32)_mx128_shufv(zero, dst_w_0, b_to_w_vr0);
                in_w_vr1 = (v4i32)_mx128_shufv(zero, dst_w_1, b_to_w_vr0);
                in_h_vr0 = (v4i32)_mx128_shufv(zero, dst_h_0, b_to_w_vr0);
                in_h_vr1 = (v4i32)_mx128_shufv(zero, dst_h_1, b_to_w_vr0);

                in_w_vr01 = (v4i32)_mx128_shufv(zero, dst_w_0, b_to_w_vr1);
                in_w_vr11 = (v4i32)_mx128_shufv(zero, dst_w_1, b_to_w_vr1);
                in_h_vr01 = (v4i32)_mx128_shufv(zero, dst_h_0, b_to_w_vr1);
                in_h_vr11 = (v4i32)_mx128_shufv(zero, dst_h_1, b_to_w_vr1);

                in_w_vr0 = _mx128_mul_w(in_w_vr0, w0lambda_1);
                in_w_vr1 = _mx128_mul_w(in_w_vr1, w1lambda_1);
                in_h_vr0 = _mx128_mul_w(in_h_vr0, w0lambda_1);
                in_h_vr1 = _mx128_mul_w(in_h_vr1, w1lambda_1);
                in_w_vr01 = _mx128_mul_w(in_w_vr01, w0lambda_11);
                in_w_vr11 = _mx128_mul_w(in_w_vr11, w1lambda_11);
                in_h_vr01 = _mx128_mul_w(in_h_vr01, w0lambda_11);
                in_h_vr11 = _mx128_mul_w(in_h_vr11, w1lambda_11);

                in_w_vr0 = _mx128_add_w(in_w_vr0, in_w_vr1);
                in_h_vr0 = _mx128_add_w(in_h_vr0, in_h_vr1);
                in_w_vr01 = _mx128_add_w(in_w_vr01, in_w_vr11);
                in_h_vr01 = _mx128_add_w(in_h_vr01, in_h_vr11);

                in_w_vr0 = _mx128_srli_w(in_w_vr0, 4);
                in_h_vr0 = _mx128_srli_w(in_h_vr0, 4);
                in_w_vr01 = _mx128_srli_w(in_w_vr01, 4);
                in_h_vr01 = _mx128_srli_w(in_h_vr01, 4);

                in_w_vr0 = _mx128_mul_w(in_w_vr0, h0lambda_1);
                in_h_vr0 = _mx128_mul_w(in_h_vr0, h1lambda_1);
                in_w_vr01 = _mx128_mul_w(in_w_vr01, h0lambda_1);
                in_h_vr01 = _mx128_mul_w(in_h_vr01, h1lambda_1);

                in_w_vr0 = _mx128_srli_w(in_w_vr0, 16);
                in_h_vr0 = _mx128_srli_w(in_h_vr0, 16);
                in_w_vr01 = _mx128_srli_w(in_w_vr01, 16);
                in_h_vr01 = _mx128_srli_w(in_h_vr01, 16);

                in_w_vr0 = _mx128_add_w(in_w_vr0, in_h_vr0);
                in_w_vr0 = _mx128_add_w(in_w_vr0, bis);
                in_w_vr01 = _mx128_add_w(in_w_vr01, in_h_vr01);
                in_w_vr01 = _mx128_add_w(in_w_vr01, bis);


                in_w_vr0 = _mx128_srli_w(in_w_vr0, 2);
                in_w_vr01 = _mx128_srli_w(in_w_vr01, 2);
                out_value = _mx128_shufv(in_w_vr01, in_w_vr0, w_to_b_vr0);
                    _mx128_su1q(out_value, ptr_pixel_uv_out, 0);
            }
        }
        for (; dst_x <= (valid_dst_w - 8); dst_x += 8) {
            uint8_t *ptr_pixel_out = ptr_pixel_out_h + dst_x;
            v4i32 w1lambda_1 = _mx128_lu1q(&w1lambda[dst_x], 0);
            v4i32 w1lambda_11 = _mx128_lu1q(&w1lambda[dst_x + 4], 0);
            v4i32 w0lambda_1 = _mx128_lu1q(&w0lambda[dst_x], 0);
            v4i32 w0lambda_11 = _mx128_lu1q(&w0lambda[dst_x + 4], 0);

            int src_fx_round = src_fx + 16 + dst_x * scale_x;
            int w1 = (src_fx_round) >> 16;

            w1 = w1 > 0 ? w1 : 0;

            uint8_t *ptr_pixel_11 = ptr_pixel_in_h0 + w1;  /*idx0*/
            uint8_t *ptr_pixel_12 = ptr_pixel_in_h1 + w1;  /*idx0*/

            v16i8 pixel11 = _mx128_lu1q(&ptr_pixel_11[0], 0);
            v16i8 pixel12 = _mx128_lu1q(&ptr_pixel_12[0], 0);
            v16u8 idx_0 = _mx128_lu1q(&w_pixel_0[dst_x], 0);
            v16u8 idx_1 = _mx128_lu1q(&w_pixel_1[dst_x], 0);

            v16u8 dst_w_0 = _mx128_shufv(pixel11, pixel11, idx_0);
            v16u8 dst_w_1 = _mx128_shufv(pixel11, pixel11, idx_1);
            v16u8 dst_h_0 = _mx128_shufv(pixel12, pixel12, idx_0);
            v16u8 dst_h_1 = _mx128_shufv(pixel12, pixel12, idx_1);

            v4i32 in_w_vr0 = (v4i32)_mx128_shufv(zero, dst_w_0, b_to_w_vr0);
            v4i32 in_w_vr01 = (v4i32)_mx128_shufv(zero, dst_w_0, b_to_w_vr1);
            v4i32 in_w_vr1 = (v4i32)_mx128_shufv(zero, dst_w_1, b_to_w_vr0);
            v4i32 in_w_vr11 = (v4i32)_mx128_shufv(zero, dst_w_1, b_to_w_vr1);

            v4i32 in_h_vr0 = (v4i32)_mx128_shufv(zero, dst_h_0, b_to_w_vr0);
            v4i32 in_h_vr01 = (v4i32)_mx128_shufv(zero, dst_h_0, b_to_w_vr1);
            v4i32 in_h_vr1 = (v4i32)_mx128_shufv(zero, dst_h_1, b_to_w_vr0);
            v4i32 in_h_vr11 = (v4i32)_mx128_shufv(zero, dst_h_1, b_to_w_vr1);

            in_w_vr0 = _mx128_mul_w(in_w_vr0, w0lambda_1);
            in_w_vr1 = _mx128_mul_w(in_w_vr1, w1lambda_1);
            in_h_vr0 = _mx128_mul_w(in_h_vr0, w0lambda_1);
            in_h_vr1 = _mx128_mul_w(in_h_vr1, w1lambda_1);

            in_w_vr01 = _mx128_mul_w(in_w_vr01, w0lambda_11);
            in_w_vr11 = _mx128_mul_w(in_w_vr11, w1lambda_11);
            in_h_vr01 = _mx128_mul_w(in_h_vr01, w0lambda_11);
            in_h_vr11 = _mx128_mul_w(in_h_vr11, w1lambda_11);

            in_w_vr0 = _mx128_add_w(in_w_vr0, in_w_vr1);
            in_h_vr0 = _mx128_add_w(in_h_vr0, in_h_vr1);
            in_w_vr01 = _mx128_add_w(in_w_vr01, in_w_vr11);
            in_h_vr01 = _mx128_add_w(in_h_vr01, in_h_vr11);

            in_w_vr0 = _mx128_srli_w(in_w_vr0, 4);
            in_h_vr0 = _mx128_srli_w(in_h_vr0, 4);
            in_w_vr01 = _mx128_srli_w(in_w_vr01, 4);
            in_h_vr01 = _mx128_srli_w(in_h_vr01, 4);

            in_w_vr0 = _mx128_mul_w(in_w_vr0, h0lambda_1);
            in_h_vr0 = _mx128_mul_w(in_h_vr0, h1lambda_1);
            in_w_vr01 = _mx128_mul_w(in_w_vr01, h0lambda_1);
            in_h_vr01 = _mx128_mul_w(in_h_vr01, h1lambda_1);

            in_w_vr0 = _mx128_srli_w(in_w_vr0, 16);
            in_h_vr0 = _mx128_srli_w(in_h_vr0, 16);
            in_w_vr01 = _mx128_srli_w(in_w_vr01, 16);
            in_h_vr01 = _mx128_srli_w(in_h_vr01, 16);

            in_w_vr0 = _mx128_add_w(in_w_vr0, in_h_vr0);
            in_w_vr01 = _mx128_add_w(in_w_vr01, in_h_vr01);
            in_w_vr0 = _mx128_add_w(in_w_vr0, bis);
            in_w_vr01 = _mx128_add_w(in_w_vr01, bis);
            in_w_vr0 = _mx128_srli_w(in_w_vr0, 2);
            in_w_vr01 = _mx128_srli_w(in_w_vr01, 2);

            v16i8 out_value = _mx128_shufv(in_w_vr01, in_w_vr0, w_to_b_vr0);
            _mx128_su1q(out_value, tmp, 0);
            memcpy(ptr_pixel_out, tmp, 8);

            if (dst_y % 2 == 0) {
                w1 = w1 & 0xfffe;
                uint8_t *ptr_pixel_uv_out = ptr_pixel_out_uv_h + dst_x;
                uint8_t *ptr_pixel_uv_11 = ptr_pixel_in_uv_h0 + w1;  /*idx0*/
                uint8_t *ptr_pixel_uv_12 = ptr_pixel_in_uv_h1 + w1;  /*idx0*/

                w1lambda_1 = _mx128_lu1q(&w1lambda_uv[dst_x], 0);
                w0lambda_1 = _mx128_lu1q(&w0lambda_uv[dst_x], 0);
                w1lambda_11 = _mx128_lu1q(&w1lambda_uv[dst_x + 4], 0);
                w0lambda_11 = _mx128_lu1q(&w0lambda_uv[dst_x + 4], 0);

                pixel11 = _mx128_lu1q(&ptr_pixel_uv_11[0], 0);
                pixel12 = _mx128_lu1q(&ptr_pixel_uv_12[0], 0);

                idx_0 = _mx128_lu1q(&w_pixel_uv_0[dst_x], 0);
                idx_1 = _mx128_lu1q(&w_pixel_uv_1[dst_x], 0);

                dst_w_0 = _mx128_shufv(pixel11, pixel11, idx_0);
                dst_w_1 = _mx128_shufv(pixel11, pixel11, idx_1);
                dst_h_0 = _mx128_shufv(pixel12, pixel12, idx_0);
                dst_h_1 = _mx128_shufv(pixel12, pixel12, idx_1);

                in_w_vr0 = (v4i32)_mx128_shufv(zero, dst_w_0, b_to_w_vr0);
                in_w_vr1 = (v4i32)_mx128_shufv(zero, dst_w_1, b_to_w_vr0);
                in_h_vr0 = (v4i32)_mx128_shufv(zero, dst_h_0, b_to_w_vr0);
                in_h_vr1 = (v4i32)_mx128_shufv(zero, dst_h_1, b_to_w_vr0);

                in_w_vr01 = (v4i32)_mx128_shufv(zero, dst_w_0, b_to_w_vr1);
                in_w_vr11 = (v4i32)_mx128_shufv(zero, dst_w_1, b_to_w_vr1);
                in_h_vr01 = (v4i32)_mx128_shufv(zero, dst_h_0, b_to_w_vr1);
                in_h_vr11 = (v4i32)_mx128_shufv(zero, dst_h_1, b_to_w_vr1);

                in_w_vr0 = _mx128_mul_w(in_w_vr0, w0lambda_1);
                in_w_vr1 = _mx128_mul_w(in_w_vr1, w1lambda_1);
                in_h_vr0 = _mx128_mul_w(in_h_vr0, w0lambda_1);
                in_h_vr1 = _mx128_mul_w(in_h_vr1, w1lambda_1);
                in_w_vr01 = _mx128_mul_w(in_w_vr01, w0lambda_11);
                in_w_vr11 = _mx128_mul_w(in_w_vr11, w1lambda_11);
                in_h_vr01 = _mx128_mul_w(in_h_vr01, w0lambda_11);
                in_h_vr11 = _mx128_mul_w(in_h_vr11, w1lambda_11);

                in_w_vr0 = _mx128_add_w(in_w_vr0, in_w_vr1);
                in_h_vr0 = _mx128_add_w(in_h_vr0, in_h_vr1);
                in_w_vr01 = _mx128_add_w(in_w_vr01, in_w_vr11);
                in_h_vr01 = _mx128_add_w(in_h_vr01, in_h_vr11);


                in_w_vr0 = _mx128_srli_w(in_w_vr0, 4);
                in_h_vr0 = _mx128_srli_w(in_h_vr0, 4);
                in_w_vr01 = _mx128_srli_w(in_w_vr01, 4);
                in_h_vr01 = _mx128_srli_w(in_h_vr01, 4);

                in_w_vr0 = _mx128_mul_w(in_w_vr0, h0lambda_1);
                in_h_vr0 = _mx128_mul_w(in_h_vr0, h1lambda_1);
                in_w_vr01 = _mx128_mul_w(in_w_vr01, h0lambda_1);
                in_h_vr01 = _mx128_mul_w(in_h_vr01, h1lambda_1);

                in_w_vr0 = _mx128_srli_w(in_w_vr0, 16);
                in_h_vr0 = _mx128_srli_w(in_h_vr0, 16);
                in_w_vr01 = _mx128_srli_w(in_w_vr01, 16);
                in_h_vr01 = _mx128_srli_w(in_h_vr01, 16);

                in_w_vr0 = _mx128_add_w(in_w_vr0, in_h_vr0);
                in_w_vr0 = _mx128_add_w(in_w_vr0, bis);
                in_w_vr01 = _mx128_add_w(in_w_vr01, in_h_vr01);
                in_w_vr01 = _mx128_add_w(in_w_vr01, bis);


                in_w_vr0 = _mx128_srli_w(in_w_vr0, 2);
                in_w_vr01 = _mx128_srli_w(in_w_vr01, 2);
                out_value = _mx128_shufv(in_w_vr01, in_w_vr0, w_to_b_vr0);
                _mx128_su1q(out_value, tmp, 0);
                memcpy(ptr_pixel_uv_out, tmp, 8);
            }
        }
        src_fy += scale_y;

    }
    free(w1lambda);
    return 0;
}

#if 0
static inline int opencv_resize_crop_simd_bk(uint8_t *src_base, int src_w, int src_h, uint8_t *dst_base_ptr, int dst_w, int dst_h) {

    int box_w = src_w;
    int box_h = src_h;
    uint8_t *src_ybase;
    uint8_t *dst_ybase;
    int iline_size = src_w;
    int oline_size = dst_w;

    int valid_dst_w = dst_w;
    int valid_dst_h = dst_h;
    float inv_scale_x = (float)box_w / (float)valid_dst_w;
    float inv_scale_y = (float)box_h / (float)valid_dst_h;
    int32_t trans_x = (int32_t)((inv_scale_x * 0.5 - 0.5) * 65536);
    int32_t trans_y = (int32_t)((inv_scale_y * 0.5 - 0.5) * 65536);
    int32_t scale_x = inv_scale_x * 65536;

    int32_t scale_y = inv_scale_y * 65536;

    int32_t src_fx = trans_x; // src float x

    int32_t src_fy = trans_y; // src float y

    int src_stride;

    int dst_stride;

    uint8_t *dst_base;

    int width_cycs = (int)(16 / inv_scale_x); /*cal pixel num one time(odd number)*/

    if (width_cycs < 4) {

        return -1;

    } else {

        width_cycs = 4;

    }

    int malloc_size = ((valid_dst_w + width_cycs - 1) / width_cycs) * width_cycs;

    uint32_t *w1lambda = (uint32_t *)malloc((malloc_size * 10) * sizeof(uint32_t));

    uint32_t *w0lambda = w1lambda + malloc_size;

    uint32_t *w1lambda_uv = w0lambda + malloc_size;

    uint32_t *w0lambda_uv = w1lambda_uv + malloc_size;

    uint32_t *h1lambda = w0lambda_uv + malloc_size;

    uint32_t *h0lambda = h1lambda + malloc_size;

    uint8_t *w_pixel_0 = (uint8_t *)(h0lambda + malloc_size);

    uint8_t *w_pixel_1 = w_pixel_0 + malloc_size;

    uint8_t *w_pixel_uv_0 = w_pixel_1 + malloc_size;

    uint8_t *w_pixel_uv_1 = w_pixel_uv_0 + malloc_size;

    if (w1lambda == NULL || w0lambda == NULL || w1lambda_uv == NULL || w0lambda_uv == NULL ||

            h1lambda == NULL || h0lambda == NULL || w_pixel_0 == NULL || w_pixel_1 == NULL ||

            w_pixel_uv_0 == NULL || w_pixel_uv_1 == NULL) {

        return -1;

    }

    int dst_x = 0;

    for (int cycs_i = 0; cycs_i < ((valid_dst_w + width_cycs - 1) / width_cycs); cycs_i++) {

        for (int i = 0; i < width_cycs; i++) {

            dst_x = cycs_i * width_cycs + i;

            int src_fx_round = src_fx + 16 + dst_x * scale_x;

            int src_x = (src_fx_round) >> 16;

            // src_x = src_x > (box_w - 1) ? box_w - 1 : src_x;

            int bias = (src_x < box_w - 1) ? 1 : 0;

            int w_x1 = (src_fx_round & 0xFFFF) >> 5;

            int w_x0 = 2048 - w_x1;

            w1lambda[dst_x] = w_x1;

            w0lambda[dst_x] = w_x0;

            int start_idx = (cycs_i * width_cycs * scale_x) >> 16;

            /*(2x + 1)<<8 + 2x = 514x + 256*/

            w_pixel_0[dst_x] = (src_x - start_idx) * 2 > 0 ? (src_x - start_idx) * 2

                : 0; /*updata shufvb idx*/

            w_pixel_1[dst_x] = (src_x - start_idx + bias) * 2;

            if (dst_x % 2 == 0) {

                start_idx = start_idx & 0xfffe;

                int index = src_x - start_idx > 0 ? src_x - start_idx : 0;

                index = index & 0xfffe;

                index = index >= 0 ? index * 2 : 0;

                w_pixel_uv_0[dst_x] = index;

                w_pixel_uv_0[dst_x + 1] = w_pixel_uv_0[dst_x] + 2;

                w_pixel_uv_1[dst_x] = ((src_x - start_idx + bias) & 0xfffe) * 2;

                w_pixel_uv_1[dst_x + 1] = w_pixel_uv_1[dst_x] + 2;



                w1lambda_uv[dst_x] = w1lambda[dst_x];

                w1lambda_uv[dst_x + 1] = w1lambda[dst_x];

                w0lambda_uv[dst_x] = w0lambda[dst_x];

                w0lambda_uv[dst_x + 1] = w0lambda[dst_x];

            }

        }

    }

    // nv12

    src_stride = src_w;

    dst_stride = dst_w;

    src_ybase = src_base + (int)(src_w * src_h);

    src_base = src_base;

    dst_base = dst_base_ptr;

    dst_ybase = dst_base_ptr + dst_stride * (dst_h);



    uint32_t index = 2;

    v4i32 bis = _mx128_mfcpu_w(index);

    int dst_y = 0;

    float h_lambda1[2] = {0};

    uint8_t tmp[16];

    v16i8 zero = _mx128_li_b(0);

    v16i8 b_to_w_vr0 = {0, 1, 1, 1, 2, 3, 3, 3, 4, 5, 5, 5, 6, 7, 7, 7};

    v16i8 b_to_w_vr1 = {8, 1, 1, 1, 10, 3, 3, 3, 12, 5, 5, 5, 14, 7, 7, 7};

    v16i8 w_to_b_vr0 = {0, 8, 16, 24, 1, 9, 17, 25, 1, 1, 1, 1, 1, 1, 1, 1};

    for (; dst_y < valid_dst_h; dst_y++) {

        int src_fy_round = src_fy + 16;

        int src_y = (src_fy_round) >> 16;

        src_y = src_y > 0 ? src_y : 0;

        int w_y1 = (src_fy_round & 0xFFFF) >> 5;

        int w_y0 = 2048 - w_y1;

        int bias = (src_y < box_h - 1) ? 1 : 0;

        int src_y1 = src_y + bias;

        v4i32 h0lambda_1 = _mx128_mfcpu_w(w_y0);

        v4i32 h1lambda_1 = _mx128_mfcpu_w(w_y1);

        uint8_t *ptr_pixel_in_h0 = src_base + src_y * iline_size;

        uint8_t *ptr_pixel_in_h1 = src_base + src_y1 * iline_size;

        uint8_t *ptr_pixel_out_h = dst_base + dst_y * oline_size;

        uint8_t *ptr_pixel_in_uv_h0 = src_ybase + (src_y / 2) * iline_size;

        uint8_t *ptr_pixel_in_uv_h1 = src_ybase + (src_y1 / 2) * iline_size;

        uint8_t *ptr_pixel_out_uv_h = dst_ybase + (dst_y / 2) * oline_size;



        int dst_x = 0;

        for (; dst_x <= (valid_dst_w - 8); dst_x += 8) {

            uint8_t *ptr_pixel_out = ptr_pixel_out_h + dst_x;

            v4i32 w1lambda_1 = _mx128_lu1q(&w1lambda[dst_x], 0);

            v4i32 w1lambda_11 = _mx128_lu1q(&w1lambda[dst_x + 4], 0);

            v4i32 w0lambda_1 = _mx128_lu1q(&w0lambda[dst_x], 0);

            v4i32 w0lambda_11 = _mx128_lu1q(&w0lambda[dst_x + 4], 0);



            int src_fx_round = src_fx + 16 + dst_x * scale_x;
            int src_fx_round1 = src_fx + 16 + (dst_x + 4) * scale_x;
            int w1 = (src_fx_round) >> 16;
            int w2 = (src_fx_round1) >> 16;

            w1 = w1 > 0 ? w1 : 0;
            w2 = w2 > 0 ? w2 : 0;



            uint8_t *ptr_pixel_11 = ptr_pixel_in_h0 + w1;  /*idx0*/

            uint8_t *ptr_pixel_12 = ptr_pixel_in_h1 + w1;  /*idx0*/

            uint8_t *ptr_pixel_111 = ptr_pixel_in_h0 + w2; /*idx0*/

            uint8_t *ptr_pixel_121 = ptr_pixel_in_h1 + w2; /*idx0*/



            v16i8 pixel11 = _mx128_lu1q(&ptr_pixel_11[0], 0);

            v16i8 pixel12 = _mx128_lu1q(&ptr_pixel_12[0], 0);

            v16i8 pixel111 = _mx128_lu1q(&ptr_pixel_111[0], 0);

            v16i8 pixel121 = _mx128_lu1q(&ptr_pixel_121[0], 0);

            v16i8 idx_0 = _mx128_lu1q(&w_pixel_0[dst_x], 0);

            v16i8 idx_1 = _mx128_lu1q(&w_pixel_1[dst_x], 0);

            v16i8 idx_01 = _mx128_lu1q(&w_pixel_0[dst_x + 4], 0);

            v16i8 idx_11 = _mx128_lu1q(&w_pixel_1[dst_x + 4], 0);



            v16i8 dst_w_0 = _mx128_shufv(pixel11, pixel11, idx_0);

            v16i8 dst_w_1 = _mx128_shufv(pixel11, pixel11, idx_1);

            v16i8 dst_h_0 = _mx128_shufv(pixel12, pixel12, idx_0);

            v16i8 dst_h_1 = _mx128_shufv(pixel12, pixel12, idx_1);

            v16i8 dst_w_01 = _mx128_shufv(pixel111, pixel111, idx_01);

            v16i8 dst_w_11 = _mx128_shufv(pixel111, pixel111, idx_11);

            v16i8 dst_h_01 = _mx128_shufv(pixel121, pixel121, idx_01);

            v16i8 dst_h_11 = _mx128_shufv(pixel121, pixel121, idx_11);



            v4i32 in_w_vr0 = (v4i32)_mx128_shufv(zero, dst_w_0, b_to_w_vr0);

            v4i32 in_w_vr01 = (v4i32)_mx128_shufv(zero, dst_w_01, b_to_w_vr0);

            v4i32 in_w_vr1 = (v4i32)_mx128_shufv(zero, dst_w_1, b_to_w_vr0);

            v4i32 in_w_vr11 = (v4i32)_mx128_shufv(zero, dst_w_11, b_to_w_vr0);

            v4i32 in_h_vr0 = (v4i32)_mx128_shufv(zero, dst_h_0, b_to_w_vr0);

            v4i32 in_h_vr01 = (v4i32)_mx128_shufv(zero, dst_h_01, b_to_w_vr0);

            v4i32 in_h_vr1 = (v4i32)_mx128_shufv(zero, dst_h_1, b_to_w_vr0);

            v4i32 in_h_vr11 = (v4i32)_mx128_shufv(zero, dst_h_11, b_to_w_vr0);



            in_w_vr0 = _mx128_mul_w(in_w_vr0, w0lambda_1);

            in_w_vr1 = _mx128_mul_w(in_w_vr1, w1lambda_1);

            in_h_vr0 = _mx128_mul_w(in_h_vr0, w0lambda_1);

            in_h_vr1 = _mx128_mul_w(in_h_vr1, w1lambda_1);



            in_w_vr01 = _mx128_mul_w(in_w_vr01, w0lambda_11);

            in_w_vr11 = _mx128_mul_w(in_w_vr11, w1lambda_11);

            in_h_vr01 = _mx128_mul_w(in_h_vr01, w0lambda_11);

            in_h_vr11 = _mx128_mul_w(in_h_vr11, w1lambda_11);



            in_w_vr0 = _mx128_add_w(in_w_vr0, in_w_vr1);

            in_h_vr0 = _mx128_add_w(in_h_vr0, in_h_vr1);

            in_w_vr01 = _mx128_add_w(in_w_vr01, in_w_vr11);

            in_h_vr01 = _mx128_add_w(in_h_vr01, in_h_vr11);



            in_w_vr0 = _mx128_srli_w(in_w_vr0, 4);

            in_h_vr0 = _mx128_srli_w(in_h_vr0, 4);

            in_w_vr01 = _mx128_srli_w(in_w_vr01, 4);

            in_h_vr01 = _mx128_srli_w(in_h_vr01, 4);



            in_w_vr0 = _mx128_mul_w(in_w_vr0, h0lambda_1);

            in_h_vr0 = _mx128_mul_w(in_h_vr0, h1lambda_1);

            in_w_vr01 = _mx128_mul_w(in_w_vr01, h0lambda_1);

            in_h_vr01 = _mx128_mul_w(in_h_vr01, h1lambda_1);



            in_w_vr0 = _mx128_srli_w(in_w_vr0, 16);

            in_h_vr0 = _mx128_srli_w(in_h_vr0, 16);

            in_w_vr01 = _mx128_srli_w(in_w_vr01, 16);

            in_h_vr01 = _mx128_srli_w(in_h_vr01, 16);



            in_w_vr0 = _mx128_add_w(in_w_vr0, in_h_vr0);

            in_w_vr01 = _mx128_add_w(in_w_vr01, in_h_vr01);

            in_w_vr0 = _mx128_add_w(in_w_vr0, bis);

            in_w_vr01 = _mx128_add_w(in_w_vr01, bis);

            in_w_vr0 = _mx128_srli_w(in_w_vr0, 2);

            in_w_vr01 = _mx128_srli_w(in_w_vr01, 2);



            v16i8 out_value = _mx128_shufv(in_w_vr01, in_w_vr0, w_to_b_vr0);

            if (dst_w <= (valid_dst_w - 16)) {

                _mx128_su1q(out_value, ptr_pixel_out, 0);

            } else {

                _mx128_su1q(out_value, tmp, 0);

                memcpy(ptr_pixel_out, tmp, 8);

            }

            if (dst_y % 2 == 0) {

                w1 = w1 & 0xfffe;

                w2 = w2 & 0xfffe;

                uint8_t *ptr_pixel_uv_out = ptr_pixel_out_uv_h + dst_x;

                uint8_t *ptr_pixel_uv_11 = ptr_pixel_in_uv_h0 + w1;  /*idx0*/

                uint8_t *ptr_pixel_uv_12 = ptr_pixel_in_uv_h1 + w1;  /*idx0*/

                uint8_t *ptr_pixel_uv_111 = ptr_pixel_in_uv_h0 + w2; /*idx0*/

                uint8_t *ptr_pixel_uv_121 = ptr_pixel_in_uv_h1 + w2; /*idx0*/



                w1lambda_1 = _mx128_lu1q(&w1lambda_uv[dst_x], 0);

                w0lambda_1 = _mx128_lu1q(&w0lambda_uv[dst_x], 0);

                w1lambda_11 = _mx128_lu1q(&w1lambda_uv[dst_x + 4], 0);

                w0lambda_11 = _mx128_lu1q(&w0lambda_uv[dst_x + 4], 0);



                pixel11 = _mx128_lu1q(&ptr_pixel_uv_11[0], 0);

                pixel12 = _mx128_lu1q(&ptr_pixel_uv_12[0], 0);

                pixel111 = _mx128_lu1q(&ptr_pixel_uv_111[0], 0);

                pixel121 = _mx128_lu1q(&ptr_pixel_uv_121[0], 0);

                idx_0 = _mx128_lu1q(&w_pixel_uv_0[dst_x], 0);

                idx_1 = _mx128_lu1q(&w_pixel_uv_1[dst_x], 0);

                idx_01 = _mx128_lu1q(&w_pixel_uv_0[dst_x + 4], 0);

                idx_11 = _mx128_lu1q(&w_pixel_uv_1[dst_x + 4], 0);



                dst_w_0 = _mx128_shufv(pixel11, pixel11, idx_0);

                dst_w_1 = _mx128_shufv(pixel11, pixel11, idx_1);

                dst_h_0 = _mx128_shufv(pixel12, pixel12, idx_0);

                dst_h_1 = _mx128_shufv(pixel12, pixel12, idx_1);

                dst_w_01 = _mx128_shufv(pixel111, pixel111, idx_01);

                dst_w_11 = _mx128_shufv(pixel111, pixel111, idx_11);

                dst_h_01 = _mx128_shufv(pixel121, pixel121, idx_01);

                dst_h_11 = _mx128_shufv(pixel121, pixel121, idx_11);



                in_w_vr0 = (v4i32)_mx128_shufv(zero, dst_w_0, b_to_w_vr0);

                in_w_vr1 = (v4i32)_mx128_shufv(zero, dst_w_1, b_to_w_vr0);

                in_h_vr0 = (v4i32)_mx128_shufv(zero, dst_h_0, b_to_w_vr0);

                in_h_vr1 = (v4i32)_mx128_shufv(zero, dst_h_1, b_to_w_vr0);

                in_w_vr01 = (v4i32)_mx128_shufv(zero, dst_w_01, b_to_w_vr0);

                in_w_vr11 = (v4i32)_mx128_shufv(zero, dst_w_11, b_to_w_vr0);

                in_h_vr01 = (v4i32)_mx128_shufv(zero, dst_h_01, b_to_w_vr0);

                in_h_vr11 = (v4i32)_mx128_shufv(zero, dst_h_11, b_to_w_vr0);



                in_w_vr0 = _mx128_mul_w(in_w_vr0, w0lambda_1);

                in_w_vr1 = _mx128_mul_w(in_w_vr1, w1lambda_1);

                in_h_vr0 = _mx128_mul_w(in_h_vr0, w0lambda_1);

                in_h_vr1 = _mx128_mul_w(in_h_vr1, w1lambda_1);

                in_w_vr01 = _mx128_mul_w(in_w_vr01, w0lambda_11);

                in_w_vr11 = _mx128_mul_w(in_w_vr11, w1lambda_11);

                in_h_vr01 = _mx128_mul_w(in_h_vr01, w0lambda_11);

                in_h_vr11 = _mx128_mul_w(in_h_vr11, w1lambda_11);



                in_w_vr0 = _mx128_add_w(in_w_vr0, in_w_vr1);

                in_h_vr0 = _mx128_add_w(in_h_vr0, in_h_vr1);

                in_w_vr01 = _mx128_add_w(in_w_vr01, in_w_vr11);

                in_h_vr01 = _mx128_add_w(in_h_vr01, in_h_vr11);



                in_w_vr0 = _mx128_srli_w(in_w_vr0, 4);

                in_h_vr0 = _mx128_srli_w(in_h_vr0, 4);

                in_w_vr01 = _mx128_srli_w(in_w_vr01, 4);

                in_h_vr01 = _mx128_srli_w(in_h_vr01, 4);



                in_w_vr0 = _mx128_mul_w(in_w_vr0, h0lambda_1);

                in_h_vr0 = _mx128_mul_w(in_h_vr0, h1lambda_1);

                in_w_vr01 = _mx128_mul_w(in_w_vr01, h0lambda_1);

                in_h_vr01 = _mx128_mul_w(in_h_vr01, h1lambda_1);



                in_w_vr0 = _mx128_srli_w(in_w_vr0, 16);

                in_h_vr0 = _mx128_srli_w(in_h_vr0, 16);

                in_w_vr01 = _mx128_srli_w(in_w_vr01, 16);

                in_h_vr01 = _mx128_srli_w(in_h_vr01, 16);



                in_w_vr0 = _mx128_add_w(in_w_vr0, in_h_vr0);

                in_w_vr0 = _mx128_add_w(in_w_vr0, bis);

                in_w_vr01 = _mx128_add_w(in_w_vr01, in_h_vr01);

                in_w_vr01 = _mx128_add_w(in_w_vr01, bis);



                in_w_vr0 = _mx128_srli_w(in_w_vr0, 2);

                in_w_vr01 = _mx128_srli_w(in_w_vr01, 2);

                out_value = _mx128_shufv(in_w_vr01, in_w_vr0, w_to_b_vr0);

                if (dst_w <= (valid_dst_w - 16)) {

                    _mx128_su1q(out_value, ptr_pixel_uv_out, 0);

                } else {

                    _mx128_su1q(out_value, tmp, 0);

                    memcpy(ptr_pixel_uv_out, tmp, 8);

                }

            }

        }



        for (; dst_x <= (valid_dst_w - width_cycs); dst_x += width_cycs) {

            uint8_t *ptr_pixel_out = ptr_pixel_out_h + dst_x;

            v4i32 w1lambda_1 = _mx128_lu1q(&w1lambda[dst_x], 0);

            v4i32 w0lambda_1 = _mx128_lu1q(&w0lambda[dst_x], 0);



            int src_fx_round = src_fx + 16 + dst_x * scale_x;

            int w1 = (src_fx_round) >> 16;

            w1 = w1 > 0 ? w1 : 0;



            uint8_t *ptr_pixel_11 = ptr_pixel_in_h0 + w1; /*idx0*/

            uint8_t *ptr_pixel_12 = ptr_pixel_in_h1 + w1; /*idx0*/



            v16i8 pixel11 = _mx128_lu1q(&ptr_pixel_11[0], 0);

            v16i8 pixel12 = _mx128_lu1q(&ptr_pixel_12[0], 0);

            v16i8 idx_0 = _mx128_lu1q(&w_pixel_0[dst_x], 0);

            v16i8 idx_1 = _mx128_lu1q(&w_pixel_1[dst_x], 0);



            v16i8 dst_w_0 = _mx128_shufv(pixel11, pixel11, idx_0);

            v16i8 dst_w_1 = _mx128_shufv(pixel11, pixel11, idx_1);

            v16i8 dst_h_0 = _mx128_shufv(pixel12, pixel12, idx_0);

            v16i8 dst_h_1 = _mx128_shufv(pixel12, pixel12, idx_1);



            v4i32 in_w_vr0 = (v4i32)_mx128_shufv(zero, dst_w_0, b_to_w_vr0);

            v4i32 in_w_vr1 = (v4i32)_mx128_shufv(zero, dst_w_1, b_to_w_vr0);

            v4i32 in_h_vr0 = (v4i32)_mx128_shufv(zero, dst_h_0, b_to_w_vr0);

            v4i32 in_h_vr1 = (v4i32)_mx128_shufv(zero, dst_h_1, b_to_w_vr0);



            in_w_vr0 = _mx128_mul_w(in_w_vr0, w0lambda_1);

            in_w_vr1 = _mx128_mul_w(in_w_vr1, w1lambda_1);

            in_h_vr0 = _mx128_mul_w(in_h_vr0, w0lambda_1);

            in_h_vr1 = _mx128_mul_w(in_h_vr1, w1lambda_1);



            in_w_vr0 = _mx128_add_w(in_w_vr0, in_w_vr1);

            in_h_vr0 = _mx128_add_w(in_h_vr0, in_h_vr1);



            in_w_vr0 = _mx128_srli_w(in_w_vr0, 4);

            in_h_vr0 = _mx128_srli_w(in_h_vr0, 4);



            in_w_vr0 = _mx128_mul_w(in_w_vr0, h0lambda_1);

            in_h_vr0 = _mx128_mul_w(in_h_vr0, h1lambda_1);



            in_w_vr0 = _mx128_srli_w(in_w_vr0, 16);

            in_h_vr0 = _mx128_srli_w(in_h_vr0, 16);



            in_w_vr0 = _mx128_add_w(in_w_vr0, in_h_vr0);

            in_w_vr0 = _mx128_add_w(in_w_vr0, bis);

            in_w_vr0 = _mx128_srli_w(in_w_vr0, 2);



            v16i8 out_value = _mx128_shufv(in_w_vr0, in_w_vr0, w_to_b_vr0);

            if (dst_w <= (valid_dst_w - 16)) {

                _mx128_su1q(out_value, ptr_pixel_out, 0);

            } else {

                _mx128_su1q(out_value, tmp, 0);

                memcpy(ptr_pixel_out, tmp, 4);

            }

            if (dst_y % 2 == 0) {

                w1 = w1 & 0xfffe;

                uint8_t *ptr_pixel_uv_out = ptr_pixel_out_uv_h + dst_x;

                uint8_t *ptr_pixel_uv_11 = ptr_pixel_in_uv_h0 + w1; /*idx0*/

                uint8_t *ptr_pixel_uv_12 = ptr_pixel_in_uv_h1 + w1; /*idx0*/

                w1lambda_1 = _mx128_lu1q(&w1lambda_uv[dst_x], 0);

                w0lambda_1 = _mx128_lu1q(&w0lambda_uv[dst_x], 0);



                pixel11 = _mx128_lu1q(&ptr_pixel_uv_11[0], 0);

                pixel12 = _mx128_lu1q(&ptr_pixel_uv_12[0], 0);

                idx_0 = _mx128_lu1q(&w_pixel_uv_0[dst_x], 0);

                idx_1 = _mx128_lu1q(&w_pixel_uv_1[dst_x], 0);



                dst_w_0 = _mx128_shufv(pixel11, pixel11, idx_0);

                dst_w_1 = _mx128_shufv(pixel11, pixel11, idx_1);

                dst_h_0 = _mx128_shufv(pixel12, pixel12, idx_0);

                dst_h_1 = _mx128_shufv(pixel12, pixel12, idx_1);



                in_w_vr0 = (v4i32)_mx128_shufv(zero, dst_w_0, b_to_w_vr0);

                in_w_vr1 = (v4i32)_mx128_shufv(zero, dst_w_1, b_to_w_vr0);

                in_h_vr0 = (v4i32)_mx128_shufv(zero, dst_h_0, b_to_w_vr0);

                in_h_vr1 = (v4i32)_mx128_shufv(zero, dst_h_1, b_to_w_vr0);



                in_w_vr0 = _mx128_mul_w(in_w_vr0, w0lambda_1);

                in_w_vr1 = _mx128_mul_w(in_w_vr1, w1lambda_1);

                in_h_vr0 = _mx128_mul_w(in_h_vr0, w0lambda_1);

                in_h_vr1 = _mx128_mul_w(in_h_vr1, w1lambda_1);



                in_w_vr0 = _mx128_add_w(in_w_vr0, in_w_vr1);

                in_h_vr0 = _mx128_add_w(in_h_vr0, in_h_vr1);



                in_w_vr0 = _mx128_srli_w(in_w_vr0, 4);

                in_h_vr0 = _mx128_srli_w(in_h_vr0, 4);



                in_w_vr0 = _mx128_mul_w(in_w_vr0, h0lambda_1);

                in_h_vr0 = _mx128_mul_w(in_h_vr0, h1lambda_1);



                in_w_vr0 = _mx128_srli_w(in_w_vr0, 16);

                in_h_vr0 = _mx128_srli_w(in_h_vr0, 16);



                in_w_vr0 = _mx128_add_w(in_w_vr0, in_h_vr0);

                in_w_vr0 = _mx128_add_w(in_w_vr0, bis);



                in_w_vr0 = _mx128_srli_w(in_w_vr0, 2);

                out_value = _mx128_shufv(in_w_vr0, in_w_vr0, w_to_b_vr0);

                if (dst_w <= (valid_dst_w - 16)) {

                    _mx128_su1q(out_value, ptr_pixel_uv_out, 0);

                } else {

                    _mx128_su1q(out_value, tmp, 0);

                    memcpy(ptr_pixel_uv_out, tmp, 4);

                }

            }

        }

        for (; dst_x < valid_dst_w; dst_x += width_cycs) {

            uint8_t *ptr_pixel_out = ptr_pixel_out_h + dst_x;

            v4i32 w1lambda_1 = _mx128_lu1q(&w1lambda[dst_x], 0);

            v4i32 w0lambda_1 = _mx128_lu1q(&w0lambda[dst_x], 0);



            int src_fx_round = src_fx + 16 + dst_x * scale_x;

            int w1 = (src_fx_round) >> 16;

            w1 = w1 > 0 ? w1 : 0;

            uint8_t *ptr_pixel_11 = ptr_pixel_in_h0 + w1; /*idx0*/

            uint8_t *ptr_pixel_12 = ptr_pixel_in_h1 + w1; /*idx0*/



            v16i8 pixel11 = _mx128_lu1q(&ptr_pixel_11[0], 0);

            v16i8 pixel12 = _mx128_lu1q(&ptr_pixel_12[0], 0);

            v16i8 idx_0 = _mx128_lu1q(&w_pixel_0[dst_x], 0);

            v16i8 idx_1 = _mx128_lu1q(&w_pixel_1[dst_x], 0);



            v16i8 dst_w_0 = _mx128_shufv(pixel11, pixel11, idx_0);

            v16i8 dst_w_1 = _mx128_shufv(pixel11, pixel11, idx_1);

            v16i8 dst_h_0 = _mx128_shufv(pixel12, pixel12, idx_0);

            v16i8 dst_h_1 = _mx128_shufv(pixel12, pixel12, idx_1);



            v4i32 in_w_vr0 = (v4i32)_mx128_shufv(zero, dst_w_0, b_to_w_vr0);

            v4i32 in_w_vr1 = (v4i32)_mx128_shufv(zero, dst_w_1, b_to_w_vr0);

            v4i32 in_h_vr0 = (v4i32)_mx128_shufv(zero, dst_h_0, b_to_w_vr0);

            v4i32 in_h_vr1 = (v4i32)_mx128_shufv(zero, dst_h_1, b_to_w_vr0);



            in_w_vr0 = _mx128_mul_w(in_w_vr0, w0lambda_1);

            in_w_vr1 = _mx128_mul_w(in_w_vr1, w1lambda_1);

            in_h_vr0 = _mx128_mul_w(in_h_vr0, w0lambda_1);

            in_h_vr1 = _mx128_mul_w(in_h_vr1, w1lambda_1);



            in_w_vr0 = _mx128_add_w(in_w_vr0, in_w_vr1);

            in_h_vr0 = _mx128_add_w(in_h_vr0, in_h_vr1);



            in_w_vr0 = _mx128_srli_w(in_w_vr0, 4);

            in_h_vr0 = _mx128_srli_w(in_h_vr0, 4);



            in_w_vr0 = _mx128_mul_w(in_w_vr0, h0lambda_1);

            in_h_vr0 = _mx128_mul_w(in_h_vr0, h1lambda_1);



            in_w_vr0 = _mx128_srli_w(in_w_vr0, 16);

            in_h_vr0 = _mx128_srli_w(in_h_vr0, 16);



            in_w_vr0 = _mx128_add_w(in_w_vr0, in_h_vr0);

            in_w_vr0 = _mx128_add_w(in_w_vr0, bis);

            in_w_vr0 = _mx128_srli_w(in_w_vr0, 2);



            v16i8 out_value = _mx128_shufv(in_w_vr0, in_w_vr0, w_to_b_vr0);

            _mx128_su1q(out_value, tmp, 0);

            memcpy(ptr_pixel_out, tmp, 2);



            if (dst_y % 2 == 0) {

                w1 = w1 & 0xfffe;

                uint8_t *ptr_pixel_uv_out = ptr_pixel_out_uv_h + dst_x;

                uint8_t *ptr_pixel_uv_11 = ptr_pixel_in_uv_h0 + w1; /*idx0*/

                uint8_t *ptr_pixel_uv_12 = ptr_pixel_in_uv_h1 + w1; /*idx0*/

                w1lambda_1 = _mx128_lu1q(&w1lambda_uv[dst_x], 0);
                w0lambda_1 = _mx128_lu1q(&w0lambda_uv[dst_x], 0);

                pixel11 = _mx128_lu1q(&ptr_pixel_uv_11[0], 0);
                pixel12 = _mx128_lu1q(&ptr_pixel_uv_12[0], 0);
                idx_0 = _mx128_lu1q(&w_pixel_uv_0[dst_x], 0);
                idx_1 = _mx128_lu1q(&w_pixel_uv_1[dst_x], 0);



                dst_w_0 = _mx128_shufv(pixel11, pixel11, idx_0);
                dst_w_1 = _mx128_shufv(pixel11, pixel11, idx_1);
                dst_h_0 = _mx128_shufv(pixel12, pixel12, idx_0);
                dst_h_1 = _mx128_shufv(pixel12, pixel12, idx_1);

                in_w_vr0 = (v4i32)_mx128_shufv(zero, dst_w_0, b_to_w_vr0);
                in_w_vr1 = (v4i32)_mx128_shufv(zero, dst_w_1, b_to_w_vr0);
                in_h_vr0 = (v4i32)_mx128_shufv(zero, dst_h_0, b_to_w_vr0);
                in_h_vr1 = (v4i32)_mx128_shufv(zero, dst_h_1, b_to_w_vr0);

                in_w_vr0 = _mx128_mul_w(in_w_vr0, w0lambda_1);
                in_w_vr1 = _mx128_mul_w(in_w_vr1, w1lambda_1);
                in_h_vr0 = _mx128_mul_w(in_h_vr0, w0lambda_1);

                in_h_vr1 = _mx128_mul_w(in_h_vr1, w1lambda_1);
                in_w_vr0 = _mx128_add_w(in_w_vr0, in_w_vr1);
                in_h_vr0 = _mx128_add_w(in_h_vr0, in_h_vr1);
                in_w_vr0 = _mx128_srli_w(in_w_vr0, 4);
                in_h_vr0 = _mx128_srli_w(in_h_vr0, 4);

                in_w_vr0 = _mx128_mul_w(in_w_vr0, h0lambda_1);
                in_h_vr0 = _mx128_mul_w(in_h_vr0, h1lambda_1);

                in_w_vr0 = _mx128_srli_w(in_w_vr0, 16);
                in_h_vr0 = _mx128_srli_w(in_h_vr0, 16);

                in_w_vr0 = _mx128_add_w(in_w_vr0, in_h_vr0);
                in_w_vr0 = _mx128_add_w(in_w_vr0, bis);

                in_w_vr0 = _mx128_srli_w(in_w_vr0, 2);
                out_value = _mx128_shufv(in_w_vr0, in_w_vr0, w_to_b_vr0);
                _mx128_su1q(out_value, tmp, 0);
                memcpy(ptr_pixel_uv_out, tmp, 2);

            }

        }

        src_fy += scale_y;

    }

    free(w1lambda);

    // free(w0lambda);

    // free(w1lambda_uv);

    // free(w0lambda_uv);

    // free(h1lambda);

    // free(h0lambda);

    // free(w_pixel_0);

    // free(w_pixel_1);

    // free(w_pixel_uv_0);

    // free(w_pixel_uv_1);

    return 0;

}
#endif
#if 0
int main(){
   // for(int index=0;index<100;index++){

   //     printf("index:%d\n",index);
        int w=1920;
        int h=24;
        if(w%2 != 0){
            w+=1;
        }

        if(h%2 != 0){
            h+=1;
        }

        int w1 = 11520;
        int h1 = 144;

        int size=w*h*1.5;
        int size1 = w1 * h1 * 1.5;

        uint8_t *input2 = (uint8_t *)malloc(size*sizeof(uint8_t));
        uint8_t *dst = (uint8_t *)malloc(size1*sizeof(uint8_t));
        int handle = open("./w1920_h24.nv12", O_RDONLY);

        if (handle == -1) {
            printf("Error: %s:%d open failed\n", __func__, __LINE__);
            return -1;
        }
        if (size != read(handle, input2, size)) {
            printf("Error %s:%d read failed(src_size:%d)\n", __func__, __LINE__, size);
            return -1;
        }
        close(handle);

        //    for (int i = 0; i < size; i++) {
        //        input2[i] = 1 + rand() % 255;
        //    }

        int src_w = w, src_h = h, dst_w = w1, dst_h = h1;
        int res = opencv_resize_crop_simd(input2, src_w, src_h, dst, dst_w, dst_h);

        if (res == 0) {
            printf("opencv resize_success!!!\n");
        } else {
            printf("opencv_resize_failed");
        }

        FILE *p2 = fopen("w11520_h144.nv12","w");

        fwrite(dst,1,size1,p2);
        fclose(p2);
        free(input2);
        free(dst);
        printf("---------Test Pass--------\n");
        return 0;
  //  }

}
#endif
