/*!
 * \copy
 *     Copyright (c)  2013, Cisco Systems
 *     All rights reserved.
 *
 *     Redistribution and use in source and binary forms, with or without
 *     modification, are permitted provided that the following conditions
 *     are met:
 *
 *        * Redistributions of source code must retain the above copyright
 *          notice, this list of conditions and the following disclaimer.
 *
 *        * Redistributions in binary form must reproduce the above copyright
 *          notice, this list of conditions and the following disclaimer in
 *          the documentation and/or other materials provided with the
 *          distribution.
 *
 *     THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *     "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *     LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *     FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *     COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *     INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *     BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *     LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *     CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *     LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *     ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *     POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifdef HAVE_NEON_AARCH64
#include "arm_arch64_common_macro.S"

.macro ABS_SUB_SUM_16BYTES arg0, arg1
    ld1     {v0.16b}, [x0], x4
    ld1     {v1.16b}, [x1], x4
    uabal   \arg0, v0.8b, v1.8b
    uabal2  \arg1, v0.16b,v1.16b
.endm

.macro ABS_SUB_SUM_8x16BYTES arg0, arg1
    ld1     {v0.16b}, [x0], x4
    ld1     {v1.16b}, [x1], x4
    uabdl   \arg0, v0.8b, v1.8b
    uabdl2  \arg1, v0.16b,v1.16b

    ABS_SUB_SUM_16BYTES \arg0, \arg1
    ABS_SUB_SUM_16BYTES \arg0, \arg1
    ABS_SUB_SUM_16BYTES \arg0, \arg1
    ABS_SUB_SUM_16BYTES \arg0, \arg1
    ABS_SUB_SUM_16BYTES \arg0, \arg1
    ABS_SUB_SUM_16BYTES \arg0, \arg1
    ABS_SUB_SUM_16BYTES \arg0, \arg1
.endm

/*
 * void vaa_calc_sad_neon(uint8_t *cur_data, uint8_t *ref_data, int32_t pic_width, int32_t pic_height, int32_t pic_stride,
 *                      int32_t *psadframe, int32_t *psad8x8)
 */
WELS_ASM_AARCH64_FUNC_BEGIN VAACalcSad_AArch64_neon
    eor     v31.16b, v31.16b, v31.16b

    SIGN_EXTENSION x4, w4
    lsl     x9, x4, #4
    sub     x10, x9, #16    //x10 keep the 16*pic_stride-16
    sub     x9, x9, x2      //x9 keep the 16*pic_stride-pic_width
vaa_calc_sad_loop0:
    mov     w11, w2
vaa_calc_sad_loop1:
    ABS_SUB_SUM_8x16BYTES v2.8h, v3.8h
    ABS_SUB_SUM_8x16BYTES v4.8h, v5.8h

    uaddlv  s2, v2.8h
    uaddlv  s3, v3.8h
    uaddlv  s4, v4.8h
    uaddlv  s5, v5.8h

    st4     {v2.s, v3.s, v4.s, v5.s}[0], [x6], #16
    sub     x0, x0, x10
    sub     x1, x1, x10
    sub     w11, w11, #16
    add     v6.2s, v2.2s, v3.2s
    add     v7.2s, v4.2s, v5.2s
    add     v6.2s, v6.2s, v7.2s
    add     v31.2s, v31.2s, v6.2s
    cbnz    w11, vaa_calc_sad_loop1

    add     x0, x0, x9
    add     x1, x1, x9
    sub     w3, w3, #16
    cbnz    w3, vaa_calc_sad_loop0

    str     s31, [x5]

WELS_ASM_AARCH64_FUNC_END

.macro SAD_SD_MAD_8x16BYTES
    ld1     {v0.16b}, [x0], x4
    ld1     {v1.16b}, [x1], x4
    uabd    v31.16b, v0.16b, v1.16b
    uaddlp  v2.8h, v31.16b
    uaddlp  v4.8h, v0.16b
    uaddlp  v5.8h, v1.16b
.rept 7
    ld1     {v0.16b}, [x0], x4
    ld1     {v1.16b}, [x1], x4
    uabd    v30.16b, v0.16b, v1.16b
    umax    v31.16b, v31.16b,v30.16b
    uadalp  v2.8h, v30.16b
    uadalp  v4.8h, v0.16b
    uadalp  v5.8h, v1.16b
.endr
.endm
/*
 * void vaa_calc_sad_bgd_neon(uint8_t *cur_data, uint8_t *ref_data, int32_t pic_width, int32_t pic_height, int32_t pic_stride,
 *                             int32_t *psadframe, int32_t *psad8x8, int32_t *p_sd8x8, uint8_t *p_mad8x8)
 */

