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

#ifndef MC_H
#define MC_H

#include "typedefs.h"

typedef void (*PWelsMcFunc) (const uint8_t* pSrc, int32_t iSrcStride, uint8_t* pDst, int32_t iDstStride,
                             int16_t iMvX, int16_t iMvY, int32_t iWidth, int32_t iHeight);

typedef void (*PWelsLumaHalfpelMcFunc) (const uint8_t* pSrc, int32_t iSrcStride, uint8_t* pDst, int32_t iDstStride,
                                        int32_t iWidth, int32_t iHeight);
typedef void (*PWelsSampleAveragingFunc) (uint8_t*, int32_t, const uint8_t*, int32_t, const uint8_t*, int32_t,
    int32_t, int32_t);

// jumpseat: chroma only; luma motion vectors are integer-pel (a copy)
typedef struct TagMcFunc {
  PWelsMcFunc                 pMcChromaFunc;
} SMcFunc;

namespace WelsCommon {

void InitMcFunc (SMcFunc* pMcFunc, uint32_t iCpu);

} // namespace WelsCommon


#if defined(__cplusplus)
extern "C" {
#endif//__cplusplus


#if defined(HAVE_NEON_AARCH64)
void McCopyWidthEq4_AArch64_neon (const uint8_t* pSrc, int32_t iSrcStride, uint8_t* pDst, int32_t iDstStride,
                                  int32_t iHeight);
void McCopyWidthEq8_AArch64_neon (const uint8_t* pSrc, int32_t iSrcStride, uint8_t* pDst, int32_t iDstStride,
                                  int32_t iHeight);
void McChromaWidthEq8_AArch64_neon (const uint8_t* pSrc, int32_t iSrcStride, uint8_t* pDst, int32_t iDstStride,
                                    int32_t* pWeights, int32_t iHeight);
void McChromaWidthEq4_AArch64_neon (const uint8_t* pSrc, int32_t iSrcStride, uint8_t* pDst, int32_t iDstStride,
                                    int32_t* pWeights, int32_t iHeight);
//width+1&&height+1
#endif

#if defined(X86_ASM)
void McChromaWidthEq4_mmx (const uint8_t* pSrc, int32_t iSrcStride, uint8_t* pDst, int32_t iDstStride,
                           const uint8_t* kpABCD, int32_t iHeight);
void McCopyWidthEq8_mmx (const uint8_t* pSrc, int32_t iSrcStride, uint8_t* pDst, int32_t iDstStride,
                         int32_t iHeight);

//***************************************************************************//
//                       SSE2 definition                                     //
//***************************************************************************//
void McChromaWidthEq8_sse2 (const uint8_t* pSrc, int32_t iSrcStride, uint8_t* pDst, int32_t iDstStride,
                            const uint8_t* kpABCD, int32_t iHeight);
void McCopyWidthEq16_sse2 (const uint8_t* pSrc, int32_t iSrcStride, uint8_t* pDst, int32_t iDstStride,
                           int32_t iHeight);






//***************************************************************************//
//                       SSSE3 definition                                    //
//***************************************************************************//
void McChromaWidthEq8_ssse3 (const uint8_t* pSrc, int32_t iSrcStride, uint8_t* pDst, int32_t iDstStride,
                             const uint8_t* kpABCD, int32_t iHeight);

//***************************************************************************//
//                       AVX2 definition                                     //
//***************************************************************************//
#ifdef HAVE_AVX2
#endif //HAVE_AVX2

#endif //X86_ASM

//***************************************************************************//
//                       LSX definition                                      //
//***************************************************************************//

#if defined(__cplusplus)
}
#endif//__cplusplus

#endif//MC_H
