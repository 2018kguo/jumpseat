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
.align 4
filter_para: .short 0, 1, -5, 20, 0, 0, 0, 0

.macro FILTER_6TAG_8BITS1 arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8
//  {   // input:src[-2], src[-1], src[0], src[1], src[2], src[3], dst_d, multiplier a/b; working: v18, v19
    uaddl v18.8h, \arg0\().8b, \arg5\().8b //v18=src[-2]+src[3]
    uaddl v19.8h, \arg2\().8b, \arg3\().8b  //src[0]+src[1]
    mla v18.8h, v19.8h, \arg7\().8h  //v18 += 20*(src[0]+src[1]), 2 cycles
    uaddl v19.8h, \arg1\().8b, \arg4\().8b  //src[-1]+src[2]
    mls v18.8h, v19.8h, \arg8\().8h  //v18 -= 5*(src[-1]+src[2]), 2 cycles
    sqrshrun \arg6\().8b, v18.8h, #5
//  }
.endm

.macro FILTER_6TAG_8BITS2 arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8
//  {   // input:src[-2], src[-1], src[0], src[1], src[2], src[3], dst_d, multiplier a/b; working: v18, v19
    uaddl2 v18.8h, \arg0\().16b, \arg5\().16b //v18=src[-2]+src[3]
    uaddl2 v19.8h, \arg2\().16b, \arg3\().16b   //src[0]+src[1]
    mla v18.8h, v19.8h, \arg7\().8h  //v18 += 20*(src[0]+src[1]), 2 cycles
    uaddl2 v19.8h, \arg1\().16b, \arg4\().16b  //src[-1]+src[2]
    mls v18.8h, v19.8h, \arg8\().8h  //v18 -= 5*(src[-1]+src[2]), 2 cycles
    sqrshrun2 \arg6\().16b, v18.8h, #5
//  }
.endm

.macro FILTER_6TAG_8BITS1_AVERAGE_WITH_0 arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8
//  {   // input:src[-2], src[-1], src[0], src[1], src[2], src[3], dst_d, multiplier a/b; working: v18, v19
    uaddl v18.8h, \arg0\().8b, \arg5\().8b //v18=src[-2]+src[3]
    uaddl v19.8h, \arg2\().8b, \arg3\().8b  //src[0]+src[1]
    mla v18.8h, v19.8h, \arg7\().8h  //v18 += 20*(src[0]+src[1]), 2 cycles
    uaddl v19.8h, \arg1\().8b, \arg4\().8b  //src[-1]+src[2]
    mls v18.8h, v19.8h, \arg8\().8h  //v18 -= 5*(src[-1]+src[2]), 2 cycles
    sqrshrun \arg6\().8b, v18.8h, #5
    uaddl  v19.8h, \arg2\().8b, \arg6\().8b
    rshrn \arg6\().8b, v19.8h, #1
//  }
.endm

.macro FILTER_6TAG_8BITS2_AVERAGE_WITH_0 arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8
//  {   // input:src[-2], src[-1], src[0], src[1], src[2], src[3], dst_d, multiplier a/b; working: v18, v19
    uaddl2 v18.8h, \arg0\().16b, \arg5\().16b //v18=src[-2]+src[3]
    uaddl2 v19.8h, \arg2\().16b, \arg3\().16b   //src[0]+src[1]
    mla v18.8h, v19.8h, \arg7\().8h  //v18 += 20*(src[0]+src[1]), 2 cycles
    uaddl2 v19.8h, \arg1\().16b, \arg4\().16b  //src[-1]+src[2]
    mls v18.8h, v19.8h, \arg8\().8h  //v18 -= 5*(src[-1]+src[2]), 2 cycles
    sqrshrun2 \arg6\().16b, v18.8h, #5
    uaddl2  v19.8h, \arg2\().16b, \arg6\().16b
    rshrn2 \arg6\().16b, v19.8h, #1
//  }
.endm

