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

#include "ComplexityAnalysis.h"
#include "cpu.h"
#include "macros.h"
#include "intra_pred_common.h"
#include "sad_common.h"

WELSVP_NAMESPACE_BEGIN

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

CComplexityAnalysisScreen::CComplexityAnalysisScreen (int32_t iCpuFlag) {
  m_eMethod   = METHOD_COMPLEXITY_ANALYSIS_SCREEN;
  WelsMemset (&m_ComplexityAnalysisParam, 0, sizeof (m_ComplexityAnalysisParam));

  m_pSadFunc = WelsSampleSad16x16_c;
  m_pIntraFunc[0] = WelsI16x16LumaPredV_c;
  m_pIntraFunc[1] = WelsI16x16LumaPredH_c;
#ifdef X86_ASM
  if (iCpuFlag & WELS_CPU_SSE2) {
    m_pSadFunc = WelsSampleSad16x16_sse2;
    m_pIntraFunc[0] = WelsI16x16LumaPredV_sse2;
    m_pIntraFunc[1] = WelsI16x16LumaPredH_sse2;

  }
#endif


#if defined (HAVE_NEON_AARCH64)
  if (iCpuFlag & WELS_CPU_NEON) {
    m_pSadFunc = WelsSampleSad16x16_AArch64_neon;
    m_pIntraFunc[0] =  WelsI16x16LumaPredV_AArch64_neon;
    m_pIntraFunc[1] = WelsI16x16LumaPredH_AArch64_neon;
  }
#endif


}

CComplexityAnalysisScreen::~CComplexityAnalysisScreen() {
}

EResult CComplexityAnalysisScreen::Process (int32_t nType, SPixMap* pSrc, SPixMap* pRef) {
  bool bScrollFlag = m_ComplexityAnalysisParam.sScrollResult.bScrollDetectFlag;
  int32_t iIdrFlag    = m_ComplexityAnalysisParam.iIdrFlag;
  int32_t iScrollMvX = m_ComplexityAnalysisParam.sScrollResult.iScrollMvX;
  int32_t iScrollMvY = m_ComplexityAnalysisParam.sScrollResult.iScrollMvY;

  if (m_ComplexityAnalysisParam.iMbRowInGom <= 0)
    return RET_INVALIDPARAM;
  if (!iIdrFlag && pRef == NULL)
    return RET_INVALIDPARAM;

  if (iIdrFlag || pRef == NULL) {
    GomComplexityAnalysisIntra (pSrc);
  } else if (!bScrollFlag || ((iScrollMvX == 0) && (iScrollMvY == 0))) {
    GomComplexityAnalysisInter (pSrc, pRef, 0);
  } else {
    GomComplexityAnalysisInter (pSrc, pRef, 1);
  }

  return RET_SUCCESS;
}


EResult CComplexityAnalysisScreen::Set (int32_t nType, void* pParam) {
  if (pParam == NULL)
    return RET_INVALIDPARAM;

  m_ComplexityAnalysisParam = * (SComplexityAnalysisScreenParam*)pParam;

  return RET_SUCCESS;
}

EResult CComplexityAnalysisScreen::Get (int32_t nType, void* pParam) {
  if (pParam == NULL)
    return RET_INVALIDPARAM;

  * (SComplexityAnalysisScreenParam*)pParam = m_ComplexityAnalysisParam;

  return RET_SUCCESS;
}

void CComplexityAnalysisScreen::GomComplexityAnalysisIntra (SPixMap* pSrc) {
  int32_t iWidth                  = pSrc->sRect.iRectWidth;
  int32_t iHeight                 = pSrc->sRect.iRectHeight;
  int32_t iBlockWidth             = iWidth  >> 4;
  int32_t iBlockHeight            = iHeight >> 4;

  int32_t iBlockSadH, iBlockSadV, iGomSad = 0;
  int32_t iIdx = 0;

  uint8_t* pPtrY = NULL;
  int32_t iStrideY = 0;
  int32_t iRowStrideY = 0;

  uint8_t* pTmpCur = NULL;

  ENFORCE_STACK_ALIGN_1D (uint8_t, iMemPredMb, 256, 16)

  pPtrY = (uint8_t*)pSrc->pPixel[0];

  iStrideY  = pSrc->iStride[0];
  iRowStrideY = iStrideY << 4;

  m_ComplexityAnalysisParam.iFrameComplexity = 0;

  for (int32_t j = 0; j < iBlockHeight; j ++) {
    pTmpCur = pPtrY;

    for (int32_t i = 0; i < iBlockWidth; i++) {
      iBlockSadH = iBlockSadV = 0x7fffffff; // INT_MAX
      if (j > 0) {
        m_pIntraFunc[0] (iMemPredMb, pTmpCur, iStrideY);
        iBlockSadH = m_pSadFunc (pTmpCur, iStrideY, iMemPredMb, 16);
      }
      if (i > 0) {
        m_pIntraFunc[1] (iMemPredMb, pTmpCur, iStrideY);
        iBlockSadV = m_pSadFunc (pTmpCur, iStrideY, iMemPredMb, 16);
      }
      if (i || j)
        iGomSad += WELS_MIN (iBlockSadH, iBlockSadV);

      pTmpCur += 16;

      if (i == iBlockWidth - 1 && ((j + 1) % m_ComplexityAnalysisParam.iMbRowInGom == 0 || j == iBlockHeight - 1)) {
        m_ComplexityAnalysisParam.pGomComplexity[iIdx] = iGomSad;
        m_ComplexityAnalysisParam.iFrameComplexity += iGomSad;
        iIdx++;
        iGomSad = 0;
      }
    }

    pPtrY += iRowStrideY;
  }
}


