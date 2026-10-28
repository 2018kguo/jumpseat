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

#include "wels_preprocess.h"
#include "picture_handle.h"
#include "encoder_context.h"
#include "utils.h"
#include "encoder.h"

namespace {

void ClearEndOfLinePadding (uint8_t* pData, int32_t iStride, int32_t iWidth, int32_t iHeight) {
  if (iWidth < iStride) {
    for (int32_t i = 0; i < iHeight; ++i)
      memset (pData + i * iStride + iWidth, 0, iStride - iWidth);
  }
}

} // anon ns.

namespace WelsEnc {



//***** entry API declaration ************************************************************************//

int32_t WelsInitScaledPic (SWelsSvcCodingParam* pParam,  Scaled_Picture*  pScaledPic, CMemoryAlign* pMemoryAlign);
bool  JudgeNeedOfScaling (SWelsSvcCodingParam* pParam, Scaled_Picture* pScaledPic);
void    FreeScaledPic (Scaled_Picture*  pScaledPic, CMemoryAlign* pMemoryAlign);
void  WelsMoveMemory_c (uint8_t* pDstY, uint8_t* pDstU, uint8_t* pDstV,  int32_t iDstStrideY, int32_t iDstStrideUV,
                        uint8_t* pSrcY, uint8_t* pSrcU, uint8_t* pSrcV, int32_t iSrcStrideY, int32_t iSrcStrideUV, int32_t iWidth,
                        int32_t iHeight);

//******* table definition ***********************************************************************//
const uint8_t g_kuiRefTemporalIdx[MAX_TEMPORAL_LEVEL][MAX_GOP_SIZE] = {
  {  0, }, // 0
  {  0,  0, }, // 1
  {  0,  0,  0,  1, }, // 2
  {  0,  0,  0,  2,  0,  1,  1,  2, }, // 3
};

const int32_t g_kiPixMapSizeInBits = sizeof (uint8_t) * 8;


inline  void   WelsUpdateSpatialIdxMap (sWelsEncCtx* pEncCtx, const int32_t iPos, SPicture* const pPic,
                                        const int32_t iDidx) {
  pEncCtx->sSpatialIndexMap[iPos].pSrc = pPic;
  pEncCtx->sSpatialIndexMap[iPos].iDid = iDidx;
}


/***************************************************************************
*
*   implement of the interface
*
***************************************************************************/



CWelsPreProcess* CWelsPreProcess::CreatePreProcess (sWelsEncCtx* pEncCtx) {

  CWelsPreProcess* pPreProcess = NULL;
  pPreProcess = WELS_NEW_OP (CWelsPreProcessScreen (pEncCtx), CWelsPreProcessScreen);
  WELS_VERIFY_RETURN_IF (NULL, NULL == pPreProcess)
  return pPreProcess;
}


CWelsPreProcess::CWelsPreProcess (sWelsEncCtx* pEncCtx) {
  m_pInterfaceVp = NULL;
  m_bInitDone = false;
  m_pEncCtx = pEncCtx;
  memset (&m_sScaledPicture, 0, sizeof (m_sScaledPicture));
  memset (m_pSpatialPic, 0, sizeof (m_pSpatialPic));
  memset (m_uiSpatialLayersInTemporal, 0, sizeof (m_uiSpatialLayersInTemporal));
  memset (m_uiSpatialPicNum, 0, sizeof (m_uiSpatialPicNum));
}

CWelsPreProcess::~CWelsPreProcess() {
  FreeScaledPic (&m_sScaledPicture,  m_pEncCtx->pMemAlign);
  WelsPreprocessDestroy();
}

int32_t CWelsPreProcess::WelsPreprocessCreate() {
  if (m_pInterfaceVp == NULL) {
    WelsCreateVpInterface ((void**) &m_pInterfaceVp, WELSVP_INTERFACE_VERION);
    if (!m_pInterfaceVp)
      goto exit;
  } else
    goto exit;

  return 0;

exit:
  WelsPreprocessDestroy();
  return 1;
}

int32_t CWelsPreProcess::WelsPreprocessDestroy() {
  WelsDestroyVpInterface (m_pInterfaceVp, WELSVP_INTERFACE_VERION);
  m_pInterfaceVp = NULL;

  return 0;
}

int32_t CWelsPreProcess::WelsPreprocessReset (sWelsEncCtx* pCtx, int32_t iWidth, int32_t iHeight) {
  int32_t iRet = -1;
  SWelsSvcCodingParam* pSvcParam = pCtx->pSvcParam;
  //init source width and height
  pSvcParam->SUsedPicRect.iLeft = 0;
  pSvcParam->SUsedPicRect.iTop  = 0;
  pSvcParam->SUsedPicRect.iWidth =  iWidth;
  pSvcParam->SUsedPicRect.iHeight = iHeight;
  if ((iWidth < 16) || ((iHeight < 16))) {
    WelsLog (& (pCtx->sLogCtx), WELS_LOG_ERROR, "Don't support width(%d) or height(%d) which is less than 16 ", iWidth,
             iHeight);
    return iRet;
  }
  if (pCtx) {
    FreeScaledPic (&m_sScaledPicture, pCtx->pMemAlign);
    iRet = InitLastSpatialPictures (pCtx);
    iRet = WelsInitScaledPic (pCtx->pSvcParam, &m_sScaledPicture, pCtx->pMemAlign);
  }

  return iRet;
}

int32_t CWelsPreProcess::AllocSpatialPictures (sWelsEncCtx* pCtx, SWelsSvcCodingParam* pParam) {
  CMemoryAlign* pMa             = pCtx->pMemAlign;
  const int32_t kiDlayerCount   = pParam->iSpatialLayerNum;
  int32_t iDlayerIndex          = 0;

  // spatial pictures
  iDlayerIndex = 0;
  do {
    const int32_t kiPicWidth = pParam->sSpatialLayers[iDlayerIndex].iVideoWidth;
    const int32_t kiPicHeight   = pParam->sSpatialLayers[iDlayerIndex].iVideoHeight;
    const uint8_t kuiLayerInTemporal = 2 + WELS_MAX (pParam->sDependencyLayers[iDlayerIndex].iHighestTemporalId, 1);
    const uint8_t kuiRefNumInTemporal = kuiLayerInTemporal + pParam->iLTRRefNum;
    uint8_t i = 0;

    m_uiSpatialPicNum[iDlayerIndex] = kuiRefNumInTemporal;
    do {
      SPicture* pPic = AllocPicture (pMa, kiPicWidth, kiPicHeight, false, 0);
      WELS_VERIFY_RETURN_IF (1, (NULL == pPic))
      m_pSpatialPic[iDlayerIndex][i] = pPic;
      ++ i;
    } while (i < kuiRefNumInTemporal);

    m_uiSpatialLayersInTemporal[iDlayerIndex] = 1;

    ++ iDlayerIndex;
  } while (iDlayerIndex < kiDlayerCount);

  return 0;
}

void CWelsPreProcess::FreeSpatialPictures (sWelsEncCtx* pCtx) {
  CMemoryAlign* pMa = pCtx->pMemAlign;
  int32_t j = 0;
  while (j < pCtx->pSvcParam->iSpatialLayerNum) {
    uint8_t i = 0;
    uint8_t uiRefNumInTemporal = m_uiSpatialPicNum[j];

    while (i < uiRefNumInTemporal) {
      if (NULL != m_pSpatialPic[j][i]) {
        FreePicture (pMa, &m_pSpatialPic[j][i]);
      }
      ++ i;
    }
    m_uiSpatialLayersInTemporal[j] = 0;
    ++ j;
  }
}

int32_t CWelsPreProcess::BuildSpatialPicList (sWelsEncCtx* pCtx, const SSourcePicture* kpSrcPic) {
  SWelsSvcCodingParam* pSvcParam = pCtx->pSvcParam;
  int32_t iSpatialNum = 0;
  int32_t iWidth = ((kpSrcPic->iPicWidth >> 1) << 1);
  int32_t iHeight = ((kpSrcPic->iPicHeight >> 1) << 1);

  if (!m_bInitDone) {
    if (WelsPreprocessCreate() != 0)
      return -1;

    if (WelsPreprocessReset (pCtx, iWidth, iHeight) != 0)
      return -1;

    m_iAvaliableRefInSpatialPicList = pSvcParam->iNumRefFrame;

    m_bInitDone = true;
  } else {
    if ((iWidth != pSvcParam->SUsedPicRect.iWidth) || (iHeight != pSvcParam->SUsedPicRect.iHeight)) {
      if (WelsPreprocessReset (pCtx, iWidth, iHeight) != 0)
        return -1;
    }
  }

  if (m_pInterfaceVp == NULL)
    return -1;

  pCtx->pVaa->bSceneChangeFlag = pCtx->pVaa->bIdrPeriodFlag = false;

  iSpatialNum = SingleLayerPreprocess (pCtx, kpSrcPic, &m_sScaledPicture);

  return iSpatialNum;
}


SPicture* CWelsPreProcess::GetBestRefPic (EUsageType iUsageType, bool bSceneLtr, EWelsSliceType eSliceType,
    int32_t kiDidx, int32_t iRefTemporalIdx) {
  SVAAFrameInfoExt* pVaaExt = static_cast<SVAAFrameInfoExt*> (m_pEncCtx->pVaa);
  SRefInfoParam* BestRefCandidateParam = (bSceneLtr) ? (& (pVaaExt->sVaaLtrBestRefCandidate[0])) :
                                         (& (pVaaExt->sVaaStrBestRefCandidate[0]));
  return m_pSpatialPic[0][BestRefCandidateParam->iSrcListIdx];
}

int32_t CWelsPreProcess::AnalyzeSpatialPic (sWelsEncCtx* pCtx, const int32_t kiDidx) {
  SWelsSvcCodingParam* pSvcParam = pCtx->pSvcParam;
  bool bCalculateBGD = (pCtx->eSliceType == P_SLICE && pSvcParam->bEnableBackgroundDetection);
  SSpatialLayerInternal* pParamInternal = &pSvcParam->sDependencyLayers[kiDidx];
  int32_t iCurTemporalIdx  = m_uiSpatialLayersInTemporal[kiDidx] - 1;

  int32_t iRefTemporalIdx = (int32_t)g_kuiRefTemporalIdx[pSvcParam->iDecompStages][pParamInternal->iCodingIndex &
                            (pSvcParam->uiGopSize - 1)];
  if (pCtx->uiTemporalId == 0 && pCtx->pLtr[pCtx->uiDependencyId].bReceivedT0LostFlag)
    iRefTemporalIdx = m_uiSpatialLayersInTemporal[kiDidx] + pCtx->pVaa->uiValidLongTermPicIdx;

  SPicture* pCurPic = m_pSpatialPic[kiDidx][iCurTemporalIdx];
  bool bCalculateVar = (pSvcParam->iRCMode >= RC_BITRATE_MODE && pCtx->eSliceType == I_SLICE);

  SPicture* pRefPic = GetBestRefPic (pSvcParam->iUsageType, pCtx->bCurFrameMarkedAsSceneLtr, pCtx->eSliceType, kiDidx,
                                     iRefTemporalIdx);

  VaaCalculation (pCtx->pVaa, pCurPic, pRefPic, false, bCalculateVar, bCalculateBGD);

  return 0;
}


int32_t CWelsPreProcess::UpdateSpatialPictures (sWelsEncCtx* pCtx, SWelsSvcCodingParam* pParam,
    const int8_t iCurTid, const int32_t kiDidx) {
  return 0;

  WelsExchangeSpatialPictures (&m_pLastSpatialPicture[kiDidx][1], &m_pLastSpatialPicture[kiDidx][0]);

  const int32_t kiCurPos = GetCurPicPosition (kiDidx);
  if (iCurTid < kiCurPos || pParam->iDecompStages == 0) {
    if ((iCurTid >= MAX_TEMPORAL_LEVEL) || (kiCurPos > MAX_TEMPORAL_LEVEL)) {
      InitLastSpatialPictures (pCtx);
      return 1;
    }
    if (pCtx->bRefOfCurTidIsLtr[kiDidx][iCurTid]) {
      const int32_t kiAvailableLtrPos = m_uiSpatialLayersInTemporal[kiDidx] + pCtx->pVaa->uiMarkLongTermPicIdx;
      WelsExchangeSpatialPictures (&m_pSpatialPic[kiDidx][kiAvailableLtrPos],
                                   &m_pSpatialPic[kiDidx][iCurTid]);
      pCtx->bRefOfCurTidIsLtr[kiDidx][iCurTid] = false;
    }
    WelsExchangeSpatialPictures (&m_pSpatialPic[kiDidx][kiCurPos],
                                 &m_pSpatialPic[kiDidx][iCurTid]);


  }
  return 0;
}


/*
 *   SingleLayerPreprocess: down sampling if applicable
 *  @return: exact number of spatial layers need to encoder indeed
 */
int32_t CWelsPreProcess::SingleLayerPreprocess (sWelsEncCtx* pCtx, const SSourcePicture* kpSrc,
    Scaled_Picture* pScaledPicture) {
  SWelsSvcCodingParam* pSvcParam    = pCtx->pSvcParam;
  int8_t  iDependencyId             = pSvcParam->iSpatialLayerNum - 1;

  SPicture* pSrcPic                 = NULL; // large
  SPicture* pDstPic                 = NULL; // small
  SSpatialLayerConfig* pDlayerParam = NULL;
  SSpatialLayerInternal* pDlayerParamInternal = NULL;
  int32_t iSpatialNum               = 0;
  int32_t iSrcWidth                 = 0;
  int32_t iSrcHeight                = 0;
  int32_t iTargetWidth              = 0;
  int32_t iTargetHeight             = 0;
  int32_t iTemporalId = 0;
  pDlayerParamInternal = &pSvcParam->sDependencyLayers[iDependencyId];
  pDlayerParam = &pSvcParam->sSpatialLayers[iDependencyId];
  iTargetWidth   = pDlayerParam->iVideoWidth;
  iTargetHeight  = pDlayerParam->iVideoHeight;

  iSrcWidth   = pSvcParam->SUsedPicRect.iWidth;
  iSrcHeight  = pSvcParam->SUsedPicRect.iHeight;
  if (pSvcParam->uiIntraPeriod) {
    pCtx->pVaa->bIdrPeriodFlag = (1 + pDlayerParamInternal->iFrameIndex >= (int32_t)pSvcParam->uiIntraPeriod) ? true :
                                 false;
    if (pCtx->pVaa->bIdrPeriodFlag) {
      WelsLog (& (pCtx->sLogCtx), WELS_LOG_DEBUG,
           "pSvcParam->uiIntraPeriod=%d, pCtx->pVaa->bIdrPeriodFlag=%d",
             pSvcParam->uiIntraPeriod,
             pCtx->pVaa->bIdrPeriodFlag);
    }
  }

  pSrcPic = pScaledPicture->pScaledInputPicture ? pScaledPicture->pScaledInputPicture : GetCurrentOrigFrame (
              iDependencyId);

  WelsMoveMemoryWrapper (pSvcParam, pSrcPic, kpSrc, iSrcWidth, iSrcHeight);


  // different scaling in between input picture and dst highest spatial picture.
  int32_t iShrinkWidth  = iSrcWidth;
  int32_t iShrinkHeight = iSrcHeight;
  pDstPic = pSrcPic;
  if (pScaledPicture->pScaledInputPicture) {
    // for highest downsampling
    pDstPic = GetCurrentOrigFrame (iDependencyId);
    iShrinkWidth = pScaledPicture->iScaledWidth[iDependencyId];
    iShrinkHeight = pScaledPicture->iScaledHeight[iDependencyId];
  }
  DownsamplePadding (pSrcPic, pDstPic, iSrcWidth, iSrcHeight, iShrinkWidth, iShrinkHeight, iTargetWidth, iTargetHeight,
                     false);

  if (pSvcParam->bEnableSceneChangeDetect && !pCtx->pVaa->bIdrPeriodFlag) {
    pCtx->pVaa->eSceneChangeIdc = (pDlayerParamInternal->bEncCurFrmAsIdrFlag ? LARGE_CHANGED_SCENE :
                                   DetectSceneChange (pDstPic));
    pCtx->pVaa->bSceneChangeFlag = (LARGE_CHANGED_SCENE == pCtx->pVaa->eSceneChangeIdc);
  }

  for (int32_t i = 0; i < pSvcParam->iSpatialLayerNum; i++) {
    pDlayerParamInternal = &pSvcParam->sDependencyLayers[i];
    iTemporalId    = pDlayerParamInternal->uiCodingIdx2TemporalId[pDlayerParamInternal->iCodingIndex &
                     (pSvcParam->uiGopSize - 1)];
    if (iTemporalId != INVALID_TEMPORAL_ID) {
      ++ iSpatialNum;
    }
  }
  pDlayerParamInternal = &pSvcParam->sDependencyLayers[iDependencyId];
  iTemporalId    = pDlayerParamInternal->uiCodingIdx2TemporalId[pDlayerParamInternal->iCodingIndex &
                   (pSvcParam->uiGopSize - 1)];
  int iActualSpatialNum = iSpatialNum - 1;
  if (iTemporalId != INVALID_TEMPORAL_ID) {
    WelsUpdateSpatialIdxMap (pCtx, iActualSpatialNum, pDstPic, iDependencyId);
    -- iActualSpatialNum;
  }

  m_pLastSpatialPicture[iDependencyId][1] = GetCurrentOrigFrame (iDependencyId);
  -- iDependencyId;


  // generate other spacial layer
  // pSrc is
  //    -- padded input pic, if downsample should be applied to generate highest layer, [if] block above
  //    -- highest layer, if no downsampling, [else] block above
  return iSpatialNum;

}


/*!
 * \brief   Whether input picture need be scaled?
 */
bool JudgeNeedOfScaling (SWelsSvcCodingParam* pParam, Scaled_Picture* pScaledPicture) {
  const int32_t kiInputPicWidth  = pParam->SUsedPicRect.iWidth;
  const int32_t kiInputPicHeight = pParam->SUsedPicRect.iHeight;
  const int32_t kiDstPicWidth    = pParam->sDependencyLayers[pParam->iSpatialLayerNum - 1].iActualWidth;
  const int32_t kiDstPicHeight   = pParam->sDependencyLayers[pParam->iSpatialLayerNum - 1].iActualHeight;
  bool bNeedDownsampling = true;

  int32_t iSpatialIdx = pParam->iSpatialLayerNum - 1;

  if (kiDstPicWidth >= kiInputPicWidth && kiDstPicHeight >= kiInputPicHeight) {
    bNeedDownsampling = false;
  }

  for (; iSpatialIdx >= 0; iSpatialIdx --) {
    SSpatialLayerInternal* pCurLayer = &pParam->sDependencyLayers[iSpatialIdx];
    int32_t iCurDstWidth             = pCurLayer->iActualWidth;
    int32_t iCurDstHeight            = pCurLayer->iActualHeight;
    int32_t iInputWidthXDstHeight    = kiInputPicWidth * iCurDstHeight;
    int32_t iInputHeightXDstWidth    = kiInputPicHeight * iCurDstWidth;

    if (iInputWidthXDstHeight > iInputHeightXDstWidth) {
      pScaledPicture->iScaledWidth[iSpatialIdx] = WELS_MAX (iCurDstWidth, 4);
      pScaledPicture->iScaledHeight[iSpatialIdx] = WELS_MAX (iInputHeightXDstWidth / kiInputPicWidth, 4);
    } else {
      pScaledPicture->iScaledWidth[iSpatialIdx] = WELS_MAX (iInputWidthXDstHeight / kiInputPicHeight, 4);
      pScaledPicture->iScaledHeight[iSpatialIdx] = WELS_MAX (iCurDstHeight, 4);
    }
  }

  return bNeedDownsampling;
}

int32_t  WelsInitScaledPic (SWelsSvcCodingParam* pParam,  Scaled_Picture*  pScaledPicture, CMemoryAlign* pMemoryAlign) {
  bool bInputPicNeedScaling = JudgeNeedOfScaling (pParam, pScaledPicture);
  if (bInputPicNeedScaling) {
    pScaledPicture->pScaledInputPicture = AllocPicture (pMemoryAlign, pParam->SUsedPicRect.iWidth,
                                          pParam->SUsedPicRect.iHeight, false, 0);
    if (pScaledPicture->pScaledInputPicture == NULL)
      return -1;

    // Avoid valgrind false positives.
    //
    // X86 SIMD downsampling routines may, for convenience, read slightly beyond
    // the input data and into the alignment padding area beyond each line. This
    // causes valgrind to warn about uninitialized values even if these values
    // only affect lanes of a SIMD vector that are effectively never used.
    //
    // Avoid these false positives by zero-initializing the padding area beyond
    // each line of the source buffer used for downsampling.
    SPicture* pPic = pScaledPicture->pScaledInputPicture;
    ClearEndOfLinePadding (pPic->pData[0], pPic->iLineSize[0], pPic->iWidthInPixel, pPic->iHeightInPixel);
    ClearEndOfLinePadding (pPic->pData[1], pPic->iLineSize[1], pPic->iWidthInPixel >> 1, pPic->iHeightInPixel >> 1);
    ClearEndOfLinePadding (pPic->pData[2], pPic->iLineSize[2], pPic->iWidthInPixel >> 1, pPic->iHeightInPixel >> 1);
  }
  return 0;
}

void  FreeScaledPic (Scaled_Picture*  pScaledPicture, CMemoryAlign* pMemoryAlign) {
  if (pScaledPicture->pScaledInputPicture) {
    FreePicture (pMemoryAlign, &pScaledPicture->pScaledInputPicture);
    pScaledPicture->pScaledInputPicture = NULL;
  }
}

int32_t CWelsPreProcess::InitLastSpatialPictures (sWelsEncCtx* pCtx) {
  int32_t iDlayerIndex          = 0;
  for (; iDlayerIndex < MAX_DEPENDENCY_LAYER; iDlayerIndex++) {
    m_pLastSpatialPicture[iDlayerIndex][0] = m_pLastSpatialPicture[iDlayerIndex][1] = NULL;
  }
  return 0;
}




int32_t CWelsPreProcess::DownsamplePadding (SPicture* pSrc, SPicture* pDstPic,  int32_t iSrcWidth, int32_t iSrcHeight,
    int32_t iShrinkWidth, int32_t iShrinkHeight, int32_t iTargetWidth, int32_t iTargetHeight, bool bForceCopy) {
  int32_t iRet = 0;
  SPixMap sSrcPixMap;
  SPixMap sDstPicMap;
  memset (&sSrcPixMap, 0, sizeof (sSrcPixMap));
  memset (&sDstPicMap, 0, sizeof (sDstPicMap));
  sSrcPixMap.pPixel[0]   = pSrc->pData[0];
  sSrcPixMap.pPixel[1]   = pSrc->pData[1];
  sSrcPixMap.pPixel[2]   = pSrc->pData[2];
  sSrcPixMap.sRect.iRectWidth  = iSrcWidth;
  sSrcPixMap.sRect.iRectHeight = iSrcHeight;
  sSrcPixMap.iStride[0]  = pSrc->iLineSize[0];
  sSrcPixMap.iStride[1]  = pSrc->iLineSize[1];
  sSrcPixMap.iStride[2]  = pSrc->iLineSize[2];
  sSrcPixMap.eFormat     = VIDEO_FORMAT_I420;

  if (iSrcWidth != iShrinkWidth || iSrcHeight != iShrinkHeight || bForceCopy) {
    int32_t iMethodIdx = METHOD_DOWNSAMPLE;
    sDstPicMap.pPixel[0]   = pDstPic->pData[0];
    sDstPicMap.pPixel[1]   = pDstPic->pData[1];
    sDstPicMap.pPixel[2]   = pDstPic->pData[2];
    sDstPicMap.sRect.iRectWidth  = iShrinkWidth;
    sDstPicMap.sRect.iRectHeight = iShrinkHeight;
    sDstPicMap.iStride[0]  = pDstPic->iLineSize[0];
    sDstPicMap.iStride[1]  = pDstPic->iLineSize[1];
    sDstPicMap.iStride[2]  = pDstPic->iLineSize[2];
    sDstPicMap.eFormat     = VIDEO_FORMAT_I420;

    if (iSrcWidth != iShrinkWidth || iSrcHeight != iShrinkHeight) {
      iRet = m_pInterfaceVp->Process (iMethodIdx, &sSrcPixMap, &sDstPicMap);
    } else {
      WelsMoveMemory_c (pDstPic->pData[0], pDstPic->pData[1], pDstPic->pData[2], pDstPic->iLineSize[0], pDstPic->iLineSize[1],
                        pSrc->pData[0], pSrc->pData[1], pSrc->pData[2], pSrc->iLineSize[0], pSrc->iLineSize[1],
                        iSrcWidth, iSrcHeight);
    }
  } else {
    memcpy (&sDstPicMap, &sSrcPixMap, sizeof (sDstPicMap)); // confirmed_safe_unsafe_usage
  }

  // get rid of odd line
  iShrinkWidth -= (iShrinkWidth & 1);
  iShrinkHeight -= (iShrinkHeight & 1);
  Padding ((uint8_t*)sDstPicMap.pPixel[0], (uint8_t*)sDstPicMap.pPixel[1], (uint8_t*)sDstPicMap.pPixel[2],
           sDstPicMap.iStride[0], sDstPicMap.iStride[1], iShrinkWidth, iTargetWidth, iShrinkHeight, iTargetHeight);

  return iRet;
}

//*********************************************************************************************************/
void CWelsPreProcess::VaaCalculation (SVAAFrameInfo* pVaaInfo, SPicture* pCurPicture, SPicture* pRefPicture,
                                      bool bCalculateSQDiff, bool bCalculateVar, bool bCalculateBGD) {
  pVaaInfo->sVaaCalcInfo.pCurY = pCurPicture->pData[0];
  pVaaInfo->sVaaCalcInfo.pRefY = pRefPicture->pData[0];
  {
    int32_t iMethodIdx = METHOD_VAA_STATISTICS;
    SPixMap sCurPixMap;
    SPixMap sRefPixMap;
    memset (&sCurPixMap, 0, sizeof (sCurPixMap));
    memset (&sRefPixMap, 0, sizeof (sRefPixMap));
    SVAACalcParam calc_param = {0};

    sCurPixMap.pPixel[0] = pCurPicture->pData[0];
    sCurPixMap.sRect.iRectWidth = pCurPicture->iWidthInPixel;
    sCurPixMap.sRect.iRectHeight = pCurPicture->iHeightInPixel;
    sCurPixMap.iStride[0] = pCurPicture->iLineSize[0];
    sCurPixMap.eFormat = VIDEO_FORMAT_I420;

    sRefPixMap.pPixel[0] = pRefPicture->pData[0];
    sRefPixMap.sRect.iRectWidth = pRefPicture->iWidthInPixel;
    sRefPixMap.sRect.iRectHeight = pRefPicture->iHeightInPixel;
    sRefPixMap.iStride[0] = pRefPicture->iLineSize[0];
    sRefPixMap.eFormat = VIDEO_FORMAT_I420;

    calc_param.iCalcBgd = bCalculateBGD;
    calc_param.pCalcResult = &pVaaInfo->sVaaCalcInfo;

    m_pInterfaceVp->Set (iMethodIdx, &calc_param);
    m_pInterfaceVp->Process (iMethodIdx, &sCurPixMap, &sRefPixMap);
  }
}





void CWelsPreProcess::AnalyzePictureComplexity (sWelsEncCtx* pCtx, SPicture* pCurPicture, SPicture* pRefPicture,
    const int32_t kiDependencyId, const bool bCalculateBGD) {
  SVAAFrameInfoExt* pVaaExt = static_cast<SVAAFrameInfoExt*> (pCtx->pVaa);
  SComplexityAnalysisScreenParam* sComplexityAnalysisParam = &pVaaExt->sComplexityScreenParam;
  SWelsSvcRc* pWelsSvcRc = &pCtx->pWelsSvcRc[kiDependencyId];

  if (pCtx->eSliceType != P_SLICE && pCtx->eSliceType != I_SLICE)
    return;

  memset (pWelsSvcRc->pGomForegroundBlockNum, 0, pWelsSvcRc->iGomSize * sizeof (int32_t));
  memset (pWelsSvcRc->pCurrentFrameGomSad, 0, pWelsSvcRc->iGomSize * sizeof (int32_t));

  sComplexityAnalysisParam->iFrameComplexity = 0;
  sComplexityAnalysisParam->pGomComplexity = pWelsSvcRc->pCurrentFrameGomSad;
  sComplexityAnalysisParam->iIdrFlag = (pCtx->eSliceType == I_SLICE);
  sComplexityAnalysisParam->iMbRowInGom = GOM_H_SCC;
  sComplexityAnalysisParam->sScrollResult.bScrollDetectFlag = false;
  sComplexityAnalysisParam->sScrollResult.iScrollMvX = 0;
  sComplexityAnalysisParam->sScrollResult.iScrollMvY = 0;

  const int32_t iMethodIdx = METHOD_COMPLEXITY_ANALYSIS_SCREEN;
  SPixMap sSrcPixMap;
  SPixMap sRefPixMap;
  memset (&sSrcPixMap, 0, sizeof (SPixMap));
  memset (&sRefPixMap, 0, sizeof (SPixMap));
  int32_t iRet = 0;

  sSrcPixMap.pPixel[0] = pCurPicture->pData[0];
  sSrcPixMap.iStride[0] = pCurPicture->iLineSize[0];
  sSrcPixMap.sRect.iRectWidth = pCurPicture->iWidthInPixel;
  sSrcPixMap.sRect.iRectHeight = pCurPicture->iHeightInPixel;
  sSrcPixMap.eFormat = VIDEO_FORMAT_I420;

  if (pRefPicture != NULL) {
    sRefPixMap.pPixel[0] = pRefPicture->pData[0];
    sRefPixMap.iStride[0] = pRefPicture->iLineSize[0];
    sRefPixMap.sRect.iRectWidth = pRefPicture->iWidthInPixel;
    sRefPixMap.sRect.iRectHeight = pRefPicture->iHeightInPixel;
    sRefPixMap.eFormat = VIDEO_FORMAT_I420;
  }

  iRet = m_pInterfaceVp->Set (iMethodIdx, (void*)sComplexityAnalysisParam);
  iRet = m_pInterfaceVp->Process (iMethodIdx, &sSrcPixMap, &sRefPixMap);
  if (iRet == 0)
    m_pInterfaceVp->Get (iMethodIdx, (void*)sComplexityAnalysisParam);
}


void CWelsPreProcess::InitPixMap (const SPicture* pPicture, SPixMap* pPixMap) {
  pPixMap->pPixel[0] = pPicture->pData[0];
  pPixMap->pPixel[1] = pPicture->pData[1];
  pPixMap->pPixel[2] = pPicture->pData[2];
  pPixMap->iStride[0] = pPicture->iLineSize[0];
  pPixMap->iStride[1] = pPicture->iLineSize[1];
  pPixMap->sRect.iRectWidth = pPicture->iWidthInPixel;
  pPixMap->sRect.iRectHeight = pPicture->iHeightInPixel;

  pPixMap->eFormat = VIDEO_FORMAT_I420;
}

SPicture** CWelsPreProcessScreen::GetReferenceSrcPicList (int32_t iTargetDid) {
  return (&m_pSpatialPic[iTargetDid][1]);
}



void CWelsPreProcessScreen::GetAvailableRefList (SPicture** pSrcPicList, uint8_t iCurTid,
    const int32_t iClosestLtrFrameNum,
    SRefInfoParam* pAvailableRefList, int32_t& iAvailableRefNum, int32_t& iAvailableSceneRefNum) {
  const int32_t iSourcePicNum = m_iAvaliableRefInSpatialPicList;
  if (0 >= iSourcePicNum) {
    iAvailableRefNum = 0;
    iAvailableSceneRefNum = 0;
    return ;
  }
  SPicture* pRefPic = NULL;
  uint8_t uiRefTid = 0;
  iAvailableRefNum = 0;
  iAvailableSceneRefNum = 0;

  //the saving order will be depend on pSrcPicList
  //TODO: use a frame_idx to find the closer ref in time distance, and correctly sort the ref list
  for (int32_t i = iSourcePicNum - 1; i >= 0; --i) {
    pRefPic = pSrcPicList[i];
    if (NULL == pRefPic || !pRefPic->bUsedAsRef) {
      continue;
    }
    uiRefTid = pRefPic->uiTemporalId;

    if (uiRefTid <= iCurTid) {
      pAvailableRefList[iAvailableRefNum].pRefPicture = pRefPic;
      pAvailableRefList[iAvailableRefNum].iSrcListIdx = i + 1; //in SrcList, the idx 0 is reserved for CurPic
      iAvailableRefNum ++;
    }
  }
}


void CWelsPreProcessScreen::InitRefJudgement (SRefJudgement* pRefJudgement) {
  pRefJudgement->iMinFrameComplexity08 = INT_MAX;
  pRefJudgement->iMinFrameComplexity11 = INT_MAX;

  pRefJudgement->iMinFrameQp = INT_MAX;
}
bool CWelsPreProcessScreen::JudgeBestRef (SPicture* pRefPic, const SRefJudgement& sRefJudgement,
    const int64_t iFrameComplexity, const bool bIsClosestLtrFrame) {
  return (bIsClosestLtrFrame ? (iFrameComplexity < sRefJudgement.iMinFrameComplexity11) :
          ((iFrameComplexity < sRefJudgement.iMinFrameComplexity08) || ((iFrameComplexity <= sRefJudgement.iMinFrameComplexity11)
              && (pRefPic->iFrameAverageQp < sRefJudgement.iMinFrameQp))));
}

void CWelsPreProcessScreen::SaveBestRefToJudgement (const int32_t iRefPictureAvQP, const int64_t iComplexity,
    SRefJudgement* pRefJudgement) {
  pRefJudgement->iMinFrameQp = iRefPictureAvQP;
  pRefJudgement->iMinFrameComplexity08 = static_cast<int32_t> (iComplexity * 0.8);
  pRefJudgement->iMinFrameComplexity11 = static_cast<int32_t> (iComplexity * 1.1);
}
void CWelsPreProcessScreen::SaveBestRefToLocal (SRefInfoParam* pRefPicInfo,
    const SSceneChangeResult& sSceneChangeResult,
    SRefInfoParam* pRefSaved) {
  memcpy (pRefSaved, pRefPicInfo, sizeof (SRefInfoParam));
  pRefSaved->pBestBlockStaticIdc = sSceneChangeResult.pStaticBlockIdc;
}

void CWelsPreProcessScreen::SaveBestRefToVaa (SRefInfoParam& sRefSaved, SRefInfoParam* pVaaBestRef) {
  (*pVaaBestRef) = sRefSaved;
}

SPicture* CWelsPreProcessScreen::GetCurrentOrigFrame (int32_t iDIdx) {
  return m_pSpatialPic[iDIdx][0];
}

ESceneChangeIdc CWelsPreProcessScreen::DetectSceneChange (SPicture* pCurPicture, SPicture* pRef) {
  sWelsEncCtx* pCtx = m_pEncCtx;
#define STATIC_SCENE_MOTION_RATIO 0.01f
  SWelsSvcCodingParam* pSvcParam = pCtx->pSvcParam;
  SVAAFrameInfoExt* pVaaExt = static_cast<SVAAFrameInfoExt*> (pCtx->pVaa);
  SSpatialLayerInternal* pParamInternal = &pSvcParam->sDependencyLayers[0];
  if (NULL == pCtx || NULL == pVaaExt || NULL == pCurPicture) {
    return LARGE_CHANGED_SCENE;
  }

  const int32_t iTargetDid = pSvcParam->iSpatialLayerNum - 1;
  if (0 != iTargetDid) {
    return LARGE_CHANGED_SCENE;
  }

  ESceneChangeIdc iVaaFrameSceneChangeIdc = LARGE_CHANGED_SCENE;
  SPicture** pRefPicList = GetReferenceSrcPicList (iTargetDid);
  if (NULL == pRefPicList) {
    return LARGE_CHANGED_SCENE;
  }

  SRefInfoParam sAvailableRefParam[MAX_REF_PIC_COUNT] = { { 0 } };
  int32_t iAvailableRefNum = 0;
  int32_t iAvailableSceneRefNum = 0;

  int32_t iSceneChangeMethodIdx = METHOD_SCENE_CHANGE_DETECTION_SCREEN;
  SSceneChangeResult sSceneChangeResult = {SIMILAR_SCENE, 0, 0, NULL};

  SPixMap sSrcMap = { { 0 } };
  SPixMap sRefMap = { { 0 } };
  SRefJudgement sLtrJudgement;
  SRefJudgement sSceneLtrJudgement;
  SRefInfoParam sLtrSaved = {0};
  SRefInfoParam sSceneLtrSaved = {0};

  int32_t iNumOfLargeChange = 0, iNumOfMediumChangeToLtr = 0;

  bool bIsClosestLtrFrame = false;
  int32_t ret = 1, iScdIdx = 0;

  SPicture* pRefPic = NULL;
  SRefInfoParam* pRefPicInfo = NULL;
  uint8_t*  pCurBlockStaticPointer = NULL;
  SLogContext* pLogCtx = & (pCtx->sLogCtx);
  const int32_t iNegligibleMotionBlocks = (static_cast<int32_t> ((pCurPicture->iWidthInPixel >> 3) *
                                          (pCurPicture->iHeightInPixel >> 3) * STATIC_SCENE_MOTION_RATIO));
  const uint8_t iCurTid = GetTemporalLevel (&pSvcParam->sDependencyLayers[m_pEncCtx->sSpatialIndexMap[0].iDid],
                          pParamInternal->iCodingIndex, pSvcParam->uiGopSize);
  if (iCurTid == INVALID_TEMPORAL_ID) {
    return LARGE_CHANGED_SCENE;
  }
  const int32_t iClosestLtrFrameNum = pCtx->pLtr[iTargetDid].iLastLtrIdx[iCurTid];
  GetAvailableRefList (pRefPicList, iCurTid, iClosestLtrFrameNum, &sAvailableRefParam[0], iAvailableRefNum,
                       iAvailableSceneRefNum);
  //after this build, pAvailableRefList[idx].iSrcListIdx is the idx of the ref in h->spatial_pic
  if (0 == iAvailableRefNum) {
    WelsLog (pLogCtx, WELS_LOG_ERROR, "SceneChangeDetect() iAvailableRefNum=0 but not I.");
    return LARGE_CHANGED_SCENE;
  }

  InitPixMap (pCurPicture, &sSrcMap);
  InitRefJudgement (&sLtrJudgement);
  InitRefJudgement (&sSceneLtrJudgement);

  for (iScdIdx = 0; iScdIdx < iAvailableRefNum; iScdIdx ++) {
    pCurBlockStaticPointer = pVaaExt->pVaaBlockStaticIdc[iScdIdx];
    sSceneChangeResult.eSceneChangeIdc = SIMILAR_SCENE;
    sSceneChangeResult.pStaticBlockIdc = pCurBlockStaticPointer;
    sSceneChangeResult.sScrollResult.bScrollDetectFlag = false;

    pRefPicInfo = & (sAvailableRefParam[iScdIdx]);
    assert (NULL != pRefPicInfo);
    pRefPic = pRefPicInfo->pRefPicture;
    InitPixMap (pRefPic, &sRefMap);

    bIsClosestLtrFrame = (pRefPic->iLongTermPicNum == iClosestLtrFrameNum);
    if (0 == iScdIdx) {
      int32_t ret = 1;
      SScrollDetectionParam* pScrollDetectInfo = & (pVaaExt->sScrollDetectInfo);
      memset (pScrollDetectInfo, 0, sizeof (SScrollDetectionParam));

      int32_t iMethodIdx = METHOD_SCROLL_DETECTION;

      m_pInterfaceVp->Set (iMethodIdx, (void*) (pScrollDetectInfo));
      ret = m_pInterfaceVp->Process (iMethodIdx, &sSrcMap, &sRefMap);

      if (ret == 0) {
        m_pInterfaceVp->Get (iMethodIdx, (void*) (pScrollDetectInfo));
      }
      sSceneChangeResult.sScrollResult = pVaaExt->sScrollDetectInfo;
    }

    m_pInterfaceVp->Set (iSceneChangeMethodIdx, (void*) (&sSceneChangeResult));
    ret = m_pInterfaceVp->Process (iSceneChangeMethodIdx, &sSrcMap, &sRefMap);

    if (ret == 0) {
      m_pInterfaceVp->Get (iSceneChangeMethodIdx, (void*)&sSceneChangeResult);

      const int64_t iFrameComplexity = sSceneChangeResult.iFrameComplexity;
      const int32_t iSceneDetectIdc = sSceneChangeResult.eSceneChangeIdc;
      const int32_t iMotionBlockNum = sSceneChangeResult.iMotionBlockNum;
      if (0 == iScdIdx)
        pVaaExt->iMovingSadPerMb = static_cast<int32_t> (iFrameComplexity / ((pCurPicture->iWidthInPixel >> 4) *
                                   (pCurPicture->iHeightInPixel >> 4)));

      const bool bCurRefIsSceneLtr = pRefPic->bIsSceneLTR;
      const int32_t iRefPicAvQP = pRefPic->iFrameAverageQp;

      //for scene change detection
      iNumOfLargeChange += (static_cast<int32_t> (LARGE_CHANGED_SCENE == iSceneDetectIdc));
      iNumOfMediumChangeToLtr += (static_cast<int32_t> ((bCurRefIsSceneLtr) && (iSceneDetectIdc != SIMILAR_SCENE)));

      //for reference selection
      //this judge can only be saved when iAvailableRefNum==1, which is very limit
      //when LTR is OFF, it can still judge from all available STR
      if (JudgeBestRef (pRefPic, sLtrJudgement, iFrameComplexity, bIsClosestLtrFrame)) {
        SaveBestRefToJudgement (iRefPicAvQP, iFrameComplexity, &sLtrJudgement);
        SaveBestRefToLocal (pRefPicInfo, sSceneChangeResult, &sLtrSaved);
      }
      if (bCurRefIsSceneLtr && JudgeBestRef (pRefPic, sSceneLtrJudgement, iFrameComplexity, bIsClosestLtrFrame)) {
        SaveBestRefToJudgement (iRefPicAvQP, iFrameComplexity, &sSceneLtrJudgement);
        SaveBestRefToLocal (pRefPicInfo, sSceneChangeResult, &sSceneLtrSaved);
      }

      if (iMotionBlockNum <= iNegligibleMotionBlocks) {
        break;
      }
    }
  }

  if (iNumOfLargeChange == iAvailableRefNum) {
    iVaaFrameSceneChangeIdc = LARGE_CHANGED_SCENE;
  } else if ((iNumOfMediumChangeToLtr == iAvailableSceneRefNum) && (0 != iAvailableSceneRefNum)) {
    iVaaFrameSceneChangeIdc = MEDIUM_CHANGED_SCENE;
  } else {
    iVaaFrameSceneChangeIdc = SIMILAR_SCENE;
  }

  WelsLog (pLogCtx, WELS_LOG_DEBUG, "iVaaFrameSceneChangeIdc = %d,codingIdx = %d", iVaaFrameSceneChangeIdc,
           pParamInternal->iCodingIndex);

  SaveBestRefToVaa (sLtrSaved, & (pVaaExt->sVaaStrBestRefCandidate[0]));
  pVaaExt->iVaaBestRefFrameNum = sLtrSaved.pRefPicture->iFrameNum;
  pVaaExt->pVaaBestBlockStaticIdc = sLtrSaved.pBestBlockStaticIdc;

  if (0 < iAvailableSceneRefNum) {
    SaveBestRefToVaa (sSceneLtrSaved, & (pVaaExt->sVaaLtrBestRefCandidate[0]));
  }

  return static_cast<ESceneChangeIdc> (iVaaFrameSceneChangeIdc);
}

void  CWelsPreProcess::Padding (uint8_t* pSrcY, uint8_t* pSrcU, uint8_t* pSrcV, int32_t iStrideY, int32_t iStrideUV,
                                int32_t iActualWidth, int32_t iPaddingWidth, int32_t iActualHeight, int32_t iPaddingHeight) {
  int32_t i;

  if (iPaddingHeight > iActualHeight) {
    for (i = iActualHeight; i < iPaddingHeight; i++) {
      memset (pSrcY + i * iStrideY, 0, iActualWidth);

      if (! (i & 1)) {
        memset (pSrcU + i / 2 * iStrideUV, 0x80, iActualWidth / 2);
        memset (pSrcV + i / 2 * iStrideUV, 0x80, iActualWidth / 2);
      }
    }
  }

  if (iPaddingWidth > iActualWidth) {
    for (i = 0; i < iPaddingHeight; i++) {
      memset (pSrcY + i * iStrideY + iActualWidth, 0, iPaddingWidth - iActualWidth);
      if (! (i & 1)) {
        memset (pSrcU + i / 2 * iStrideUV + iActualWidth / 2, 0x80, (iPaddingWidth - iActualWidth) / 2);
        memset (pSrcV + i / 2 * iStrideUV + iActualWidth / 2, 0x80, (iPaddingWidth - iActualWidth) / 2);
      }
    }
  }
}


int32_t CWelsPreProcess::UpdateBlockIdcForScreen (uint8_t*  pCurBlockStaticPointer, const SPicture* kpRefPic,
    const SPicture* kpSrcPic) {
  int32_t iSceneChangeMethodIdx = METHOD_SCENE_CHANGE_DETECTION_SCREEN;
  SSceneChangeResult sSceneChangeResult = {SIMILAR_SCENE, 0, 0, NULL};
  sSceneChangeResult.pStaticBlockIdc = pCurBlockStaticPointer;
  sSceneChangeResult.sScrollResult.bScrollDetectFlag = false;

  SPixMap sSrcMap = { { 0 } };
  SPixMap sRefMap = { { 0 } };
  InitPixMap (kpSrcPic, &sSrcMap);
  InitPixMap (kpRefPic, &sRefMap);

  m_pInterfaceVp->Set (iSceneChangeMethodIdx, (void*) (&sSceneChangeResult));
  int32_t iRet = m_pInterfaceVp->Process (iSceneChangeMethodIdx, &sSrcMap, &sRefMap);
  if (iRet == 0) {
    m_pInterfaceVp->Get (iSceneChangeMethodIdx, (void*)&sSceneChangeResult);
    return 0;
  }
  return iRet;
}

/*!
* \brief    exchange two picture pData planes
* \param    ppPic1      picture pointer to picture 1
* \param    ppPic2      picture pointer to picture 2
* \return   none
*/
void CWelsPreProcess::WelsExchangeSpatialPictures (SPicture** ppPic1, SPicture** ppPic2) {
  SPicture* tmp = *ppPic1;

  assert (*ppPic1 != *ppPic2);

  *ppPic1 = *ppPic2;
  *ppPic2 = tmp;
}

void CWelsPreProcess::UpdateSrcList (SPicture* pCurPicture, const int32_t kiCurDid, SPicture** pShortRefList,
                                     const uint32_t kuiShortRefCount) {
  SPicture** pRefSrcList = &m_pSpatialPic[kiCurDid][0];

  //pRefSrcList[0] is for current frame
  if (pCurPicture->bUsedAsRef || pCurPicture->bIsLongRef) {
    if (pCurPicture->iPictureType == P_SLICE && pCurPicture->uiTemporalId != 0) {
      for (int iRefIdx = kuiShortRefCount - 1; iRefIdx >= 0; --iRefIdx) {
        WelsExchangeSpatialPictures (&pRefSrcList[iRefIdx + 1],
                                     &pRefSrcList[iRefIdx]);
      }
      m_iAvaliableRefInSpatialPicList = kuiShortRefCount;
    } else {
      WelsExchangeSpatialPictures (&pRefSrcList[0], &pRefSrcList[1]);
      for (int32_t i = MAX_SHORT_REF_COUNT - 1; i > 0  ; --i) {
        if (pRefSrcList[i + 1] != NULL) {
          pRefSrcList[i + 1]->SetUnref();
        }
      }
      m_iAvaliableRefInSpatialPicList = 1;
    }
  }
  (GetCurrentOrigFrame (kiCurDid))->SetUnref();
}

//TODO: may opti later
//TODO: not use this func?
void* WelsMemcpy (void* dst, const void* kpSrc, uint32_t uiSize) {
  return ::memcpy (dst, kpSrc, uiSize);
}

//i420_to_i420_c
void  WelsMoveMemory_c (uint8_t* pDstY, uint8_t* pDstU, uint8_t* pDstV,  int32_t iDstStrideY, int32_t iDstStrideUV,
                        uint8_t* pSrcY, uint8_t* pSrcU, uint8_t* pSrcV, int32_t iSrcStrideY, int32_t iSrcStrideUV, int32_t iWidth,
                        int32_t iHeight) {
  int32_t   iWidth2 = iWidth >> 1;
  int32_t   iHeight2 = iHeight >> 1;
  int32_t   j;

  for (j = iHeight; j; j--) {
    WelsMemcpy (pDstY, pSrcY, iWidth);
    pDstY += iDstStrideY;
    pSrcY += iSrcStrideY;
  }

  for (j = iHeight2; j; j--) {
    WelsMemcpy (pDstU, pSrcU, iWidth2);
    WelsMemcpy (pDstV, pSrcV, iWidth2);
    pDstU += iDstStrideUV;
    pDstV += iDstStrideUV;
    pSrcU += iSrcStrideUV;
    pSrcV += iSrcStrideUV;
  }
}

void  CWelsPreProcess::WelsMoveMemoryWrapper (SWelsSvcCodingParam* pSvcParam, SPicture* pDstPic,
    const SSourcePicture* kpSrc,
    const int32_t kiTargetWidth, const int32_t kiTargetHeight) {
  if (VIDEO_FORMAT_I420 != (kpSrc->iColorFormat & (~VIDEO_FORMAT_VFlip)))
    return;

  int32_t  iSrcWidth       = kpSrc->iPicWidth;
  int32_t  iSrcHeight      = kpSrc->iPicHeight;

  if (iSrcHeight > kiTargetHeight) iSrcHeight = kiTargetHeight;
  if (iSrcWidth > kiTargetWidth)   iSrcWidth  = kiTargetWidth;

  // copy from fr26 to fix the odd uiSize failed issue
  if (iSrcWidth & 0x1)  -- iSrcWidth;
  if (iSrcHeight & 0x1) -- iSrcHeight;

  const int32_t kiSrcTopOffsetY = pSvcParam->SUsedPicRect.iTop;
  const int32_t kiSrcTopOffsetUV = (kiSrcTopOffsetY >> 1);
  const int32_t kiSrcLeftOffsetY = pSvcParam->SUsedPicRect.iLeft;
  const int32_t kiSrcLeftOffsetUV = (kiSrcLeftOffsetY >> 1);
  int32_t  iSrcOffset[3]       = {0, 0, 0};
  iSrcOffset[0] = kpSrc->iStride[0] * kiSrcTopOffsetY + kiSrcLeftOffsetY;
  iSrcOffset[1] = kpSrc->iStride[1] * kiSrcTopOffsetUV + kiSrcLeftOffsetUV ;
  iSrcOffset[2] = kpSrc->iStride[2] * kiSrcTopOffsetUV + kiSrcLeftOffsetUV;

  uint8_t* pSrcY = kpSrc->pData[0] + iSrcOffset[0];
  uint8_t* pSrcU = kpSrc->pData[1] + iSrcOffset[1];
  uint8_t* pSrcV = kpSrc->pData[2] + iSrcOffset[2];
  const int32_t kiSrcStrideY = kpSrc->iStride[0];
  const int32_t kiSrcStrideUV = kpSrc->iStride[1];

  uint8_t* pDstY = pDstPic->pData[0];
  uint8_t* pDstU = pDstPic->pData[1];
  uint8_t* pDstV = pDstPic->pData[2];
  const int32_t kiDstStrideY = pDstPic->iLineSize[0];
  const int32_t kiDstStrideUV = pDstPic->iLineSize[1];

  if (pSrcY) {
    if (iSrcWidth <= 0 || iSrcHeight <= 0 || (iSrcWidth * iSrcHeight > (MAX_MBS_PER_FRAME << 8)))
      return;
    if (kiSrcTopOffsetY >= iSrcHeight || kiSrcLeftOffsetY >= iSrcWidth || iSrcWidth > kiSrcStrideY)
      return;
  }
  if (pDstY) {
    if (kiTargetWidth <= 0 || kiTargetHeight <= 0 || (kiTargetWidth * kiTargetHeight > (MAX_MBS_PER_FRAME << 8)))
      return;
    if (kiTargetWidth > kiDstStrideY)
      return;
  }

  if (pSrcY == NULL || pSrcU == NULL || pSrcV == NULL || pDstY == NULL || pDstU == NULL || pDstV == NULL
      || (iSrcWidth & 1) || (iSrcHeight & 1)) {
  } else {
    //i420_to_i420_c
    WelsMoveMemory_c (pDstY,  pDstU,  pDstV,  kiDstStrideY, kiDstStrideUV,
                      pSrcY,  pSrcU,  pSrcV, kiSrcStrideY, kiSrcStrideUV, iSrcWidth, iSrcHeight);

    //in VP Process
    if (kiTargetWidth > iSrcWidth || kiTargetHeight > iSrcHeight) {
      Padding (pDstY, pDstU, pDstV, kiDstStrideY, kiDstStrideUV, iSrcWidth, kiTargetWidth, iSrcHeight, kiTargetHeight);
    }
  }

}



//*********************************************************************************************************/
} // namespace WelsEnc