.macro FILTER_6TAG_8BITS1_AVERAGE_WITH_1 arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8
//  {   // input:src[-2], src[-1], src[0], src[1], src[2], src[3], dst_d, multiplier a/b; working: v18, v19
    uaddl v18.8h, \arg0\().8b, \arg5\().8b //v18=src[-2]+src[3]
    uaddl v19.8h, \arg2\().8b, \arg3\().8b  //src[0]+src[1]
    mla v18.8h, v19.8h, \arg7\().8h  //v18 += 20*(src[0]+src[1]), 2 cycles
    uaddl v19.8h, \arg1\().8b, \arg4\().8b  //src[-1]+src[2]
    mls v18.8h, v19.8h, \arg8\().8h  //v18 -= 5*(src[-1]+src[2]), 2 cycles
    sqrshrun \arg6\().8b, v18.8h, #5
    uaddl  v19.8h, \arg3\().8b, \arg6\().8b
    rshrn \arg6\().8b, v19.8h, #1
//  }
.endm

.macro FILTER_6TAG_8BITS2_AVERAGE_WITH_1 arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8
//  {   // input:src[-2], src[-1], src[0], src[1], src[2], src[3], dst_d, multiplier a/b; working: v18, v19
    uaddl2 v18.8h, \arg0\().16b, \arg5\().16b //v18=src[-2]+src[3]
    uaddl2 v19.8h, \arg2\().16b, \arg3\().16b   //src[0]+src[1]
    mla v18.8h, v19.8h, \arg7\().8h  //v18 += 20*(src[0]+src[1]), 2 cycles
    uaddl2 v19.8h, \arg1\().16b, \arg4\().16b  //src[-1]+src[2]
    mls v18.8h, v19.8h, \arg8\().8h  //v18 -= 5*(src[-1]+src[2]), 2 cycles
    sqrshrun2 \arg6\().16b, v18.8h, #5
    uaddl2  v19.8h, \arg3\().16b, \arg6\().16b
    rshrn2 \arg6\().16b, v19.8h, #1
//  }
.endm

.macro FILTER_6TAG_8BITS_TO_16BITS1 arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8
//  {   // input:d_src[-2], d_src[-1], d_src[0], d_src[1], d_src[2], d_src[3], dst_q, multiplier a/b; working:v31
    uaddl   \arg6\().8h, \arg0\().8b, \arg5\().8b       //dst_q=src[-2]+src[3]
    uaddl   v31.8h, \arg2\().8b, \arg3\().8b    //src[0]+src[1]
    mla \arg6\().8h, v31.8h, \arg7\().8h    //dst_q += 20*(src[0]+src[1]), 2 cycles
    uaddl   v31.8h, \arg1\().8b, \arg4\().8b    //src[-1]+src[2]
    mls \arg6\().8h, v31.8h, \arg8\().8h    //dst_q -= 5*(src[-1]+src[2]), 2 cycles
//  }
.endm

.macro FILTER_6TAG_8BITS_TO_16BITS2 arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8
//  {   // input:d_src[-2], d_src[-1], d_src[0], d_src[1], d_src[2], d_src[3], dst_q, multiplier a/b; working:v31
    uaddl2  \arg6\().8h, \arg0\().16b, \arg5\().16b     //dst_q=src[-2]+src[3]
    uaddl2  v31.8h, \arg2\().16b, \arg3\().16b  //src[0]+src[1]
    mla \arg6\().8h, v31.8h, \arg7\().8h    //dst_q += 20*(src[0]+src[1]), 2 cycles
    uaddl2  v31.8h, \arg1\().16b, \arg4\().16b  //src[-1]+src[2]
    mls \arg6\().8h, v31.8h, \arg8\().8h    //dst_q -= 5*(src[-1]+src[2]), 2 cycles
//  }
.endm