void CComplexityAnalysisScreen::GomComplexityAnalysisInter (SPixMap* pSrc, SPixMap* pRef, bool bScrollFlag) {
  int32_t iWidth                  = pSrc->sRect.iRectWidth;
  int32_t iHeight                 = pSrc->sRect.iRectHeight;
  int32_t iBlockWidth             = iWidth  >> 4;
  int32_t iBlockHeight            = iHeight >> 4;

  int32_t iInterSad, iScrollSad, iBlockSadH, iBlockSadV, iGomSad = 0;
  int32_t iIdx = 0;

  int32_t iScrollMvX = m_ComplexityAnalysisParam.sScrollResult.iScrollMvX;
  int32_t iScrollMvY = m_ComplexityAnalysisParam.sScrollResult.iScrollMvY;

  uint8_t* pPtrX = NULL, *pPtrY = NULL;
  int32_t iStrideX = 0, iStrideY = 0;
  int32_t iRowStrideX = 0, iRowStrideY = 0;

  uint8_t* pTmpRef = NULL, *pTmpCur = NULL, *pTmpRefScroll = NULL;

  ENFORCE_STACK_ALIGN_1D (uint8_t, iMemPredMb, 256, 16)

  pPtrX = (uint8_t*)pRef->pPixel[0];
  pPtrY = (uint8_t*)pSrc->pPixel[0];

  iStrideX  = pRef->iStride[0];
  iStrideY  = pSrc->iStride[0];

  iRowStrideX  = pRef->iStride[0] << 4;
  iRowStrideY  = pSrc->iStride[0] << 4;

  m_ComplexityAnalysisParam.iFrameComplexity = 0;

  for (int32_t j = 0; j < iBlockHeight; j ++) {
    pTmpRef  = pPtrX;
    pTmpCur  = pPtrY;

    for (int32_t i = 0; i < iBlockWidth; i++) {
      int32_t iBlockPointX = i << 4;
      int32_t iBlockPointY = j << 4;

      iInterSad = m_pSadFunc (pTmpCur, iStrideY, pTmpRef, iStrideX);
      if (bScrollFlag) {
        if ((iInterSad != 0) &&
            (iBlockPointX + iScrollMvX >= 0) && (iBlockPointX + iScrollMvX <= iWidth - 8) &&
            (iBlockPointY + iScrollMvY >= 0) && (iBlockPointY + iScrollMvY <= iHeight - 8)) {
          pTmpRefScroll = pTmpRef - iScrollMvY * iStrideX + iScrollMvX;
          iScrollSad = m_pSadFunc (pTmpCur, iStrideY, pTmpRefScroll, iStrideX);

          if (iScrollSad < iInterSad) {
            iInterSad = iScrollSad;
          }
        }

      }

      iBlockSadH = iBlockSadV = 0x7fffffff; // INT_MAX

      // jumpseat: an unchanged block's cost is 0 whatever intra would cost.
      if (j > 0 && iInterSad) {
        m_pIntraFunc[0] (iMemPredMb, pTmpCur, iStrideY);
        iBlockSadH = m_pSadFunc (pTmpCur, iStrideY, iMemPredMb, 16);
      }
      if (i > 0 && iInterSad) {
        m_pIntraFunc[1] (iMemPredMb, pTmpCur, iStrideY);
        iBlockSadV = m_pSadFunc (pTmpCur, iStrideY, iMemPredMb, 16);
      }

      iGomSad += WELS_MIN (WELS_MIN (iBlockSadH, iBlockSadV), iInterSad);

      if (i == iBlockWidth - 1 && ((j + 1) % m_ComplexityAnalysisParam.iMbRowInGom == 0 || j == iBlockHeight - 1)) {
        m_ComplexityAnalysisParam.pGomComplexity[iIdx] = iGomSad;
        m_ComplexityAnalysisParam.iFrameComplexity += iGomSad;
        iIdx++;
        iGomSad = 0;
      }

      pTmpRef += 16;
      pTmpCur += 16;
    }
    pPtrX += iRowStrideX;
    pPtrY += iRowStrideY;
  }
}

WELSVP_NAMESPACE_END
