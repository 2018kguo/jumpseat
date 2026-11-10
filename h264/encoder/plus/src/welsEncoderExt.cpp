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

#include <assert.h>
#include "welsEncoderExt.h"
#include "welsCodecTrace.h"
#include "typedefs.h"
#include "wels_const.h"
#include "utils.h"
#include "macros.h"
#include "version.h"
#include "crt_util_safe_x.h" // Safe CRT routines like util for cross platforms
#include "ref_list_mgr_svc.h"
#include "codec_ver.h"

#include <time.h>
#include <measure_time.h>
#include <sys/time.h>

namespace WelsEnc {

/*
 *  CWelsH264SVCEncoder class implementation
 */
CWelsH264SVCEncoder::CWelsH264SVCEncoder()
  : m_pEncContext (NULL),
    m_pWelsTrace (NULL),
    m_iMaxPicWidth (0),
    m_iMaxPicHeight (0),
    m_iCspInternal (0),
    m_bInitialFlag (false) {



  InitEncoder();
}

CWelsH264SVCEncoder::~CWelsH264SVCEncoder() {
  if (m_pWelsTrace) {
    WelsLog (&m_pWelsTrace->m_sLogCtx, WELS_LOG_INFO, "CWelsH264SVCEncoder::~CWelsH264SVCEncoder()");
  }



  Uninitialize();

  if (m_pWelsTrace) {
    delete m_pWelsTrace;
    m_pWelsTrace = NULL;
  }
}

void CWelsH264SVCEncoder::InitEncoder (void) {

  m_pWelsTrace = new welsCodecTrace();
  if (m_pWelsTrace == NULL) {
    return;
  }
  m_pWelsTrace->SetCodecInstance (this);
}

/* Interfaces override from ISVCEncoder */

int CWelsH264SVCEncoder::GetDefaultParams (SEncParamExt* argv) {
  SWelsSvcCodingParam::FillDefault (*argv);
  return cmResultSuccess;
}

/*
 *  SVC Encoder Initialization
 */
int CWelsH264SVCEncoder::Initialize (const SEncParamBase* argv) {
  return cmInitParaError; // jumpseat: InitializeExt only
}

int CWelsH264SVCEncoder::InitializeExt (const SEncParamExt* argv) {
  if (m_pWelsTrace == NULL) {
    return cmMallocMemeError;
  }

  WelsLog (&m_pWelsTrace->m_sLogCtx, WELS_LOG_INFO, "CWelsH264SVCEncoder::InitEncoder(), openh264 codec version = %s",
           VERSION_NUMBER);

  if (NULL == argv) {
    WelsLog (&m_pWelsTrace->m_sLogCtx, WELS_LOG_ERROR, "CWelsH264SVCEncoder::InitializeExt(), invalid argv= 0x%p",
             argv);
    return cmInitParaError;
  }

  SWelsSvcCodingParam sConfig;
  // Convert SEncParamExt into WelsSVCParamConfig here..
  if (sConfig.ParamTranscode (*argv)) {
    WelsLog (&m_pWelsTrace->m_sLogCtx, WELS_LOG_ERROR,
             "CWelsH264SVCEncoder::InitializeExt(), parameter_translation failed.");
    Uninitialize();
    return cmInitParaError;
  }

  return InitializeInternal (&sConfig);
}

int CWelsH264SVCEncoder::InitializeInternal (SWelsSvcCodingParam* pCfg) {
  if (NULL == pCfg) {
    WelsLog (&m_pWelsTrace->m_sLogCtx, WELS_LOG_ERROR, "CWelsH264SVCEncoder::Initialize(), invalid argv= 0x%p.",
             pCfg);
    return cmInitParaError;
  }

  if (m_bInitialFlag) {
    WelsLog (&m_pWelsTrace->m_sLogCtx, WELS_LOG_WARNING,
             "CWelsH264SVCEncoder::Initialize(), reinitialize, m_bInitialFlag= %d.",
             m_bInitialFlag);
    Uninitialize();
  }
  // Check valid parameters
  const int32_t iNumOfLayers = pCfg->iSpatialLayerNum;
  if (iNumOfLayers < 1 || iNumOfLayers > MAX_DEPENDENCY_LAYER) {
    WelsLog (&m_pWelsTrace->m_sLogCtx, WELS_LOG_ERROR,
             "CWelsH264SVCEncoder::Initialize(), invalid iSpatialLayerNum= %d, valid at range of [1, %d].", iNumOfLayers,
             MAX_DEPENDENCY_LAYER);
    Uninitialize();
    return cmInitParaError;
  }
  if (pCfg->iTemporalLayerNum < 1)
    pCfg->iTemporalLayerNum = 1;
  if (pCfg->iTemporalLayerNum > MAX_TEMPORAL_LEVEL) {
    WelsLog (&m_pWelsTrace->m_sLogCtx, WELS_LOG_ERROR,
             "CWelsH264SVCEncoder::Initialize(), invalid iTemporalLayerNum= %d, valid at range of [1, %d].",
             pCfg->iTemporalLayerNum, MAX_TEMPORAL_LEVEL);
    Uninitialize();
    return cmInitParaError;
  }

  // assert( cfg.uiGopSize >= 1 && ( cfg.uiIntraPeriod && (cfg.uiIntraPeriod % cfg.uiGopSize) == 0) );

  if (pCfg->uiGopSize < 1 || pCfg->uiGopSize > MAX_GOP_SIZE) {
    WelsLog (&m_pWelsTrace->m_sLogCtx, WELS_LOG_ERROR,
             "CWelsH264SVCEncoder::Initialize(), invalid uiGopSize= %d, valid at range of [1, %d].", pCfg->uiGopSize,
             MAX_GOP_SIZE);
    Uninitialize();
    return cmInitParaError;
  }

  if (!WELS_POWER2_IF (pCfg->uiGopSize)) {
    WelsLog (&m_pWelsTrace->m_sLogCtx, WELS_LOG_ERROR,
             "CWelsH264SVCEncoder::Initialize(), invalid uiGopSize= %d, valid at range of [1, %d] and yield to power of 2.",
             pCfg->uiGopSize, MAX_GOP_SIZE);
    Uninitialize();
    return cmInitParaError;
  }

  if (pCfg->uiIntraPeriod && pCfg->uiIntraPeriod < pCfg->uiGopSize) {
    WelsLog (&m_pWelsTrace->m_sLogCtx, WELS_LOG_ERROR,
             "CWelsH264SVCEncoder::Initialize(), invalid uiIntraPeriod= %d, valid in case it equals to 0 for unlimited intra period or exceeds specified uiGopSize= %d.",
             pCfg->uiIntraPeriod, pCfg->uiGopSize);
    Uninitialize();
    return cmInitParaError;
  }

  if ((pCfg->uiIntraPeriod && (pCfg->uiIntraPeriod & (pCfg->uiGopSize - 1)) != 0)) {
    WelsLog (&m_pWelsTrace->m_sLogCtx, WELS_LOG_ERROR,
             "CWelsH264SVCEncoder::Initialize(), invalid uiIntraPeriod= %d, valid in case it equals to 0 for unlimited intra period or exceeds specified uiGopSize= %d also multiple of it.",
             pCfg->uiIntraPeriod, pCfg->uiGopSize);
    Uninitialize();
    return cmInitParaError;
  }
  if (pCfg->iUsageType == SCREEN_CONTENT_REAL_TIME) {
    if (pCfg->bEnableLongTermReference) {
      pCfg->iLTRRefNum = LONG_TERM_REF_NUM_SCREEN;
      if (pCfg->iNumRefFrame == AUTO_REF_PIC_COUNT)
        pCfg->iNumRefFrame = WELS_MAX (1, WELS_LOG2 (pCfg->uiGopSize)) + pCfg->iLTRRefNum;
    } else {
      pCfg->iLTRRefNum = 0;
      if (pCfg->iNumRefFrame == AUTO_REF_PIC_COUNT)
        pCfg->iNumRefFrame = WELS_MAX (1, pCfg->uiGopSize >> 1);
    }
  } else {
    pCfg->iLTRRefNum = pCfg->bEnableLongTermReference ? LONG_TERM_REF_NUM : 0;
    if (pCfg->iNumRefFrame == AUTO_REF_PIC_COUNT) {
      pCfg->iNumRefFrame = ((pCfg->uiGopSize >> 1) > 1) ? ((pCfg->uiGopSize >> 1) + pCfg->iLTRRefNum) :
                           (MIN_REF_PIC_COUNT + pCfg->iLTRRefNum);
      pCfg->iNumRefFrame = WELS_CLIP3 (pCfg->iNumRefFrame, MIN_REF_PIC_COUNT, MAX_REFERENCE_PICTURE_COUNT_NUM_CAMERA);
    }
  }

  if (pCfg->iLtrMarkPeriod == 0) {
    pCfg->iLtrMarkPeriod = 30;
  }

  const int32_t kiDecStages = WELS_LOG2 (pCfg->uiGopSize);
  pCfg->iTemporalLayerNum        = (int8_t) (1 + kiDecStages);
  pCfg->iLoopFilterAlphaC0Offset = WELS_CLIP3 (pCfg->iLoopFilterAlphaC0Offset, -6, 6);
  pCfg->iLoopFilterBetaOffset    = WELS_CLIP3 (pCfg->iLoopFilterBetaOffset, -6, 6);

  // decide property list size between INIT_TYPE_PARAMETER_BASED/INIT_TYPE_CONFIG_BASED
  m_iMaxPicWidth  = pCfg->iPicWidth;
  m_iMaxPicHeight = pCfg->iPicHeight;

  if (WelsInitEncoderExt (&m_pEncContext, pCfg, &m_pWelsTrace->m_sLogCtx, NULL)) {
    WelsLog (&m_pWelsTrace->m_sLogCtx, WELS_LOG_ERROR, "CWelsH264SVCEncoder::Initialize(), WelsInitEncoderExt failed.");
    WelsLog (&m_pWelsTrace->m_sLogCtx, WELS_LOG_DEBUG,
             "Problematic Input Base Param: iUsageType=%d, Resolution=%dx%d, FR=%f, TLayerNum=%d, DLayerNum=%d",
             pCfg->iUsageType, pCfg->iPicWidth, pCfg->iPicHeight, pCfg->fMaxFrameRate, pCfg->iTemporalLayerNum,
             pCfg->iSpatialLayerNum);
    Uninitialize();
    return cmInitParaError;
  }

  m_bInitialFlag  = true;

  return cmResultSuccess;
}

/*
 *  SVC Encoder Uninitialization
 */
int32_t CWelsH264SVCEncoder::Uninitialize() {
  if (!m_bInitialFlag) {
    return 0;
  }

  WelsLog (&m_pWelsTrace->m_sLogCtx, WELS_LOG_INFO, "CWelsH264SVCEncoder::Uninitialize(), openh264 codec version = %s.",
           VERSION_NUMBER);

  if (NULL != m_pEncContext) {
    WelsUninitEncoderExt (&m_pEncContext);
    m_pEncContext = NULL;
  }

  m_bInitialFlag = false;

  return 0;
}


/*
 *  SVC core encoding
 */
int CWelsH264SVCEncoder::EncodeFrame (const SSourcePicture* kpSrcPic, SFrameBSInfo* pBsInfo) {
  if (! (kpSrcPic && m_bInitialFlag && pBsInfo)) {
    WelsLog (&m_pWelsTrace->m_sLogCtx, WELS_LOG_ERROR, "CWelsH264SVCEncoder::EncodeFrame(), cmInitParaError.");
    return cmInitParaError;
  }
  if (kpSrcPic->iColorFormat != videoFormatI420) {
    WelsLog (&m_pWelsTrace->m_sLogCtx, WELS_LOG_ERROR, "CWelsH264SVCEncoder::EncodeFrame(), wrong iColorFormat %d",
             kpSrcPic->iColorFormat);
    return cmInitParaError;
  }

  const int32_t kiEncoderReturn = EncodeFrameInternal (kpSrcPic, pBsInfo);

  if (kiEncoderReturn != cmResultSuccess) {
    WelsLog (&m_pWelsTrace->m_sLogCtx, WELS_LOG_ERROR, "CWelsH264SVCEncoder::EncodeFrame(), kiEncoderReturn %d",
             kiEncoderReturn);
    return kiEncoderReturn;
  }


  return kiEncoderReturn;
}


int CWelsH264SVCEncoder ::EncodeFrameInternal (const SSourcePicture*  pSrcPic, SFrameBSInfo* pBsInfo) {

  if ((pSrcPic->iPicWidth < 16) || ((pSrcPic->iPicHeight < 16))) {
    WelsLog (&m_pWelsTrace->m_sLogCtx, WELS_LOG_ERROR, "Don't support width(%d) or height(%d) which is less than 16!",
             pSrcPic->iPicWidth, pSrcPic->iPicHeight);
    return cmUnsupportedData;
  }

  const int32_t kiEncoderReturn = WelsEncoderEncodeExt (m_pEncContext, pBsInfo, pSrcPic);
  if ((kiEncoderReturn == ENC_RETURN_MEMALLOCERR) || (kiEncoderReturn == ENC_RETURN_MEMOVERFLOWFOUND)) {
    WelsLog (&m_pWelsTrace->m_sLogCtx, WELS_LOG_DEBUG, "CWelsH264SVCEncoder::EncodeFrame() not succeed, err=%d",
             kiEncoderReturn);
    WelsUninitEncoderExt (&m_pEncContext);
    return cmMallocMemeError;
  } else if ((kiEncoderReturn != ENC_RETURN_SUCCESS) && (kiEncoderReturn == ENC_RETURN_CORRECTED)) {
    WelsLog (&m_pWelsTrace->m_sLogCtx, WELS_LOG_ERROR, "unexpected return(%d) from EncodeFrameInternal()!",
             kiEncoderReturn);
    return cmUnknownReason;
  }

  ///////////////////for test
#ifdef DUMP_SRC_PICTURE
  DumpSrcPicture (pSrcPic, m_pEncContext->pSvcParam->iUsageType);
#endif // DUMP_SRC_PICTURE

  return cmResultSuccess;

}

int CWelsH264SVCEncoder::EncodeParameterSets (SFrameBSInfo* pBsInfo) {
  return cmInitParaError; // jumpseat: parameter sets come with each IDR
}

/*
 *  Force key frame
 */
int CWelsH264SVCEncoder::ForceIntraFrame (bool bIDR, int iLayerId) {
  if (bIDR) {
    if (! (m_pEncContext && m_bInitialFlag)) {
      return 1;
    }

    ForceCodingIDR (m_pEncContext, iLayerId);
  } else {
    WelsLog (&m_pWelsTrace->m_sLogCtx, WELS_LOG_INFO,
             "CWelsH264SVCEncoder::ForceIntraFrame(),nothing to do as bIDR set to false");
  }

  return 0;
}



/************************************************************************
* InDataFormat, IDRInterval, SVC Encode Param, Frame Rate, Bitrate,..
************************************************************************/
int CWelsH264SVCEncoder::SetOption (ENCODER_OPTION eOptionId, void* pOption) {
  if (NULL == pOption) {
    return cmInitParaError;
  }

  if (NULL == m_pEncContext || false == m_bInitialFlag) {
    return cmInitExpected;
  }

  switch (eOptionId) {
  case ENCODER_OPTION_DATAFORMAT: { // Input color space
    int32_t iValue = * ((int32_t*)pOption);
    int32_t iColorspace = iValue;
    if (iColorspace == 0) {
      return cmInitParaError;
    }

    m_iCspInternal = iColorspace;
    WelsLog (&m_pWelsTrace->m_sLogCtx, WELS_LOG_INFO,
             "CWelsH264SVCEncoder::SetOption():ENCODER_OPTION_DATAFORMAT, m_iCspInternal = 0x%x", m_iCspInternal);
  }
  break;

  case ENCODER_OPTION_BITRATE: { // Target bit-rate
    SBitrateInfo* pInfo = (static_cast<SBitrateInfo*> (pOption));
    int32_t iBitrate = pInfo->iBitrate;
    if (iBitrate <= 0) {
      WelsLog (&m_pWelsTrace->m_sLogCtx, WELS_LOG_ERROR,
               "CWelsH264SVCEncoder::SetOption():ENCODER_OPTION_BITRATE,iBitrate = %d",
               iBitrate);
      return cmInitParaError;
    }
    iBitrate = WELS_CLIP3 (iBitrate, MIN_BIT_RATE, MAX_BIT_RATE);
    switch (pInfo->iLayer) {
    case SPATIAL_LAYER_ALL:
      m_pEncContext->pSvcParam->iTargetBitrate = iBitrate;
      break;
    case SPATIAL_LAYER_0:
      m_pEncContext->pSvcParam->sSpatialLayers[0].iSpatialBitrate = iBitrate;
      break;
    default:
      WelsLog (&m_pWelsTrace->m_sLogCtx, WELS_LOG_ERROR,
               "CWelsH264SVCEncoder::SetOption():ENCODER_OPTION_BITRATE,iLayer = %d",
               pInfo->iLayer);
      return cmInitParaError;
      break;
    }
    //adjust to valid range
    if (WelsEncoderApplyBitRate (&m_pWelsTrace->m_sLogCtx, m_pEncContext->pSvcParam, pInfo->iLayer)) {
      WelsLog (&m_pWelsTrace->m_sLogCtx, WELS_LOG_ERROR,
               "CWelsH264SVCEncoder::SetOption():ENCODER_OPTION_BITRATE layerId= %d,iSpatialBitrate = %d", pInfo->iLayer, iBitrate);
      return cmInitParaError;
    } else {
      WelsLog (&m_pWelsTrace->m_sLogCtx, WELS_LOG_INFO,
               "CWelsH264SVCEncoder::SetOption():ENCODER_OPTION_BITRATE layerId= %d,iSpatialBitrate = %d", pInfo->iLayer, iBitrate);

    }

  }
  break;
  case ENCODER_OPTION_MAX_BITRATE: { // Target bit-rate
    SBitrateInfo* pInfo = (static_cast<SBitrateInfo*> (pOption));
    int32_t iBitrate = pInfo->iBitrate;
    if (iBitrate <= 0) {
      WelsLog (&m_pWelsTrace->m_sLogCtx, WELS_LOG_ERROR,
               "CWelsH264SVCEncoder::SetOption():ENCODER_OPTION_MAX_BITRATE,iBitrate = %d",
               iBitrate);
      return cmInitParaError;
    }
    iBitrate = WELS_CLIP3 (iBitrate, MIN_BIT_RATE, MAX_BIT_RATE);
    switch (pInfo->iLayer) {
    case SPATIAL_LAYER_ALL:
      m_pEncContext->pSvcParam->iMaxBitrate = iBitrate;
      break;
    case SPATIAL_LAYER_0:
      m_pEncContext->pSvcParam->sSpatialLayers[0].iMaxSpatialBitrate = iBitrate;
      break;
    default:
      WelsLog (&m_pWelsTrace->m_sLogCtx, WELS_LOG_ERROR,
               "CWelsH264SVCEncoder::SetOption():ENCODER_OPTION_MAX_BITRATE,iLayer = %d",
               pInfo->iLayer);
      return cmInitParaError;
      break;
    }
    //adjust to valid range
    if (WelsEncoderApplyBitRate (&m_pWelsTrace->m_sLogCtx, m_pEncContext->pSvcParam, pInfo->iLayer)) {
      WelsLog (&m_pWelsTrace->m_sLogCtx, WELS_LOG_ERROR,
               "CWelsH264SVCEncoder::SetOption():ENCODER_OPTION_BITRATE layerId= %d,iMaxSpatialBitrate = %d", pInfo->iLayer, iBitrate);
      return cmInitParaError;
    } else {
      WelsLog (&m_pWelsTrace->m_sLogCtx, WELS_LOG_INFO,
               "CWelsH264SVCEncoder::SetOption():ENCODER_OPTION_BITRATE layerId= %d,iMaxSpatialBitrate = %d", pInfo->iLayer, iBitrate);

    }
  }
  break;

  default:
    return cmInitParaError;
  }

  return 0;
}

int CWelsH264SVCEncoder::GetOption (ENCODER_OPTION eOptionId, void* pOption) {
  return cmInitParaError; // jumpseat: no options to read
}

}

using namespace WelsEnc;

int32_t WelsCreateSVCEncoder (ISVCEncoder** ppEncoder) {
  if ((*ppEncoder = new CWelsH264SVCEncoder()) != NULL) {
    return 0;
  }

  return 1;
}

void WelsDestroySVCEncoder (ISVCEncoder* pEncoder) {
  CWelsH264SVCEncoder* pSVCEncoder = (CWelsH264SVCEncoder*)pEncoder;

  if (pSVCEncoder) {
    delete pSVCEncoder;
    pSVCEncoder = NULL;
  }
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