.macro FILTER_3_IN_16BITS_TO_8BITS1 arg0, arg1, arg2, arg3
//  {   // input:a, b, c, dst_d;
    sub \arg0\().8h, \arg0\().8h, \arg1\().8h           //a-b
    sshr    \arg0\().8h, \arg0\().8h, #2            //(a-b)/4
    sub \arg0\().8h, \arg0\().8h, \arg1\().8h           //(a-b)/4-b
    add \arg0\().8h, \arg0\().8h, \arg2\().8h           //(a-b)/4-b+c
    sshr    \arg0\().8h, \arg0\().8h, #2            //((a-b)/4-b+c)/4
    add \arg0\().8h, \arg0\().8h, \arg2\().8h           //((a-b)/4-b+c)/4+c = (a-5*b+20*c)/16
    sqrshrun    \arg3\().8b, \arg0\().8h, #6        //(+32)>>6
//  }
.endm

.macro FILTER_3_IN_16BITS_TO_8BITS2 arg0, arg1, arg2, arg3
//  {   // input:a, b, c, dst_d;
    sub \arg0\().8h, \arg0\().8h, \arg1\().8h           //a-b
    sshr    \arg0\().8h, \arg0\().8h, #2            //(a-b)/4
    sub \arg0\().8h, \arg0\().8h, \arg1\().8h           //(a-b)/4-b
    add \arg0\().8h, \arg0\().8h, \arg2\().8h           //(a-b)/4-b+c
    sshr    \arg0\().8h, \arg0\().8h, #2            //((a-b)/4-b+c)/4
    add \arg0\().8h, \arg0\().8h, \arg2\().8h           //((a-b)/4-b+c)/4+c = (a-5*b+20*c)/16
    sqrshrun2   \arg3\().16b, \arg0\().8h, #6       //(+32)>>6
//  }
.endm

.macro UNPACK_2_16BITS_TO_ABC arg0, arg1, arg2, arg3, arg4
//  {   // input:q_src[-2:5], q_src[6:13](avail 8+5)/q_src[6:**](avail 4+5), dst_a, dst_b, dst_c;
    ext \arg4\().16b, \arg0\().16b, \arg1\().16b, #4        //src[0]
    ext \arg3\().16b, \arg0\().16b, \arg1\().16b, #6        //src[1]
    add \arg4\().8h, \arg4\().8h, \arg3\().8h                   //c=src[0]+src[1]

    ext \arg3\().16b, \arg0\().16b, \arg1\().16b, #2        //src[-1]
    ext \arg2\().16b, \arg0\().16b, \arg1\().16b, #8        //src[2]
    add \arg3\().8h, \arg3\().8h, \arg2\().8h                   //b=src[-1]+src[2]

    ext \arg2\().16b, \arg0\().16b, \arg1\().16b, #10       //src[3]
    add \arg2\().8h, \arg2\().8h, \arg0\().8h                   //a=src[-2]+src[3]
//  }
.endm

.macro AVERAGE_TWO_8BITS1 arg0, arg1, arg2
//  {   // input:dst_d, src_d A and B; working: v5
    uaddl   v30.8h, \arg2\().8b, \arg1\().8b
    rshrn   \arg0\().8b, v30.8h, #1
//  }
.endm

.macro AVERAGE_TWO_8BITS2 arg0, arg1, arg2
//  {   // input:dst_d, src_d A and B; working: v5
    uaddl2  v30.8h, \arg2\().16b, \arg1\().16b
    rshrn2  \arg0\().16b, v30.8h, #1
//  }
.endm

.macro FILTER_SINGLE_TAG_8BITS arg0, arg1, arg2, arg3
// when width=17/9, used
//  {   // input: src_d{Y[0][1][2][3][4][5]X},
    rev64   \arg2\().8b, \arg0\().8b                // X[5][4][3][2][1][0]O
    uaddl   \arg2\().8h, \arg0\().8b, \arg2\().8b           // each 16bits, *[50][41][32][23][14][05]*
    mul \arg2\().4h, \arg2\().4h, \arg1\().4h           // 0+1*[50]-5*[41]+20[32]
    addv \arg3, \arg2\().4h
    sqrshrun \arg0\().8b, \arg0\().8h, #5
//  }
.endm