.macro SAD_SSD_BGD_8x16BYTES_1
    ld1     {v0.16b}, [x0], x4
    ld1     {v1.16b}, [x1], x4
    uabd    v31.16b, v0.16b, v1.16b
    umull   v30.8h, v31.8b, v31.8b
    uaddlp  v29.4s, v30.8h
    umull2  v30.8h, v31.16b, v31.16b
    uadalp  v29.4s, v30.8h      //  p_sqdiff

    uaddlp  v28.8h, v0.16b      //  p_sum
    umull   v30.8h, v0.8b, v0.8b
    uaddlp  v27.4s, v30.8h
    umull2  v30.8h, v0.16b, v0.16b
    uadalp  v27.4s, v30.8h      //  p_sqsum

    uaddlp  v2.8h, v31.16b      //  p_sad
    uaddlp  v4.8h, v0.16b
    uaddlp  v5.8h, v1.16b
.rept 7
    ld1     {v0.16b}, [x0], x4
    ld1     {v1.16b}, [x1], x4
    uabd    v3.16b, v0.16b, v1.16b
    umax    v31.16b, v31.16b,v3.16b     //p_mad
    umull   v30.8h, v3.8b, v3.8b
    uadalp  v29.4s, v30.8h
    umull2  v30.8h, v3.16b, v3.16b
    uadalp  v29.4s, v30.8h              //  p_sqdiff

    uadalp  v28.8h, v0.16b              //  p_sum
    umull   v30.8h, v0.8b, v0.8b
    uadalp  v27.4s, v30.8h
    umull2  v30.8h, v0.16b, v0.16b
    uadalp  v27.4s, v30.8h              //  p_sqsum

    uadalp  v2.8h, v3.16b              //p_sad
    uadalp  v4.8h, v0.16b
    uadalp  v5.8h, v1.16b               //p_sd
.endr
.endm

.macro SAD_SSD_BGD_8x16BYTES_2
    ld1     {v0.16b}, [x0], x4
    ld1     {v1.16b}, [x1], x4
    uabd    v26.16b, v0.16b, v1.16b
    umull   v30.8h, v26.8b, v26.8b
    uadalp  v29.4s, v30.8h
    umull2  v30.8h, v26.16b, v26.16b
    uadalp  v29.4s, v30.8h      //  p_sqdiff

    uadalp  v28.8h, v0.16b      //  p_sum
    umull   v30.8h, v0.8b, v0.8b
    uadalp  v27.4s, v30.8h
    umull2  v30.8h, v0.16b, v0.16b
    uadalp  v27.4s, v30.8h      //  p_sqsum

    uaddlp  v16.8h,v26.16b      //  p_sad
    uaddlp  v6.8h, v0.16b
    uaddlp  v7.8h, v1.16b
.rept 7
    ld1     {v0.16b}, [x0], x4
    ld1     {v1.16b}, [x1], x4
    uabd    v3.16b, v0.16b, v1.16b
    umax    v26.16b, v26.16b,v3.16b     //p_mad
    umull   v30.8h, v3.8b, v3.8b
    uadalp  v29.4s, v30.8h
    umull2  v30.8h, v3.16b, v3.16b
    uadalp  v29.4s, v30.8h              //  p_sqdiff

    uadalp  v28.8h, v0.16b              //  p_sum
    umull   v30.8h, v0.8b, v0.8b
    uadalp  v27.4s, v30.8h
    umull2  v30.8h, v0.16b, v0.16b
    uadalp  v27.4s, v30.8h              //  p_sqsum

    uadalp  v16.8h, v3.16b              //p_sad
    uadalp  v6.8h, v0.16b
    uadalp  v7.8h, v1.16b               //p_sd
.endr
.endm

/*
 * void vaa_calc_sad_ssd_bgd_c(uint8_t *cur_data, uint8_t *ref_data, int32_t pic_width, int32_t pic_height, int32_t pic_stride,
 *        int32_t *psadframe,int32_t *psad8x8, int32_t *psum16x16, int32_t *psqsum16x16, int32_t *psqdiff16x16, int32_t *p_sd8x8, uint8_t *p_mad8x8)
 */

.macro SAD_SSD_8x16BYTES_1
    ld1     {v0.16b}, [x0], x4
    ld1     {v1.16b}, [x1], x4
    uabd    v31.16b, v0.16b, v1.16b
    umull   v30.8h, v31.8b, v31.8b
    uaddlp  v29.4s, v30.8h
    umull2  v30.8h, v31.16b, v31.16b
    uadalp  v29.4s, v30.8h      //  p_sqdiff

    uaddlp  v28.8h, v0.16b      //  p_sum
    umull   v30.8h, v0.8b, v0.8b
    uaddlp  v27.4s, v30.8h
    umull2  v30.8h, v0.16b, v0.16b
    uadalp  v27.4s, v30.8h      //  p_sqsum

    uaddlp  v2.8h, v31.16b      //  p_sad
