/*!
 * \copy
 *     Copyright (c)  2009-2013, Cisco Systems
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
 *
 * \file    mc.c
 *
 * \brief   Interfaces implementation for motion compensation
 *
 * \date    03/17/2009 Created
 *
 *************************************************************************************
 */

#include "mc.h"

#include "cpu_core.h"
#include "ls_defines.h"
#include "macros.h"

namespace {

typedef void (*PMcChromaWidthExtFunc) (const uint8_t* pSrc, int32_t iSrcStride, uint8_t* pDst, int32_t iDstStride,
                                       const uint8_t* kpABCD, int32_t iHeight);
typedef void (*PWelsSampleWidthAveragingFunc) (uint8_t*, int32_t, const uint8_t*, int32_t, const uint8_t*,
    int32_t, int32_t);
typedef void (*PWelsMcWidthHeightFunc) (const uint8_t* pSrc, int32_t iSrcStride, uint8_t* pDst, int32_t iDstStride,
                                        int32_t iWidth, int32_t iHeight);

/*------------------weight for chroma fraction pixel interpolation------------------*/
//iA = (8 - dx) * (8 - dy);
//iB = dx * (8 - dy);
//iC = (8 - dx) * dy;
//iD = dx * dy
static const uint8_t g_kuiABCD[8][8][4] = { //g_kA[dy][dx], g_kB[dy][dx], g_kC[dy][dx], g_kD[dy][dx]
  {
    {64, 0, 0, 0}, {56, 8, 0, 0}, {48, 16, 0, 0}, {40, 24, 0, 0},
    {32, 32, 0, 0}, {24, 40, 0, 0}, {16, 48, 0, 0}, {8, 56, 0, 0}
  },
  {
    {56, 0, 8, 0}, {49, 7, 7, 1}, {42, 14, 6, 2}, {35, 21, 5, 3},
    {28, 28, 4, 4}, {21, 35, 3, 5}, {14, 42, 2, 6}, {7, 49, 1, 7}
  },
  {
    {48, 0, 16, 0}, {42, 6, 14, 2}, {36, 12, 12, 4}, {30, 18, 10, 6},
    {24, 24, 8, 8}, {18, 30, 6, 10}, {12, 36, 4, 12}, {6, 42, 2, 14}
  },
  {
    {40, 0, 24, 0}, {35, 5, 21, 3}, {30, 10, 18, 6}, {25, 15, 15, 9},
    {20, 20, 12, 12}, {15, 25, 9, 15}, {10, 30, 6, 18}, {5, 35, 3, 21}
  },
  {
    {32, 0, 32, 0}, {28, 4, 28, 4}, {24, 8, 24, 8}, {20, 12, 20, 12},
    {16, 16, 16, 16}, {12, 20, 12, 20}, {8, 24, 8, 24}, {4, 28, 4, 28}
  },
  {
    {24, 0, 40, 0}, {21, 3, 35, 5}, {18, 6, 30, 10}, {15, 9, 25, 15},
    {12, 12, 20, 20}, {9, 15, 15, 25}, {6, 18, 10, 30}, {3, 21, 5, 35}
  },
  {
    {16, 0, 48, 0}, {14, 2, 42, 6}, {12, 4, 36, 12}, {10, 6, 30, 18},
    {8, 8, 24, 24}, {6, 10, 18, 30}, {4, 12, 12, 36}, {2, 14, 6, 42}
  },
  {
    {8, 0, 56, 0}, {7, 1, 49, 7}, {6, 2, 42, 14}, {5, 3, 35, 21},
    {4, 4, 28, 28}, {3, 5, 21, 35}, {2, 6, 14, 42}, {1, 7, 7, 49}
  }
};

//***************************************************************************//
//                          C code implementation                            //
//***************************************************************************//
static inline void McCopyWidthEq2_c (const uint8_t* pSrc, int32_t iSrcStride, uint8_t* pDst, int32_t iDstStride,
                                     int32_t iHeight) {
  int32_t i;
  for (i = 0; i < iHeight; i++) { // iWidth == 2 only for chroma
    ST16A2 (pDst, LD16 (pSrc));
    pDst += iDstStride;
    pSrc += iSrcStride;
  }
}

static inline void McCopyWidthEq4_c (const uint8_t* pSrc, int32_t iSrcStride, uint8_t* pDst, int32_t iDstStride,
                                     int32_t iHeight) {
  int32_t i;
  for (i = 0; i < iHeight; i++) {
    ST32A4 (pDst, LD32 (pSrc));
    pDst += iDstStride;
    pSrc += iSrcStride;
  }
}

static inline void McCopyWidthEq8_c (const uint8_t* pSrc, int32_t iSrcStride, uint8_t* pDst, int32_t iDstStride,
                                     int32_t iHeight) {
  int32_t i;
  for (i = 0; i < iHeight; i++) {
    ST64A8 (pDst, LD64 (pSrc));
    pDst += iDstStride;
    pSrc += iSrcStride;
  }
}

static inline void McCopyWidthEq16_c (const uint8_t* pSrc, int32_t iSrcStride, uint8_t* pDst, int32_t iDstStride,
                                      int32_t iHeight) {
  int32_t i;
  for (i = 0; i < iHeight; i++) {
    ST64A8 (pDst  , LD64 (pSrc));
    ST64A8 (pDst + 8, LD64 (pSrc + 8));
    pDst += iDstStride;
    pSrc += iSrcStride;
  }
}

//--------------------Luma sample MC------------------//


static inline void McCopy_c (const uint8_t* pSrc, int32_t iSrcStride, uint8_t* pDst, int32_t iDstStride, int32_t iWidth,
                             int32_t iHeight) {
  if (iWidth == 16)
    McCopyWidthEq16_c (pSrc, iSrcStride, pDst, iDstStride, iHeight);
  else if (iWidth == 8)
    McCopyWidthEq8_c (pSrc, iSrcStride, pDst, iDstStride, iHeight);
  else if (iWidth == 4)
    McCopyWidthEq4_c (pSrc, iSrcStride, pDst, iDstStride, iHeight);
  else //here iWidth == 2
    McCopyWidthEq2_c (pSrc, iSrcStride, pDst, iDstStride, iHeight);
}






static inline void McChromaWithFragMv_c (const uint8_t* pSrc, int32_t iSrcStride, uint8_t* pDst, int32_t iDstStride,
    int16_t iMvX, int16_t iMvY, int32_t iWidth, int32_t iHeight) {
  int32_t i, j;
  int32_t iA, iB, iC, iD;
  const uint8_t* pSrcNext = pSrc + iSrcStride;
  const uint8_t* pABCD = g_kuiABCD[iMvY & 0x07][iMvX & 0x07];
  iA = pABCD[0];
  iB = pABCD[1];
  iC = pABCD[2];
  iD = pABCD[3];
  for (i = 0; i < iHeight; i++) {
    for (j = 0; j < iWidth; j++) {
      pDst[j] = (iA * pSrc[j] + iB * pSrc[j + 1] + iC * pSrcNext[j] + iD * pSrcNext[j + 1] + 32) >> 6;
    }
    pDst     += iDstStride;
    pSrc      = pSrcNext;
    pSrcNext += iSrcStride;
  }
}

void McChroma_c (const uint8_t* pSrc, int32_t iSrcStride, uint8_t* pDst, int32_t iDstStride,
                 int16_t iMvX, int16_t iMvY, int32_t iWidth, int32_t iHeight)
//pSrc has been added the offset of mv
{
  const int32_t kiD8x = iMvX & 0x07;
  const int32_t kiD8y = iMvY & 0x07;
  if (0 == kiD8x && 0 == kiD8y)
    McCopy_c (pSrc, iSrcStride, pDst, iDstStride, iWidth, iHeight);
  else
    McChromaWithFragMv_c (pSrc, iSrcStride, pDst, iDstStride, iMvX, iMvY, iWidth, iHeight);
}

#if defined(X86_ASM)






static inline void McCopy_sse2 (const uint8_t* pSrc, int32_t iSrcStride, uint8_t* pDst, int32_t iDstStride,
                                int32_t iWidth,
                                int32_t iHeight) {
  if (iWidth == 16)
    McCopyWidthEq16_sse2 (pSrc, iSrcStride, pDst, iDstStride, iHeight);
  else if (iWidth == 8)
    McCopyWidthEq8_mmx (pSrc, iSrcStride, pDst, iDstStride, iHeight);
  else if (iWidth == 4)
    McCopyWidthEq4_c (pSrc, iSrcStride, pDst, iDstStride, iHeight);
  else
    McCopyWidthEq2_c (pSrc, iSrcStride, pDst, iDstStride, iHeight);
}






void McChroma_sse2 (const uint8_t* pSrc, int32_t iSrcStride, uint8_t* pDst, int32_t iDstStride,
                    int16_t iMvX, int16_t iMvY, int32_t iWidth, int32_t iHeight) {
  static const PMcChromaWidthExtFunc kpMcChromaWidthFuncs[2] = {
    McChromaWidthEq4_mmx,
    McChromaWidthEq8_sse2
  };
  const int32_t kiD8x = iMvX & 0x07;
  const int32_t kiD8y = iMvY & 0x07;
  if (kiD8x == 0 && kiD8y == 0) {
    McCopy_sse2 (pSrc, iSrcStride, pDst, iDstStride, iWidth, iHeight);
    return;
  }
  if (iWidth != 2) {
    kpMcChromaWidthFuncs[iWidth >> 3] (pSrc, iSrcStride, pDst, iDstStride, g_kuiABCD[kiD8y][kiD8x], iHeight);
  } else
    McChromaWithFragMv_c (pSrc, iSrcStride, pDst, iDstStride, iMvX, iMvY, iWidth, iHeight);
}

//***************************************************************************//
//                          SSSE3 implementation                             //
//***************************************************************************//


















void McChroma_ssse3 (const uint8_t* pSrc, int32_t iSrcStride, uint8_t* pDst, int32_t iDstStride,
                     int16_t iMvX, int16_t iMvY, int32_t iWidth, int32_t iHeight) {
  static const PMcChromaWidthExtFunc kpMcChromaWidthFuncs[2] = {
    McChromaWidthEq4_mmx,
    McChromaWidthEq8_ssse3
  };
  const int32_t kiD8x = iMvX & 0x07;
  const int32_t kiD8y = iMvY & 0x07;
  if (kiD8x == 0 && kiD8y == 0) {
    McCopy_sse2 (pSrc, iSrcStride, pDst, iDstStride, iWidth, iHeight);
    return;
  }
  if (iWidth != 2) {
    kpMcChromaWidthFuncs[iWidth >> 3] (pSrc, iSrcStride, pDst, iDstStride, g_kuiABCD[kiD8y][kiD8x], iHeight);
  } else
    McChromaWithFragMv_c (pSrc, iSrcStride, pDst, iDstStride, iMvX, iMvY, iWidth, iHeight);
}

//***************************************************************************//
//                          AVX2 implementation                              //
//***************************************************************************//

#ifdef HAVE_AVX2
















#endif //HAVE_AVX2


#endif //X86_ASM
//***************************************************************************//
//                       NEON implementation                      //
//***************************************************************************//
#if defined(HAVE_NEON_AARCH64)


void McChroma_AArch64_neon (const uint8_t* pSrc, int32_t iSrcStride, uint8_t* pDst, int32_t iDstStride,
                            int16_t iMvX, int16_t iMvY, int32_t iWidth, int32_t iHeight) {
  if (0 == iMvX && 0 == iMvY) {
    if (8 == iWidth)
      McCopyWidthEq8_AArch64_neon (pSrc, iSrcStride, pDst, iDstStride, iHeight);
    else if (iWidth == 4)
      McCopyWidthEq4_AArch64_neon (pSrc, iSrcStride, pDst, iDstStride, iHeight);
    else //here iWidth == 2
      McCopyWidthEq2_c (pSrc, iSrcStride, pDst, iDstStride, iHeight);
  } else {
    const int32_t kiD8x = iMvX & 0x07;
    const int32_t kiD8y = iMvY & 0x07;
    if (8 == iWidth)
      McChromaWidthEq8_AArch64_neon (pSrc, iSrcStride, pDst, iDstStride, (int32_t*) (g_kuiABCD[kiD8y][kiD8x]), iHeight);
    else if (4 == iWidth)
      McChromaWidthEq4_AArch64_neon (pSrc, iSrcStride, pDst, iDstStride, (int32_t*) (g_kuiABCD[kiD8y][kiD8x]), iHeight);
    else //here iWidth == 2
      McChromaWithFragMv_c (pSrc, iSrcStride, pDst, iDstStride, iMvX, iMvY, iWidth, iHeight);
  }
}
#endif



} // anon ns.

void WelsCommon::InitMcFunc (SMcFunc* pMcFuncs, uint32_t uiCpuFlag) {
  pMcFuncs->pMcChromaFunc     = McChroma_c;

#if defined (X86_ASM)
  if (uiCpuFlag & WELS_CPU_SSE2) {
    pMcFuncs->pMcChromaFunc     = McChroma_sse2;
  }
  if (uiCpuFlag & WELS_CPU_SSSE3) {
    pMcFuncs->pMcChromaFunc = McChroma_ssse3;
  }
#endif //(X86_ASM)

#if defined(HAVE_NEON_AARCH64)
  if (uiCpuFlag & WELS_CPU_NEON) {
    pMcFuncs->pMcChromaFunc     = McChroma_AArch64_neon;
  }
#endif
}