.macro UNPACK_FILTER_SINGLE_TAG_16BITS arg0, arg1, arg2, arg3, arg4, arg5
//  {   // each 16bits; input: d_dst, d_src[0:5], para, working, working, d(low part of d_dst)
    ext \arg3\().16b, \arg1\().16b, \arg1\().16b, #14       // X[0][1][2][3][4][5]O
    ext \arg4\().16b, \arg3\().16b, \arg3\().16b, #8      // [3][4][5]OX[0][1][2]
    rev64  \arg4\().8h, \arg4\().8h         // X[5][4][3][2][1][0]O
    add   \arg3\().8h, \arg3\().8h, \arg4\().8h    // each 16bits, *[50][41][32][23][14][05]*
    smull \arg3\().4s, \arg3\().4h, \arg2\().4h         // 0+1*[50]-5*[41]+20[32]
    saddlv \arg5, \arg3\().4s
    //sshr \arg0\().2d, \arg0\().2d, #4
    sqrshrun \arg0\().2s, \arg0\().2d, #10
    uqxtn \arg0\().4h, \arg0\().4s
    uqxtn \arg0\().8b, \arg0\().8h
    //   }
.endm

.macro VEC4_LD1_8BITS_16ELEMENT arg0, arg1, arg2, arg3, arg4, arg5
//{//load 16bytes * 4rows
    ld1 {\arg2\().16b}, [\arg0], \arg1
    ld1 {\arg3\().16b}, [\arg0], \arg1
    ld1 {\arg4\().16b}, [\arg0], \arg1
    ld1 {\arg5\().16b}, [\arg0], \arg1
//}
.endm

.macro VEC4_ST1_8BITS_8ELEMENT arg0, arg1, arg2, arg3, arg4, arg5
//{
    st1 {\arg2\().8b}, [\arg0], \arg1
    st1 {\arg3\().8b}, [\arg0], \arg1
    st1 {\arg4\().8b}, [\arg0], \arg1
    st1 {\arg5\().8b}, [\arg0], \arg1
//}
.endm

.macro VEC4_UADDL_8BITS arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9, arg10, arg11
//{
    uaddl \arg8\().8h, \arg0\().8b, \arg1\().8b
    uaddl \arg9\().8h, \arg2\().8b, \arg3\().8b
    uaddl \arg10\().8h, \arg4\().8b, \arg5\().8b
    uaddl \arg11\().8h, \arg6\().8b, \arg7\().8b
//}
.endm

.macro VEC4_UADDL2_8BITS arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9, arg10, arg11
//{
    uaddl \arg8\().8h, \arg0\().16b, \arg1\().16b
    uaddl \arg9\().8h, \arg2\().16b, \arg3\().16b
    uaddl \arg10\().8h, \arg4\().16b, \arg5\().16b
    uaddl \arg11\().8h, \arg6\().16b, \arg7\().16b
//}
.endm

.macro VEC4_MLS_16BITS arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9, arg10, arg11
//{
    mls   \arg8\().8h, \arg0\().8h, \arg1\().8h
    mls   \arg9\().8h, \arg2\().8h, \arg3\().8h
    mls   \arg10\().8h, \arg4\().8h, \arg5\().8h
    mls   \arg11\().8h, \arg6\().8h, \arg7\().8h
//}
.endm

.macro VEC4_MLA_16BITS arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9, arg10, arg11
//{
    mla   \arg8\().8h, \arg0\().8h, \arg1\().8h
    mla   \arg9\().8h, \arg2\().8h, \arg3\().8h
    mla   \arg10\().8h, \arg4\().8h, \arg5\().8h
    mla   \arg11\().8h, \arg6\().8h, \arg7\().8h
//}
.endm

.macro VEC4_SQRSHRUN_16BITS_SHIFT5 arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7
//{
    sqrshrun \arg4\().8b, \arg0\().8h, #5
    sqrshrun \arg5\().8b, \arg1\().8h, #5
    sqrshrun \arg6\().8b, \arg2\().8h, #5
    sqrshrun \arg7\().8b, \arg3\().8h, #5
//}
.endm