.rept 7
    ld1     {v0.16b}, [x0], x4
    ld1     {v1.16b}, [x1], x4
    uabd    v3.16b, v0.16b, v1.16b
    umull   v30.8h, v3.8b, v3.8b
    uadalp  v29.4s, v30.8h
    umull2  v30.8h, v3.16b, v3.16b
    uadalp  v29.4s, v30.8h              //  p_sqdiff

    uadalp  v28.8h, v0.16b              //  p_sum
    umull   v30.8h, v0.8b, v0.8b
    uadalp  v27.4s, v30.8h
    umull2  v30.8h, v0.16b, v0.16b
    uadalp  v27.4s, v30.8h              //  p_sqsum

    uadalp  v2.8h, v3.16b              //p_sad
.endr
.endm

.macro SAD_SSD_8x16BYTES_2
    ld1     {v0.16b}, [x0], x4
    ld1     {v1.16b}, [x1], x4
    uabd    v26.16b, v0.16b, v1.16b
    umull   v30.8h, v26.8b, v26.8b
    uadalp  v29.4s, v30.8h
    umull2  v30.8h, v26.16b, v26.16b
    uadalp  v29.4s, v30.8h      //  p_sqdiff

    uadalp  v28.8h, v0.16b      //  p_sum
    umull   v30.8h, v0.8b, v0.8b
    uadalp  v27.4s, v30.8h
    umull2  v30.8h, v0.16b, v0.16b
    uadalp  v27.4s, v30.8h      //  p_sqsum

    uaddlp  v16.8h,v26.16b      //  p_sad
    uaddlp  v6.8h, v0.16b
    uaddlp  v7.8h, v1.16b
.rept 7
    ld1     {v0.16b}, [x0], x4
    ld1     {v1.16b}, [x1], x4
    uabd    v3.16b, v0.16b, v1.16b
    umull   v30.8h, v3.8b, v3.8b
    uadalp  v29.4s, v30.8h
    umull2  v30.8h, v3.16b, v3.16b
    uadalp  v29.4s, v30.8h              //  p_sqdiff

    uadalp  v28.8h, v0.16b              //  p_sum
    umull   v30.8h, v0.8b, v0.8b
    uadalp  v27.4s, v30.8h
    umull2  v30.8h, v0.16b, v0.16b
    uadalp  v27.4s, v30.8h              //  p_sqsum

    uadalp  v16.8h, v3.16b              //p_sad
.endr
.endm
/*
 * void vaa_calc_sad_ssd_c(uint8_t *cur_data, uint8_t *ref_data, int32_t pic_width, int32_t pic_height, int32_t pic_stride,
 *        int32_t *psadframe,int32_t *psad8x8, int32_t *psum16x16, int32_t *psqsum16x16, int32_t *psqdiff16x16)
 */

.macro SAD_VAR_8x16BYTES_1
    ld1     {v0.16b}, [x0], x4
    ld1     {v1.16b}, [x1], x4
    uabd    v31.16b, v0.16b, v1.16b
    uaddlp  v2.8h, v31.16b      //  p_sad

    uaddlp  v28.8h, v0.16b      //  p_sum
    umull   v30.8h, v0.8b, v0.8b
    uaddlp  v27.4s, v30.8h
    umull2  v30.8h, v0.16b, v0.16b
    uadalp  v27.4s, v30.8h      //  p_sqsum

.rept 7
    ld1     {v0.16b}, [x0], x4
    ld1     {v1.16b}, [x1], x4
    uabd    v3.16b, v0.16b, v1.16b
    uadalp  v2.8h, v3.16b              //p_sad

    uadalp  v28.8h, v0.16b              //  p_sum
    umull   v30.8h, v0.8b, v0.8b
    uadalp  v27.4s, v30.8h
    umull2  v30.8h, v0.16b, v0.16b
    uadalp  v27.4s, v30.8h              //  p_sqsum
.endr
.endm
.macro SAD_VAR_8x16BYTES_2
    ld1     {v0.16b}, [x0], x4
    ld1     {v1.16b}, [x1], x4
    uabd    v26.16b, v0.16b, v1.16b
    uaddlp  v16.8h,v26.16b      //  p_sad

    uadalp  v28.8h, v0.16b      //  p_sum
    umull   v30.8h, v0.8b, v0.8b
    uadalp  v27.4s, v30.8h
    umull2  v30.8h, v0.16b, v0.16b
    uadalp  v27.4s, v30.8h      //  p_sqsum
.rept 7
    ld1     {v0.16b}, [x0], x4
    ld1     {v1.16b}, [x1], x4
    uabd    v3.16b, v0.16b, v1.16b
    uadalp  v16.8h, v3.16b              //p_sad

    uadalp  v28.8h, v0.16b              //  p_sum
    umull   v30.8h, v0.8b, v0.8b
    uadalp  v27.4s, v30.8h
    umull2  v30.8h, v0.16b, v0.16b
    uadalp  v27.4s, v30.8h              //  p_sqsum
.endr
.endm

/*
 * void vaa_calc_sad_var_c(uint8_t *cur_data, uint8_t *ref_data, int32_t pic_width, int32_t pic_height, int32_t pic_stride,
 *        int32_t *psadframe,int32_t *psad8x8, int32_t *psum16x16, int32_t *psqsum16x16)
 */

#endif
