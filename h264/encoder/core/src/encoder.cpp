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
 * \file    encoder.c
 *
 * \brief   core encoder
 *
 * \date    5/14/2009 Created
 *
 *************************************************************************************
 */
#include "encoder.h"
#include "cpu_core.h"

#include "decode_mb_aux.h"
#include "get_intra_predictor.h"

#include "ref_list_mgr_svc.h"
#include "mc.h"
#include "paraset_strategy.h"
#include "sample.h"

#include "svc_enc_golomb.h"
#include "svc_base_layer_md.h"
#include "svc_mode_decision.h"
#include "set_mb_syn_cavlc.h"
#include "crt_util_safe_x.h" // Safe CRT routines like utils for cross_platforms

//  global   function  pointers  definition
namespace WelsEnc {
/* Motion compensation */




void WelsInitBGDFunc (SWelsFuncPtrList* pFuncList, const bool kbEnableBackgroundDetection) {
  // jumpseat: no background detection (screen content)
  pFuncList->pfInterMdBackgroundDecision = WelsMdInterJudgeBGDPskipFalse;
}

/*!
 * \brief   initialize function pointers that potentially used in Wels encoding
 * \param   pEncCtx     sWelsEncCtx*
 * \return  successful - 0; otherwise none 0 for failed
 */
int32_t InitFunctionPointers (sWelsEncCtx* pEncCtx, SWelsSvcCodingParam* pParam, uint32_t uiCpuFlag) {
  int32_t iReturn = ENC_RETURN_SUCCESS;
  SWelsFuncPtrList* pFuncList = pEncCtx->pFuncList;

  /* Functionality utilization of CPU instructions dependency */
#if defined(X86_ASM)
  if (uiCpuFlag & WELS_CPU_MMXEXT) {
  }
  if (uiCpuFlag & WELS_CPU_SSE2) {
  }
#endif//X86_ASM


#if defined(HAVE_NEON_AARCH64)
  if (uiCpuFlag & WELS_CPU_NEON) {
  }
#endif

  InitExpandPictureFunc (& (pFuncList->sExpandPicFunc), uiCpuFlag);

  /* Intra_Prediction_fn*/
  WelsInitIntraPredFuncs (pFuncList, uiCpuFlag);

  /* ME func */
  WelsInitMeFunc (pFuncList, uiCpuFlag);

  /* sad, satd, average */
  WelsInitSampleSadFunc (pFuncList, uiCpuFlag);

  //
  WelsInitBGDFunc (pFuncList, pParam->bEnableBackgroundDetection);
  pFuncList->pfSCDPSkipDecision = WelsMdInterJudgeSCDPskip;

  InitIntraAnalysisVaaInfo (pFuncList, uiCpuFlag);

  /* Motion compensation */
  /*init pixel average function*/
  /*get one column or row pixel when refinement*/
  InitMcFunc (&pFuncList->sMcFuncs, uiCpuFlag);
  InitCoeffFunc (pFuncList, uiCpuFlag, pParam->iEntropyCodingModeFlag);

  WelsInitEncodingFuncs (pFuncList, uiCpuFlag);
  WelsInitReconstructionFuncs (pFuncList, uiCpuFlag);


  InitFillNeighborCacheInterFunc (pFuncList, pParam->bEnableBackgroundDetection);

  pFuncList->pParametersetStrategy = IWelsParametersetStrategy::CreateParametersetStrategy (pParam->eSpsPpsIdStrategy,
                                     pParam->bSimulcastAVC, pParam->iSpatialLayerNum);
  WELS_VERIFY_RETURN_IF (ENC_RETURN_MEMALLOCERR, (NULL == pFuncList->pParametersetStrategy))

  return iReturn;
}

void UpdateFrameNum (sWelsEncCtx* pEncCtx, const int32_t kiDidx) {
  SSpatialLayerInternal* pParamInternal = &pEncCtx->pSvcParam->sDependencyLayers[kiDidx];
  bool bNeedFrameNumIncreasing = false;

  if (NRI_PRI_LOWEST != pEncCtx->eLastNalPriority[kiDidx]) {
    bNeedFrameNumIncreasing = true;
  }

  if (bNeedFrameNumIncreasing) {
    if (pParamInternal->iFrameNum < (1 << pEncCtx->pSps->uiLog2MaxFrameNum) - 1)
      ++ pParamInternal->iFrameNum;
    else
      pParamInternal->iFrameNum = 0;    // if iFrameNum overflow
  }

  pEncCtx->eLastNalPriority[kiDidx] = NRI_PRI_LOWEST;
}



void InitBitStream (sWelsEncCtx* pEncCtx) {
  // for bitstream writing
  pEncCtx->iPosBsBuffer         = 0;    // reset bs pBuffer position
  pEncCtx->pOut->iNalIndex      = 0;    // reset NAL index
  pEncCtx->pOut->iLayerBsIndex  = 0;    // reset index of Layer Bs

  InitBits (&pEncCtx->pOut->sBsWrite, pEncCtx->pOut->pBsBuffer, pEncCtx->pOut->uiSize);
}
/*!
 * \brief   initialize frame coding
 */
void InitFrameCoding (sWelsEncCtx* pEncCtx, const EVideoFrameType keFrameType, const int32_t kiDidx) {
  SSpatialLayerInternal* pParamInternal = &pEncCtx->pSvcParam->sDependencyLayers[kiDidx];
  if (keFrameType == videoFrameTypeP) {
    ++pParamInternal->iFrameIndex;

    if (pParamInternal->iPOC < (1 << pEncCtx->pSps->iLog2MaxPocLsb) -
        2)     // if iPOC type is no 0, this need be modification
      pParamInternal->iPOC += 2;   // for POC type 0
    else
      pParamInternal->iPOC = 0;

    UpdateFrameNum (pEncCtx, kiDidx);

    pEncCtx->eNalType           = NAL_UNIT_CODED_SLICE;
    pEncCtx->eSliceType         = P_SLICE;
    pEncCtx->eNalPriority       = NRI_PRI_HIGH;
  } else if (keFrameType == videoFrameTypeIDR) {
    pParamInternal->iFrameNum          = 0;
    pParamInternal->iPOC               = 0;
    pParamInternal->bEncCurFrmAsIdrFlag = false;
    pParamInternal->iFrameIndex = 0;

    pEncCtx->eNalType           = NAL_UNIT_CODED_SLICE_IDR;
    pEncCtx->eSliceType         = I_SLICE;
    pEncCtx->eNalPriority       = NRI_PRI_HIGHEST;

    pParamInternal->iCodingIndex       = 0;

    // reset_ref_list

    // rc_init_gop
  } else if (keFrameType == videoFrameTypeI) {
    if (pParamInternal->iPOC < (1 << pEncCtx->pSps->iLog2MaxPocLsb) -
        2)     // if iPOC type is no 0, this need be modification
      pParamInternal->iPOC += 2;   // for POC type 0
    else
      pParamInternal->iPOC = 0;

    UpdateFrameNum (pEncCtx, kiDidx);

    pEncCtx->eNalType     = NAL_UNIT_CODED_SLICE;
    pEncCtx->eSliceType   = I_SLICE;
    pEncCtx->eNalPriority = NRI_PRI_HIGHEST;

    // rc_init_gop
  } else { // B pictures are not supported now, any else?
    assert (0);
  }

}

EVideoFrameType DecideFrameType (sWelsEncCtx* pEncCtx, const int8_t kiSpatialNum, const int32_t kiDidx,
                                 bool bSkipFrameFlag) {
  SWelsSvcCodingParam* pSvcParam = pEncCtx->pSvcParam;
  SSpatialLayerInternal* pParamInternal = &pEncCtx->pSvcParam->sDependencyLayers[kiDidx];
  EVideoFrameType iFrameType = videoFrameTypeInvalid;
  bool bSceneChangeFlag = false;
  if ((!pSvcParam->bEnableSceneChangeDetect) || pEncCtx->pVaa->bIdrPeriodFlag ||
      (kiSpatialNum < pSvcParam->iSpatialLayerNum)) {
    bSceneChangeFlag = false;
  } else {
    bSceneChangeFlag = pEncCtx->pVaa->bSceneChangeFlag;
  }
  if (pEncCtx->pVaa->bIdrPeriodFlag || pParamInternal->bEncCurFrmAsIdrFlag || (!pSvcParam->bEnableLongTermReference
      && bSceneChangeFlag && !bSkipFrameFlag)) {
    iFrameType = videoFrameTypeIDR;
  } else if (pSvcParam->bEnableLongTermReference && (bSceneChangeFlag
             || pEncCtx->pVaa->eSceneChangeIdc == LARGE_CHANGED_SCENE)) {
    int iActualLtrcount = 0;
    SPicture** pLongTermRefList = pEncCtx->ppRefPicListExt[0]->pLongRefList;
    for (int i = 0; i < pSvcParam->iLTRRefNum; ++i) {
      if (NULL != pLongTermRefList[i] && pLongTermRefList[i]->bUsedAsRef && pLongTermRefList[i]->bIsLongRef
          && pLongTermRefList[i]->bIsSceneLTR) {
        ++iActualLtrcount;
      }
    }
    if (iActualLtrcount == pSvcParam->iLTRRefNum && bSceneChangeFlag) {
      iFrameType = videoFrameTypeIDR;
    } else {
      iFrameType = videoFrameTypeP;
      pEncCtx->bCurFrameMarkedAsSceneLtr = true;
    }
  } else {
    iFrameType = videoFrameTypeP;
  }
  if (videoFrameTypeP == iFrameType && bSkipFrameFlag) {
    iFrameType = videoFrameTypeSkip;
  } else if (videoFrameTypeIDR == iFrameType) {
    pParamInternal->iCodingIndex = 0;
    pEncCtx->bCurFrameMarkedAsSceneLtr   = true;
  }
  return iFrameType;
}





}
