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

