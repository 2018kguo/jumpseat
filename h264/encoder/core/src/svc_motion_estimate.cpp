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
 * \file  svc motion estimate.c
 *
 * \brief  Interfaces introduced in svc mb motion estimation
 *
 * \date  08/11/2009 Created
 *
 *************************************************************************************
 */

#include "cpu_core.h"
#include "ls_defines.h"
#include "svc_motion_estimate.h"
#include "wels_transpose_matrix.h"

namespace WelsEnc {

const int32_t QStepx16ByQp[52] = {  /* save QStep<<4 for int32_t */
  10,  11,  13,  14,  16,  18,  /* 0~5   */
  20,  22,  26,  28,  32,  36,  /* 6~11  */
  40,  44,  52,  56,  64,  72,  /* 12~17 */
  80,  88,  104, 112, 128, 144, /* 18~23 */
  160, 176, 208, 224, 256, 288, /* 24~29 */
  320, 352, 416, 448, 512, 576, /* 30~35 */
  640, 704, 832, 896, 1024, 1152, /* 36~41 */
  1280, 1408, 1664, 1792, 2048, 2304, /* 42~47 */
  2560, 2816, 3328, 3584     /* 48~51 */
};

static inline void UpdateMeResults (const SMVUnitXY ksBestMv, const uint32_t kiBestSadCost, uint8_t* pRef,
                                    SWelsME* pMe) {
  pMe->sMv = ksBestMv;
  pMe->pRefMb = pRef;
  pMe->uiSadCost = kiBestSadCost;
}
static inline void MeEndIntepelSearch (SWelsME* pMe) {
  /* -> qpel mv */
  pMe->sMv.iMvX *= (1 << 2);
  pMe->sMv.iMvY *= (1 << 2);
  pMe->uiSatdCost = pMe->uiSadCost;
}

void WelsInitMeFunc (SWelsFuncPtrList* pFuncList, uint32_t uiCpuFlag) {
  pFuncList->pfCheckDirectionalMv = CheckDirectionalMv;

  //for cross serarch
  pFuncList->pfVerticalFullSearch = LineFullSearch_c;
  pFuncList->pfHorizontalFullSearch = LineFullSearch_c;

#if defined (X86_ASM)
  if (uiCpuFlag & WELS_CPU_SSE41) {
    pFuncList->pfSampleSadHor8[0] = SampleSad8x8Hor8_sse41;
    pFuncList->pfSampleSadHor8[1] = SampleSad16x16Hor8_sse41;
    pFuncList->pfVerticalFullSearch = VerticalFullSearchUsingSSE41;
    pFuncList->pfHorizontalFullSearch = HorizontalFullSearchUsingSSE41;
  }
#endif
}

/*!
 * \brief  BL mb motion estimate search
 *
 * \param  enc      Wels encoder context
 * \param  pMe          Wels me information
 *
 * \return  NONE
 */

void WelsMotionEstimateSearch (SWelsFuncPtrList* pFuncList, SDqLayer* pCurDqLayer, SWelsME* pMe, SSlice* pSlice) {
  const int32_t kiStrideEnc = pCurDqLayer->iEncStride[0];
  const int32_t kiStrideRef = pCurDqLayer->pRefPic->iLineSize[0];

  //  Step 1: Initial point prediction
  if (!WelsMotionEstimateInitialPoint (pFuncList, pMe, pSlice, kiStrideEnc, kiStrideRef)) {
    pFuncList->pfSearchMethod[pMe->uiBlockSize] (pFuncList, pMe, pSlice, kiStrideEnc, kiStrideRef);
    MeEndIntepelSearch (pMe);
  }

  pFuncList->pfCalculateSatd (pFuncList->sSampleDealingFuncs.pfSampleSatd[pMe->uiBlockSize], pMe, kiStrideEnc,
                              kiStrideRef);
}

void WelsMotionEstimateSearchStatic (SWelsFuncPtrList* pFuncList, SDqLayer* pCurDqLayer, SWelsME* pMe,
                                     SSlice* pLpslice) {
  const int32_t kiStrideEnc = pCurDqLayer->iEncStride[0];
  const int32_t kiStrideRef = pCurDqLayer->pRefPic->iLineSize[0];

  pMe->sMv.iMvX = pMe->sMv.iMvY = 0;
  pMe->uiSadCost =
    pFuncList->sSampleDealingFuncs.pfSampleSad[pMe->uiBlockSize] (pMe->pEncMb, kiStrideEnc, pMe->pRefMb, kiStrideRef) ;
  pMe->uiSadCost += COST_MVD (pMe->pMvdCost, - pMe->sMvp.iMvX, - pMe->sMvp.iMvY);
  MeEndIntepelSearch (pMe);
  pFuncList->pfCalculateSatd (pFuncList->sSampleDealingFuncs.pfSampleSatd[pMe->uiBlockSize], pMe, kiStrideEnc,
                              kiStrideRef);
}

void WelsMotionEstimateSearchScrolled (SWelsFuncPtrList* pFuncList, SDqLayer* pCurDqLayer, SWelsME* pMe,
                                       SSlice* pSlice) {
  const int32_t kiStrideEnc = pCurDqLayer->iEncStride[0];
  const int32_t kiStrideRef = pCurDqLayer->pRefPic->iLineSize[0];

  pMe->sMv = pMe->sDirectionalMv;
  pMe->pRefMb = pMe->pColoRefMb + pMe->sMv.iMvY * kiStrideRef + pMe->sMv.iMvX;
  pMe->uiSadCost =
    pFuncList->sSampleDealingFuncs.pfSampleSad[pMe->uiBlockSize] (pMe->pEncMb, kiStrideEnc, pMe->pRefMb, kiStrideRef)
    + COST_MVD (pMe->pMvdCost, (pMe->sMv.iMvX * (1 << 2)) - pMe->sMvp.iMvX, (pMe->sMv.iMvY * (1 << 2)) - pMe->sMvp.iMvY);
  MeEndIntepelSearch (pMe);
  pFuncList->pfCalculateSatd (pFuncList->sSampleDealingFuncs.pfSampleSatd[pMe->uiBlockSize], pMe, kiStrideEnc,
                              kiStrideRef);
}
/*!
 * \brief  EL mb motion estimate initial point testing
 *
 * \param  pix_pFuncList  SSampleDealingFunc
 * \param  pMe          Wels me information
 * \param  mv_range  search range in motion estimate
 * \param  point      the best match point in motion estimation
 *
 * \return  NONE
 */
bool WelsMotionEstimateInitialPoint (SWelsFuncPtrList* pFuncList, SWelsME* pMe, SSlice* pSlice, int32_t iStrideEnc,
                                     int32_t iStrideRef) {
  PSampleSadSatdCostFunc pSad    = pFuncList->sSampleDealingFuncs.pfSampleSad[pMe->uiBlockSize];
  const uint16_t* kpMvdCost  = pMe->pMvdCost;
  uint8_t* const kpEncMb    = pMe->pEncMb;
  int16_t iMvc0, iMvc1;
  int32_t iSadCost;
  int32_t iBestSadCost;
  uint8_t* pRefMb;
  uint8_t* pFref2;
  uint32_t i;
  const uint32_t kuiMvcNum    = pSlice->uiMvcNum;
  const SMVUnitXY* kpMvcList  = &pSlice->sMvc[0];
  const SMVUnitXY ksMvStartMin    = pSlice->sMvStartMin;
  const SMVUnitXY ksMvStartMax    = pSlice->sMvStartMax;
  const SMVUnitXY ksMvp    = pMe->sMvp;
  SMVUnitXY sMv;

  //  Step 1: Initial point prediction
  // init with sMvp
  sMv.iMvX  = WELS_CLIP3 ((2 + ksMvp.iMvX) >> 2, ksMvStartMin.iMvX, ksMvStartMax.iMvX);
  sMv.iMvY  = WELS_CLIP3 ((2 + ksMvp.iMvY) >> 2, ksMvStartMin.iMvY, ksMvStartMax.iMvY);

  pRefMb = &pMe->pRefMb[sMv.iMvY * iStrideRef + sMv.iMvX];

  iBestSadCost = pSad (kpEncMb, iStrideEnc, pRefMb, iStrideRef);
  iBestSadCost += COST_MVD (kpMvdCost, ((sMv.iMvX) * (1 << 2)) - ksMvp.iMvX, ((sMv.iMvY) * (1 << 2)) - ksMvp.iMvY);

  for (i = 0; i < kuiMvcNum; i++) {
    //clipping here is essential since some pOut-of-range MVC may happen here (i.e., refer to baseMV)
    iMvc0 = WELS_CLIP3 ((2 + kpMvcList[i].iMvX) >> 2, ksMvStartMin.iMvX, ksMvStartMax.iMvX);
    iMvc1 = WELS_CLIP3 ((2 + kpMvcList[i].iMvY) >> 2, ksMvStartMin.iMvY, ksMvStartMax.iMvY);

    if (((iMvc0 - sMv.iMvX) || (iMvc1 - sMv.iMvY))) {
      pFref2 = &pMe->pRefMb[iMvc1 * iStrideRef + iMvc0];

      iSadCost = pSad (kpEncMb, iStrideEnc, pFref2, iStrideRef) +
                 COST_MVD (kpMvdCost, (iMvc0 * (1 << 2)) - ksMvp.iMvX, (iMvc1 * (1 << 2)) - ksMvp.iMvY);

      if (iSadCost < iBestSadCost) {
        sMv.iMvX = iMvc0;
        sMv.iMvY = iMvc1;
        pRefMb = pFref2;
        iBestSadCost = iSadCost;
      }
    }
  }

  if (pFuncList->pfCheckDirectionalMv
      (pSad, pMe, ksMvStartMin, ksMvStartMax, iStrideEnc, iStrideRef, iSadCost)) {
    sMv = pMe->sDirectionalMv;
    pRefMb =  &pMe->pColoRefMb[sMv.iMvY * iStrideRef + sMv.iMvX];
    iBestSadCost = iSadCost;
  }

  UpdateMeResults (sMv, iBestSadCost, pRefMb, pMe);
  if (iBestSadCost < static_cast<int32_t> (pMe->uSadPredISatd.uiSadPred)) {
    //Initial point early Stop
    MeEndIntepelSearch (pMe);
    return true;
  }
  return false;
}

void NotCalculateSatdCost (PSampleSadSatdCostFunc pSatd, SWelsME* pMe,
                           const int32_t kiEncStride, const int32_t kiRefStride) {
}

/////////////////////////
// Diamond Search Basics
/////////////////////////
bool WelsMeSadCostSelect (int32_t* iSadCost, const uint16_t* kpMvdCost, int32_t* pBestCost, const int32_t kiDx,
                          const int32_t kiDy, int32_t* pIx, int32_t* pIy) {
  int32_t iTempSadCost[4];
  int32_t iInputSadCost = *pBestCost;
  iTempSadCost[0] = iSadCost[0] + COST_MVD (kpMvdCost, kiDx, kiDy - 4);
  iTempSadCost[1] = iSadCost[1] + COST_MVD (kpMvdCost, kiDx, kiDy + 4);
  iTempSadCost[2] = iSadCost[2] + COST_MVD (kpMvdCost, kiDx - 4, kiDy);
  iTempSadCost[3] = iSadCost[3] + COST_MVD (kpMvdCost, kiDx + 4, kiDy);

  if (iTempSadCost[0] < *pBestCost) {
    *pBestCost = iTempSadCost[0];
    *pIx = 0;
    *pIy = 1;
  }

  if (iTempSadCost[1] < *pBestCost) {
    *pBestCost = iTempSadCost[1];
    *pIx = 0;
    *pIy = -1;
  }

  if (iTempSadCost[2] < *pBestCost) {
    *pBestCost = iTempSadCost[2];
    *pIx = 1;
    *pIy = 0;
  }

  if (iTempSadCost[3] < *pBestCost) {
    *pBestCost = iTempSadCost[3];
    *pIx = -1;
    *pIy = 0;
  }
  return (*pBestCost == iInputSadCost);
}

void WelsDiamondSearch (SWelsFuncPtrList* pFuncList, SWelsME* pMe, SSlice* pSlice,
                        const int32_t kiStrideEnc,  const int32_t kiStrideRef) {
  PSample4SadCostFunc      pSad          =  pFuncList->sSampleDealingFuncs.pfSample4Sad[pMe->uiBlockSize];

  uint8_t* pFref = pMe->pRefMb;
  uint8_t* const kpEncMb = pMe->pEncMb;
  const uint16_t* kpMvdCost = pMe->pMvdCost;

  const SMVUnitXY ksMvStartMin    = pSlice->sMvStartMin;
  const SMVUnitXY ksMvStartMax    = pSlice->sMvStartMax;

  int32_t iMvDx = ((pMe->sMv.iMvX) * (1 << 2)) - pMe->sMvp.iMvX;
  int32_t iMvDy = ((pMe->sMv.iMvY) * (1 << 2)) - pMe->sMvp.iMvY;

  uint8_t* pRefMb = pFref;
  int32_t iBestCost = (pMe->uiSadCost);

  int32_t iTimeThreshold = ITERATIVE_TIMES;
  ENFORCE_STACK_ALIGN_1D (int32_t, iSadCosts, 4, 16)

  while (iTimeThreshold--) {
    pMe->sMv.iMvX = (iMvDx + pMe->sMvp.iMvX) >> 2;
    pMe->sMv.iMvY = (iMvDy + pMe->sMvp.iMvY) >> 2;
    if (!CheckMvInRange (pMe->sMv, ksMvStartMin, ksMvStartMax))
      continue;
    pSad (kpEncMb, kiStrideEnc, pRefMb, kiStrideRef, &iSadCosts[0]);

    int32_t iX, iY;

    const bool kbIsBestCostWorse = WelsMeSadCostSelect (iSadCosts, kpMvdCost, &iBestCost, iMvDx, iMvDy, &iX, &iY);
    if (kbIsBestCostWorse)
      break;

    iMvDx -= (iX * (1 << 2)) ;
    iMvDy -= (iY * (1 << 2)) ;

    pRefMb -= (iX + iY * kiStrideRef);

  }

  /* integer-pel mv */
  pMe->sMv.iMvX = (iMvDx + pMe->sMvp.iMvX) >> 2;
  pMe->sMv.iMvY = (iMvDy + pMe->sMvp.iMvY) >> 2;
  pMe->uiSatdCost = pMe->uiSadCost = (iBestCost);
  pMe->pRefMb = pRefMb;
}

/////////////////////////
// DirectionalMv Basics
/////////////////////////
bool CheckDirectionalMv (PSampleSadSatdCostFunc pSad, SWelsME* pMe,
                         const SMVUnitXY ksMinMv, const SMVUnitXY ksMaxMv, const int32_t kiEncStride, const int32_t kiRefStride,
                         int32_t& iBestSadCost) {
  const int16_t kiMvX = pMe->sDirectionalMv.iMvX;
  const int16_t kiMvY = pMe->sDirectionalMv.iMvY;

  //Check MV from scrolling detection
  if ((BLOCK_16x16 != pMe->uiBlockSize) //scrolled_MV with P16x16 is checked SKIP checking function
      && (kiMvX | kiMvY)   //(0,0) checked in ordinary initial point checking
      && CheckMvInRange (pMe->sDirectionalMv, ksMinMv, ksMaxMv)) {
    uint8_t* pRef = &pMe->pColoRefMb[kiMvY * kiRefStride + kiMvX];
    uint32_t uiCurrentSadCost = pSad (pMe->pEncMb, kiEncStride,  pRef, kiRefStride) +
                                COST_MVD (pMe->pMvdCost, (kiMvX * (1 << 2)) - pMe->sMvp.iMvX, (kiMvY * (1 << 2)) - pMe->sMvp.iMvY);
    if (uiCurrentSadCost < pMe->uiSadCost) {
      iBestSadCost = uiCurrentSadCost;
      return true;
    }
  }
  return false;
}

/////////////////////////
// Cross Search Basics
/////////////////////////
#if defined (X86_ASM)
void CalcMvdCostx8_c (uint16_t* pMvdCost, const int32_t kiStartMv, uint16_t* pMvdTable, const uint16_t kiFixedCost) {
  uint16_t* pBaseCost  = pMvdCost;
  const int32_t kiOffset = (kiStartMv * (1 << 2));
  uint16_t* pMvd  = pMvdTable + kiOffset;
  for (int32_t i = 0; i < 8; ++ i) {
    pBaseCost[i] = ((*pMvd) + kiFixedCost);
    pMvd += 4;
  }
}
void VerticalFullSearchUsingSSE41 (SWelsFuncPtrList* pFuncList, SWelsME* pMe,
                                   uint16_t* pMvdTable,
                                   const int32_t kiEncStride, const int32_t kiRefStride,
                                   const int16_t kiMinMv, const int16_t kiMaxMv,
                                   const bool bVerticalSearch) {
  uint8_t*  kpEncMb = pMe->pEncMb;
  const int32_t kiCurMeBlockPix = pMe->iCurMeBlockPixY;
  uint8_t* pRef         = &pMe->pColoRefMb[kiMinMv * kiRefStride];

  const int32_t kiCurMeBlockPixY = pMe->iCurMeBlockPixY;

  int32_t iMinPos = kiCurMeBlockPixY + kiMinMv;
  int32_t iMaxPos = kiCurMeBlockPixY + kiMaxMv;
  int32_t iFixedMvd = * (pMvdTable - pMe->sMvp.iMvX);
  uint16_t* pMvdCost  = & (pMvdTable[ (kiMinMv * (1 << 2)) - pMe->sMvp.iMvY]);
  int16_t iStartMv = 0;

  const int32_t kIsBlock16x16 = pMe->uiBlockSize == BLOCK_16x16;
  const int32_t kiEdgeBlocks = kIsBlock16x16 ? 16 : 8;
  PSampleSadHor8Func pSampleSadHor8 = pFuncList->pfSampleSadHor8[kIsBlock16x16];
  PSampleSadSatdCostFunc pSad = pFuncList->sSampleDealingFuncs.pfSampleSad[pMe->uiBlockSize];
  PTransposeMatrixBlockFunc TransposeMatrixBlock = kIsBlock16x16 ? TransposeMatrixBlock16x16_sse2 :
      TransposeMatrixBlock8x8_mmx;
  PTransposeMatrixBlocksFunc TransposeMatrixBlocks = kIsBlock16x16 ? TransposeMatrixBlocksx16_sse2 :
      TransposeMatrixBlocksx8_mmx;

  const int32_t kiDiff   = iMaxPos - iMinPos;
  const int32_t kiRowNum  = WELS_ALIGN ((kiDiff - kiEdgeBlocks + 1), kiEdgeBlocks);
  const int32_t kiBlocksNum  = kIsBlock16x16 ? (kiRowNum >> 4) : (kiRowNum >> 3);
  int32_t iCountLoop8  = (kiRowNum - kiEdgeBlocks) >> 3;
  const int32_t kiRemainingVectors  = kiDiff - (iCountLoop8 << 3);
  const int32_t kiMatrixStride  = MAX_VERTICAL_MV_RANGE;
  ENFORCE_STACK_ALIGN_2D (uint8_t, uiMatrixRef, 16, kiMatrixStride, 16);  // transpose matrix result for ref
  ENFORCE_STACK_ALIGN_2D (uint8_t, uiMatrixEnc, 16, 16, 16);     // transpose matrix result for enc
  assert (kiRowNum <= kiMatrixStride); // make sure effective memory

  TransposeMatrixBlock (&uiMatrixEnc[0][0], 16, kpEncMb, kiEncStride);
  TransposeMatrixBlocks (&uiMatrixRef[0][0], kiMatrixStride, pRef, kiRefStride, kiBlocksNum);
  ENFORCE_STACK_ALIGN_1D (uint16_t, uiBaseCost, 8, 16);
  int32_t iTargetPos   = iMinPos;
  int16_t iBestPos    = pMe->sMv.iMvX;
  uint32_t uiBestCost   = pMe->uiSadCost;
  uint32_t uiCostMin;
  int32_t iIndexMinPos;
  kpEncMb = &uiMatrixEnc[0][0];
  pRef = &uiMatrixRef[0][0];

  while (iCountLoop8 > 0) {
    CalcMvdCostx8_c (uiBaseCost, iStartMv, pMvdCost, iFixedMvd);
    uiCostMin = pSampleSadHor8 (kpEncMb, 16, pRef, kiMatrixStride, uiBaseCost, &iIndexMinPos);
    if (uiCostMin < uiBestCost) {
      uiBestCost = uiCostMin;
      iBestPos  = iTargetPos + iIndexMinPos;
    }
    iTargetPos += 8;
    pRef += 8;
    iStartMv += 8;
    -- iCountLoop8;
  }
  if (kiRemainingVectors > 0) {
    kpEncMb = pMe->pEncMb;
    pRef = &pMe->pColoRefMb[ (iTargetPos - kiCurMeBlockPix) * kiRefStride];
    while (iTargetPos < iMaxPos) {
      const uint16_t uiMvdCost = pMvdCost[iStartMv * (1 << 2)];
      uint32_t uiSadCost = pSad (kpEncMb, kiEncStride, pRef, kiRefStride) + (iFixedMvd + uiMvdCost);
      if (uiSadCost < uiBestCost) {
        uiBestCost = uiSadCost;
        iBestPos = iTargetPos;
      }
      iStartMv++;
      pRef += kiRefStride;
      ++iTargetPos;
    }
  }
  if (uiBestCost < pMe->uiSadCost) {
    SMVUnitXY sBestMv;
    sBestMv.iMvX = 0;
    sBestMv.iMvY = iBestPos - kiCurMeBlockPix;
    UpdateMeResults (sBestMv, uiBestCost, &pMe->pColoRefMb[sBestMv.iMvY * kiRefStride], pMe);
  }
}

void HorizontalFullSearchUsingSSE41 (SWelsFuncPtrList* pFuncList, SWelsME* pMe,
                                     uint16_t* pMvdTable,
                                     const int32_t kiEncStride, const int32_t kiRefStride,
                                     const int16_t kiMinMv, const int16_t kiMaxMv,
                                     const bool bVerticalSearch) {
  uint8_t* kpEncMb = pMe->pEncMb;

  const int32_t iCurMeBlockPixX = pMe->iCurMeBlockPixX;
  int32_t iMinPos = iCurMeBlockPixX + kiMinMv;
  int32_t iMaxPos = iCurMeBlockPixX + kiMaxMv;
  int32_t iFixedMvd = * (pMvdTable - pMe->sMvp.iMvY);
  uint16_t* pMvdCost  = & (pMvdTable[ (kiMinMv * (1 << 2)) - pMe->sMvp.iMvX]);
  int16_t iStartMv = 0;
  uint8_t* pRef         = &pMe->pColoRefMb[kiMinMv];
  const int32_t kIsBlock16x16 = pMe->uiBlockSize == BLOCK_16x16;
  PSampleSadHor8Func pSampleSadHor8 = pFuncList->pfSampleSadHor8[kIsBlock16x16];
  PSampleSadSatdCostFunc pSad = pFuncList->sSampleDealingFuncs.pfSampleSad[pMe->uiBlockSize];
  ENFORCE_STACK_ALIGN_1D (uint16_t, uiBaseCost, 8, 16);
  const int32_t kiNumVector = iMaxPos - iMinPos;
  int32_t iCountLoop8 = kiNumVector >> 3;
  const int32_t kiRemainingLoop8 = kiNumVector & 7;
  int32_t iTargetPos   = iMinPos;
  int16_t iBestPos    = pMe->sMv.iMvX;
  uint32_t uiBestCost   = pMe->uiSadCost;
  uint32_t uiCostMin;
  int32_t iIndexMinPos;

  while (iCountLoop8 > 0) {
    CalcMvdCostx8_c (uiBaseCost, iStartMv, pMvdCost, iFixedMvd);
    uiCostMin = pSampleSadHor8 (kpEncMb, kiEncStride, pRef, kiRefStride, uiBaseCost, &iIndexMinPos);
    if (uiCostMin < uiBestCost) {
      uiBestCost = uiCostMin;
      iBestPos  = iTargetPos + iIndexMinPos;
    }
    iTargetPos += 8;
    pRef += 8;
    iStartMv += 8;
    -- iCountLoop8;
  }
  if (kiRemainingLoop8 > 0) {
    while (iTargetPos < iMaxPos) {
      const uint16_t uiMvdCost = pMvdCost[iStartMv * (1 << 2)];
      uint32_t uiSadCost = pSad (kpEncMb, kiEncStride, pRef, kiRefStride) + (iFixedMvd + uiMvdCost);
      if (uiSadCost < uiBestCost) {
        uiBestCost = uiSadCost;
        iBestPos = iTargetPos;
      }
      iStartMv++;
      ++pRef;
      ++iTargetPos;
    }
  }
  if (uiBestCost < pMe->uiSadCost) {
    SMVUnitXY sBestMv;
    sBestMv.iMvX = iBestPos - iCurMeBlockPixX;
    sBestMv.iMvY = 0;
    UpdateMeResults (sBestMv, uiBestCost, &pMe->pColoRefMb[sBestMv.iMvX], pMe);
  }
}
#endif
void LineFullSearch_c (SWelsFuncPtrList* pFuncList, SWelsME* pMe,
                       uint16_t* pMvdTable,
                       const int32_t kiEncStride, const int32_t kiRefStride,
                       const int16_t iMinMv, const int16_t iMaxMv,
                       const bool bVerticalSearch) {
  PSampleSadSatdCostFunc pSad = pFuncList->sSampleDealingFuncs.pfSampleSad[pMe->uiBlockSize];
  const int32_t kiCurMeBlockPixX = pMe->iCurMeBlockPixX;
  const int32_t kiCurMeBlockPixY = pMe->iCurMeBlockPixY;
  int32_t iMinPos, iMaxPos;
  int32_t iFixedMvd;
  int32_t iCurMeBlockPix;
  int32_t iStride;
  uint16_t* pMvdCost;

  if (bVerticalSearch) {
    iMinPos = kiCurMeBlockPixY + iMinMv;
    iMaxPos = kiCurMeBlockPixY + iMaxMv;
    iFixedMvd = * (pMvdTable - pMe->sMvp.iMvX);
    iCurMeBlockPix = pMe->iCurMeBlockPixY;
    iStride = kiRefStride;
    pMvdCost  = & (pMvdTable[ (iMinMv * (1 << 2)) - pMe->sMvp.iMvY]);
  } else {
    iMinPos = kiCurMeBlockPixX + iMinMv;
    iMaxPos = kiCurMeBlockPixX + iMaxMv;
    iFixedMvd = * (pMvdTable - pMe->sMvp.iMvY);
    iCurMeBlockPix = pMe->iCurMeBlockPixX;
    iStride = 1;
    pMvdCost  = & (pMvdTable[ (iMinMv * (1 << 2)) - pMe->sMvp.iMvX]);
  }
  uint8_t* pRef            = &pMe->pColoRefMb[ iMinMv * iStride];
  uint32_t uiBestCost    = 0xFFFFFFFF;
  int32_t iBestPos       = 0;

  for (int32_t iTargetPos = iMinPos; iTargetPos < iMaxPos; ++ iTargetPos) {
    uint8_t* const kpEncMb  = pMe->pEncMb;
    uint32_t uiSadCost = pSad (kpEncMb, kiEncStride, pRef, kiRefStride) + (iFixedMvd + *pMvdCost);
    if (uiSadCost < uiBestCost) {
      uiBestCost  = uiSadCost;
      iBestPos  = iTargetPos;
    }
    pRef += iStride;
    pMvdCost += 4;
  }

  if (uiBestCost < pMe->uiSadCost) {
    SMVUnitXY sBestMv;
    sBestMv.iMvX = bVerticalSearch ? 0 : (iBestPos - iCurMeBlockPix);
    sBestMv.iMvY = bVerticalSearch ? (iBestPos - iCurMeBlockPix) : 0;
    UpdateMeResults (sBestMv, uiBestCost, &pMe->pColoRefMb[sBestMv.iMvY * kiRefStride + sBestMv.iMvX], pMe);
  }
}

void WelsMotionCrossSearch (SWelsFuncPtrList* pFuncList, SWelsME* pMe, SSlice* pSlice,
                            const int32_t kiEncStride,  const int32_t kiRefStride) {
  PLineFullSearchFunc pfVerticalFullSearchFunc = pFuncList->pfVerticalFullSearch;
  PLineFullSearchFunc pfHorizontalFullSearchFunc = pFuncList->pfHorizontalFullSearch;

  //vertical search
  pfVerticalFullSearchFunc (pFuncList, pMe,
                            pMe->pMvdCost,
                            kiEncStride, kiRefStride,
                            pSlice->sMvStartMin.iMvY,
                            pSlice->sMvStartMax.iMvY, true);

  //horizontal search
  if (pMe->uiSadCost >= pMe->uiSadCostThreshold) {
    pfHorizontalFullSearchFunc (pFuncList, pMe,
                                pMe->pMvdCost,
                                kiEncStride, kiRefStride,
                                pSlice->sMvStartMin.iMvX,
                                pSlice->sMvStartMax.iMvX,
                                false);
  }
}

/////////////////////////
// Feature Search Basics
/////////////////////////
//memory related
int32_t RequestFeatureSearchPreparation (CMemoryAlign* pMa, const int32_t kiFrameWidth,  const int32_t kiFrameHeight,
    const int32_t iNeedFeatureStorage,
    SFeatureSearchPreparation* pFeatureSearchPreparation) {
  const int32_t kiFeatureStrategyIndex = iNeedFeatureStorage >> 16;
  const bool bFme8x8 = ((iNeedFeatureStorage & 0x0000FF & ME_FME) == ME_FME);
  const int32_t kiMarginSize = bFme8x8 ? 8 : 16;
  const int32_t kiFrameSize = (kiFrameWidth - kiMarginSize) * (kiFrameHeight - kiMarginSize);
  int32_t iListOfFeatureOfBlock;

  if (0 == kiFeatureStrategyIndex) {
    iListOfFeatureOfBlock = sizeof (uint16_t) * kiFrameSize;
  } else {
    iListOfFeatureOfBlock = sizeof (uint16_t) * kiFrameSize +
                            (kiFrameWidth - kiMarginSize) * sizeof (uint32_t) + kiFrameWidth * 8 * sizeof (uint8_t);
  }
  pFeatureSearchPreparation->pFeatureOfBlock =
    (uint16_t*)pMa->WelsMallocz (iListOfFeatureOfBlock, "pFeatureOfBlock");
  WELS_VERIFY_RETURN_IF (ENC_RETURN_MEMALLOCERR, NULL == (pFeatureSearchPreparation->pFeatureOfBlock))

  pFeatureSearchPreparation->uiFeatureStrategyIndex = kiFeatureStrategyIndex;
  pFeatureSearchPreparation->bFMESwitchFlag = true;
  pFeatureSearchPreparation->uiFMEGoodFrameCount = FMESWITCH_DEFAULT_GOODFRAME_NUM;
  pFeatureSearchPreparation->iHighFreMbCount = 0;

  return ENC_RETURN_SUCCESS;
}
int32_t ReleaseFeatureSearchPreparation (CMemoryAlign* pMa, uint16_t*& pFeatureOfBlock) {
  if (pMa && pFeatureOfBlock) {
    pMa->WelsFree (pFeatureOfBlock, "pFeatureOfBlock");
    pFeatureOfBlock = NULL;
    return ENC_RETURN_SUCCESS;
  }
  return ENC_RETURN_UNEXPECTED;
}

int32_t RequestScreenBlockFeatureStorage (CMemoryAlign* pMa, const int32_t kiFrameWidth,  const int32_t kiFrameHeight,
    const int32_t iNeedFeatureStorage,
    SScreenBlockFeatureStorage* pScreenBlockFeatureStorage) {

  const int32_t kiFeatureStrategyIndex = iNeedFeatureStorage >> 16;
  const int32_t kiMe8x8FME = iNeedFeatureStorage & 0x0000FF & ME_FME;
  const int32_t kiMe16x16FME = ((iNeedFeatureStorage & 0x00FF00) >> 8) & ME_FME;
  if ((kiMe8x8FME == ME_FME) && (kiMe16x16FME == ME_FME)) {
    return ENC_RETURN_UNSUPPORTED_PARA;
    //the following memory allocation cannot support when FME at both size
  }

  const bool bIsBlock8x8 = (kiMe8x8FME == ME_FME);
  const int32_t kiMarginSize = bIsBlock8x8 ? 8 : 16;
  const int32_t kiFrameSize = (kiFrameWidth - kiMarginSize) * (kiFrameHeight - kiMarginSize);
  const int32_t kiListSize  = (0 == kiFeatureStrategyIndex) ? (bIsBlock8x8 ? LIST_SIZE_SUM_8x8 : LIST_SIZE_SUM_16x16) :
                              256;

  pScreenBlockFeatureStorage->pTimesOfFeatureValue = (uint32_t*)pMa->WelsMallocz (kiListSize * sizeof (uint32_t),
      "pScreenBlockFeatureStorage->pTimesOfFeatureValue");
  WELS_VERIFY_RETURN_IF (ENC_RETURN_MEMALLOCERR, NULL == pScreenBlockFeatureStorage->pTimesOfFeatureValue)

  pScreenBlockFeatureStorage->pLocationOfFeature = (uint16_t**)pMa->WelsMallocz (kiListSize * sizeof (uint16_t*),
      "pScreenBlockFeatureStorage->pLocationOfFeature");
  WELS_VERIFY_RETURN_IF (ENC_RETURN_MEMALLOCERR, NULL == pScreenBlockFeatureStorage->pLocationOfFeature)

  pScreenBlockFeatureStorage->pLocationPointer = (uint16_t*)pMa->WelsMallocz (2 * kiFrameSize * sizeof (uint16_t),
      "pScreenBlockFeatureStorage->pLocationPointer");
  WELS_VERIFY_RETURN_IF (ENC_RETURN_MEMALLOCERR, NULL == pScreenBlockFeatureStorage->pLocationPointer)
  //  uint16_t* pFeatureValuePointerList[WELS_MAX (LIST_SIZE_SUM_16x16, LIST_SIZE_MSE_16x16)] = {0};
  pScreenBlockFeatureStorage->pFeatureValuePointerList = (uint16_t**)pMa->WelsMallocz (WELS_MAX (LIST_SIZE_SUM_16x16,
      LIST_SIZE_MSE_16x16) * sizeof (uint16_t*),
      "pScreenBlockFeatureStorage->pFeatureValuePointerList");
  WELS_VERIFY_RETURN_IF (ENC_RETURN_MEMALLOCERR, NULL == pScreenBlockFeatureStorage->pFeatureValuePointerList)

  pScreenBlockFeatureStorage->pFeatureOfBlockPointer = NULL;
  pScreenBlockFeatureStorage->iIs16x16 = !bIsBlock8x8;
  pScreenBlockFeatureStorage->uiFeatureStrategyIndex = kiFeatureStrategyIndex;
  pScreenBlockFeatureStorage->iActualListSize = kiListSize;
  WelsSetMemMultiplebytes_c (pScreenBlockFeatureStorage->uiSadCostThreshold, UINT_MAX, BLOCK_SIZE_ALL, sizeof (uint32_t));
  pScreenBlockFeatureStorage->bRefBlockFeatureCalculated = false;

  return ENC_RETURN_SUCCESS;
}
int32_t ReleaseScreenBlockFeatureStorage (CMemoryAlign* pMa, SScreenBlockFeatureStorage* pScreenBlockFeatureStorage) {
  if (pMa && pScreenBlockFeatureStorage) {
    if (pScreenBlockFeatureStorage->pTimesOfFeatureValue) {
      pMa->WelsFree (pScreenBlockFeatureStorage->pTimesOfFeatureValue, "pScreenBlockFeatureStorage->pTimesOfFeatureValue");
      pScreenBlockFeatureStorage->pTimesOfFeatureValue = NULL;
    }

    if (pScreenBlockFeatureStorage->pLocationOfFeature) {
      pMa->WelsFree (pScreenBlockFeatureStorage->pLocationOfFeature, "pScreenBlockFeatureStorage->pLocationOfFeature");
      pScreenBlockFeatureStorage->pLocationOfFeature = NULL;
    }

    if (pScreenBlockFeatureStorage->pLocationPointer) {
      pMa->WelsFree (pScreenBlockFeatureStorage->pLocationPointer, "pScreenBlockFeatureStorage->pLocationPointer");
      pScreenBlockFeatureStorage->pLocationPointer = NULL;
    }

    if (pScreenBlockFeatureStorage->pFeatureValuePointerList) {
      pMa->WelsFree (pScreenBlockFeatureStorage->pFeatureValuePointerList,
                     "pScreenBlockFeatureStorage->pFeatureValuePointerList");
      pScreenBlockFeatureStorage->pFeatureValuePointerList = NULL;
    }

    return ENC_RETURN_SUCCESS;
  }
  return ENC_RETURN_UNEXPECTED;
}

// jumpseat: the cross search's thresholds, as PerformFMEPreprocess sets
// them, without the feature index; UINT_MAX (no cross search) when off.
void SetCrossSearchThresholds (SPicture* pRef, SScreenBlockFeatureStorage* pStorage, const bool kbOn) {
  const uint32_t kuiQstepx16 = QStepx16ByQp[WelsMedian (0, pRef->iFrameAverageQp, 51)];
  const uint32_t kuiThreshold16x16 = kbOn ? ((30 * (kuiQstepx16 + 160)) >> 3) : UINT_MAX;
  pStorage->uiSadCostThreshold[BLOCK_16x16] = kuiThreshold16x16;
  pStorage->uiSadCostThreshold[BLOCK_8x8] = kbOn ? (kuiThreshold16x16 >> 2) : UINT_MAX;
  pStorage->uiSadCostThreshold[BLOCK_16x8] = pStorage->uiSadCostThreshold[BLOCK_8x16]
      = pStorage->uiSadCostThreshold[BLOCK_4x4] = UINT_MAX;
}

/////////////////////////
// Search function options
/////////////////////////
void WelsDiamondCrossSearch (SWelsFuncPtrList* pFunc, SWelsME* pMe, SSlice* pSlice, const int32_t kiEncStride,
                             const int32_t kiRefStride) {
  //  Step 1: diamond search
  WelsDiamondSearch (pFunc, pMe, pSlice, kiEncStride, kiRefStride);

  //  Step 2: CROSS search
  pMe->uiSadCostThreshold = pMe->pRefFeatureStorage->uiSadCostThreshold[pMe->uiBlockSize];
  if (pMe->uiSadCost >= pMe->uiSadCostThreshold) {
    WelsMotionCrossSearch (pFunc, pMe, pSlice, kiEncStride, kiRefStride);
  }
}

} // namespace WelsEnc

