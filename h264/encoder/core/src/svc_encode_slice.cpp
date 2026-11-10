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
 * \file    svc_encode_slice.c
 *
 * \brief   svc encoding slice
 *
 * \date    2009.07.27 Created
 *
 *************************************************************************************
 */

#include "ls_defines.h"
#include "svc_encode_slice.h"
#include "svc_enc_golomb.h"
#include "svc_base_layer_md.h"
#include "svc_encode_mb.h"
#include "svc_set_mb_syn.h"
#include "set_mb_syn_cabac.h"
#include "decode_mb_aux.h"
#include "svc_mode_decision.h"

namespace WelsEnc {
//#define ENC_TRACE

typedef int32_t (*PWelsCodingSliceFunc) (sWelsEncCtx* pCtx, SSlice* pSlice);
typedef void (*PWelsSliceHeaderWriteFunc) (sWelsEncCtx* pCtx, SBitStringAux* pBs, SDqLayer* pCurLayer, SSlice* pSlice,
    IWelsParametersetStrategy* pParametersetStrategy);

void UpdateNonZeroCountCache (SMB* pMb, SMbCache* pMbCache) {
  ST32 (&pMbCache->iNonZeroCoeffCount[9], LD32 (&pMb->pNonZeroCount[ 0]));
  ST32 (&pMbCache->iNonZeroCoeffCount[17], LD32 (&pMb->pNonZeroCount[ 4]));
  ST32 (&pMbCache->iNonZeroCoeffCount[25], LD32 (&pMb->pNonZeroCount[ 8]));
  ST32 (&pMbCache->iNonZeroCoeffCount[33], LD32 (&pMb->pNonZeroCount[12]));

  ST16 (&pMbCache->iNonZeroCoeffCount[14], LD16 (&pMb->pNonZeroCount[16]));
  ST16 (&pMbCache->iNonZeroCoeffCount[38], LD16 (&pMb->pNonZeroCount[18]));
  ST16 (&pMbCache->iNonZeroCoeffCount[22], LD16 (&pMb->pNonZeroCount[20]));
  ST16 (&pMbCache->iNonZeroCoeffCount[46], LD16 (&pMb->pNonZeroCount[22]));
}

void WelsSliceHeaderScalExtInit (SDqLayer* pCurLayer, SSlice* pSlice) {
  SSliceHeaderExt* pSliceHeadExt = &pSlice->sSliceHeaderExt;
  SNalUnitHeaderExt* pNalHeadExt = &pCurLayer->sLayerInfo.sNalHeaderExt;

  uint8_t uiDependencyId = pNalHeadExt->uiDependencyId;

  pSliceHeadExt->bSliceSkipFlag = false;

  if (uiDependencyId > 0) { //spatial EL
    //bothe adaptive and default flags should equal to 0.
    pSliceHeadExt->bAdaptiveBaseModeFlag     =
      pSliceHeadExt->bAdaptiveMotionPredFlag   =
        pSliceHeadExt->bAdaptiveResidualPredFlag = false;

    pSliceHeadExt->bDefaultBaseModeFlag     =
      pSliceHeadExt->bDefaultMotionPredFlag   =
        pSliceHeadExt->bDefaultResidualPredFlag = false;
  }
}

void WelsSliceHeaderExtInit (sWelsEncCtx* pEncCtx, SDqLayer* pCurLayer, SSlice* pSlice) {
  SSliceHeaderExt* pCurSliceExt = &pSlice->sSliceHeaderExt;
  SSliceHeader* pCurSliceHeader  = &pCurSliceExt->sSliceHeader;
  SSpatialLayerInternal* pParamInternal = &pEncCtx->pSvcParam->sDependencyLayers[pEncCtx->uiDependencyId];
  pCurSliceHeader->eSliceType = pEncCtx->eSliceType;

  pCurSliceExt->bStoreRefBasePicFlag = false;

  pCurSliceHeader->iFrameNum      = pParamInternal->iFrameNum;
  pCurSliceHeader->uiIdrPicId     = pParamInternal->uiIdrPicId;
  pCurSliceHeader->iPicOrderCntLsb = pEncCtx->pEncPic->iFramePoc;      // 0

  if (P_SLICE == pEncCtx->eSliceType) {
    pCurSliceHeader->uiNumRefIdxL0Active = 1;
    if (pCurSliceHeader->uiRefCount > 0 &&
        pCurSliceHeader->uiRefCount <= pCurLayer->sLayerInfo.pSpsP->iNumRefFrames) {
      pCurSliceHeader->bNumRefIdxActiveOverrideFlag = true;
      pCurSliceHeader->uiNumRefIdxL0Active = pCurSliceHeader->uiRefCount;
    }
    //to solve mismatch between debug&release
    else {
      pCurSliceHeader->bNumRefIdxActiveOverrideFlag = false;
    }
  }

  pCurSliceHeader->iSliceQpDelta = pEncCtx->iGlobalQp - pCurLayer->sLayerInfo.pPpsP->iPicInitQp;

  //for deblocking initial
  pCurSliceHeader->uiDisableDeblockingFilterIdc = pCurLayer->iLoopFilterDisableIdc;
  pCurSliceHeader->iSliceAlphaC0Offset =
    pCurLayer->iLoopFilterAlphaC0Offset; // need update iSliceAlphaC0Offset & iSliceBetaOffset for pSlice-header if loop_filter_idc != 1
  pCurSliceHeader->iSliceBetaOffset = pCurLayer->iLoopFilterBetaOffset;
  pCurSliceExt->uiDisableInterLayerDeblockingFilterIdc = pCurLayer->uiDisableInterLayerDeblockingFilterIdc;

  if (pSlice->bSliceHeaderExtFlag) {
    WelsSliceHeaderScalExtInit (pCurLayer, pSlice);
  } else {
    //both adaptive and default flags should equal to 0.
    pCurSliceExt->bAdaptiveBaseModeFlag =
      pCurSliceExt->bAdaptiveMotionPredFlag =
        pCurSliceExt->bAdaptiveResidualPredFlag = false;

    pCurSliceExt->bDefaultBaseModeFlag =
      pCurSliceExt->bDefaultMotionPredFlag =
        pCurSliceExt->bDefaultResidualPredFlag  = false;
  }
}



/* count MB types if enabled FRAME_INFO_OUTPUT*/

/*!
* \brief    write reference picture list on reordering syntax in Slice header
*/
void WriteReferenceReorder (SBitStringAux* pBs, SSliceHeader* sSliceHeader) {
  SRefPicListReorderSyntax* pRefOrdering    = &sSliceHeader->sRefReordering;
  uint8_t eSliceType                        = sSliceHeader->eSliceType % 5;
  int16_t n = 0;

  if (I_SLICE != eSliceType && SI_SLICE != eSliceType) { // !I && !SI
    BsWriteOneBit (pBs, true);
//    {
    uint16_t uiReorderingOfPicNumsIdc;
    do {
      uiReorderingOfPicNumsIdc = pRefOrdering->SReorderingSyntax[n].uiReorderingOfPicNumsIdc;
      BsWriteUE (pBs, uiReorderingOfPicNumsIdc);
      if (0 == uiReorderingOfPicNumsIdc || 1 == uiReorderingOfPicNumsIdc)
        BsWriteUE (pBs, pRefOrdering->SReorderingSyntax[n].uiAbsDiffPicNumMinus1);
      else if (2 == uiReorderingOfPicNumsIdc)
        BsWriteUE (pBs, pRefOrdering->SReorderingSyntax[n].iLongTermPicNum);

      n ++;
    } while (3 != uiReorderingOfPicNumsIdc);
//    }
  }
}

/*!
* \brief    write reference picture marking syntax in pSlice header
*/
void WriteRefPicMarking (SBitStringAux* pBs, SSliceHeader* pSliceHeader, SNalUnitHeaderExt* pNalHdrExt) {
  SRefPicMarking* sRefMarking = &pSliceHeader->sRefMarking;
  int16_t n = 0;

  if (pNalHdrExt->bIdrFlag) {
    BsWriteOneBit (pBs, sRefMarking->bNoOutputOfPriorPicsFlag);
    BsWriteOneBit (pBs, sRefMarking->bLongTermRefFlag);
  } else {
    BsWriteOneBit (pBs, sRefMarking->bAdaptiveRefPicMarkingModeFlag);

    if (sRefMarking->bAdaptiveRefPicMarkingModeFlag) {
      int32_t iMmcoType;
      do {
        iMmcoType = sRefMarking->SMmcoRef[n].iMmcoType;
        BsWriteUE (pBs, iMmcoType);
        if (1 == iMmcoType || 3 == iMmcoType)
          BsWriteUE (pBs, sRefMarking->SMmcoRef[n].iDiffOfPicNum - 1);

        if (2 == iMmcoType)
          BsWriteUE (pBs, sRefMarking->SMmcoRef[n].iLongTermPicNum);

        if (3 == iMmcoType || 6 == iMmcoType)
          BsWriteUE (pBs, sRefMarking->SMmcoRef[n].iLongTermFrameIdx);

        if (4 == iMmcoType)
          BsWriteUE (pBs, sRefMarking->SMmcoRef[n].iMaxLongTermFrameIdx + 1);

        n ++;
      } while (0 != iMmcoType);
    }

  }
}

void WelsSliceHeaderWrite (sWelsEncCtx* pCtx, SBitStringAux* pBs, SDqLayer* pCurLayer, SSlice* pSlice,
                           IWelsParametersetStrategy* pParametersetStrategy) {
  SWelsSPS* pSps = pCurLayer->sLayerInfo.pSpsP;
  SWelsPPS* pPps = pCurLayer->sLayerInfo.pPpsP;
  SSliceHeader* pSliceHeader      = &pSlice->sSliceHeaderExt.sSliceHeader;
  SNalUnitHeaderExt* pNalHead   = &pCurLayer->sLayerInfo.sNalHeaderExt;

  BsWriteUE (pBs, pSliceHeader->iFirstMbInSlice);
  BsWriteUE (pBs, pSliceHeader->eSliceType);    /* same type things */

  BsWriteUE (pBs, pSliceHeader->pPps->iPpsId + pParametersetStrategy->GetPpsIdOffset (pSliceHeader->pPps->iPpsId));

  BsWriteBits (pBs, pSps->uiLog2MaxFrameNum, pSliceHeader->iFrameNum);

  if (pNalHead->bIdrFlag) { /* NAL IDR */
    BsWriteUE (pBs, pSliceHeader->uiIdrPicId);
  }

  if (pSps->uiPocType == 0) {
    BsWriteBits (pBs, pSps->iLog2MaxPocLsb, pSliceHeader->iPicOrderCntLsb);
  } else if (pSps->uiPocType == 1) {
    // TODO: implement.
    assert (0);
  } else {
    // no-op for uiPocType == 2.
  }

  if (P_SLICE == pSliceHeader->eSliceType) {
    BsWriteOneBit (pBs, pSliceHeader->bNumRefIdxActiveOverrideFlag);
    if (pSliceHeader->bNumRefIdxActiveOverrideFlag) {
      BsWriteUE (pBs, WELS_CLIP3 (pSliceHeader->uiNumRefIdxL0Active - 1, 0, MAX_REF_PIC_COUNT));
    }
  }

  if (!pNalHead->bIdrFlag)
    WriteReferenceReorder (pBs, pSliceHeader);

  if (pNalHead->sNalUnitHeader.uiNalRefIdc) {
    WriteRefPicMarking (pBs, pSliceHeader, pNalHead);
  }

  if (pSliceHeader->eSliceType  != I_SLICE) {
    BsWriteUE (pBs, pSlice->iCabacInitIdc);
  }
  BsWriteSE (pBs, pSliceHeader->iSliceQpDelta);       /* pSlice qp delta */

  if (pPps->bDeblockingFilterControlPresentFlag) {
    switch (pSliceHeader->uiDisableDeblockingFilterIdc) {
    case 0:
    case 3:
    case 4:
    case 6:
      BsWriteUE (pBs, 0);
      break;
    case 1:
      BsWriteUE (pBs, 1);
      break;
    case 2:
    case 5:
      BsWriteUE (pBs, 2);
      break;
    default:
      WelsLog (&pCtx->sLogCtx, WELS_LOG_ERROR, "Invalid uiDisableDeblockingFilterIdc %d",
               pSliceHeader->uiDisableDeblockingFilterIdc);
      break;
    }
    if (1 != pSliceHeader->uiDisableDeblockingFilterIdc) {
      BsWriteSE (pBs, pSliceHeader->iSliceAlphaC0Offset >> 1);
      BsWriteSE (pBs, pSliceHeader->iSliceBetaOffset >> 1);
    }
  }
}


//only BaseLayer inter MB and SpatialLayer (uiQualityId = 0) inter MB calling this pFunc.
//only for inter part
void WelsInterMbEncode (sWelsEncCtx* pEncCtx, SSlice* pSlice, SMB* pCurMb) {
  SMbCache* pMbCache = &pSlice->sMbCacheInfo;

  WelsDctMb (pMbCache->pCoeffLevel,  pMbCache->SPicData.pEncMb[0], pEncCtx->pCurDqLayer->iEncStride[0],
             pMbCache->pMemPredLuma, pEncCtx->pFuncList->pfDctFourT4);
  WelsEncInterY (pEncCtx->pFuncList, pCurMb, pMbCache);
}


//only BaseLayer inter MB and SpatialLayer (uiQualityId = 0) inter MB calling this pFunc.
//only for I SSlice
void WelsIMbChromaEncode (sWelsEncCtx* pEncCtx, SMB* pCurMb, SMbCache* pMbCache) {
  SWelsFuncPtrList* pFunc       = pEncCtx->pFuncList;
  SDqLayer* pCurLayer           = pEncCtx->pCurDqLayer;
  const int32_t kiEncStride     = pCurLayer->iEncStride[1];
  const int32_t kiCsStride      = pCurLayer->iCsStride[1];
  int16_t* pCurRS               = pMbCache->pCoeffLevel;
  uint8_t* pBestPred            = pMbCache->pBestPredIntraChroma;
  uint8_t* pCsCb                = pMbCache->SPicData.pCsMb[1];
  uint8_t* pCsCr                = pMbCache->SPicData.pCsMb[2];

  //cb
  pFunc->pfDctFourT4 (pCurRS,    pMbCache->SPicData.pEncMb[1], kiEncStride, pBestPred,    8);
  WelsEncRecUV (pFunc, pCurMb, pMbCache, pCurRS,    1);
  pFunc->pfIDctFourT4 (pCsCb, kiCsStride, pBestPred,    8, pCurRS);

  //cr
  pFunc->pfDctFourT4 (pCurRS + 64, pMbCache->SPicData.pEncMb[2], kiEncStride, pBestPred + 64, 8);
  WelsEncRecUV (pFunc, pCurMb, pMbCache, pCurRS + 64, 2);
  pFunc->pfIDctFourT4 (pCsCr, kiCsStride, pBestPred + 64, 8, pCurRS + 64);
}


//only BaseLayer inter MB and SpatialLayer (uiQualityId = 0) inter MB calling this pFunc.
//for P SSlice (intra part + inter part)
void WelsPMbChromaEncode (sWelsEncCtx* pEncCtx, SSlice* pSlice, SMB* pCurMb) {
  SWelsFuncPtrList* pFunc       = pEncCtx->pFuncList;
  SDqLayer* pCurLayer           = pEncCtx->pCurDqLayer;
  const int32_t kiEncStride     = pCurLayer->iEncStride[1];
  SMbCache* pMbCache            = &pSlice->sMbCacheInfo;
  int16_t* pCurRS               = pMbCache->pCoeffLevel + 256;
  uint8_t* pBestPred            = pMbCache->pMemPredChroma;

  pFunc->pfDctFourT4 (pCurRS,       pMbCache->SPicData.pEncMb[1],   kiEncStride,    pBestPred,      8);
  pFunc->pfDctFourT4 (pCurRS + 64,  pMbCache->SPicData.pEncMb[2],   kiEncStride,    pBestPred + 64, 8);

  WelsEncRecUV (pFunc, pCurMb, pMbCache, pCurRS, 1);
  WelsEncRecUV (pFunc, pCurMb, pMbCache, pCurRS + 64, 2);
}

void OutputPMbWithoutConstructCsRsNoCopy (sWelsEncCtx* pCtx, SDqLayer* pDq, SSlice* pSlice, SMB* pMb) {
  if ((IS_INTER (pMb->uiMbType) && !IS_SKIP (pMb->uiMbType))
      || IS_I_BL (pMb->uiMbType)) { //intra have been reconstructed, NO COPY from CS to pDecPic--
    SMbCache* pMbCache                  = &pSlice->sMbCacheInfo;
    uint8_t* pDecY                      = pMbCache->SPicData.pDecMb[0];
    uint8_t* pDecU                      = pMbCache->SPicData.pDecMb[1];
    uint8_t* pDecV                      = pMbCache->SPicData.pDecMb[2];
    int16_t* pScaledTcoeff              = pMbCache->pCoeffLevel;
    const int32_t kiDecStrideLuma       = pDq->pDecPic->iLineSize[0];
    const int32_t kiDecStrideChroma     = pDq->pDecPic->iLineSize[1];
    PIDctFunc pfIdctFour4x4             = pCtx->pFuncList->pfIDctFourT4;

    WelsIDctT4RecOnMb (pDecY, kiDecStrideLuma, pDecY, kiDecStrideLuma, pScaledTcoeff,  pfIdctFour4x4);
    pfIdctFour4x4 (pDecU, kiDecStrideChroma, pDecU, kiDecStrideChroma, pScaledTcoeff + 256);
    pfIdctFour4x4 (pDecV, kiDecStrideChroma, pDecV, kiDecStrideChroma, pScaledTcoeff + 320);
  }
}

// for intra non-dynamic pSlice
//encapsulate two kinds of reconstruction:
//first. store base or highest Dependency Layer with only one quality (without CS RS reconstruction)
//second. lower than highest Dependency Layer, and for every Dependency Layer with one quality layer(single layer)
int32_t WelsISliceMdEnc (sWelsEncCtx* pEncCtx, SSlice* pSlice) { //pMd + encoding
  SDqLayer* pCurLayer           = pEncCtx->pCurDqLayer;
  SMbCache* pMbCache            = &pSlice->sMbCacheInfo;
  SSliceHeaderExt* pSliceHdExt  = &pSlice->sSliceHeaderExt;
  SMB* pMbList                  = pCurLayer->sMbDataP;
  SMB* pCurMb                   = NULL;
  const int32_t kiSliceFirstMbXY = pSliceHdExt->sSliceHeader.iFirstMbInSlice;
  int32_t iNextMbIdx            = kiSliceFirstMbXY;
  const int32_t kiTotalNumMb    = pCurLayer->iMbWidth * pCurLayer->iMbHeight;
  int32_t iCurMbIdx             = 0, iNumMbCoded = 0;
  const int32_t kiSliceIdx      = pSlice->iSliceIdx;

  SWelsMD sMd;
  int32_t iEncReturn = ENC_RETURN_SUCCESS;
  WelsInitSliceCabac (pEncCtx, pSlice);
  for (; ;) {
    iCurMbIdx = iNextMbIdx;
    pCurMb = &pMbList[ iCurMbIdx ];

    WelsRcMbInitGom (pEncCtx, pCurMb, pSlice);
    WelsMdIntraInit (pEncCtx, pCurMb, pMbCache, kiSliceFirstMbXY);

    sMd.iLambda = g_kiQpCostTable[pCurMb->uiLumaQp];
    WelsMdIntraMb (pEncCtx, &sMd, pCurMb, pMbCache);
    UpdateNonZeroCountCache (pCurMb, pMbCache);


    iEncReturn = WelsSpatialWriteMbSynCabac (pEncCtx, pSlice, pCurMb);
    if (ENC_RETURN_SUCCESS != iEncReturn)
      return iEncReturn;

    pCurMb->uiSliceIdc = kiSliceIdx;


    WelsMdUpdateRefMbQp (pCurLayer, pCurMb, pMbCache->bCollocatedPredFlag, I_SLICE);
    WelsRcMbInfoUpdateGom (pEncCtx, pCurMb, sMd.iCostLuma, pSlice);

    ++iNumMbCoded;
    iNextMbIdx = WelsGetNextMbOfSlice (pCurLayer, iCurMbIdx);
    if (iNextMbIdx == -1 || iNextMbIdx >= kiTotalNumMb || iNumMbCoded >= kiTotalNumMb) {
      break;
    }
  }

  return ENC_RETURN_SUCCESS;
}


//encapsulate two kinds of reconstruction:
// first. store base or highest Dependency Layer with only one quality (without CS RS reconstruction)
// second. lower than highest Dependency Layer, and for every Dependency Layer with one quality layer(single layer)
int32_t WelsPSliceMdEnc (sWelsEncCtx* pEncCtx, SSlice* pSlice,  const bool kbIsHighestDlayerFlag) { //pMd + encoding
  const SSliceHeaderExt*    kpShExt             = &pSlice->sSliceHeaderExt;
  const SSliceHeader*       kpSh                = &kpShExt->sSliceHeader;
  const int32_t             kiSliceFirstMbXY    = kpSh->iFirstMbInSlice;
  SWelsMD sMd;

  sMd.uiRef         = kpSh->uiRefIndex;
  sMd.bMdUsingSad   = (pEncCtx->pSvcParam->iComplexityMode == LOW_COMPLEXITY);
  if (!pEncCtx->pCurDqLayer->bBaseLayerAvailableFlag || !kbIsHighestDlayerFlag)
    memset (&sMd.sMe, 0, sizeof (sMd.sMe));

  //pMb loop
  return WelsMdInterMbLoop (pEncCtx, pSlice, &sMd, kiSliceFirstMbXY);
}


int32_t WelsCodePSlice (sWelsEncCtx* pEncCtx, SSlice* pSlice) {
  //pSlice-level init should be outside and before this function
  SDqLayer* pCurLayer = pEncCtx->pCurDqLayer;

  const bool kbHighestSpatial = pEncCtx->pSvcParam->iSpatialLayerNum ==
                                (pCurLayer->sLayerInfo.sNalHeaderExt.uiDependencyId + 1);
  pEncCtx->pFuncList->pfInterMd = WelsMdInterMb;
  return WelsPSliceMdEnc (pEncCtx, pSlice, kbHighestSpatial);
}


// 1st index: 0: for P pSlice; 1: for I pSlice;
// 2nd index: 0: for non-dynamic pSlice; 1: for dynamic I pSlice;
//Allocate slice's MB cache buffer
int32_t AllocMbCacheAligned (SMbCache* pMbCache, CMemoryAlign* pMa) {
  pMbCache->pMemPredMb = (uint8_t*)pMa->WelsMallocz (2 * 256 * sizeof (uint8_t), "pMbCache->pMemPredMb");
  WELS_VERIFY_RETURN_IF (1, (NULL == pMbCache->pMemPredMb));

  pMbCache->pCoeffLevel = (int16_t*)pMa->WelsMallocz (MB_COEFF_LIST_SIZE * sizeof (int16_t), "pMbCache->pCoeffLevel");
  WELS_VERIFY_RETURN_IF (1, (NULL == pMbCache->pCoeffLevel));
  pMbCache->pSkipMb = (uint8_t*)pMa->WelsMallocz (384 * sizeof (uint8_t), "pMbCache->pSkipMb");
  WELS_VERIFY_RETURN_IF (1, (NULL == pMbCache->pSkipMb));
  pMbCache->pMemPredBlk4 = (uint8_t*)pMa->WelsMallocz (2 * 16 * sizeof (uint8_t), "pMbCache->pMemPredBlk4");
  WELS_VERIFY_RETURN_IF (1, (NULL == pMbCache->pMemPredBlk4));
  pMbCache->pBufferInterPredMe = (uint8_t*)pMa->WelsMallocz (4 * 640 * sizeof (uint8_t), "pMbCache->pBufferInterPredMe");
  WELS_VERIFY_RETURN_IF (1, (NULL == pMbCache->pBufferInterPredMe));
  pMbCache->pPrevIntra4x4PredModeFlag = (bool*)pMa->WelsMallocz (16 * sizeof (bool),
                                        "pMbCache->pPrevIntra4x4PredModeFlag");
  WELS_VERIFY_RETURN_IF (1, (NULL == pMbCache->pPrevIntra4x4PredModeFlag));
  pMbCache->pRemIntra4x4PredModeFlag = (int8_t*)pMa->WelsMallocz (16 * sizeof (int8_t),
                                       "pMbCache->pRemIntra4x4PredModeFlag");
  WELS_VERIFY_RETURN_IF (1, (NULL == pMbCache->pRemIntra4x4PredModeFlag));
  pMbCache->pDct = (SDCTCoeff*)pMa->WelsMallocz (sizeof (SDCTCoeff), "pMbCache->pDct");
  WELS_VERIFY_RETURN_IF (1, (NULL == pMbCache->pDct));

  return 0;
}

//  Free slice's MB cache buffer
void FreeMbCache (SMbCache* pMbCache, CMemoryAlign* pMa) {
  if (NULL != pMbCache->pCoeffLevel) {
    pMa->WelsFree (pMbCache->pCoeffLevel, "pMbCache->pCoeffLevel");
    pMbCache->pCoeffLevel = NULL;
  }
  if (NULL != pMbCache->pMemPredMb) {
    pMa->WelsFree (pMbCache->pMemPredMb, "pMbCache->pMemPredMb");
    pMbCache->pMemPredMb = NULL;
  }
  if (NULL != pMbCache->pSkipMb) {
    pMa->WelsFree (pMbCache->pSkipMb, "pMbCache->pSkipMb");
    pMbCache->pSkipMb = NULL;
  }
  if (NULL != pMbCache->pMemPredBlk4) {
    pMa->WelsFree (pMbCache->pMemPredBlk4, "pMbCache->pMemPredBlk4");
    pMbCache->pMemPredBlk4 = NULL;
  }
  if (NULL != pMbCache->pBufferInterPredMe) {
    pMa->WelsFree (pMbCache->pBufferInterPredMe, "pMbCache->pBufferInterPredMe");
    pMbCache->pBufferInterPredMe = NULL;
  }
  if (NULL != pMbCache->pPrevIntra4x4PredModeFlag) {
    pMa->WelsFree (pMbCache->pPrevIntra4x4PredModeFlag, "pMbCache->pPrevIntra4x4PredModeFlag");
    pMbCache->pPrevIntra4x4PredModeFlag = NULL;
  }
  if (NULL != pMbCache->pRemIntra4x4PredModeFlag) {
    pMa->WelsFree (pMbCache->pRemIntra4x4PredModeFlag, "pMbCache->pRemIntra4x4PredModeFlag");
    pMbCache->pRemIntra4x4PredModeFlag = NULL;
  }
  if (NULL != pMbCache->pDct) {
    pMa->WelsFree (pMbCache->pDct, "pMbCache->pDct");
    pMbCache->pDct = NULL;
  }
}

//Initialize slice's boundary info)
int32_t InitSliceBoundaryInfo (SDqLayer* pCurLayer,
                               SSliceArgument* pSliceArgument,
                               const int32_t kiSliceNumInFrame) {
  const int32_t kiMBWidth             = pCurLayer->iMbWidth;
  const int32_t kiMBHeight            = pCurLayer->iMbHeight;
  const int32_t kiCountNumMbInFrame   = kiMBWidth * kiMBHeight;
  int32_t iSliceIdx                   = 0;
  int32_t iFirstMBInSlice             = 0;
  int32_t iMbNumInSlice               = 0;

  for (; iSliceIdx < kiSliceNumInFrame; iSliceIdx++) {
    iFirstMBInSlice = 0;
    iMbNumInSlice   = kiCountNumMbInFrame;

    pCurLayer->pCountMbNumInSlice[iSliceIdx] = iMbNumInSlice;
    pCurLayer->pFirstMbIdxOfSlice[iSliceIdx] = iFirstMBInSlice;
  }

  return ENC_RETURN_SUCCESS;
}

int32_t SetSliceBoundaryInfo (SDqLayer* pCurLayer, SSlice* pSlice, const int32_t kiSliceIdx) {
  if (NULL == pCurLayer || NULL == pSlice  ||
      NULL == pCurLayer->pFirstMbIdxOfSlice ||
      NULL == pCurLayer->pCountMbNumInSlice) {

    return ENC_RETURN_UNEXPECTED;
  }

  pSlice->sSliceHeaderExt.sSliceHeader.iFirstMbInSlice = pCurLayer->pFirstMbIdxOfSlice[kiSliceIdx];
  pSlice->iCountMbNumInSlice  = pCurLayer->pCountMbNumInSlice[kiSliceIdx];

  return ENC_RETURN_SUCCESS;
}

//Allocate slice's MB info buffer
int32_t AllocateSliceMBBuffer (SSlice* pSlice, CMemoryAlign* pMa) {
  if (AllocMbCacheAligned (&pSlice->sMbCacheInfo, pMa)) {
    return ENC_RETURN_MEMALLOCERR;
  }

  return ENC_RETURN_SUCCESS;
}

// Initialize slice bs buffer info
int32_t InitSliceBsBuffer (SSlice* pSlice,
                           SBitStringAux* pBsWrite,
                           bool bIndependenceBsBuffer,
                           const int32_t iMaxSliceBufferSize,
                           CMemoryAlign* pMa) {
  pSlice->sSliceBs.uiSize  = iMaxSliceBufferSize;
  pSlice->sSliceBs.uiBsPos = 0;

  if (bIndependenceBsBuffer) {
    pSlice->pSliceBsa      = &pSlice->sSliceBs.sBsWrite;
    pSlice->sSliceBs.pBs   = (uint8_t*)pMa->WelsMallocz (iMaxSliceBufferSize, "sSliceBs.pBs");
    if (NULL == pSlice->sSliceBs.pBs) {
      return ENC_RETURN_MEMALLOCERR;
    }
  } else {
    pSlice->pSliceBsa      = pBsWrite;
    pSlice->sSliceBs.pBs   = NULL;
  }
  return ENC_RETURN_SUCCESS;
}

//free slice bs buffer
void FreeSliceBuffer (SSlice*& pSliceList, const int32_t kiMaxSliceNum, CMemoryAlign* pMa, const char* kpTag) {
  if (NULL != pSliceList) {
    int32_t iSliceIdx = 0;
    while (iSliceIdx < kiMaxSliceNum) {
      SSlice* pSlice = &pSliceList[iSliceIdx];
      FreeMbCache (&pSlice->sMbCacheInfo, pMa);

      //slice bs buffer
      if (NULL != pSlice->sSliceBs.pBs) {
        pMa->WelsFree (pSlice->sSliceBs.pBs, "sSliceBs.pBs");
        pSlice->sSliceBs.pBs = NULL;
      }
      ++ iSliceIdx;
    }
    pMa->WelsFree (pSliceList, kpTag);
    pSliceList = NULL;
  }
}

int32_t InitSliceList (SSlice*& pSliceList,
                       SBitStringAux* pBsWrite,
                       const int32_t kiMaxSliceNum,
                       const int32_t kiMaxSliceBufferSize,
                       const bool bIndependenceBsBuffer,
                       CMemoryAlign* pMa) {
  int32_t iSliceIdx               = 0;
  int32_t iRet                    = 0;

  if (kiMaxSliceBufferSize <= 0) {
    return ENC_RETURN_UNEXPECTED;
  }

  while (iSliceIdx < kiMaxSliceNum) {
    SSlice* pSlice = pSliceList + iSliceIdx;
    if (NULL == pSlice) {
      return ENC_RETURN_MEMALLOCERR;
    }

    pSlice->iSliceIdx          = iSliceIdx;
    pSlice->uiBufferIdx        = 0;
    pSlice->iCountMbNumInSlice = 0;
    pSlice->sSliceHeaderExt.sSliceHeader.iFirstMbInSlice = 0;

    iRet = InitSliceBsBuffer (pSlice,
                              pBsWrite,
                              bIndependenceBsBuffer,
                              kiMaxSliceBufferSize,
                              pMa);
    if (ENC_RETURN_SUCCESS != iRet) {
      return iRet;
    }

    iRet = AllocateSliceMBBuffer (pSlice, pMa);

    if (ENC_RETURN_SUCCESS != iRet) {
      return iRet;
    }
    ++ iSliceIdx;
  }
  return ENC_RETURN_SUCCESS;
}


int32_t InitSliceThreadInfo (sWelsEncCtx* pCtx,
                             SDqLayer* pDqLayer,
                             const int32_t kiDlayerIndex,
                             CMemoryAlign* pMa) {
  int32_t iThreadNum      = pCtx->pSvcParam->iMultipleThreadIdc;
  int32_t iMaxSliceNum    = 0;
  int32_t iSlcBufferNum   = 0;
  int32_t iIdx            = 0;
  int32_t iRet            = 0;

  assert (iThreadNum > 0);

  //for fixed slice num case, no need to reallocate, so one slice buffer for all thread
  if (pDqLayer->bThreadSlcBufferFlag) {
    iMaxSliceNum  = pDqLayer->iMaxSliceNum / iThreadNum + 1;
    iSlcBufferNum = iThreadNum;
  } else {
    iMaxSliceNum  = pDqLayer->iMaxSliceNum;
    iSlcBufferNum = 1;
  }

  while (iIdx < iSlcBufferNum) {
    pDqLayer->sSliceBufferInfo[iIdx].iMaxSliceNum   = iMaxSliceNum;
    pDqLayer->sSliceBufferInfo[iIdx].iCodedSliceNum = 0;
    pDqLayer->sSliceBufferInfo[iIdx].pSliceBuffer   = (SSlice*)pMa->WelsMallocz (sizeof (SSlice) * iMaxSliceNum,
        "pSliceBuffer");
    if (NULL == pDqLayer->sSliceBufferInfo[iIdx].pSliceBuffer) {
      WelsLog (& (pCtx->sLogCtx), WELS_LOG_ERROR,
               "CWelsH264SVCEncoder::InitSliceThreadInfo: pSliceThreadInfo->pSliceBuffer[iIdx] is NULL");
      return ENC_RETURN_MEMALLOCERR;
    }
    iRet = InitSliceList (pDqLayer->sSliceBufferInfo[iIdx].pSliceBuffer,
                          &pCtx->pOut->sBsWrite,
                          iMaxSliceNum,
                          pCtx->iSliceBufferSize[kiDlayerIndex],
                          pDqLayer->bSliceBsBufferFlag,
                          pMa);
    if (ENC_RETURN_SUCCESS != iRet) {
      return iRet;
    }
    iIdx++;
  }

  for (; iIdx < MAX_THREADS_NUM; iIdx++) {
    pDqLayer->sSliceBufferInfo[iIdx].iMaxSliceNum   = 0;
    pDqLayer->sSliceBufferInfo[iIdx].iCodedSliceNum = 0;
    pDqLayer->sSliceBufferInfo[iIdx].pSliceBuffer   = NULL;
  }

  return ENC_RETURN_SUCCESS;
}

int32_t InitSliceInLayer (sWelsEncCtx* pCtx,
                          SDqLayer* pDqLayer,
                          const int32_t kiDlayerIndex,
                          CMemoryAlign* pMa)  {
  int32_t iRet         = 0;
  int32_t iSliceIdx    = 0;
  int32_t iSlcBuffIdx  = 0;
  int32_t iStartIdx    = 0;
  int32_t iMaxSliceNum = pDqLayer->iMaxSliceNum;
  SSliceArgument* pSliceArgument  = & pCtx->pSvcParam->sSpatialLayers[kiDlayerIndex].sSliceArgument;

  //SM_SINGLE_SLICE mode using single-thread bs writer pOut->sBsWrite
  //even though multi-thread is on for other layers
  pDqLayer->bSliceBsBufferFlag = (pCtx->pSvcParam->iMultipleThreadIdc > 1 &&
                                  SM_SINGLE_SLICE != pSliceArgument->uiSliceMode) ? true : false;

  pDqLayer->bThreadSlcBufferFlag = (pCtx->pSvcParam->iMultipleThreadIdc > 1 &&
                                    SM_SIZELIMITED_SLICE == pSliceArgument->uiSliceMode) ? true : false;

  iRet = InitSliceThreadInfo (pCtx,
                              pDqLayer,
                              kiDlayerIndex,
                              pMa);
  if (ENC_RETURN_SUCCESS != iRet) {
    return ENC_RETURN_MEMALLOCERR;
  }

  pDqLayer->iMaxSliceNum = 0;
  for (iSlcBuffIdx = 0; iSlcBuffIdx < pCtx->iActiveThreadsNum; iSlcBuffIdx++) {
    pDqLayer->iMaxSliceNum += pDqLayer->sSliceBufferInfo[iSlcBuffIdx].iMaxSliceNum;
  }

  pDqLayer->ppSliceInLayer = (SSlice**)pMa->WelsMallocz (sizeof (SSlice*) * pDqLayer->iMaxSliceNum, "ppSliceInLayer");
  if (NULL ==  pDqLayer->ppSliceInLayer) {
    WelsLog (& (pCtx->sLogCtx), WELS_LOG_ERROR, "CWelsH264SVCEncoder::InitSliceInLayer() pDqLayer->ppSliceInLayer is NULL");
    return ENC_RETURN_MEMALLOCERR;
  }

  pDqLayer->pFirstMbIdxOfSlice = (int32_t*)pMa->WelsMallocz (sizeof (int32_t*) * pDqLayer->iMaxSliceNum,
                                 "pFirstMbIdxOfSlice");
  if (NULL ==  pDqLayer->pFirstMbIdxOfSlice) {
    WelsLog (& (pCtx->sLogCtx), WELS_LOG_ERROR,
             "CWelsH264SVCEncoder::InitSliceInLayer() pDqLayer->pFirstMbIdxOfSlice is NULL");
    return ENC_RETURN_MEMALLOCERR;
  }

  pDqLayer->pCountMbNumInSlice = (int32_t*)pMa->WelsMallocz (sizeof (int32_t*) * pDqLayer->iMaxSliceNum,
                                 "pCountMbNumInSlice");
  if (NULL ==  pDqLayer->pCountMbNumInSlice) {
    WelsLog (& (pCtx->sLogCtx), WELS_LOG_ERROR,
             "CWelsH264SVCEncoder::InitSliceInLayer() pDqLayer->pCountMbNumInSlice is NULL");
    return ENC_RETURN_MEMALLOCERR;
  }

  iRet = InitSliceBoundaryInfo (pDqLayer, pSliceArgument, iMaxSliceNum);
  if (ENC_RETURN_SUCCESS != iRet) {
    return iRet;
  }

  iStartIdx = 0;
  for (iSlcBuffIdx = 0; iSlcBuffIdx < pCtx->iActiveThreadsNum; iSlcBuffIdx++) {
    for (iSliceIdx = 0; iSliceIdx < pDqLayer->sSliceBufferInfo[iSlcBuffIdx].iMaxSliceNum; iSliceIdx++) {
      pDqLayer->ppSliceInLayer[iStartIdx + iSliceIdx] = pDqLayer->sSliceBufferInfo[iSlcBuffIdx].pSliceBuffer + iSliceIdx;
    }

    iStartIdx += pDqLayer->sSliceBufferInfo[iSlcBuffIdx].iMaxSliceNum;
  }

  return ENC_RETURN_SUCCESS;
}

void InitSliceHeadWithBase (SSlice* pSlice, SSlice* pBaseSlice) {
  if (NULL == pSlice || NULL == pBaseSlice) {
    return;
  }

  SSliceHeaderExt* pBaseSHExt = &pBaseSlice->sSliceHeaderExt;
  SSliceHeaderExt* pSHExt     = &pSlice->sSliceHeaderExt;

  pSlice->bSliceHeaderExtFlag = pBaseSlice->bSliceHeaderExtFlag;
  pSHExt->sSliceHeader.iPpsId = pBaseSHExt->sSliceHeader.iPpsId;
  pSHExt->sSliceHeader.pPps   = pBaseSHExt->sSliceHeader.pPps;
  pSHExt->sSliceHeader.iSpsId = pBaseSHExt->sSliceHeader.iSpsId;
  pSHExt->sSliceHeader.pSps   = pBaseSHExt->sSliceHeader.pSps;
}














int32_t WelsCodeOneSlice (sWelsEncCtx* pEncCtx, SSlice* pCurSlice, const int32_t kiNalType) {
  SDqLayer* pCurLayer              = pEncCtx->pCurDqLayer;
  SWelsSvcRc* pWelsSvcRc           = &pEncCtx->pWelsSvcRc[pEncCtx->uiDependencyId];
  SNalUnitHeaderExt* pNalHeadExt   = &pCurLayer->sLayerInfo.sNalHeaderExt;
  SBitStringAux* pBs               = pCurSlice->pSliceBsa;
  if (I_SLICE == pEncCtx->eSliceType) {
    pNalHeadExt->bIdrFlag = 1;
    pCurSlice->sScaleShift = 0;
  } else {
    const uint32_t kuiTemporalId = pNalHeadExt->uiTemporalId;
    pCurSlice->sScaleShift = kuiTemporalId ? (kuiTemporalId - pEncCtx->pRefPic->uiTemporalId) : 0;
  }

  WelsSliceHeaderExtInit (pEncCtx, pCurLayer, pCurSlice);

  //RomRC init slice by slice
  if (pWelsSvcRc->bGomRC) {
    GomRCInitForOneSlice (pCurSlice, pWelsSvcRc->iBitsPerMb);
  }

  WelsSliceHeaderWrite (pEncCtx, pBs, pCurLayer, pCurSlice,
      pEncCtx->pFuncList->pParametersetStrategy);

  pCurSlice->uiLastMbQp = pCurLayer->sLayerInfo.pPpsP->iPicInitQp + pCurSlice->sSliceHeaderExt.sSliceHeader.iSliceQpDelta;

  int32_t iEncReturn = (pNalHeadExt->bIdrFlag ? WelsISliceMdEnc : WelsCodePSlice) (pEncCtx, pCurSlice);
  if (ENC_RETURN_SUCCESS != iEncReturn)
    return iEncReturn;

  WelsCabacEncodeFlush (&pCurSlice->sCabacCtx);
  pBs->pCurBuf = WelsCabacEncodeGetPtr (&pCurSlice->sCabacCtx);

  return ENC_RETURN_SUCCESS;
}




///////////////
//  pMb loop
///////////////
inline void WelsInitInterMDStruc (const SMB* pCurMb, uint16_t* pMvdCostTable, const int32_t kiMvdInterTableStride,
                                  SWelsMD* pMd) {
  pMd->iLambda = g_kiQpCostTable[pCurMb->uiLumaQp];
  pMd->pMvdCost = &pMvdCostTable[pCurMb->uiLumaQp * kiMvdInterTableStride];
  pMd-> iMbPixX = (pCurMb->iMbX << 4);
  pMd-> iMbPixY = (pCurMb->iMbY << 4);
  memset (&pMd->iBlock8x8StaticIdc[0], 0, sizeof (pMd->iBlock8x8StaticIdc));
}
// for inter non-dynamic pSlice
int32_t WelsMdInterMbLoop (sWelsEncCtx* pEncCtx, SSlice* pSlice, void* pWelsMd, const int32_t kiSliceFirstMbXY) {
  SWelsMD* pMd          = (SWelsMD*)pWelsMd;
  SDqLayer* pCurLayer   = pEncCtx->pCurDqLayer;
  SMbCache* pMbCache    = &pSlice->sMbCacheInfo;
  SMB* pMbList          = pCurLayer->sMbDataP;
  SMB* pCurMb           = NULL;
  int32_t iNumMbCoded   = 0;
  int32_t iNextMbIdx    = kiSliceFirstMbXY;
  int32_t iCurMbIdx     = -1;
  const int32_t kiTotalNumMb = pCurLayer->iMbWidth * pCurLayer->iMbHeight;
  const int32_t kiMvdInterTableStride = pEncCtx->iMvdCostTableStride;
  uint16_t* pMvdCostTable = &pEncCtx->pMvdCostTable[pEncCtx->iMvdCostTableSize];
  const int32_t kiSliceIdx = pSlice->iSliceIdx;
  int32_t iEncReturn = ENC_RETURN_SUCCESS;
  WelsInitSliceCabac (pEncCtx, pSlice);
  for (;;) {
    //point to current pMb
    iCurMbIdx = iNextMbIdx;
    pCurMb = &pMbList[ iCurMbIdx ];


    //step(1): set QP for the current MB
    WelsRcMbInitGom (pEncCtx, pCurMb, pSlice);

    //step (2). save some vale for future use, initial pWelsMd
    WelsMdIntraInit (pEncCtx, pCurMb, pMbCache, kiSliceFirstMbXY);
    WelsMdInterInit (pEncCtx, pSlice, pCurMb, kiSliceFirstMbXY);

    WelsInitInterMDStruc (pCurMb, pMvdCostTable, kiMvdInterTableStride, pMd);
    pEncCtx->pFuncList->pfInterMd (pEncCtx, pMd, pSlice, pCurMb, pMbCache);
    //mb_qp

    //step (4): save from the MD process from future use
    WelsMdInterSaveSadAndRefMbType ((pCurLayer->pDecPic->uiRefMbType), pMbCache, pCurMb, pMd);

    WelsMdUpdateRefMbQp (pCurLayer, pCurMb, pMbCache->bCollocatedPredFlag,
        pEncCtx->pRefPic->iPictureType);

    //step (5): update cache
    UpdateNonZeroCountCache (pCurMb, pMbCache);

    //step (6): begin to write bit stream; if the pSlice size is controlled, the writing may be skipped

    iEncReturn = WelsSpatialWriteMbSynCabac (pEncCtx, pSlice, pCurMb);
    if (ENC_RETURN_SUCCESS != iEncReturn)
      return iEncReturn;


    //step (7): reconstruct current MB
    pCurMb->uiSliceIdc = kiSliceIdx;
    OutputPMbWithoutConstructCsRsNoCopy (pEncCtx, pCurLayer, pSlice, pCurMb);


    //step (8): update status and other parameters
    WelsRcMbInfoUpdateGom (pEncCtx, pCurMb, pMd->iCostLuma, pSlice);

    /*judge if all pMb in cur pSlice has been encoded*/
    ++ iNumMbCoded;
    iNextMbIdx = WelsGetNextMbOfSlice (pCurLayer, iCurMbIdx);
    //whether all of MB in current pSlice encoded or not
    if (iNextMbIdx == -1 || iNextMbIdx >= kiTotalNumMb || iNumMbCoded >= kiTotalNumMb) {
      break;
    }
  }

  return iEncReturn;
}


}//namespace WelsEnc