.macro VEC4_SQRSHRUN2_16BITS_SHIFT5 arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7
//{
    sqrshrun2 \arg4\().16b, \arg0\().8h, #5
    sqrshrun2 \arg5\().16b, \arg1\().8h, #5
    sqrshrun2 \arg6\().16b, \arg2\().8h, #5
    sqrshrun2 \arg7\().16b, \arg3\().8h, #5
//}
.endm

.macro VEC4_RSHRN_16BITS_SHIFT1 arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7
//{
    rshrn \arg4\().8b, \arg0\().8h, #1
    rshrn \arg5\().8b, \arg1\().8h, #1
    rshrn \arg6\().8b, \arg2\().8h, #1
    rshrn \arg7\().8b, \arg3\().8h, #1
//}
.endm

//void McHorVer22WidthEq4_AArch64_neon (const uint8_t* pSrc, int32_t iSrcStride, uint8_t* pDst, int32_t iDstStride,
//                                      int32_t iHeight);

//void McCopyWidthEq8_AArch64_neon (const uint8_t* pSrc, int32_t iSrcStride, uint8_t* pDst, int32_t iDstStride,
//                                  int32_t iHeight);
WELS_ASM_AARCH64_FUNC_BEGIN McCopyWidthEq8_AArch64_neon
    //prfm pldl1strm, [x0]
    SIGN_EXTENSION x1,w1
    SIGN_EXTENSION x3,w3
    SIGN_EXTENSION x4,w4
w8_copy_loop:
    //prfm pldl1strm, [x0, x1]
    ld1 {v0.8b}, [x0], x1  //read 16Byte : 0 line
    st1 {v0.8b}, [x2], x3 //write 16Byte : 0 line
    //prfm pldl1strm, [x0, x1]
    ld1 {v1.8b}, [x0], x1  //read 16Byte : 1 line
    st1 {v1.8b}, [x2], x3 //write 16Byte : 1 line

    sub x4, x4, #2
    cbnz x4, w8_copy_loop
WELS_ASM_AARCH64_FUNC_END

WELS_ASM_AARCH64_FUNC_BEGIN McCopyWidthEq4_AArch64_neon
    //prfm pldl1strm, [x0]
    SIGN_EXTENSION x1,w1
    SIGN_EXTENSION x3,w3
    SIGN_EXTENSION x4,w4
w4_copy_loop:
    //prfm pldl1strm, [x0, x1]
    ld1 {v0.s}[0], [x0], x1  //read 16Byte : 0 line
    st1 {v0.s}[0], [x2], x3 //write 16Byte : 0 line
    //prfm pldl1strm, [x0, x1]
    ld1 {v1.s}[0], [x0], x1  //read 16Byte : 1 line
    st1 {v1.s}[0], [x2], x3 //write 16Byte : 1 line

    sub x4, x4, #2
    cbnz x4, w4_copy_loop
WELS_ASM_AARCH64_FUNC_END

//void PixStrideAvgWidthEq16_AArch64_neon (uint8_t* pDst, int32_t iDstStride, const uint8_t* pSrcA, int32_t iSrcStrideA,
//const uint8_t* pSrcB, int32_t iSrcStrideB, int32_t iHeight);

//void McChromaWidthEq8_AArch64_neon (const uint8_t* pSrc, int32_t iSrcStride, uint8_t* pDst, int32_t iDstStride,
//                                    int32_t* pWeights, int32_t iHeight);
WELS_ASM_AARCH64_FUNC_BEGIN McChromaWidthEq8_AArch64_neon
    SIGN_EXTENSION x1,w1
    SIGN_EXTENSION x3,w3
    SIGN_EXTENSION x5,w5
    ld4r {v28.8b, v29.8b, v30.8b, v31.8b}, [x4] //load A/B/C/D
    ld1 {v16.16b}, [x0], x1  // src[x]
    ext v17.16b, v16.16b, v16.16b, #1  // src[x+1]
w8_mc_chroma_loop:
    ld1 {v18.16b}, [x0], x1  // src[x+stride]
    ext v19.16b, v18.16b, v18.16b, #1  // src[x+stride+1]

    ld1 {v20.16b}, [x0], x1  // src[x+2*stride]
    ext v21.16b, v20.16b, v20.16b, #1  // src[x+2*stride+1]

    ld1 {v22.16b}, [x0], x1  // src[x+3*stride]
    ext v23.16b, v22.16b, v22.16b, #1  // src[x+3*stride+1]

    ld1 {v24.16b}, [x0], x1  // src[x+4*stride]
    ext v25.16b, v24.16b, v24.16b, #1  // src[x+4*stride+1]

    umull v0.8h, v16.8b, v28.8b
    umull v2.8h, v18.8b, v28.8b
    umull v4.8h, v20.8b, v28.8b
    umull v6.8h, v22.8b, v28.8b

    umlal v0.8h, v17.8b, v29.8b
    umlal v2.8h, v19.8b, v29.8b
    umlal v4.8h, v21.8b, v29.8b
    umlal v6.8h, v23.8b, v29.8b

    umlal v0.8h, v18.8b, v30.8b
    umlal v2.8h, v20.8b, v30.8b
    umlal v4.8h, v22.8b, v30.8b
    umlal v6.8h, v24.8b, v30.8b

    umlal v0.8h, v19.8b, v31.8b
    umlal v2.8h, v21.8b, v31.8b
    umlal v4.8h, v23.8b, v31.8b
    umlal v6.8h, v25.8b, v31.8b

    rshrn v1.8b, v0.8h, #6
    st1 {v1.8b}, [x2], x3

    rshrn v3.8b, v2.8h, #6
    st1 {v3.8b}, [x2], x3

    rshrn v5.8b, v4.8h, #6
    st1 {v5.8b}, [x2], x3

    rshrn v7.8b, v6.8h, #6
    st1 {v7.8b}, [x2], x3

    mov v16.16b, v24.16b
    mov v17.16b, v25.16b
    sub x5, x5, #4
    cbnz x5, w8_mc_chroma_loop
WELS_ASM_AARCH64_FUNC_END
//void McChromaWidthEq4_AArch64_neon (const uint8_t* pSrc, int32_t iSrcStride, uint8_t* pDst, int32_t iDstStride,
//                                    int32_t* pWeights, int32_t iHeight);
WELS_ASM_AARCH64_FUNC_BEGIN McChromaWidthEq4_AArch64_neon
    SIGN_EXTENSION x1,w1
    SIGN_EXTENSION x3,w3
    SIGN_EXTENSION x5,w5
    ld4r {v4.8b, v5.8b, v6.8b, v7.8b}, [x4] //load A/B/C/D
    ld1 {v0.8b}, [x0], x1  // src[x]
    ext v1.8b, v0.8b, v0.8b, #1  // src[x+1]
w4_mc_chroma_loop:
    ld1 {v2.8b}, [x0], x1  // src[x+stride]
    ext v3.8b, v2.8b, v2.8b, #1  // src[x+stride+1]
    ld1 {v18.8b}, [x0], x1  // src[x+2*stride]
    ext v19.8b, v18.8b, v18.8b, #1  // src[x+2*stride+1]

    zip1 v0.4s, v0.4s, v2.4s
    zip1 v1.4s, v1.4s, v3.4s
    zip1 v2.4s, v2.4s, v18.4s
    zip1 v3.4s, v3.4s, v19.4s

    umull v16.8h, v0.8b, v4.8b
    umlal v16.8h, v1.8b, v5.8b
    umlal v16.8h, v2.8b, v6.8b
    umlal v16.8h, v3.8b, v7.8b
    rshrn v17.8b, v16.8h, #6
    st1 {v17.s}[0], [x2], x3
    st1 {v17.s}[1], [x2], x3

    mov v0.8b, v18.8b
    mov v1.8b, v19.8b
    sub x5, x5, #2
    cbnz x5, w4_mc_chroma_loop
WELS_ASM_AARCH64_FUNC_END

#endif

