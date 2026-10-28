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
 * \file    encoder_ext.c
 *
 * \brief   core encoder for SVC
 *
 * \date    7/24/2009 Created
 *
 *************************************************************************************
 */

#include "encoder.h"
#include "cpu.h"
#include "utils.h"
#include "svc_enc_golomb.h"
#include "au_set.h"
#include "picture_handle.h"
#include "svc_base_layer_md.h"
#include "svc_encode_slice.h"
#include "svc_mode_decision.h"
#include "decode_mb_aux.h"
#include "ref_list_mgr_svc.h"
#include "ls_defines.h"
#include "crt_util_safe_x.h" // Safe CRT routines like utils for cross platforms
#include "measure_time.h"
#include "svc_set_mb_syn.h"

namespace WelsEnc {




int32_t WelsBitRateVerification (SLogContext* pLogCtx, SSpatialLayerConfig* pLayerParam, int32_t iLayerId) {
  if ((pLayerParam->iSpatialBitrate <= 0)
      || (static_cast<float> (pLayerParam->iSpatialBitrate) < pLayerParam->fFrameRate)) {
    WelsLog (pLogCtx, WELS_LOG_ERROR, "Invalid bitrate settings in layer %d, bitrate= %d at FrameRate(%f)", iLayerId,
             pLayerParam->iSpatialBitrate, pLayerParam->fFrameRate);
    return ENC_RETURN_UNSUPPORTED_PARA;
  }

  // deal with LEVEL_MAX_BR and MAX_BR setting
  const SLevelLimits* pCurLevelLimit = g_ksLevelLimits;
  while ((pCurLevelLimit->uiLevelIdc != LEVEL_5_2) && (pCurLevelLimit->uiLevelIdc != pLayerParam->uiLevelIdc))
    pCurLevelLimit++;
  const int32_t iLevelMaxBitrate = pCurLevelLimit->uiMaxBR * CpbBrNalFactor;
  const int32_t iLevel52MaxBitrate = g_ksLevelLimits[LEVEL_NUMBER - 1].uiMaxBR * CpbBrNalFactor;
  if (UNSPECIFIED_BIT_RATE != iLevelMaxBitrate) {
    if ((pLayerParam->iMaxSpatialBitrate == UNSPECIFIED_BIT_RATE)
        || (pLayerParam->iMaxSpatialBitrate > iLevel52MaxBitrate)) {
      pLayerParam->iMaxSpatialBitrate = iLevelMaxBitrate;
      WelsLog (pLogCtx, WELS_LOG_INFO,
               "Current MaxSpatialBitrate is invalid (UNSPECIFIED_BIT_RATE or larger than LEVEL5_2) but level setting is valid, set iMaxSpatialBitrate to %d from level (%d)",
               pLayerParam->iMaxSpatialBitrate, pLayerParam->uiLevelIdc);
    } else if (pLayerParam->iMaxSpatialBitrate > iLevelMaxBitrate) {
      ELevelIdc iCurLevel = pLayerParam->uiLevelIdc;
      WelsAdjustLevel (pLayerParam, pCurLevelLimit);
      WelsLog (pLogCtx, WELS_LOG_INFO,
               "LevelIdc is changed from (%d) to (%d) according to the iMaxSpatialBitrate(%d)",
               iCurLevel, pLayerParam->uiLevelIdc, pLayerParam->iMaxSpatialBitrate);
    }
  } else if ((pLayerParam->iMaxSpatialBitrate != UNSPECIFIED_BIT_RATE)
             && (pLayerParam->iMaxSpatialBitrate > iLevel52MaxBitrate)) {
    // no level limitation, just need to check if iMaxSpatialBitrate is too big from reasonable
    WelsLog (pLogCtx, WELS_LOG_WARNING,
             "No LevelIdc setting and iMaxSpatialBitrate (%d) is considered too big to be valid, changed to UNSPECIFIED_BIT_RATE",
             pLayerParam->iMaxSpatialBitrate);
    pLayerParam->iMaxSpatialBitrate = UNSPECIFIED_BIT_RATE;
  }

  // deal with iSpatialBitrate and iMaxSpatialBitrate setting
  if (pLayerParam->iMaxSpatialBitrate != UNSPECIFIED_BIT_RATE) {
    if (pLayerParam->iMaxSpatialBitrate == pLayerParam->iSpatialBitrate) {
      WelsLog (pLogCtx, WELS_LOG_INFO,
               "Setting MaxSpatialBitrate (%d) the same at SpatialBitrate (%d) will make the actual bit rate lower than SpatialBitrate",
               pLayerParam->iMaxSpatialBitrate, pLayerParam->iSpatialBitrate);
    } else if (pLayerParam->iMaxSpatialBitrate < pLayerParam->iSpatialBitrate) {
      WelsLog (pLogCtx, WELS_LOG_ERROR,
               "MaxSpatialBitrate (%d) should be larger than SpatialBitrate (%d), considering it as error setting",
               pLayerParam->iMaxSpatialBitrate, pLayerParam->iSpatialBitrate);
      return ENC_RETURN_UNSUPPORTED_PARA;
    }
  }
  return ENC_RETURN_SUCCESS;
}

void CheckProfileSetting (SLogContext* pLogCtx, SWelsSvcCodingParam* pParam, int32_t iLayer, EProfileIdc uiProfileIdc) {
  SSpatialLayerConfig* pLayerInfo = &pParam->sSpatialLayers[iLayer];
  pLayerInfo->uiProfileIdc = uiProfileIdc;
  if (iLayer == SPATIAL_LAYER_0) {
    if ((uiProfileIdc != PRO_BASELINE) && (uiProfileIdc != PRO_MAIN) && (uiProfileIdc != PRO_HIGH)) {
      WelsLog (pLogCtx, WELS_LOG_WARNING, "layerId(%d) doesn't support profile(%d), change to UNSPECIFIC profile", iLayer,
               uiProfileIdc);
      pLayerInfo->uiProfileIdc = PRO_UNKNOWN;
    }
  } else {
    if ((uiProfileIdc != PRO_SCALABLE_BASELINE) && (uiProfileIdc != PRO_SCALABLE_HIGH)) {
      pLayerInfo->uiProfileIdc = PRO_SCALABLE_BASELINE;
      WelsLog (pLogCtx, WELS_LOG_WARNING, "layerId(%d) doesn't support profile(%d), change to scalable baseline profile",
               iLayer, uiProfileIdc);
    }
  }
}
void CheckLevelSetting (SLogContext* pLogCtx, SWelsSvcCodingParam* pParam, int32_t iLayer, ELevelIdc uiLevelIdc) {
  SSpatialLayerConfig* pLayerInfo = &pParam->sSpatialLayers[iLayer];
  pLayerInfo->uiLevelIdc = LEVEL_UNKNOWN;
  int32_t iLevelIdx = LEVEL_NUMBER - 1;
  do {
    if (g_ksLevelLimits[iLevelIdx].uiLevelIdc == uiLevelIdc) {
      pLayerInfo->uiLevelIdc = uiLevelIdc;
      break;
    }
    iLevelIdx--;
  } while (iLevelIdx >= 0);
}

int32_t SliceArgumentValidationFixedSliceMode (SLogContext* pLogCtx,
    SSliceArgument* pSliceArgument, const RC_MODES  kiRCMode,
    const int32_t kiPicWidth,       const int32_t kiPicHeight) {
  int32_t iCpuCores            = 0;
  int32_t iIdx                 = 0;
  const int32_t iMbWidth       = (kiPicWidth + 15) >> 4;
  const int32_t iMbHeight      = (kiPicHeight + 15) >> 4;
  const int32_t iMbNumInFrame  = iMbWidth * iMbHeight;
  bool  bSingleMode            = false;

  pSliceArgument->uiSliceSizeConstraint = 0;

  if (pSliceArgument->uiSliceNum == 0)
    pSliceArgument->uiSliceNum = 1; // jumpseat: one thread, so one slice

  if (pSliceArgument->uiSliceNum <= 1) {
    WelsLog (pLogCtx, WELS_LOG_INFO,
             "SliceArgumentValidationFixedSliceMode(), uiSliceNum(%d) you set for SM_FIXEDSLCNUM_SLICE, now turn to SM_SINGLE_SLICE type!",
             pSliceArgument->uiSliceNum);
    bSingleMode = true;
  }

  // considering the coding efficient and performance,
  // iCountMbNum constraint by MIN_NUM_MB_PER_SLICE condition of multi-pSlice mode settting
  if (iMbNumInFrame <= MIN_NUM_MB_PER_SLICE) {
    WelsLog (pLogCtx, WELS_LOG_INFO,
             "SliceArgumentValidationFixedSliceMode(), uiSliceNum(%d) you set for SM_FIXEDSLCNUM_SLICE, now turn to SM_SINGLE_SLICE type as CountMbNum less than MIN_NUM_MB_PER_SLICE!",
             pSliceArgument->uiSliceNum);
    bSingleMode = true;
  }

  if (bSingleMode) {
    pSliceArgument->uiSliceMode = SM_SINGLE_SLICE;
    pSliceArgument->uiSliceNum = 1;
    for (iIdx = 0; iIdx < MAX_SLICES_NUM; iIdx++) {
      pSliceArgument->uiSliceMbNum[iIdx] = 0;
    }
    return ENC_RETURN_SUCCESS;
  }

  if (pSliceArgument->uiSliceNum > MAX_SLICES_NUM) {
    pSliceArgument->uiSliceNum = MAX_SLICES_NUM;
    WelsLog (pLogCtx, WELS_LOG_WARNING,
             "SliceArgumentValidationFixedSliceMode(), uiSliceNum exceed MAX_SLICES_NUM! So setting slice num eqaul to MAX_SLICES_NUM(%d)!",
             pSliceArgument->uiSliceNum);
  }

  if (kiRCMode != RC_OFF_MODE) { // multiple slices verify with gom
    //check uiSliceNum and set uiSliceMbNum with current uiSliceNum
    if (!GomValidCheckSliceNum (iMbWidth, iMbHeight, &pSliceArgument->uiSliceNum)) {
      WelsLog (pLogCtx, WELS_LOG_WARNING,
               "SliceArgumentValidationFixedSliceMode(), unsupported setting with Resolution and uiSliceNum combination under RC on! So uiSliceNum is changed to %d!",
               pSliceArgument->uiSliceNum);
    }

    if (pSliceArgument->uiSliceNum <= 1 ||
        !GomValidCheckSliceMbNum (iMbWidth, iMbHeight, pSliceArgument)) {
      WelsLog (pLogCtx, WELS_LOG_ERROR,
               "SliceArgumentValidationFixedSliceMode(), unsupported setting with Resolution and uiSliceNum (%d) combination  under RC on! Consider setting single slice with this resolution!",
               pSliceArgument->uiSliceNum);
      return ENC_RETURN_UNSUPPORTED_PARA;
    }
  } else if (!CheckFixedSliceNumMultiSliceSetting (iMbNumInFrame, pSliceArgument)) {
    //check uiSliceMbNum with current uiSliceNum
    WelsLog (pLogCtx, WELS_LOG_ERROR,
             "SliceArgumentValidationFixedSliceMode(), invalid uiSliceMbNum (%d) settings!,now turn to SM_SINGLE_SLICE type",
             pSliceArgument->uiSliceMbNum[0]);
    pSliceArgument->uiSliceMode = SM_SINGLE_SLICE;
    pSliceArgument->uiSliceNum  = 1;
    for (iIdx = 0; iIdx < MAX_SLICES_NUM; iIdx++) {
      pSliceArgument->uiSliceMbNum[iIdx] = 0;
    }
  }

  return ENC_RETURN_SUCCESS;
}


/*!
 * \brief   validate checking in parameter configuration
 * \pParam  pParam      SWelsSvcCodingParam*
 * \return  successful - 0; otherwise none 0 for failed
 */
int32_t ParamValidation (SLogContext* pLogCtx, SWelsSvcCodingParam* pCfg) {
  const float fEpsn = 0.000001f;
  int32_t i = 0;

  assert (pCfg != NULL);

  if (! (pCfg->iUsageType < INPUT_CONTENT_TYPE_ALL)) {
    WelsLog (pLogCtx, WELS_LOG_ERROR, "ParamValidation(),Invalid usage type = %d", pCfg->iUsageType);
    return ENC_RETURN_UNSUPPORTED_PARA;
  }
  if (pCfg->bEnableSceneChangeDetect == false) {
    pCfg->bEnableSceneChangeDetect = true;
    WelsLog (pLogCtx, WELS_LOG_WARNING,
             "ParamValidation(), screen change detection should be turned on, change bEnableSceneChangeDetect as true");
  }

  //turn off adaptive quant now, algorithms needs to be refactored
  pCfg->bEnableAdaptiveQuant = false;


  if (!CheckInRangeCloseOpen (pCfg->iLoopFilterDisableIdc, DEBLOCKING_IDC_0, DEBLOCKING_IDC_2 + 1) ||
      !CheckInRangeCloseOpen (pCfg->iLoopFilterAlphaC0Offset, DEBLOCKING_OFFSET_MINUS, DEBLOCKING_OFFSET + 1) ||
      !CheckInRangeCloseOpen (pCfg->iLoopFilterBetaOffset, DEBLOCKING_OFFSET_MINUS, DEBLOCKING_OFFSET + 1)) {
    WelsLog (pLogCtx, WELS_LOG_ERROR,
             "ParamValidation, Invalid iLoopFilterDisableIdc(%d) or iLoopFilterAlphaC0Offset(%d) or iLoopFilterBetaOffset(%d)!",
             pCfg->iLoopFilterDisableIdc, pCfg->iLoopFilterAlphaC0Offset, pCfg->iLoopFilterBetaOffset);
    return ENC_RETURN_UNSUPPORTED_PARA;
  }

  for (i = 0; i < pCfg->iSpatialLayerNum; ++ i) {
    SSpatialLayerInternal* fDlp = &pCfg->sDependencyLayers[i];
    SSpatialLayerConfig* pConfig = &pCfg->sSpatialLayers[i];
    if (fDlp->fOutputFrameRate > fDlp->fInputFrameRate || (fDlp->fInputFrameRate >= -fEpsn
        && fDlp->fInputFrameRate <= fEpsn)
        || (fDlp->fOutputFrameRate >= -fEpsn && fDlp->fOutputFrameRate <= fEpsn)) {
      WelsLog (pLogCtx, WELS_LOG_ERROR,
               "Invalid settings in input frame rate(%.6f) or output frame rate(%.6f) of layer #%d config file..",
               fDlp->fInputFrameRate, fDlp->fOutputFrameRate, i);
      return ENC_RETURN_INVALIDINPUT;
    }
    if (UINT_MAX == GetLogFactor (fDlp->fOutputFrameRate, fDlp->fInputFrameRate)) {
      WelsLog (pLogCtx, WELS_LOG_WARNING,
               "AUTO CORRECT: Invalid settings in input frame rate(%.6f) and output frame rate(%.6f) of layer #%d config file: iResult of output frame rate divided by input frame rate should be power of 2(i.e,in/pOut=2^n). \n Auto correcting Output Framerate to Input Framerate %f!\n",
               fDlp->fInputFrameRate, fDlp->fOutputFrameRate, i, fDlp->fInputFrameRate);
      fDlp->fOutputFrameRate = fDlp->fInputFrameRate;
      pConfig->fFrameRate = fDlp->fOutputFrameRate;
    }
  }

  //bitrate setting validation
  int32_t  iTotalBitrate = 0;
  if (pCfg->iTargetBitrate <= 0) {
    WelsLog (pLogCtx, WELS_LOG_ERROR, "Invalid bitrate settings in total configure, bitrate= %d", pCfg->iTargetBitrate);
    return ENC_RETURN_INVALIDINPUT;
  }
  for (i = 0; i < pCfg->iSpatialLayerNum; ++ i) {
    SSpatialLayerConfig* pSpatialLayer = &pCfg->sSpatialLayers[i];
    iTotalBitrate += pSpatialLayer->iSpatialBitrate;

    if (WelsBitRateVerification (pLogCtx, pSpatialLayer, i) != ENC_RETURN_SUCCESS)
      return ENC_RETURN_INVALIDINPUT;
  }
  if (iTotalBitrate > pCfg->iTargetBitrate) {
    WelsLog (pLogCtx, WELS_LOG_ERROR,
             "Invalid settings in bitrate. the sum of each layer bitrate(%d) is larger than total bitrate setting(%d)",
             iTotalBitrate, pCfg->iTargetBitrate);
    return ENC_RETURN_INVALIDINPUT;
  }
  WelsLog (pLogCtx, WELS_LOG_WARNING,
               "bEnableFrameSkip = %d,bitrate can't be controlled for RC_QUALITY_MODE,RC_BITRATE_MODE and RC_TIMESTAMP_MODE without enabling skip frame.",
               pCfg->bEnableFrameSkip);
  if ((pCfg->iMaxQp <= 0) || (pCfg->iMinQp <= 0)) {
    WelsLog (pLogCtx, WELS_LOG_INFO, "Change QP Range from(%d,%d) to (%d,%d)", pCfg->iMinQp, pCfg->iMaxQp, MIN_SCREEN_QP,
             MAX_SCREEN_QP);
    pCfg->iMinQp = MIN_SCREEN_QP;
    pCfg->iMaxQp = MAX_SCREEN_QP;

  }
  pCfg->iMinQp = WELS_CLIP3 (pCfg->iMinQp, GOM_MIN_QP_MODE, QP_MAX_VALUE);
  pCfg->iMaxQp = WELS_CLIP3 (pCfg->iMaxQp, pCfg->iMinQp, QP_MAX_VALUE);
  // ref-frames validation
  if (((pCfg->iUsageType == CAMERA_VIDEO_REAL_TIME) || (pCfg->iUsageType == SCREEN_CONTENT_REAL_TIME))
      ? WelsCheckRefFrameLimitationNumRefFirst (pLogCtx, pCfg)
      : WelsCheckRefFrameLimitationLevelIdcFirst (pLogCtx, pCfg)) {
    WelsLog (pLogCtx, WELS_LOG_ERROR, "WelsCheckRefFrameLimitation failed");
    return ENC_RETURN_INVALIDINPUT;
  }
  return ENC_RETURN_SUCCESS;
}


int32_t ParamValidationExt (SLogContext* pLogCtx, SWelsSvcCodingParam* pCodingParam) {
  int8_t i = 0;
  int32_t iIdx = 0;

  assert (pCodingParam != NULL);
  if (NULL == pCodingParam)
    return ENC_RETURN_INVALIDINPUT;

  if (pCodingParam->iSpatialLayerNum < 1 || pCodingParam->iSpatialLayerNum > MAX_DEPENDENCY_LAYER) {
    WelsLog (pLogCtx, WELS_LOG_ERROR, "ParamValidationExt(), monitor invalid pCodingParam->iSpatialLayerNum: %d!",
             pCodingParam->iSpatialLayerNum);
    return ENC_RETURN_UNSUPPORTED_PARA;
  }

  if (pCodingParam->iTemporalLayerNum < 1 || pCodingParam->iTemporalLayerNum > MAX_TEMPORAL_LEVEL) {
    WelsLog (pLogCtx, WELS_LOG_ERROR, "ParamValidationExt(), monitor invalid pCodingParam->iTemporalLayerNum: %d!",
             pCodingParam->iTemporalLayerNum);
    return ENC_RETURN_UNSUPPORTED_PARA;
  }

  if (pCodingParam->uiGopSize < 1 || pCodingParam->uiGopSize > MAX_GOP_SIZE) {
    WelsLog (pLogCtx, WELS_LOG_ERROR, "ParamValidationExt(), monitor invalid pCodingParam->uiGopSize: %d!",
             pCodingParam->uiGopSize);
    return ENC_RETURN_UNSUPPORTED_PARA;
  }


  if (pCodingParam->uiIntraPeriod && pCodingParam->uiIntraPeriod < pCodingParam->uiGopSize) {
    WelsLog (pLogCtx, WELS_LOG_ERROR,
             "ParamValidationExt(), uiIntraPeriod(%d) should be not less than that of uiGopSize(%d) or -1 specified!",
             pCodingParam->uiIntraPeriod, pCodingParam->uiGopSize);
    return ENC_RETURN_UNSUPPORTED_PARA;
  }

  if (pCodingParam->uiIntraPeriod && (pCodingParam->uiIntraPeriod & (pCodingParam->uiGopSize - 1)) != 0) {
    WelsLog (pLogCtx, WELS_LOG_ERROR,
             "ParamValidationExt(), uiIntraPeriod(%d) should be multiple of uiGopSize(%d) or -1 specified!",
             pCodingParam->uiIntraPeriod, pCodingParam->uiGopSize);
    return ENC_RETURN_UNSUPPORTED_PARA;
  }

  //about iMultipleThreadIdc, bDeblockingParallelFlag, iLoopFilterDisableIdc, & uiSliceMode
  // (1) Single Thread
  //    if (THREAD==1)//single thread
  //            no parallel_deblocking: bDeblockingParallelFlag = 0;
  // (2) Multi Thread: see uiSliceMode decision
  //now is single thread. no parallel deblocking, set flag=0
  pCodingParam->bDeblockingParallelFlag = false;

  // eSpsPpsIdStrategy checkings
  if (pCodingParam->iUsageType == SCREEN_CONTENT_REAL_TIME && (SPS_LISTING & pCodingParam->eSpsPpsIdStrategy)) {
    WelsLog (pLogCtx, WELS_LOG_WARNING,
             "ParamValidationExt(), eSpsPpsIdStrategy setting (%d) with iUsageType (%d) not supported! eSpsPpsIdStrategy adjusted to CONSTANT_ID",
             pCodingParam->eSpsPpsIdStrategy, pCodingParam->iUsageType);
    pCodingParam->eSpsPpsIdStrategy = CONSTANT_ID;
  }



  for (i = 0; i < pCodingParam->iSpatialLayerNum; ++ i) {
    SSpatialLayerConfig* pSpatialLayer = &pCodingParam->sSpatialLayers[i];
    int32_t kiPicWidth = pSpatialLayer->iVideoWidth;
    int32_t kiPicHeight = pSpatialLayer->iVideoHeight;
    uint32_t iMbWidth           = 0;
    uint32_t iMbHeight          = 0;
    int32_t iMbNumInFrame       = 0;
    uint32_t iMaxSliceNum       = MAX_SLICES_NUM;
    int32_t  iReturn            = 0;

    if ((pCodingParam->iPicWidth > 0) && (pCodingParam->iPicHeight > 0)
        && (kiPicWidth == 0) && (kiPicHeight == 0)
        && (pCodingParam->iSpatialLayerNum == 1)) {
      kiPicWidth = pSpatialLayer->iVideoWidth = pCodingParam->iPicWidth;
      kiPicHeight = pSpatialLayer->iVideoHeight = pCodingParam->iPicHeight;
      WelsLog (pLogCtx, WELS_LOG_DEBUG,
               "ParamValidationExt(), layer resolution is not set, set to general resolution %d x %d",
               pSpatialLayer->iVideoWidth, pSpatialLayer->iVideoHeight);
    }

    if ((kiPicWidth <= 0) || (kiPicHeight <= 0) || (kiPicWidth * kiPicHeight > (MAX_MBS_PER_FRAME << 8))) {
      WelsLog (pLogCtx, WELS_LOG_ERROR,
               "ParamValidationExt(), width > 0, height > 0, width * height <= %d, invalid %d x %d in dependency layer settings!",
               (MAX_MBS_PER_FRAME << 8), kiPicWidth, kiPicHeight);
      return ENC_RETURN_UNSUPPORTED_PARA;
    }
    if ((kiPicWidth & 0x0F) != 0 || (kiPicHeight & 0x0F) != 0) {
      WelsLog (pLogCtx, WELS_LOG_ERROR,
               "ParamValidationExt(), in layer #%d iWidth x iHeight(%d x %d) both should be multiple of 16, can not support with arbitrary size currently!",
               i, kiPicWidth, kiPicHeight);
      return ENC_RETURN_UNSUPPORTED_PARA;
    }

    if (pSpatialLayer->sSliceArgument.uiSliceMode >= SM_RESERVED) {
      WelsLog (pLogCtx, WELS_LOG_ERROR, "ParamValidationExt(), invalid uiSliceMode (%d) settings!",
               pSpatialLayer->sSliceArgument.uiSliceMode);
      return ENC_RETURN_UNSUPPORTED_PARA;
    }
    if ((pCodingParam->uiMaxNalSize != 0) && (pSpatialLayer->sSliceArgument.uiSliceMode != SM_SIZELIMITED_SLICE)) {
      WelsLog (pLogCtx, WELS_LOG_WARNING,
               "ParamValidationExt(), current layer %d uiSliceMode (%d) settings may not fulfill MaxNalSize = %d", i,
               pSpatialLayer->sSliceArgument.uiSliceMode, pCodingParam->uiMaxNalSize);
    }
    CheckProfileSetting (pLogCtx, pCodingParam, i, pSpatialLayer->uiProfileIdc);
    CheckLevelSetting (pLogCtx, pCodingParam, i, pSpatialLayer->uiLevelIdc);
    //check pSlice settings under multi-pSlice
    if (kiPicWidth <= 16 && kiPicHeight <= 16) {
      //only have one MB, set to single_slice
      pSpatialLayer->sSliceArgument.uiSliceMode = SM_SINGLE_SLICE;
    }
    switch (pSpatialLayer->sSliceArgument.uiSliceMode) {
    case SM_SINGLE_SLICE:
      pSpatialLayer->sSliceArgument.uiSliceNum = 1;
      pSpatialLayer->sSliceArgument.uiSliceSizeConstraint = 0;
      for (iIdx = 0; iIdx < MAX_SLICES_NUM; iIdx++) {
        pSpatialLayer->sSliceArgument.uiSliceMbNum[iIdx] = 0;
      }
      break;
    case SM_FIXEDSLCNUM_SLICE: {
      iReturn = SliceArgumentValidationFixedSliceMode (pLogCtx, &pSpatialLayer->sSliceArgument, pCodingParam->iRCMode,
                kiPicWidth, kiPicHeight);
      if (iReturn)
        return ENC_RETURN_UNSUPPORTED_PARA;
    }
    break;
    case SM_RASTER_SLICE: {
      pSpatialLayer->sSliceArgument.uiSliceSizeConstraint = 0;

      iMbWidth  = (kiPicWidth + 15) >> 4;
      iMbHeight = (kiPicHeight + 15) >> 4;
      iMbNumInFrame = iMbWidth * iMbHeight;
      iMaxSliceNum = MAX_SLICES_NUM;
      if (pSpatialLayer->sSliceArgument.uiSliceMbNum[0] == 0) {
        if (iMbHeight > iMaxSliceNum) {
          WelsLog (pLogCtx, WELS_LOG_ERROR, "ParamValidationExt(), invalid uiSliceNum (%d) settings more than MAX(%d)!",
                   iMbHeight, MAX_SLICES_NUM);
          return ENC_RETURN_UNSUPPORTED_PARA;
        }
        pSpatialLayer->sSliceArgument.uiSliceNum = iMbHeight;
        for (uint32_t j = 0; j < iMbHeight; j++) {
          pSpatialLayer->sSliceArgument.uiSliceMbNum[j] = iMbWidth;
        }
        if (!CheckRowMbMultiSliceSetting (iMbWidth,
                                          &pSpatialLayer->sSliceArgument)) { // verify interleave mode settings
          WelsLog (pLogCtx, WELS_LOG_ERROR, "ParamValidationExt(), invalid uiSliceMbNum (%d) settings!",
                   pSpatialLayer->sSliceArgument.uiSliceMbNum[0]);
          return ENC_RETURN_UNSUPPORTED_PARA;
        }
        break;
      }

      if (!CheckRasterMultiSliceSetting (iMbNumInFrame,
                                         &pSpatialLayer->sSliceArgument)) { // verify interleave mode settings
        WelsLog (pLogCtx, WELS_LOG_ERROR, "ParamValidationExt(), invalid uiSliceMbNum (%d) settings!",
                 pSpatialLayer->sSliceArgument.uiSliceMbNum[0]);
        return ENC_RETURN_UNSUPPORTED_PARA;
      }
      if (pSpatialLayer->sSliceArgument.uiSliceNum <= 0
          || pSpatialLayer->sSliceArgument.uiSliceNum > iMaxSliceNum) { // verify interleave mode settings
        WelsLog (pLogCtx, WELS_LOG_ERROR, "ParamValidationExt(), invalid uiSliceNum (%d) in SM_RASTER_SLICE settings!",
                 pSpatialLayer->sSliceArgument.uiSliceNum);
        return ENC_RETURN_UNSUPPORTED_PARA;
      }
      if (pSpatialLayer->sSliceArgument.uiSliceNum == 1) {
        WelsLog (pLogCtx, WELS_LOG_WARNING,
                 "ParamValidationExt(), pSlice setting for SM_RASTER_SLICE now turn to SM_SINGLE_SLICE!");
        pSpatialLayer->sSliceArgument.uiSliceMode = SM_SINGLE_SLICE;
        break;
      }
      if ((pCodingParam->iRCMode != RC_OFF_MODE) && pSpatialLayer->sSliceArgument.uiSliceNum > 1) {
        WelsLog (pLogCtx, WELS_LOG_ERROR, "ParamValidationExt(), WARNING: GOM based RC do not support SM_RASTER_SLICE!");
      }
      // considering the coding efficient and performance, iCountMbNum constraint by MIN_NUM_MB_PER_SLICE condition of multi-pSlice mode settting
      if (iMbNumInFrame <= MIN_NUM_MB_PER_SLICE) {
        pSpatialLayer->sSliceArgument.uiSliceMode = SM_SINGLE_SLICE;
        pSpatialLayer->sSliceArgument.uiSliceNum = 1;
        break;
      }
    }
    break;
    case SM_SIZELIMITED_SLICE: {
      iMbWidth  = (kiPicWidth + 15) >> 4;
      iMbHeight = (kiPicHeight + 15) >> 4;
      if (pSpatialLayer->sSliceArgument.uiSliceSizeConstraint <= MAX_MACROBLOCK_SIZE_IN_BYTE) {
        WelsLog (pLogCtx, WELS_LOG_ERROR,
                 "ParamValidationExt(), invalid iSliceSize (%d) settings!should be larger than  MAX_MACROBLOCK_SIZE_IN_BYTE(%d)",
                 pSpatialLayer->sSliceArgument.uiSliceSizeConstraint, MAX_MACROBLOCK_SIZE_IN_BYTE);
        return ENC_RETURN_UNSUPPORTED_PARA;
      }

      if (pCodingParam->uiMaxNalSize > 0) {
        if (pCodingParam->uiMaxNalSize < (NAL_HEADER_ADD_0X30BYTES + MAX_MACROBLOCK_SIZE_IN_BYTE)) {
          WelsLog (pLogCtx, WELS_LOG_ERROR,
                   "ParamValidationExt(), invalid uiMaxNalSize (%d) settings! should be larger than (NAL_HEADER_ADD_0X30BYTES + MAX_MACROBLOCK_SIZE_IN_BYTE)(%d)",
                   pCodingParam->uiMaxNalSize, (NAL_HEADER_ADD_0X30BYTES + MAX_MACROBLOCK_SIZE_IN_BYTE));
          return ENC_RETURN_UNSUPPORTED_PARA;
        }

        if (pSpatialLayer->sSliceArgument.uiSliceSizeConstraint > (pCodingParam->uiMaxNalSize -
            NAL_HEADER_ADD_0X30BYTES)) {
          WelsLog (pLogCtx, WELS_LOG_WARNING,
                   "ParamValidationExt(), slice mode = SM_SIZELIMITED_SLICE, uiSliceSizeConstraint = %d ,uiMaxNalsize = %d, will take uiMaxNalsize!",
                   pSpatialLayer->sSliceArgument.uiSliceSizeConstraint, pCodingParam->uiMaxNalSize);
          pSpatialLayer->sSliceArgument.uiSliceSizeConstraint =  pCodingParam->uiMaxNalSize - NAL_HEADER_ADD_0X30BYTES;
        }
      }
      pSpatialLayer->sSliceArgument.uiSliceSizeConstraint -= NAL_HEADER_ADD_0X30BYTES;
    }
    break;
    default: {
      WelsLog (pLogCtx, WELS_LOG_ERROR, "ParamValidationExt(), invalid uiSliceMode (%d) settings!",
               pCodingParam->sSpatialLayers[0].sSliceArgument.uiSliceMode);
      return ENC_RETURN_UNSUPPORTED_PARA;

    }
    break;
    }
  }
  for (i = 0; i < pCodingParam->iSpatialLayerNum; ++ i) {
    SSpatialLayerConfig* pLayerInfo = &pCodingParam->sSpatialLayers[i];
    if ((pLayerInfo->uiProfileIdc == PRO_BASELINE) || (pLayerInfo->uiProfileIdc == PRO_SCALABLE_BASELINE)) {
      if (pCodingParam->iEntropyCodingModeFlag != 0) {
        pCodingParam->iEntropyCodingModeFlag = 0;
        WelsLog (pLogCtx, WELS_LOG_WARNING, "layerId(%d) Profile is baseline, Change CABAC to CAVLC", i);
      }
    } else if (pLayerInfo->uiProfileIdc == PRO_UNKNOWN) {
      if ((i == 0) || pCodingParam->bSimulcastAVC) {
        pLayerInfo->uiProfileIdc = (pCodingParam->iEntropyCodingModeFlag) ? PRO_HIGH : PRO_BASELINE;
      } else {
        pLayerInfo->uiProfileIdc = PRO_SCALABLE_BASELINE;
      }
    }
  }
  return ParamValidation (pLogCtx, pCodingParam);
}



int32_t WelsEncoderApplyBitRate (SLogContext* pLogCtx, SWelsSvcCodingParam* pParam, int iLayer) {
  //TODO (Sijia):  this is a temporary solution which keep the ratio between layers
  //but it is also possible to fulfill the bitrate of lower layer first

  SSpatialLayerConfig* pLayerParam;
  const int32_t iNumLayers = pParam->iSpatialLayerNum;
  int32_t i, iOrigTotalBitrate = 0;
  if (iLayer == SPATIAL_LAYER_ALL) {
    //read old BR
    for (i = 0; i < iNumLayers; i++) {
      iOrigTotalBitrate += pParam->sSpatialLayers[i].iSpatialBitrate;
    }
    //write new BR
    float fRatio = 0.0;
    for (i = 0; i < iNumLayers; i++) {
      pLayerParam = & (pParam->sSpatialLayers[i]);
      fRatio = pLayerParam->iSpatialBitrate / (static_cast<float> (iOrigTotalBitrate));
      pLayerParam->iSpatialBitrate = static_cast<int32_t> (pParam->iTargetBitrate * fRatio);

      if (WelsBitRateVerification (pLogCtx, pLayerParam, i) != ENC_RETURN_SUCCESS)
        return ENC_RETURN_UNSUPPORTED_PARA;
    }
  } else {
    return WelsBitRateVerification (pLogCtx, & (pParam->sSpatialLayers[iLayer]), iLayer);
  }
  return ENC_RETURN_SUCCESS;
}

/*!
 * \brief   acquire count number of layers and NALs based on configurable paramters dependency
 * \pParam  pCtx            sWelsEncCtx*
 * \pParam  pParam          SWelsSvcCodingParam*
 * \pParam  pCountLayers    pointer of count number of layers indeed
 * \pParam  iCountNals      pointer of count number of nals indeed
 * \return  0 - successful; otherwise failed
 */
int32_t AcquireLayersNals (sWelsEncCtx** ppCtx, SWelsSvcCodingParam* pParam, int32_t* pCountLayers,
                           int32_t* pCountNals) {
  int32_t iCountNumLayers       = 0;
  int32_t iCountNumNals         = 0;
  int32_t iNumDependencyLayers  = 0;
  int32_t iDIndex               = 0;

  if (NULL == pParam || NULL == ppCtx || NULL == *ppCtx)
    return 1;

  iNumDependencyLayers = pParam->iSpatialLayerNum;

  do {
    SSpatialLayerConfig* pDLayer = &pParam->sSpatialLayers[iDIndex];
//    pDLayer->ptr_cfg = pParam;
    int32_t iOrgNumNals = iCountNumNals;

    //Note: Sep. 2010
    //Review this part and suggest no change, since the memory over-use
    //(1) counts little to the overall performance
    //(2) should not be critial even under mobile case
    /*if ( SM_SINGLE_SLICE != pDLayer->sSliceArgument.uiSliceMode )*/
    const int32_t kiNumOfSlice = GetInitialSliceNum (&pDLayer->sSliceArgument);

    // NEED check iCountNals value in case multiple slices is used
    iCountNumNals += kiNumOfSlice; // for pSlice VCL NALs
    // plus prefix NALs
    if (iDIndex == 0)
      iCountNumNals += kiNumOfSlice;
    assert (iCountNumNals - iOrgNumNals <= MAX_NAL_UNITS_IN_LAYER);
    if (kiNumOfSlice > MAX_SLICES_NUM) {
      WelsLog (& (*ppCtx)->sLogCtx, WELS_LOG_ERROR,
               "AcquireLayersNals(), num_of_slice(%d) > MAX_SLICES_NUM(%d) per (iDid= %d, qid= %d) settings!",
               kiNumOfSlice, MAX_SLICES_NUM, iDIndex, 0);
      return 1;
    }

    if (iCountNumNals - iOrgNumNals > MAX_NAL_UNITS_IN_LAYER) {
      WelsLog (& (*ppCtx)->sLogCtx, WELS_LOG_ERROR,
               "AcquireLayersNals(), num_of_nals(%d) > MAX_NAL_UNITS_IN_LAYER(%d) per (iDid= %d, qid= %d) settings!",
               (iCountNumNals - iOrgNumNals), MAX_NAL_UNITS_IN_LAYER, iDIndex, 0);
      return 1;
    }

    iCountNumLayers ++;

    ++ iDIndex;
  } while (iDIndex < iNumDependencyLayers);

  if (NULL == (*ppCtx)->pFuncList || NULL == (*ppCtx)->pFuncList->pParametersetStrategy) {
    WelsLog (& (*ppCtx)->sLogCtx, WELS_LOG_ERROR,
             "AcquireLayersNals(), pFuncList and pParametersetStrategy needed to be initialized first!");
    return 1;
  }
  // count parasets
  iCountNumNals += 1 + iNumDependencyLayers + (iCountNumLayers << 1) +
                   iCountNumLayers // plus iCountNumLayers for reserved application
                   + (*ppCtx)->pFuncList->pParametersetStrategy->GetAllNeededParasetNum();

  // to check number of layers / nals / slices dependencies, 12/8/2010
  if (iCountNumLayers > MAX_LAYER_NUM_OF_FRAME) {
    WelsLog (& (*ppCtx)->sLogCtx, WELS_LOG_ERROR, "AcquireLayersNals(), iCountNumLayers(%d) > MAX_LAYER_NUM_OF_FRAME(%d)!",
             iCountNumLayers, MAX_LAYER_NUM_OF_FRAME);
    return 1;
  }

  if (NULL != pCountLayers)
    *pCountLayers = iCountNumLayers;
  if (NULL != pCountNals)
    *pCountNals = iCountNumNals;
  return 0;
}

static  void  InitMbInfo (sWelsEncCtx* pEnc, SMB*   pList, SDqLayer* pLayer, const int32_t kiDlayerId,
                          const int32_t kiMaxMbNum) {
  int32_t  iMbWidth     = pLayer->iMbWidth;
  int32_t  iMbHeight    = pLayer->iMbHeight;
  int32_t  iIdx;
  int32_t  iMbNum       = iMbWidth * iMbHeight;
  uint32_t uiNeighborAvail;
  const int32_t kiOffset = (kiDlayerId & 0x01) * kiMaxMbNum;
  SMVUnitXY (*pLayerMvUnitBlock4x4)[MB_BLOCK4x4_NUM] = (SMVUnitXY (*)[MB_BLOCK4x4_NUM]) (
        &pEnc->pMvUnitBlock4x4[MB_BLOCK4x4_NUM * kiOffset]);
  int8_t (*pLayerRefIndexBlock8x8)[MB_BLOCK8x8_NUM] = (int8_t (*)[MB_BLOCK8x8_NUM]) (
        &pEnc->pRefIndexBlock4x4[MB_BLOCK8x8_NUM * kiOffset]);

  for (iIdx = 0; iIdx < iMbNum; iIdx++) {
    bool     bLeft;
    bool     bTop;
    bool     bLeftTop;
    bool     bRightTop;
    int32_t  iLeftXY, iTopXY, iLeftTopXY, iRightTopXY;
    uint16_t  uiSliceIdc; //[0..65535] > 36864 of LEVEL5.2

    pList[iIdx].iMbX = pEnc->pStrideTab->pMbIndexX[kiDlayerId][iIdx];
    pList[iIdx].iMbY = pEnc->pStrideTab->pMbIndexY[kiDlayerId][iIdx];
    pList[iIdx].iMbXY = iIdx;

    uiSliceIdc = WelsMbToSliceIdc (pLayer, iIdx);
    iLeftXY = iIdx - 1;
    iTopXY = iIdx - iMbWidth;
    iLeftTopXY = iTopXY - 1;
    iRightTopXY = iTopXY + 1;

    bLeft = (pList[iIdx].iMbX > 0) && (uiSliceIdc == WelsMbToSliceIdc (pLayer, iLeftXY));
    bTop = (pList[iIdx].iMbY > 0) && (uiSliceIdc == WelsMbToSliceIdc (pLayer, iTopXY));
    bLeftTop = (pList[iIdx].iMbX > 0) && (pList[iIdx].iMbY > 0) && (uiSliceIdc ==
               WelsMbToSliceIdc (pLayer, iLeftTopXY));
    bRightTop = (pList[iIdx].iMbX < (iMbWidth - 1)) && (pList[iIdx].iMbY > 0) && (uiSliceIdc ==
                WelsMbToSliceIdc (pLayer, iRightTopXY));

    uiNeighborAvail = 0;
    if (bLeft) {
      uiNeighborAvail |= LEFT_MB_POS;
    }
    if (bTop) {
      uiNeighborAvail |= TOP_MB_POS;
    }
    if (bLeftTop) {
      uiNeighborAvail |= TOPLEFT_MB_POS;
    }
    if (bRightTop) {
      uiNeighborAvail |= TOPRIGHT_MB_POS;
    }
    pList[iIdx].uiSliceIdc      = uiSliceIdc; // merge from svc_hd_opt_b for multiple slices coding
    pList[iIdx].uiNeighborAvail = uiNeighborAvail;
    uiNeighborAvail = 0;
    if (pList[iIdx].iMbX >= BASE_MV_MB_NMB)
      uiNeighborAvail |= LEFT_MB_POS;
    if (pList[iIdx].iMbX <= (iMbWidth - 1 - BASE_MV_MB_NMB))
      uiNeighborAvail |= RIGHT_MB_POS;
    if (pList[iIdx].iMbY >= BASE_MV_MB_NMB)
      uiNeighborAvail |= TOP_MB_POS;
    if (pList[iIdx].iMbY <= (iMbHeight - 1 - BASE_MV_MB_NMB))
      uiNeighborAvail |= BOTTOM_MB_POS;

    pList[iIdx].sMv                     = pLayerMvUnitBlock4x4[iIdx];
    pList[iIdx].pRefIndex               = pLayerRefIndexBlock8x8[iIdx];
    pList[iIdx].pSadCost                = &pEnc->pSadCostMb[iIdx];
    pList[iIdx].pIntra4x4PredMode       = &pEnc->pIntra4x4PredModeBlocks[iIdx * INTRA_4x4_MODE_NUM];
    pList[iIdx].pNonZeroCount           = &pEnc->pNonZeroCountBlocks[iIdx * MB_LUMA_CHROMA_BLOCK4x4_NUM];
  }
}


int32_t   InitMbListD (sWelsEncCtx** ppCtx) {
  int32_t iNumDlayer = (*ppCtx)->pSvcParam->iSpatialLayerNum;
  int32_t iMbSize[MAX_DEPENDENCY_LAYER] = { 0 };
  int32_t iOverallMbNum = 0;
  int32_t iMbWidth = 0;
  int32_t iMbHeight = 0;
  int32_t i;

  if (iNumDlayer > MAX_DEPENDENCY_LAYER)
    return 1;

  for (i = 0; i < iNumDlayer; i++) {
    iMbWidth = ((*ppCtx)->pSvcParam->sSpatialLayers[i].iVideoWidth + 15) >> 4;
    iMbHeight = ((*ppCtx)->pSvcParam->sSpatialLayers[i].iVideoHeight + 15) >> 4;
    iMbSize[i] = iMbWidth  * iMbHeight;
    iOverallMbNum += iMbSize[i];
  }

  (*ppCtx)->ppMbListD = static_cast<SMB**> ((*ppCtx)->pMemAlign->WelsMallocz (iNumDlayer * sizeof (SMB*), "ppMbListD"));
  (*ppCtx)->ppMbListD[0] = NULL;
  WELS_VERIFY_RETURN_IF (1, (*ppCtx)->ppMbListD == NULL)
  (*ppCtx)->ppMbListD[0] = static_cast<SMB*> ((*ppCtx)->pMemAlign->WelsMallocz (iOverallMbNum * sizeof (SMB),
                           "ppMbListD[0]"));
  WELS_VERIFY_RETURN_IF (1, (*ppCtx)->ppMbListD[0] == NULL)
  (*ppCtx)->ppDqLayerList[0]->sMbDataP = (*ppCtx)->ppMbListD[0];
  InitMbInfo (*ppCtx, (*ppCtx)->ppMbListD[0], (*ppCtx)->ppDqLayerList[0], 0, iMbSize[iNumDlayer - 1]);
  for (i = 1; i < iNumDlayer; i++) {
    (*ppCtx)->ppMbListD[i] = (*ppCtx)->ppMbListD[i - 1] + iMbSize[i - 1];
    (*ppCtx)->ppDqLayerList[i]->sMbDataP = (*ppCtx)->ppMbListD[i];
    InitMbInfo (*ppCtx, (*ppCtx)->ppMbListD[i], (*ppCtx)->ppDqLayerList[i], i, iMbSize[iNumDlayer - 1]);
  }

  return 0;
}

void FreeSliceInLayer (SDqLayer* pDq, CMemoryAlign* pMa) {
  int32_t iIdx = 0;
  for (; iIdx < MAX_THREADS_NUM; iIdx ++) {
    FreeSliceBuffer (pDq->sSliceBufferInfo[iIdx].pSliceBuffer,
                     pDq->sSliceBufferInfo[iIdx].iMaxSliceNum,
                     pMa, "pSliceBuffer");
  }
}

void FreeDqLayer (SDqLayer*& pDq, CMemoryAlign* pMa) {
  if (NULL == pDq) {
    return;
  }

  FreeSliceInLayer (pDq, pMa);

  if (pDq->ppSliceInLayer) {
    pMa->WelsFree (pDq->ppSliceInLayer, "ppSliceInLayer");
    pDq->ppSliceInLayer = NULL;
  }

  if (pDq->pFirstMbIdxOfSlice) {
    pMa->WelsFree (pDq->pFirstMbIdxOfSlice, "pFirstMbIdxOfSlice");
    pDq->pFirstMbIdxOfSlice = NULL;
  }

  if (pDq->pCountMbNumInSlice) {
    pMa->WelsFree (pDq->pCountMbNumInSlice, "pCountMbNumInSlice");
    pDq->pCountMbNumInSlice = NULL;
  }

  if (pDq->pFeatureSearchPreparation) {
    ReleaseFeatureSearchPreparation (pMa, pDq->pFeatureSearchPreparation->pFeatureOfBlock);
    pMa->WelsFree (pDq->pFeatureSearchPreparation, "pFeatureSearchPreparation");
    pDq->pFeatureSearchPreparation = NULL;
  }

  UninitSlicePEncCtx (pDq, pMa);
  pDq->iMaxSliceNum = 0;

  pMa->WelsFree (pDq, "pDqLayer");
  pDq = NULL;
}

void  FreeRefList (SRefList*& pRefList, CMemoryAlign* pMa, const int iMaxNumRefFrame) {
  if (NULL == pRefList) {
    return;
  }

  int32_t iRef = 0;
  do {
    if (pRefList->pRef[iRef] != NULL) {
      FreePicture (pMa, &pRefList->pRef[iRef]);
    }
    ++ iRef;
  } while (iRef < 1 + iMaxNumRefFrame);

  pMa->WelsFree (pRefList, "pRefList");
  pRefList = NULL;
}

/*!
 * \brief   initialize ppDqLayerList and slicepEncCtx_list due to count number of layers available
 * \pParam  pCtx            sWelsEncCtx*
 * \return  0 - successful; otherwise failed
 */
static inline int32_t InitDqLayers (sWelsEncCtx** ppCtx, SExistingParasetList* pExistingParasetList) {
  SWelsSvcCodingParam* pParam   = NULL;
  SWelsSPS* pSps                = NULL;
  SSubsetSps* pSubsetSps        = NULL;
  SWelsPPS* pPps                = NULL;
  CMemoryAlign* pMa             = NULL;
  int32_t iDlayerCount          = 0;
  int32_t iDlayerIndex          = 0;
  int32_t iSpsId               = 0;
  uint32_t iPpsId               = 0;
  uint32_t iNumRef              = 0;
  int32_t iResult               = 0;

  if (NULL == ppCtx || NULL == *ppCtx)
    return 1;

  pMa           = (*ppCtx)->pMemAlign;
  pParam        = (*ppCtx)->pSvcParam;
  iDlayerCount  = pParam->iSpatialLayerNum;
  iNumRef       = pParam->iMaxNumRefFrame;

  const int32_t kiFeatureStrategyIndex = FME_DEFAULT_FEATURE_INDEX;
  const int32_t kiMe16x16 = ME_DIA_CROSS;
  const int32_t kiMe8x8 = ME_DIA_CROSS_FME;
  const int32_t kiNeedFeatureStorage = (pParam->iUsageType != SCREEN_CONTENT_REAL_TIME) ? 0 :
                                       ((kiFeatureStrategyIndex << 16) + ((kiMe16x16 & 0x00FF) << 8) + (kiMe8x8 & 0x00FF));

  iDlayerIndex = 0;
  while (iDlayerIndex < iDlayerCount) {
    SRefList* pRefList          = NULL;
    uint32_t i                  = 0;
    const int32_t kiWidth       = pParam->sSpatialLayers[iDlayerIndex].iVideoWidth;
    const int32_t kiHeight      = pParam->sSpatialLayers[iDlayerIndex].iVideoHeight;
    int32_t iPicWidth           = WELS_ALIGN (kiWidth, MB_WIDTH_LUMA) + (PADDING_LENGTH << 1);  // with iWidth of horizon
    int32_t iPicChromaWidth     = iPicWidth >> 1;

    iPicWidth = WELS_ALIGN (iPicWidth,
                            32); // 32(or 16 for chroma below) to match original imp. here instead of iCacheLineSize
    iPicChromaWidth = WELS_ALIGN (iPicChromaWidth, 16);

    WelsGetEncBlockStrideOffset ((*ppCtx)->pStrideTab->pStrideEncBlockOffset[iDlayerIndex], iPicWidth, iPicChromaWidth);

    // pRef list
    pRefList = (SRefList*)pMa->WelsMallocz (sizeof (SRefList), "pRefList");
    WELS_VERIFY_RETURN_IF (1, (NULL == pRefList))
    do {
      pRefList->pRef[i] = AllocPicture (pMa, kiWidth, kiHeight, true,
                                        (iDlayerIndex == iDlayerCount - 1) ? kiNeedFeatureStorage : 0); // to use actual size of current layer
      WELS_VERIFY_RETURN_PROC_IF (1, (NULL == pRefList->pRef[i]), FreeRefList (pRefList, pMa, iNumRef))
      ++ i;
    } while (i < 1 + iNumRef);

    pRefList->pNextBuffer = pRefList->pRef[0];
    (*ppCtx)->ppRefPicListExt[iDlayerIndex] = pRefList;
    ++ iDlayerIndex;
  }

  iDlayerIndex = 0;
  while (iDlayerIndex < iDlayerCount) {
    SDqLayer* pDqLayer              = NULL;
    SSpatialLayerConfig* pDlayer    = &pParam->sSpatialLayers[iDlayerIndex];
    SSpatialLayerInternal* pParamInternal    = &pParam->sDependencyLayers[iDlayerIndex];
    const int32_t kiMbW             = (pDlayer->iVideoWidth + 0x0f) >> 4;
    const int32_t kiMbH             = (pDlayer->iVideoHeight + 0x0f) >> 4;

    pParamInternal->iCodingIndex = 0;
    pParamInternal->iFrameIndex = 0;
    pParamInternal->iFrameNum = 0;
    pParamInternal->iPOC = 0;
    pParamInternal->uiIdrPicId = 0;
    pParamInternal->bEncCurFrmAsIdrFlag = true;  // make sure first frame is IDR
    // pDq layers list
    pDqLayer = (SDqLayer*)pMa->WelsMallocz (sizeof (SDqLayer), "pDqLayer");
    WELS_VERIFY_RETURN_PROC_IF (1, (NULL == pDqLayer), FreeDqLayer (pDqLayer, pMa))

    pDqLayer->bNeedAdjustingSlicing = false;

    pDqLayer->iMbWidth  = kiMbW;
    pDqLayer->iMbHeight = kiMbH;

    int32_t iMaxSliceNum            = 1;
    const int32_t kiSliceNum = GetInitialSliceNum (&pDlayer->sSliceArgument);
    if (iMaxSliceNum < kiSliceNum)
      iMaxSliceNum = kiSliceNum;
    pDqLayer->iMaxSliceNum = iMaxSliceNum;

    iResult = InitSliceInLayer (*ppCtx, pDqLayer, iDlayerIndex, pMa);
    if (iResult) {
      WelsLog (& (*ppCtx)->sLogCtx, WELS_LOG_WARNING, "InitDqLayers(), InitSliceInLayer failed(%d)!", iResult);
      FreeDqLayer (pDqLayer, pMa);
      return iResult;
    }

    //deblocking parameters initialization
    //target-layer deblocking
    pDqLayer->iLoopFilterDisableIdc     = 1; // jumpseat: no deblocking filter (removed)
    pDqLayer->iLoopFilterAlphaC0Offset  = (pParam->iLoopFilterAlphaC0Offset) << 1;
    pDqLayer->iLoopFilterBetaOffset     = (pParam->iLoopFilterBetaOffset) << 1;
    //parallel deblocking
    pDqLayer->bDeblockingParallelFlag   = pParam->bDeblockingParallelFlag;

    //deblocking parameter adjustment
    //bDeblockingParallelFlag
    pDqLayer->bDeblockingParallelFlag = false;

    //
    if (kiNeedFeatureStorage && iDlayerIndex == iDlayerCount - 1) {
      pDqLayer->pFeatureSearchPreparation = static_cast<SFeatureSearchPreparation*> (pMa->WelsMallocz (sizeof (
                                              SFeatureSearchPreparation), "pFeatureSearchPreparation"));
      WELS_VERIFY_RETURN_IF (1, NULL == pDqLayer->pFeatureSearchPreparation)
      int32_t iReturn = RequestFeatureSearchPreparation (pMa, pDlayer->iVideoWidth, pDlayer->iVideoHeight,
                        kiNeedFeatureStorage,
                        pDqLayer->pFeatureSearchPreparation);
      WELS_VERIFY_RETURN_IF (1, ENC_RETURN_SUCCESS != iReturn)
    } else {
      pDqLayer->pFeatureSearchPreparation = NULL;
    }

    (*ppCtx)->ppDqLayerList[iDlayerIndex] = pDqLayer;

    ++ iDlayerIndex;
  }

  // for dynamically malloc for parameter sets memory instead of maximal items for standard to reduce size, 3/18/2010
  WELS_VERIFY_RETURN_IF (1, (NULL == (*ppCtx)->pFuncList))
  WELS_VERIFY_RETURN_IF (1, (NULL == (*ppCtx)->pFuncList->pParametersetStrategy))
  const int32_t kiNeededSpsNum = (*ppCtx)->pFuncList->pParametersetStrategy->GetNeededSpsNum();
  const int32_t kiNeededSubsetSpsNum = (*ppCtx)->pFuncList->pParametersetStrategy->GetNeededSubsetSpsNum();
  (*ppCtx)->pSpsArray = (SWelsSPS*)pMa->WelsMallocz (kiNeededSpsNum * sizeof (SWelsSPS), "pSpsArray");
  WELS_VERIFY_RETURN_IF (1, (NULL == (*ppCtx)->pSpsArray))
  if (kiNeededSubsetSpsNum > 0) {
    (*ppCtx)->pSubsetArray = (SSubsetSps*)pMa->WelsMallocz (kiNeededSubsetSpsNum * sizeof (SSubsetSps), "pSubsetArray");
    WELS_VERIFY_RETURN_IF (1, (NULL == (*ppCtx)->pSubsetArray))
  } else {
    (*ppCtx)->pSubsetArray = NULL;
  }

  // PPS
  const int32_t kiNeededPpsNum = (*ppCtx)->pFuncList->pParametersetStrategy->GetNeededPpsNum();
  (*ppCtx)->pPPSArray = (SWelsPPS*)pMa->WelsMallocz (kiNeededPpsNum * sizeof (SWelsPPS), "pPPSArray");
  WELS_VERIFY_RETURN_IF (1, (NULL == (*ppCtx)->pPPSArray))

  (*ppCtx)->pFuncList->pParametersetStrategy->LoadPrevious (pExistingParasetList, (*ppCtx)->pSpsArray,
      (*ppCtx)->pSubsetArray, (*ppCtx)->pPPSArray);


  (*ppCtx)->pDqIdcMap = (SDqIdc*)pMa->WelsMallocz (iDlayerCount * sizeof (SDqIdc), "pDqIdcMap");
  WELS_VERIFY_RETURN_IF (1, (NULL == (*ppCtx)->pDqIdcMap))

  iDlayerIndex = 0;
  while (iDlayerIndex < iDlayerCount) {
    SDqIdc* pDqIdc                      = & (*ppCtx)->pDqIdcMap[iDlayerIndex];
    const bool bUseSubsetSps            = (!pParam->bSimulcastAVC) && (iDlayerIndex > BASE_DEPENDENCY_ID);
    SSpatialLayerConfig* pDlayerParam   = &pParam->sSpatialLayers[iDlayerIndex];
    bool bSvcBaselayer = (!pParam->bSimulcastAVC) && (iDlayerCount > BASE_DEPENDENCY_ID)
                         && (iDlayerIndex == BASE_DEPENDENCY_ID);
    pDqIdc->uiSpatialId = iDlayerIndex;

    iSpsId = (*ppCtx)->pFuncList->pParametersetStrategy->GenerateNewSps (*ppCtx, bUseSubsetSps, iDlayerIndex,
             iDlayerCount, iSpsId, pSps, pSubsetSps, bSvcBaselayer);
    WELS_VERIFY_RETURN_IF (ENC_RETURN_UNSUPPORTED_PARA, (0 > iSpsId))
    if (!bUseSubsetSps) {
      pSps = & ((*ppCtx)->pSpsArray[iSpsId]);
    } else {
      pSubsetSps = & ((*ppCtx)->pSubsetArray[iSpsId]);
    }

    iPpsId = (*ppCtx)->pFuncList->pParametersetStrategy->InitPps ((*ppCtx), iSpsId, pSps, pSubsetSps, iPpsId, true,
             bUseSubsetSps, pParam->iEntropyCodingModeFlag != 0);
    pPps = & ((*ppCtx)->pPPSArray[iPpsId]);

    // Not using FMO in SVC coding so far, come back if need FMO
    {
      iResult = InitSlicePEncCtx ((*ppCtx)->ppDqLayerList[iDlayerIndex],
                                  (*ppCtx)->pMemAlign,
                                  false,
                                  pSps->iMbWidth,
                                  pSps->iMbHeight,
                                  & (pDlayerParam->sSliceArgument),
                                  pPps);
      if (iResult) {
        WelsLog (& (*ppCtx)->sLogCtx, WELS_LOG_WARNING, "InitDqLayers(), InitSlicePEncCtx failed(%d)!", iResult);
        return iResult;
      }
    }
    pDqIdc->iSpsId = iSpsId;
    pDqIdc->iPpsId = iPpsId;

    if ((pParam->bSimulcastAVC) || (bUseSubsetSps))
      ++ iSpsId;
    ++ iPpsId;
    if (bUseSubsetSps) {
      ++ (*ppCtx)->iSubsetSpsNum;
    } else {
      ++ (*ppCtx)->iSpsNum;
    }
    ++ (*ppCtx)->iPpsNum;

    ++ iDlayerIndex;
  }

  (*ppCtx)->pFuncList->pParametersetStrategy->UpdateParaSetNum ((*ppCtx));
  return ENC_RETURN_SUCCESS;
}

int32_t AllocStrideTables (sWelsEncCtx** ppCtx, const int32_t kiNumSpatialLayers) {
  CMemoryAlign* pMa             = (*ppCtx)->pMemAlign;
  SWelsSvcCodingParam* pParam   = (*ppCtx)->pSvcParam;
  SStrideTables* pPtr           = NULL;
  int16_t* pTmpRow              = NULL, *pRowX = NULL, *pRowY = NULL, *p = NULL;
  uint8_t* pBase                = NULL;
  uint8_t* pBaseDec = NULL, *pBaseEnc = NULL, *pBaseMbX = NULL, *pBaseMbY = NULL;
  struct {
    int32_t iMbWidth;
    int32_t iCountMbNum;                // count number of SMB in each spatial
    int32_t iSizeAllMbAlignCache;       // cache line size aligned in each spatial
  } sMbSizeMap[MAX_DEPENDENCY_LAYER] = {{ 0 }};
  int32_t iLineSizeY[MAX_DEPENDENCY_LAYER][2] = {{ 0 }};
  int32_t iLineSizeUV[MAX_DEPENDENCY_LAYER][2] = {{ 0 }};
  int32_t iMapSpatialIdx[MAX_DEPENDENCY_LAYER][2] = {{ 0 }};
  int32_t iSizeDec              = 0;
  int32_t iSizeEnc              = 0;
  int32_t iCountLayersNeedCs[2] = {0};
  const int32_t kiUnit1Size = 24 * sizeof (int32_t);
  int32_t iUnit2Size            = 0;
  int32_t iNeedAllocSize        = 0;
  int32_t iRowSize              = 0;
  int16_t iMaxMbWidth           = 0;
  int16_t iMaxMbHeight          = 0;
  int32_t i                     = 0;
  int32_t iSpatialIdx           = 0;
  int32_t iTemporalIdx          = 0;
  int32_t iCntTid               = 0;

  if (kiNumSpatialLayers <= 0 || kiNumSpatialLayers > MAX_DEPENDENCY_LAYER)
    return 1;

  pPtr = (SStrideTables*)pMa->WelsMallocz (sizeof (SStrideTables), "SStrideTables");
  if (NULL == pPtr)
    return 1;
  (*ppCtx)->pStrideTab = pPtr;

  iCntTid = pParam->iTemporalLayerNum > 1 ? 2 : 1;

  iSpatialIdx = 0;
  while (iSpatialIdx < kiNumSpatialLayers) {
    const int32_t kiTmpWidth = (pParam->sSpatialLayers[iSpatialIdx].iVideoWidth + 15) >> 4;
    const int32_t kiTmpHeight = (pParam->sSpatialLayers[iSpatialIdx].iVideoHeight + 15) >> 4;
    int32_t iNumMb = kiTmpWidth * kiTmpHeight;

    sMbSizeMap[iSpatialIdx].iMbWidth    = kiTmpWidth;
    sMbSizeMap[iSpatialIdx].iCountMbNum = iNumMb;

    iNumMb *= sizeof (int16_t);
    sMbSizeMap[iSpatialIdx].iSizeAllMbAlignCache = iNumMb;
    iUnit2Size += iNumMb;

    ++ iSpatialIdx;
  }

  // Adaptive size_cs, size_fdec by implementation dependency
  iTemporalIdx = 0;
  while (iTemporalIdx < iCntTid) {
    const bool kbBaseTemporalFlag = (iTemporalIdx == 0);

    iSpatialIdx = 0;
    while (iSpatialIdx < kiNumSpatialLayers) {
      SSpatialLayerConfig* fDlp = &pParam->sSpatialLayers[iSpatialIdx];

      const int32_t kiWidthPad = WELS_ALIGN (fDlp->iVideoWidth, 16) + (PADDING_LENGTH << 1);
      iLineSizeY[iSpatialIdx][kbBaseTemporalFlag]  = WELS_ALIGN (kiWidthPad, 32);
      iLineSizeUV[iSpatialIdx][kbBaseTemporalFlag] = WELS_ALIGN ((kiWidthPad >> 1), 16);

      iMapSpatialIdx[iCountLayersNeedCs[kbBaseTemporalFlag]][kbBaseTemporalFlag] = iSpatialIdx;
      ++ iCountLayersNeedCs[kbBaseTemporalFlag];
      ++ iSpatialIdx;
    }
    ++ iTemporalIdx;
  }
  iSizeDec = kiUnit1Size * (iCountLayersNeedCs[0] + iCountLayersNeedCs[1]);
  iSizeEnc = kiUnit1Size * kiNumSpatialLayers;

  iNeedAllocSize = iSizeDec + iSizeEnc + (iUnit2Size << 1);

  pBase = (uint8_t*)pMa->WelsMallocz (iNeedAllocSize, "pBase");
  if (NULL == pBase) {
    return 1;
  }

  pBaseDec = pBase;                     // iCountLayersNeedCs
  pBaseEnc = pBaseDec + iSizeDec;       // iNumSpatialLayers
  pBaseMbX = pBaseEnc + iSizeEnc;       // iNumSpatialLayers
  pBaseMbY = pBaseMbX + iUnit2Size;     // iNumSpatialLayers

  iTemporalIdx = 0;
  while (iTemporalIdx < iCntTid) {
    const bool kbBaseTemporalFlag = (iTemporalIdx == 0);

    iSpatialIdx = 0;
    while (iSpatialIdx < iCountLayersNeedCs[kbBaseTemporalFlag]) {
      const int32_t kiActualSpatialIdx = iMapSpatialIdx[iSpatialIdx][kbBaseTemporalFlag];
      const int32_t kiLumaWidth        = iLineSizeY[kiActualSpatialIdx][kbBaseTemporalFlag];
      const int32_t kiChromaWidth      = iLineSizeUV[kiActualSpatialIdx][kbBaseTemporalFlag];

      WelsGetEncBlockStrideOffset ((int32_t*)pBaseDec, kiLumaWidth, kiChromaWidth);

      pPtr->pStrideDecBlockOffset[kiActualSpatialIdx][kbBaseTemporalFlag] = (int32_t*)pBaseDec;
      pBaseDec += kiUnit1Size;

      ++ iSpatialIdx;
    }
    ++ iTemporalIdx;
  }
  iTemporalIdx = 0;
  while (iTemporalIdx < iCntTid) {
    const bool kbBaseTemporalFlag = (iTemporalIdx == 0);

    iSpatialIdx = 0;
    while (iSpatialIdx < kiNumSpatialLayers) {
      int32_t iMatchIndex = 0;
      bool bInMap = false;
      bool bMatchFlag = false;

      i = 0;
      while (i < iCountLayersNeedCs[kbBaseTemporalFlag]) {
        const int32_t kiActualIdx = iMapSpatialIdx[i][kbBaseTemporalFlag];
        if (kiActualIdx == iSpatialIdx) {
          bInMap = true;
          break;
        }
        if (!bMatchFlag) {
          iMatchIndex = kiActualIdx;
          bMatchFlag = true;
        }
        ++ i;
      }

      if (bInMap) {
        ++ iSpatialIdx;
        continue;
      }

      // not in spatial map and assign match one to it
      pPtr->pStrideDecBlockOffset[iSpatialIdx][kbBaseTemporalFlag] =
        pPtr->pStrideDecBlockOffset[iMatchIndex][kbBaseTemporalFlag];

      ++ iSpatialIdx;
    }
    ++ iTemporalIdx;
  }

  iSpatialIdx = 0;
  while (iSpatialIdx < kiNumSpatialLayers) {
    const int32_t kiAllocMbSize = sMbSizeMap[iSpatialIdx].iSizeAllMbAlignCache;

    pPtr->pStrideEncBlockOffset[iSpatialIdx]    = (int32_t*)pBaseEnc;

    pPtr->pMbIndexX[iSpatialIdx]                = (int16_t*)pBaseMbX;
    pPtr->pMbIndexY[iSpatialIdx]                = (int16_t*)pBaseMbY;

    pBaseEnc += kiUnit1Size;
    pBaseMbX += kiAllocMbSize;
    pBaseMbY += kiAllocMbSize;

    ++ iSpatialIdx;
  }

  while (iSpatialIdx < MAX_DEPENDENCY_LAYER) {
    pPtr->pStrideDecBlockOffset[iSpatialIdx][0] = NULL;
    pPtr->pStrideDecBlockOffset[iSpatialIdx][1] = NULL;
    pPtr->pStrideEncBlockOffset[iSpatialIdx]    = NULL;
    pPtr->pMbIndexX[iSpatialIdx]                = NULL;
    pPtr->pMbIndexY[iSpatialIdx]                = NULL;

    ++ iSpatialIdx;
  }

  // initialize pMbIndexX and pMbIndexY tables as below

  iMaxMbWidth   = sMbSizeMap[kiNumSpatialLayers - 1].iMbWidth;
  iMaxMbWidth   = WELS_ALIGN (iMaxMbWidth, 4);  // 4 loops for int16_t required introduced as below
  iRowSize      = iMaxMbWidth * sizeof (int16_t);

  pTmpRow = (int16_t*)pMa->WelsMallocz (iRowSize, "pTmpRow");
  if (NULL == pTmpRow) {
    return 1;
  }
  pRowX = pTmpRow;
  pRowY = pRowX;
  // initialize pRowX & pRowY
  i = 0;
  p = pRowX;
  while (i < iMaxMbWidth) {
    *p          = i;
    * (p + 1)   = 1 + i;
    * (p + 2)   = 2 + i;
    * (p + 3)   = 3 + i;

    p += 4;
    i += 4;
  }

  iSpatialIdx = kiNumSpatialLayers;
  while (--iSpatialIdx >= 0) {
    int16_t* pMbIndexX = pPtr->pMbIndexX[iSpatialIdx];
    const int32_t kiMbWidth     = sMbSizeMap[iSpatialIdx].iMbWidth;
    const int32_t kiMbHeight    = sMbSizeMap[iSpatialIdx].iCountMbNum / kiMbWidth;
    const int32_t kiLineSize    = kiMbWidth * sizeof (int16_t);

    i = 0;
    while (i < kiMbHeight) {
      memcpy (pMbIndexX, pRowX, kiLineSize); // confirmed_safe_unsafe_usage

      pMbIndexX += kiMbWidth;
      ++ i;
    }
  }

  memset (pRowY, 0, iRowSize);
  iMaxMbHeight = sMbSizeMap[kiNumSpatialLayers - 1].iCountMbNum / sMbSizeMap[kiNumSpatialLayers - 1].iMbWidth;
  i = 0;
  for (;;) {
    ENFORCE_STACK_ALIGN_1D (int16_t, t, 4, 16)

    int32_t t32 = 0;
    int16_t j = 0;

    for (iSpatialIdx = kiNumSpatialLayers - 1; iSpatialIdx >= 0; -- iSpatialIdx) {
      const int32_t kiMbWidth  = sMbSizeMap[iSpatialIdx].iMbWidth;
      const int32_t kiMbHeight = sMbSizeMap[iSpatialIdx].iCountMbNum / kiMbWidth;
      const int32_t kiLineSize = kiMbWidth * sizeof (int16_t);
      int16_t* pMbIndexY = pPtr->pMbIndexY[iSpatialIdx] + i * kiMbWidth;

      if (i < kiMbHeight) {
        memcpy (pMbIndexY, pRowY, kiLineSize); // confirmed_safe_unsafe_usage
      }
    }
    ++ i;
    if (i >= iMaxMbHeight)
      break;

    t32 = i | (i << 16);
    ST32 (t, t32);
    ST32 (t + 2, t32);

    p = pRowY;
    while (j < iMaxMbWidth) {
      ST64 (p, LD64 (t));

      p += 4;
      j += 4;
    }
  }

  pMa->WelsFree (pTmpRow, "pTmpRow");
  pTmpRow = NULL;

  return 0;
}
int32_t RequestMemoryVaaScreen (SVAAFrameInfo* pVaa,  CMemoryAlign* pMa,  const int32_t iNumRef,
                                const int32_t iCountMax8x8BNum) {
  SVAAFrameInfoExt* pVaaExt = static_cast<SVAAFrameInfoExt*> (pVaa);

  pVaaExt->pVaaBlockStaticIdc[0] = (static_cast<uint8_t*> (pMa->WelsMallocz (iNumRef * iCountMax8x8BNum * sizeof (
                                      uint8_t), "pVaa->pVaaBlockStaticIdc[0]")));
  if (NULL == pVaaExt->pVaaBlockStaticIdc[0]) {
    return 1;
  }

  for (int32_t idx = 1; idx < iNumRef; idx++) {
    pVaaExt->pVaaBlockStaticIdc[idx] = pVaaExt->pVaaBlockStaticIdc[idx - 1] + iCountMax8x8BNum;
  }
  return 0;
}
void ReleaseMemoryVaaScreen (SVAAFrameInfo* pVaa,  CMemoryAlign* pMa, const int32_t iNumRef) {
  SVAAFrameInfoExt* pVaaExt = static_cast<SVAAFrameInfoExt*> (pVaa);
  if (pVaaExt && pMa && pVaaExt->pVaaBlockStaticIdc[0]) {
    pMa->WelsFree (pVaaExt->pVaaBlockStaticIdc[0], "pVaa->pVaaBlockStaticIdc[0]");

    for (int32_t idx = 0; idx < iNumRef; idx++) {
      pVaaExt->pVaaBlockStaticIdc[idx] = NULL;
    }
  }
}
/*!
 * \brief   request specific memory for SVC
 * \pParam  pEncCtx     sWelsEncCtx*
 * \return  successful - 0; otherwise none 0 for failed
 */
void GetMvMvdRange (SWelsSvcCodingParam* pParam, int32_t& iMvRange, int32_t& iMvdRange) {
  ELevelIdc iMinLevelIdc = LEVEL_5_2;
  int32_t iMinMv = 0;
  int32_t iMaxMv = 0;
  int32_t iFixMvRange = pParam->iUsageType ? EXPANDED_MV_RANGE : CAMERA_STARTMV_RANGE;
  int32_t iFixMvdRange = (pParam->iUsageType ? EXPANDED_MVD_RANGE : ((pParam->iSpatialLayerNum == 1) ? CAMERA_MVD_RANGE :
                          CAMERA_HIGHLAYER_MVD_RANGE));
  for (int32_t iLayer = 0; iLayer < pParam->iSpatialLayerNum; iLayer++) {
    if (pParam->sSpatialLayers[iLayer].uiLevelIdc < iMinLevelIdc)
      iMinLevelIdc = pParam->sSpatialLayers[iLayer].uiLevelIdc;
  }
  const SLevelLimits* pLevelLimit = g_ksLevelLimits;
  while ((pLevelLimit->uiLevelIdc != LEVEL_5_2) && (pLevelLimit->uiLevelIdc != iMinLevelIdc))
    pLevelLimit++;
  iMinMv = (pLevelLimit->iMinVmv) >> 2;
  iMaxMv = (pLevelLimit->iMaxVmv) >> 2;

  iMvRange = WELS_MIN (WELS_ABS (iMinMv), iMaxMv);

  iMvRange = WELS_MIN (iMvRange, iFixMvRange);

  iMvdRange = (iMvRange + 1) << 1;

  iMvdRange = WELS_MIN (iMvdRange, iFixMvdRange);
}
int32_t RequestMemorySvc (sWelsEncCtx** ppCtx, SExistingParasetList* pExistingParasetList) {
  SWelsSvcCodingParam* pParam           = (*ppCtx)->pSvcParam;
  CMemoryAlign* pMa                     = (*ppCtx)->pMemAlign;
  SSpatialLayerConfig* pFinalSpatial    = NULL;
  int32_t iCountBsLen                   = 0;
  int32_t iCountNals                    = 0;
  int32_t iMaxPicWidth                  = 0;
  int32_t iMaxPicHeight                 = 0;
  int32_t iCountMaxMbNum                = 0;
  int32_t iIndex                        = 0;
  int32_t iCountLayers                  = 0;
  int32_t iResult                       = 0;
  float fCompressRatioThr               = .5f;
  const int32_t kiNumDependencyLayers   = pParam->iSpatialLayerNum;
  int32_t iVclLayersBsSizeCount         = 0;
  int32_t iNonVclLayersBsSizeCount      = 0;
  int32_t iTargetSpatialBsSize          = 0;

  if (kiNumDependencyLayers < 1 || kiNumDependencyLayers > MAX_DEPENDENCY_LAYER) {
    WelsLog (& (*ppCtx)->sLogCtx, WELS_LOG_WARNING, "RequestMemorySvc() failed due to invalid iNumDependencyLayers(%d)!",
             kiNumDependencyLayers);
    return 1;
  }

  if (pParam->uiGopSize == 0 || (pParam->uiIntraPeriod && ((pParam->uiIntraPeriod % pParam->uiGopSize) != 0))) {
    WelsLog (& (*ppCtx)->sLogCtx, WELS_LOG_WARNING,
             "RequestMemorySvc() failed due to invalid uiIntraPeriod(%d) (=multipler of uiGopSize(%d)!",
             pParam->uiIntraPeriod, pParam->uiGopSize);
    return 1;
  }

  pFinalSpatial = &pParam->sSpatialLayers[kiNumDependencyLayers - 1];
  iMaxPicWidth  = pFinalSpatial->iVideoWidth;
  iMaxPicHeight = pFinalSpatial->iVideoHeight;
  iCountMaxMbNum = ((15 + iMaxPicWidth) >> 4) * ((15 + iMaxPicHeight) >> 4);

  iResult = AcquireLayersNals (ppCtx, pParam, &iCountLayers, &iCountNals);
  if (iResult) {
    WelsLog (& (*ppCtx)->sLogCtx, WELS_LOG_WARNING, "RequestMemorySvc(), AcquireLayersNals failed(%d)!", iResult);
    return 1;
  }

  const int32_t kiSpsSize = (*ppCtx)->pFuncList->pParametersetStrategy->GetNeededSpsNum() * SPS_BUFFER_SIZE;
  const int32_t kiPpsSize = (*ppCtx)->pFuncList->pParametersetStrategy->GetNeededPpsNum() * PPS_BUFFER_SIZE;
  iNonVclLayersBsSizeCount = SSEI_BUFFER_SIZE + kiSpsSize + kiPpsSize;

  bool    bDynamicSlice = false;
  uint32_t uiMaxSliceNumEstimation = 0;
  int32_t iSliceBufferSize = 0;
  int32_t iMaxSliceBufferSize = 0;
  int32_t iTotalLength = 0;
  int32_t iLayerBsSize = 0;
  iIndex = 0;
  while (iIndex < pParam->iSpatialLayerNum) {
    SSpatialLayerConfig* fDlp = &pParam->sSpatialLayers[iIndex];

    fCompressRatioThr = COMPRESS_RATIO_THR;

    iLayerBsSize = WELS_ROUND (((3 * fDlp->iVideoWidth * fDlp->iVideoHeight) >> 1) * fCompressRatioThr) +
                   MAX_MACROBLOCK_SIZE_IN_BYTE_x2;
    iLayerBsSize = WELS_ALIGN (iLayerBsSize, 4); // 4 bytes alinged
    iVclLayersBsSizeCount += iLayerBsSize;

    SSliceArgument* pSliceArgument = & (fDlp->sSliceArgument);
    (*ppCtx)->iMaxSliceCount = WELS_MAX ((*ppCtx)->iMaxSliceCount, (int) pSliceArgument->uiSliceNum);
    iSliceBufferSize = ((iLayerBsSize / pSliceArgument->uiSliceNum) << 1) + MAX_MACROBLOCK_SIZE_IN_BYTE_x2;
    iMaxSliceBufferSize                = WELS_MAX (iMaxSliceBufferSize, iSliceBufferSize);
    (*ppCtx)->iSliceBufferSize[iIndex] = iSliceBufferSize;
    ++ iIndex;
  }
  iTargetSpatialBsSize = iLayerBsSize;
  iCountBsLen = iNonVclLayersBsSizeCount + iVclLayersBsSizeCount;

  iMaxSliceBufferSize = WELS_MIN (iMaxSliceBufferSize, iTargetSpatialBsSize);
  iTotalLength = iCountBsLen;

  pParam->iNumRefFrame = WELS_CLIP3 (pParam->iNumRefFrame, MIN_REF_PIC_COUNT,
                                     (pParam->iUsageType == CAMERA_VIDEO_REAL_TIME ? MAX_REFERENCE_PICTURE_COUNT_NUM_CAMERA :
                                      MAX_REFERENCE_PICTURE_COUNT_NUM_SCREEN));

  // Output
  (*ppCtx)->pOut = (SWelsEncoderOutput*)pMa->WelsMallocz (sizeof (SWelsEncoderOutput), "SWelsEncoderOutput");
  WELS_VERIFY_RETURN_IF (1, (NULL == (*ppCtx)->pOut))
  (*ppCtx)->pOut->pBsBuffer = (uint8_t*)pMa->WelsMallocz (iCountBsLen, "pOut->pBsBuffer");
  WELS_VERIFY_RETURN_IF (1, (NULL == (*ppCtx)->pOut->pBsBuffer))
  (*ppCtx)->pOut->uiSize = iCountBsLen;
  (*ppCtx)->pOut->sNalList = (SWelsNalRaw*)pMa->WelsMallocz (iCountNals * sizeof (SWelsNalRaw), "pOut->sNalList");
  WELS_VERIFY_RETURN_IF (1, (NULL == (*ppCtx)->pOut->sNalList))
  (*ppCtx)->pOut->pNalLen = (int32_t*)pMa->WelsMallocz (iCountNals * sizeof (int32_t), "pOut->pNalLen");
  WELS_VERIFY_RETURN_IF (1, (NULL == (*ppCtx)->pOut->pNalLen))
  (*ppCtx)->pOut->iCountNals    = iCountNals;
  (*ppCtx)->pOut->iNalIndex     = 0;
  (*ppCtx)->pOut->iLayerBsIndex = 0;

  (*ppCtx)->pFrameBs = (uint8_t*)pMa->WelsMalloc (iTotalLength, "pFrameBs");
  WELS_VERIFY_RETURN_IF (1, (NULL == (*ppCtx)->pFrameBs))
  (*ppCtx)->iFrameBsSize = iTotalLength;
  (*ppCtx)->iPosBsBuffer = 0;

  // for dynamic slice mode&& CABAC,allocate slice buffer to restore slice data
  // for pSlice bs buffers

  (*ppCtx)->pIntra4x4PredModeBlocks = static_cast<int8_t*>
                                      (pMa->WelsMallocz (iCountMaxMbNum * INTRA_4x4_MODE_NUM, "pIntra4x4PredModeBlocks"));
  WELS_VERIFY_RETURN_IF (1, (NULL == (*ppCtx)->pIntra4x4PredModeBlocks))

  (*ppCtx)->pNonZeroCountBlocks = static_cast<int8_t*>
                                  (pMa->WelsMallocz (iCountMaxMbNum * MB_LUMA_CHROMA_BLOCK4x4_NUM, "pNonZeroCountBlocks"));
  WELS_VERIFY_RETURN_IF (1, (NULL == (*ppCtx)->pNonZeroCountBlocks))

  (*ppCtx)->pMvUnitBlock4x4 = static_cast<SMVUnitXY*>
                              (pMa->WelsMallocz (iCountMaxMbNum * 2 * MB_BLOCK4x4_NUM * sizeof (SMVUnitXY), "pMvUnitBlock4x4"));
  WELS_VERIFY_RETURN_IF (1, (NULL == (*ppCtx)->pMvUnitBlock4x4))

  (*ppCtx)->pRefIndexBlock4x4 = static_cast<int8_t*>
                                (pMa->WelsMallocz (iCountMaxMbNum * 2 * MB_BLOCK8x8_NUM * sizeof (int8_t), "pRefIndexBlock4x4"));
  WELS_VERIFY_RETURN_IF (1, (NULL == (*ppCtx)->pRefIndexBlock4x4))

  (*ppCtx)->pSadCostMb = static_cast<int32_t*>
                         (pMa->WelsMallocz (iCountMaxMbNum * sizeof (int32_t), "pSadCostMb"));
  WELS_VERIFY_RETURN_IF (1, (NULL == (*ppCtx)->pSadCostMb))

  (*ppCtx)->iGlobalQp = 26;   // global qp in default

  (*ppCtx)->pLtr = (SLTRState*)pMa->WelsMallocz (kiNumDependencyLayers * sizeof (SLTRState), "SLTRState");
  WELS_VERIFY_RETURN_IF (1, (NULL == (*ppCtx)->pLtr))
  int32_t i = 0;
  for (i = 0; i < kiNumDependencyLayers; i++) {
    ResetLtrState (& (*ppCtx)->pLtr[i]);
  }

  // stride tables
  if (AllocStrideTables (ppCtx, kiNumDependencyLayers)) {
    WelsLog (& (*ppCtx)->sLogCtx, WELS_LOG_WARNING, "RequestMemorySvc(), AllocStrideTables failed!");
    return 1;
  }

  //Rate control module memory allocation
  // only malloc once for RC pData, 12/14/2009
  (*ppCtx)->pWelsSvcRc = (SWelsSvcRc*)pMa->WelsMallocz (kiNumDependencyLayers * sizeof (SWelsSvcRc), "pWelsSvcRc");
  WELS_VERIFY_RETURN_IF (1, (NULL == (*ppCtx)->pWelsSvcRc))
  //End of Rate control module memory allocation

  //pVaa memory allocation
  (*ppCtx)->pVaa = (SVAAFrameInfoExt*)pMa->WelsMallocz (sizeof (SVAAFrameInfoExt), "pVaa");
  WELS_VERIFY_RETURN_IF (1, (NULL == (*ppCtx)->pVaa))
  if (RequestMemoryVaaScreen ((*ppCtx)->pVaa, pMa, (*ppCtx)->pSvcParam->iMaxNumRefFrame, iCountMaxMbNum << 2)) {
    WelsLog (& (*ppCtx)->sLogCtx, WELS_LOG_WARNING, "RequestMemorySvc(), RequestMemoryVaaScreen failed!");
    return 1;
  }

  if ((*ppCtx)->pSvcParam->bEnableAdaptiveQuant) { //malloc mem
    (*ppCtx)->pVaa->sAdaptiveQuantParam.pMotionTextureUnit   = static_cast<SMotionTextureUnit*>
        (pMa->WelsMallocz (iCountMaxMbNum * sizeof (SMotionTextureUnit), "pVaa->sAdaptiveQuantParam.pMotionTextureUnit"));
    WELS_VERIFY_RETURN_IF (1, (NULL == (*ppCtx)->pVaa->sAdaptiveQuantParam.pMotionTextureUnit))
    (*ppCtx)->pVaa->sAdaptiveQuantParam.pMotionTextureIndexToDeltaQp   = static_cast<int8_t*>
        (pMa->WelsMallocz (iCountMaxMbNum * sizeof (int8_t), "pVaa->sAdaptiveQuantParam.pMotionTextureIndexToDeltaQp"));
    WELS_VERIFY_RETURN_IF (1, (NULL == (*ppCtx)->pVaa->sAdaptiveQuantParam.pMotionTextureIndexToDeltaQp))
  }

  (*ppCtx)->pVaa->pVaaBackgroundMbFlag = (int8_t*)pMa->WelsMallocz (iCountMaxMbNum * sizeof (int8_t),
                                         "pVaa->pVaaBackgroundMbFlag");
  WELS_VERIFY_RETURN_IF (1, (NULL == (*ppCtx)->pVaa->pVaaBackgroundMbFlag))

  (*ppCtx)->pVaa->sVaaCalcInfo.pSad8x8 = static_cast<int32_t (*)[4]>
                                         (pMa->WelsMallocz (iCountMaxMbNum * 4 * sizeof (int32_t), "pVaa->sVaaCalcInfo.sad8x8"));
  WELS_VERIFY_RETURN_IF (1, (NULL == (*ppCtx)->pVaa->sVaaCalcInfo.pSad8x8))
  (*ppCtx)->pVaa->sVaaCalcInfo.pSsd16x16 = static_cast<int32_t*>
      (pMa->WelsMallocz (iCountMaxMbNum * sizeof (int32_t), "pVaa->sVaaCalcInfo.pSsd16x16"));
  WELS_VERIFY_RETURN_IF (1, (NULL == (*ppCtx)->pVaa->sVaaCalcInfo.pSsd16x16))
  (*ppCtx)->pVaa->sVaaCalcInfo.pSum16x16 = static_cast<int32_t*>
      (pMa->WelsMallocz (iCountMaxMbNum * sizeof (int32_t), "pVaa->sVaaCalcInfo.pSum16x16"));
  WELS_VERIFY_RETURN_IF (1, (NULL == (*ppCtx)->pVaa->sVaaCalcInfo.pSum16x16))
  (*ppCtx)->pVaa->sVaaCalcInfo.pSumOfSquare16x16 = static_cast<int32_t*>
      (pMa->WelsMallocz (iCountMaxMbNum * sizeof (int32_t), "pVaa->sVaaCalcInfo.pSumOfSquare16x16"));
  WELS_VERIFY_RETURN_IF (1, (NULL == (*ppCtx)->pVaa->sVaaCalcInfo.pSumOfSquare16x16))

  if ((*ppCtx)->pSvcParam->bEnableBackgroundDetection) { //BGD control
    (*ppCtx)->pVaa->sVaaCalcInfo.pSumOfDiff8x8 = static_cast<int32_t (*)[4]>
        (pMa->WelsMallocz (iCountMaxMbNum * 4 * sizeof (int32_t), "pVaa->sVaaCalcInfo.pSumOfDiff8x8"));
    WELS_VERIFY_RETURN_IF (1, (NULL == (*ppCtx)->pVaa->sVaaCalcInfo.pSumOfDiff8x8))
    (*ppCtx)->pVaa->sVaaCalcInfo.pMad8x8 = static_cast<uint8_t (*)[4]>
                                           (pMa->WelsMallocz (iCountMaxMbNum * 4 * sizeof (uint8_t), "pVaa->sVaaCalcInfo.pMad8x8"));
    WELS_VERIFY_RETURN_IF (1, (NULL == (*ppCtx)->pVaa->sVaaCalcInfo.pMad8x8))
  }

  //End of pVaa memory allocation

  (*ppCtx)->ppRefPicListExt = (SRefList**)pMa->WelsMallocz (kiNumDependencyLayers * sizeof (SRefList*),
                              "ppRefPicListExt");
  WELS_VERIFY_RETURN_IF (1, (NULL == (*ppCtx)->ppRefPicListExt))

  (*ppCtx)->ppDqLayerList = (SDqLayer**)pMa->WelsMallocz (kiNumDependencyLayers * sizeof (SDqLayer*), "ppDqLayerList");
  WELS_VERIFY_RETURN_IF (1, (NULL == (*ppCtx)->ppDqLayerList))

  iResult = InitDqLayers (ppCtx, pExistingParasetList);
  if (iResult) {
    WelsLog (& (*ppCtx)->sLogCtx, WELS_LOG_WARNING, "RequestMemorySvc(), InitDqLayers failed(%d)!", iResult);
    return iResult;
  }

  if (InitMbListD (ppCtx)) {
    WelsLog (& (*ppCtx)->sLogCtx, WELS_LOG_WARNING, "RequestMemorySvc(), InitMbListD failed!");
    return 1;
  }

  int32_t iMvdRange = 0;
  GetMvMvdRange (pParam, (*ppCtx)->iMvRange, iMvdRange);
  const uint32_t kuiMvdInterTableSize   = (iMvdRange << 2); //intepel*4=qpel
  const uint32_t kuiMvdInterTableStride =  1 + (kuiMvdInterTableSize << 1);//qpel_mv_range*2=(+/-);
  const uint32_t kuiMvdCacheAlignedSize = kuiMvdInterTableStride * sizeof (uint16_t);

  (*ppCtx)->iMvdCostTableSize = kuiMvdInterTableSize;
  (*ppCtx)->iMvdCostTableStride = kuiMvdInterTableStride;
  (*ppCtx)->pMvdCostTable = (uint16_t*)pMa->WelsMallocz (52 * kuiMvdCacheAlignedSize, "pMvdCostTable");
  WELS_VERIFY_RETURN_IF (1, (NULL == (*ppCtx)->pMvdCostTable))
  MvdCostInit ((*ppCtx)->pMvdCostTable, kuiMvdInterTableStride);  //should put to a better place?

  if ((*ppCtx)->ppRefPicListExt[0] != NULL && (*ppCtx)->ppRefPicListExt[0]->pRef[0] != NULL)
    (*ppCtx)->pDecPic = (*ppCtx)->ppRefPicListExt[0]->pRef[0];
  else
    (*ppCtx)->pDecPic = NULL; // error here

  (*ppCtx)->pSps = & (*ppCtx)->pSpsArray[0];
  (*ppCtx)->pPps = & (*ppCtx)->pPPSArray[0];

  return 0;
}


/*!
 * \brief   free memory in SVC core encoder
 * \pParam  pEncCtx     sWelsEncCtx*
 * \return  none
 */
void FreeMemorySvc (sWelsEncCtx** ppCtx) {
  if (NULL != *ppCtx) {
    sWelsEncCtx* pCtx = *ppCtx;
    CMemoryAlign* pMa = pCtx->pMemAlign;
    SWelsSvcCodingParam* pParam = pCtx->pSvcParam;
    int32_t ilayer = 0;

    // SStrideTables
    if (NULL != pCtx->pStrideTab) {
      if (NULL != pCtx->pStrideTab->pStrideDecBlockOffset[0][1]) {
        pMa->WelsFree (pCtx->pStrideTab->pStrideDecBlockOffset[0][1], "pBase");
        pCtx->pStrideTab->pStrideDecBlockOffset[0][1] = NULL;
      }
      pMa->WelsFree (pCtx->pStrideTab, "SStrideTables");
      pCtx->pStrideTab = NULL;
    }
    // pDq idc map
    if (NULL != pCtx->pDqIdcMap) {
      pMa->WelsFree (pCtx->pDqIdcMap, "pDqIdcMap");
      pCtx->pDqIdcMap = NULL;
    }

    if (NULL != pCtx->pOut) {
      // bs pBuffer
      if (NULL != pCtx->pOut->pBsBuffer) {
        pMa->WelsFree (pCtx->pOut->pBsBuffer, "pOut->pBsBuffer");
        pCtx->pOut->pBsBuffer = NULL;
      }
      // NALs list
      if (NULL != pCtx->pOut->sNalList) {
        pMa->WelsFree (pCtx->pOut->sNalList, "pOut->sNalList");
        pCtx->pOut->sNalList = NULL;
      }
      // NALs len
      if (NULL != pCtx->pOut->pNalLen) {
        pMa->WelsFree (pCtx->pOut->pNalLen, "pOut->pNalLen");
        pCtx->pOut->pNalLen = NULL;
      }
      pMa->WelsFree (pCtx->pOut, "SWelsEncoderOutput");
      pCtx->pOut = NULL;
    }



    // frame bitstream pBuffer
    if (NULL != pCtx->pFrameBs) {
      pMa->WelsFree (pCtx->pFrameBs, "pFrameBs");
      pCtx->pFrameBs = NULL;
    }
    for (int32_t iIdx = 0; iIdx < MAX_THREADS_NUM; iIdx++) {
      pMa->WelsFree (pCtx->pDynamicBsBuffer[iIdx], "DynamicSliceBs");
      pCtx->pDynamicBsBuffer[iIdx] = NULL;

    }
    // pSpsArray
    if (NULL != pCtx->pSpsArray) {
      pMa->WelsFree (pCtx->pSpsArray, "pSpsArray");
      pCtx->pSpsArray = NULL;
    }
    // pPPSArray
    if (NULL != pCtx->pPPSArray) {
      pMa->WelsFree (pCtx->pPPSArray, "pPPSArray");
      pCtx->pPPSArray = NULL;
    }
    // subset_sps_array
    if (NULL != pCtx->pSubsetArray) {
      pMa->WelsFree (pCtx->pSubsetArray, "pSubsetArray");
      pCtx->pSubsetArray = NULL;
    }

    if (NULL != pCtx->pIntra4x4PredModeBlocks) {
      pMa->WelsFree (pCtx->pIntra4x4PredModeBlocks, "pIntra4x4PredModeBlocks");
      pCtx->pIntra4x4PredModeBlocks = NULL;
    }

    if (NULL != pCtx->pNonZeroCountBlocks) {
      pMa->WelsFree (pCtx->pNonZeroCountBlocks, "pNonZeroCountBlocks");
      pCtx->pNonZeroCountBlocks = NULL;
    }

    if (NULL != pCtx->pMvUnitBlock4x4) {
      pMa->WelsFree (pCtx->pMvUnitBlock4x4, "pMvUnitBlock4x4");
      pCtx->pMvUnitBlock4x4 = NULL;
    }

    if (NULL != pCtx->pRefIndexBlock4x4) {
      pMa->WelsFree (pCtx->pRefIndexBlock4x4, "pRefIndexBlock4x4");
      pCtx->pRefIndexBlock4x4 = NULL;
    }

    if (NULL != pCtx->ppMbListD) {
      if (NULL != pCtx->ppMbListD[0]) {
        pMa->WelsFree (pCtx->ppMbListD[0], "ppMbListD[0]");
        (*ppCtx)->ppMbListD[0] = NULL;
      }
      pMa->WelsFree (pCtx->ppMbListD, "ppMbListD");
      pCtx->ppMbListD = NULL;
    }

    if (NULL != pCtx->pSadCostMb) {
      pMa->WelsFree (pCtx->pSadCostMb, "pSadCostMb");
      pCtx->pSadCostMb = NULL;
    }

    // SLTRState
    if (NULL != pCtx->pLtr) {
      pMa->WelsFree (pCtx->pLtr, "SLTRState");
      pCtx->pLtr = NULL;
    }

    // pDq layers list
    ilayer = 0;
    if (NULL != pCtx->ppDqLayerList && pParam != NULL) {
      while (ilayer < pParam->iSpatialLayerNum) {
        SDqLayer* pDq = pCtx->ppDqLayerList[ilayer];
        // pDq layers
        if (NULL != pDq) {
          FreeDqLayer (pDq, pMa);
          pCtx->ppDqLayerList[ilayer] = NULL;
        }
        ++ ilayer;
      }
      pMa->WelsFree (pCtx->ppDqLayerList, "ppDqLayerList");
      pCtx->ppDqLayerList = NULL;
    }
    // reference picture list extension
    if (NULL != pCtx->ppRefPicListExt && pParam != NULL) {
      ilayer = 0;
      while (ilayer < pParam->iSpatialLayerNum) {
        FreeRefList (pCtx->ppRefPicListExt[ilayer], pMa, pParam->iMaxNumRefFrame);
        pCtx->ppRefPicListExt[ilayer] = NULL;
        ++ ilayer;
      }

      pMa->WelsFree (pCtx->ppRefPicListExt, "ppRefPicListExt");
      pCtx->ppRefPicListExt = NULL;
    }

    // VAA
    if (NULL != pCtx->pVaa) {

      pMa->WelsFree (pCtx->pVaa->pVaaBackgroundMbFlag, "pVaa->pVaaBackgroundMbFlag");
      pCtx->pVaa->pVaaBackgroundMbFlag = NULL;
      pMa->WelsFree (pCtx->pVaa->sVaaCalcInfo.pSad8x8, "pVaa->sVaaCalcInfo.sad8x8");
      pCtx->pVaa->sVaaCalcInfo.pSad8x8 = NULL;
      pMa->WelsFree (pCtx->pVaa->sVaaCalcInfo.pSsd16x16, "pVaa->sVaaCalcInfo.pSsd16x16");
      pCtx->pVaa->sVaaCalcInfo.pSsd16x16 = NULL;
      pMa->WelsFree (pCtx->pVaa->sVaaCalcInfo.pSum16x16, "pVaa->sVaaCalcInfo.pSum16x16");
      pCtx->pVaa->sVaaCalcInfo.pSum16x16 = NULL;
      pMa->WelsFree (pCtx->pVaa->sVaaCalcInfo.pSumOfSquare16x16, "pVaa->sVaaCalcInfo.pSumOfSquare16x16");
      pCtx->pVaa->sVaaCalcInfo.pSumOfSquare16x16 = NULL;

      ReleaseMemoryVaaScreen (pCtx->pVaa, pMa, pCtx->pSvcParam->iMaxNumRefFrame);
      pMa->WelsFree (pCtx->pVaa, "pVaa");
      pCtx->pVaa = NULL;
    }

    // rate control module memory free
    if (NULL != pCtx->pWelsSvcRc) {
      WelsRcFreeMemory (pCtx);
      pMa->WelsFree (pCtx->pWelsSvcRc, "pWelsSvcRc");
      pCtx->pWelsSvcRc = NULL;
    }

    /* MVD cost tables for Inter */
    if (NULL != pCtx->pMvdCostTable) {
      pMa->WelsFree (pCtx->pMvdCostTable, "pMvdCostTable");
      pCtx->pMvdCostTable = NULL;
    }

    FreeCodingParam (&pCtx->pSvcParam, pMa);
    if (NULL != pCtx->pFuncList) {
      if (NULL != pCtx->pFuncList->pParametersetStrategy) {
        WELS_DELETE_OP (pCtx->pFuncList->pParametersetStrategy);
      }

      pMa->WelsFree (pCtx->pFuncList, "SWelsFuncPtrList");
      pCtx->pFuncList = NULL;
    }

    assert (pMa->WelsGetMemoryUsage() == 0); // ensure all memory free well

    if ((*ppCtx)->pMemAlign != NULL) {
      WelsLog (& (*ppCtx)->sLogCtx, WELS_LOG_INFO, "FreeMemorySvc(), verify memory usage (%d bytes) after free..",
               (*ppCtx)->pMemAlign->WelsGetMemoryUsage());
      WELS_DELETE_OP ((*ppCtx)->pMemAlign);
    }

    free (*ppCtx);
    *ppCtx = NULL;
  }
}

int32_t InitSliceSettings (SLogContext* pLogCtx,     SWelsSvcCodingParam* pCodingParam,
                           const int32_t kiCpuCores, int16_t* pMaxSliceCount) {
  int32_t iSpatialIdx = 0, iSpatialNum = pCodingParam->iSpatialLayerNum;
  uint16_t iMaxSliceCount = 0;

  do {
    SSpatialLayerConfig* pDlp           = &pCodingParam->sSpatialLayers[iSpatialIdx];
    SSliceArgument* pSliceArgument      = &pDlp->sSliceArgument;
    int32_t iReturn                     = 0;

    switch (pSliceArgument->uiSliceMode) {
    case SM_SIZELIMITED_SLICE:
      iMaxSliceCount = AVERSLICENUM_CONSTRAINT;
      break; // go through for SM_SIZELIMITED_SLICE?
    case SM_FIXEDSLCNUM_SLICE: {
      iReturn = SliceArgumentValidationFixedSliceMode (pLogCtx, &pDlp->sSliceArgument, pCodingParam->iRCMode,
                pDlp->iVideoWidth, pDlp->iVideoHeight);
      if (iReturn)
        return ENC_RETURN_UNSUPPORTED_PARA;

      if (pSliceArgument->uiSliceNum > iMaxSliceCount) {
        iMaxSliceCount = pSliceArgument->uiSliceNum;
      }
    }
    break;
    case SM_SINGLE_SLICE:
      if (pSliceArgument->uiSliceNum > iMaxSliceCount)
        iMaxSliceCount = pSliceArgument->uiSliceNum;
      break;
    case SM_RASTER_SLICE:
      if (pSliceArgument->uiSliceNum > iMaxSliceCount)
        iMaxSliceCount = pSliceArgument->uiSliceNum;
      break;
    default:
      break;
    }

    ++ iSpatialIdx;
  } while (iSpatialIdx < iSpatialNum);

  pCodingParam->iMultipleThreadIdc = WELS_MIN (kiCpuCores, iMaxSliceCount);
   // Disable loop filter on slice boundaries since that's not allowed with multithreading
  *pMaxSliceCount = iMaxSliceCount;

  return ENC_RETURN_SUCCESS;
}

/*!
 * \brief   log output for cpu features/capabilities
 */
void OutputCpuFeaturesLog (SLogContext* pLogCtx, uint32_t uiCpuFeatureFlags, uint32_t uiCpuCores,
                           int32_t iCacheLineSize) {
  // welstracer output
  WelsLog (pLogCtx, WELS_LOG_INFO, "WELS CPU features/capacities (0x%x) detected: \t"
           "HTT:      %c, "
           "MMX:      %c, "
           "MMXEX:    %c, "
           "SSE:      %c, "
           "SSE2:     %c, "
           "SSE3:     %c, "
           "SSSE3:    %c, "
           "SSE4.1:   %c, "
           "SSE4.2:   %c, "
           "AVX:      %c, "
           "FMA:      %c, "
           "X87-FPU:  %c, "
           "3DNOW:    %c, "
           "3DNOWEX:  %c, "
           "ALTIVEC:  %c, "
           "CMOV:     %c, "
           "MOVBE:    %c, "
           "AES:      %c, "
           "NUMBER OF LOGIC PROCESSORS ON CHIP: %d, "
           "CPU CACHE LINE SIZE (BYTES):        %d",
           uiCpuFeatureFlags,
           (uiCpuFeatureFlags & WELS_CPU_HTT) ? 'Y' : 'N',
           (uiCpuFeatureFlags & WELS_CPU_MMX) ? 'Y' : 'N',
           (uiCpuFeatureFlags & WELS_CPU_MMXEXT) ? 'Y' : 'N',
           (uiCpuFeatureFlags & WELS_CPU_SSE) ? 'Y' : 'N',
           (uiCpuFeatureFlags & WELS_CPU_SSE2) ? 'Y' : 'N',
           (uiCpuFeatureFlags & WELS_CPU_SSE3) ? 'Y' : 'N',
           (uiCpuFeatureFlags & WELS_CPU_SSSE3) ? 'Y' : 'N',
           (uiCpuFeatureFlags & WELS_CPU_SSE41) ? 'Y' : 'N',
           (uiCpuFeatureFlags & WELS_CPU_SSE42) ? 'Y' : 'N',
           (uiCpuFeatureFlags & WELS_CPU_AVX) ? 'Y' : 'N',
           (uiCpuFeatureFlags & WELS_CPU_FMA) ? 'Y' : 'N',
           (uiCpuFeatureFlags & WELS_CPU_FPU) ? 'Y' : 'N',
           (uiCpuFeatureFlags & WELS_CPU_3DNOW) ? 'Y' : 'N',
           (uiCpuFeatureFlags & WELS_CPU_3DNOWEXT) ? 'Y' : 'N',
           (uiCpuFeatureFlags & WELS_CPU_ALTIVEC) ? 'Y' : 'N',
           (uiCpuFeatureFlags & WELS_CPU_CMOV) ? 'Y' : 'N',
           (uiCpuFeatureFlags & WELS_CPU_MOVBE) ? 'Y' : 'N',
           (uiCpuFeatureFlags & WELS_CPU_AES) ? 'Y' : 'N',
           uiCpuCores,
           iCacheLineSize);
}
/*
 *
 * status information output
 */


int32_t GetMultipleThreadIdc (SLogContext* pLogCtx, SWelsSvcCodingParam* pCodingParam, int16_t& iSliceNum,
                              int32_t& iCacheLineSize, uint32_t& uiCpuFeatureFlags) {
  // for cpu features detection, Only detect once??
  int32_t uiCpuCores =
    0; // number of logic processors on physical processor package, zero logic processors means HTT not supported
  uiCpuFeatureFlags = WelsCPUFeatureDetect (&uiCpuCores); // detect cpu capacity features

#ifdef X86_ASM
  if (uiCpuFeatureFlags & WELS_CPU_CACHELINE_128)
    iCacheLineSize = 128;
  else if (uiCpuFeatureFlags & WELS_CPU_CACHELINE_64)
    iCacheLineSize = 64;
  else if (uiCpuFeatureFlags & WELS_CPU_CACHELINE_32)
    iCacheLineSize = 32;
  else if (uiCpuFeatureFlags & WELS_CPU_CACHELINE_16)
    iCacheLineSize = 16;
  OutputCpuFeaturesLog (pLogCtx, uiCpuFeatureFlags, uiCpuCores, iCacheLineSize);
#else
  iCacheLineSize = 16; // 16 bytes aligned in default
#endif//X86_ASM

  pCodingParam->iMultipleThreadIdc = 1; // jumpseat: threads removed
  uiCpuCores = 1;

  if (InitSliceSettings (pLogCtx, pCodingParam, uiCpuCores, &iSliceNum)) {
    WelsLog (pLogCtx, WELS_LOG_ERROR, "GetMultipleThreadIdc(), InitSliceSettings failed.");
    return 1;
  }
  return 0;
}

/*!
 * \brief   uninitialize Wels encoder core library
 * \pParam  pEncCtx     sWelsEncCtx*
 * \return  none
 */
void WelsUninitEncoderExt (sWelsEncCtx** ppCtx) {
  if (NULL == ppCtx || NULL == *ppCtx)
    return;

  WelsLog (& (*ppCtx)->sLogCtx, WELS_LOG_INFO,
           "WelsUninitEncoderExt(), pCtx= %p, iMultipleThreadIdc= %d.",
           (void*) (*ppCtx), (*ppCtx)->pSvcParam->iMultipleThreadIdc);


  if ((*ppCtx)->pVpp) {
    (*ppCtx)->pVpp->FreeSpatialPictures (*ppCtx);
    WELS_DELETE_OP ((*ppCtx)->pVpp);
  }
  FreeMemorySvc (ppCtx);
  *ppCtx = NULL;
}

/*!
 * \brief   initialize Wels avc encoder core library
 * \pParam  ppCtx       sWelsEncCtx**
 * \pParam  pParam      SWelsSvcCodingParam*
 * \return  successful - 0; otherwise none 0 for failed
 */
int32_t WelsInitEncoderExt (sWelsEncCtx** ppCtx, SWelsSvcCodingParam* pCodingParam, SLogContext* pLogCtx,
                            SExistingParasetList* pExistingParasetList) {
  sWelsEncCtx* pCtx      = NULL;
  int32_t iRet           = 0;
  int16_t iSliceNum      = 1;    // number of slices used
  int32_t iCacheLineSize = 16;   // on chip cache line size in byte
  uint32_t uiCpuFeatureFlags = 0;
  if (NULL == ppCtx || NULL == pCodingParam) {
    WelsLog (pLogCtx, WELS_LOG_ERROR, "WelsInitEncoderExt(), NULL == ppCtx(0x%p) or NULL == pCodingParam(0x%p).",
             (void*)ppCtx, (void*)pCodingParam);
    return 1;
  }

  iRet = ParamValidationExt (pLogCtx, pCodingParam);
  if (iRet != 0) {
    WelsLog (pLogCtx, WELS_LOG_ERROR, "WelsInitEncoderExt(), ParamValidationExt failed return %d.", iRet);
    return iRet;
  }
  iRet = pCodingParam->DetermineTemporalSettings();
  if (iRet != ENC_RETURN_SUCCESS) {
    WelsLog (pLogCtx, WELS_LOG_ERROR,
             "WelsInitEncoderExt(), DetermineTemporalSettings failed return %d (check in/out frame rate and temporal layer setting! -- in/out = 2^x, x <= temppral_layer_num)",
             iRet);
    return iRet;
  }
  iRet = GetMultipleThreadIdc (pLogCtx, pCodingParam, iSliceNum, iCacheLineSize, uiCpuFeatureFlags);
  if (iRet != 0) {
    WelsLog (pLogCtx, WELS_LOG_ERROR, "WelsInitEncoderExt(), GetMultipleThreadIdc failed return %d.", iRet);
    return iRet;
  }


  *ppCtx = NULL;

  pCtx = static_cast<sWelsEncCtx*> (malloc (sizeof (sWelsEncCtx)));

  WELS_VERIFY_RETURN_IF (1, (NULL == pCtx))
  memset (pCtx, 0, sizeof (sWelsEncCtx));

  pCtx->sLogCtx = *pLogCtx;

  pCtx->pMemAlign = new CMemoryAlign (iCacheLineSize);
  WELS_VERIFY_RETURN_PROC_IF (1, (NULL == pCtx->pMemAlign), WelsUninitEncoderExt (&pCtx))

  iRet = AllocCodingParam (&pCtx->pSvcParam, pCtx->pMemAlign);
  if (iRet != 0) {
    WelsUninitEncoderExt (&pCtx);
    return iRet;
  }
  memcpy ((void*) pCtx->pSvcParam, pCodingParam, sizeof (SWelsSvcCodingParam)); // confirmed_safe_unsafe_usage

  pCtx->pFuncList = (SWelsFuncPtrList*)pCtx->pMemAlign->WelsMallocz (sizeof (SWelsFuncPtrList), "SWelsFuncPtrList");
  if (NULL == pCtx->pFuncList) {
    WelsUninitEncoderExt (&pCtx);
    return 1;
  }
  InitFunctionPointers (pCtx, pCtx->pSvcParam, uiCpuFeatureFlags);

  pCtx->iActiveThreadsNum = pCodingParam->iMultipleThreadIdc;
  pCtx->iMaxSliceCount = iSliceNum;
  iRet = RequestMemorySvc (&pCtx, pExistingParasetList);
  if (iRet != 0) {
    WelsLog (pLogCtx, WELS_LOG_ERROR, "WelsInitEncoderExt(), RequestMemorySvc failed return %d.", iRet);
    WelsUninitEncoderExt (&pCtx);
    return iRet;
  }

  WelsRcInitModule (pCtx);

  pCtx->pVpp = CWelsPreProcess::CreatePreProcess (pCtx);
  if (pCtx->pVpp == NULL) {
    iRet = 1;
    WelsLog (pLogCtx, WELS_LOG_ERROR, "WelsInitEncoderExt(), pOut of memory in case new CWelsPreProcess().");
    WelsUninitEncoderExt (&pCtx);
    return iRet;
  }
  if ((iRet = pCtx->pVpp->AllocSpatialPictures (pCtx, pCtx->pSvcParam)) != 0) {
    WelsLog (pLogCtx, WELS_LOG_ERROR, "WelsInitEncoderExt(), pVPP alloc spatial pictures failed");
    WelsUninitEncoderExt (&pCtx);
    return iRet;
  }

  WelsLog (pLogCtx, WELS_LOG_INFO, "WelsInitEncoderExt() exit, overall memory usage: %llu bytes",
           static_cast<unsigned long long> (sizeof (sWelsEncCtx) /* requested size from malloc() or new operator */
               + pCtx->pMemAlign->WelsGetMemoryUsage())  /* requested size from CMemoryAlign::WelsMalloc() */
          );

  pCtx->iStatisticsLogInterval = STATISTICS_LOG_INTERVAL_MS;
  pCtx->uiLastTimestamp = -1;
  pCtx->bDeliveryFlag = true;
  *ppCtx = pCtx;

  WelsLog (pLogCtx, WELS_LOG_INFO, "WelsInitEncoderExt(), pCtx= 0x%p.", (void*)pCtx);

  return 0;
}
/*!
 * \brief   get temporal level due to configuration and coding context
 */
int32_t GetTemporalLevel (SSpatialLayerInternal* fDlp, const int32_t kiFrameNum, const int32_t kiGopSize) {
  const int32_t kiCodingIdx = kiFrameNum & (kiGopSize - 1);

  return fDlp->uiCodingIdx2TemporalId[kiCodingIdx];
}






/*!
 * \brief   initialize current layer
 */
void WelsInitCurrentLayer (sWelsEncCtx* pCtx,
                           const int32_t kiWidth,
                           const int32_t kiHeight) {
  SWelsSvcCodingParam* pParam   = pCtx->pSvcParam;
  SPicture* pEncPic             = pCtx->pEncPic;
  SPicture* pDecPic             = pCtx->pDecPic;
  SDqLayer* pCurDq              = pCtx->pCurDqLayer;
  SSlice*   pBaseSlice          = pCurDq->ppSliceInLayer[0];
  const uint8_t kiCurDid        = pCtx->uiDependencyId;
  const bool kbUseSubsetSpsFlag = (!pParam->bSimulcastAVC) && (kiCurDid > BASE_DEPENDENCY_ID);
  SNalUnitHeaderExt* pNalHdExt  = &pCurDq->sLayerInfo.sNalHeaderExt;
  SNalUnitHeader* pNalHd        = &pNalHdExt->sNalUnitHeader;
  SDqIdc* pDqIdc                = &pCtx->pDqIdcMap[kiCurDid];
  int32_t iIdx                  = 0;
  int32_t iSliceCount           = pCurDq->iMaxSliceNum;
  SSpatialLayerInternal* pParamInternal = &pParam->sDependencyLayers[kiCurDid];
  if (NULL == pCurDq || NULL == pBaseSlice)
    return;

  pCurDq->pDecPic = pDecPic;

  assert (iSliceCount > 0);

  int32_t iCurPpsId = pDqIdc->iPpsId;
  int32_t iCurSpsId = pDqIdc->iSpsId;

  iCurPpsId = pCtx->pFuncList->pParametersetStrategy->GetCurrentPpsId (iCurPpsId,
              WELS_ABS (pParamInternal->uiIdrPicId - 1) % MAX_PPS_COUNT);

  pBaseSlice->sSliceHeaderExt.sSliceHeader.iPpsId       = iCurPpsId;
  pCurDq->sLayerInfo.pPpsP                              =
    pBaseSlice->sSliceHeaderExt.sSliceHeader.pPps       = &pCtx->pPPSArray[iCurPpsId];

  pBaseSlice->sSliceHeaderExt.sSliceHeader.iSpsId       = iCurSpsId;
  if (kbUseSubsetSpsFlag) {
    pCurDq->sLayerInfo.pSubsetSpsP                      = &pCtx->pSubsetArray[iCurSpsId];
    pCurDq->sLayerInfo.pSpsP                            =
      pBaseSlice->sSliceHeaderExt.sSliceHeader.pSps     = &pCurDq->sLayerInfo.pSubsetSpsP->pSps;
  } else {
    pCurDq->sLayerInfo.pSubsetSpsP                      = NULL;
    pCurDq->sLayerInfo.pSpsP                            =
      pBaseSlice->sSliceHeaderExt.sSliceHeader.pSps     = &pCtx->pSpsArray[iCurSpsId];
  }

  pBaseSlice->bSliceHeaderExtFlag = (NAL_UNIT_CODED_SLICE_EXT == pCtx->eNalType);

  iIdx = 1;
  while (iIdx < iSliceCount) {
    InitSliceHeadWithBase (pCurDq->ppSliceInLayer[iIdx], pBaseSlice);
    ++ iIdx;
  }

  memset (pNalHdExt, 0, sizeof (SNalUnitHeaderExt));
  pNalHd->uiNalRefIdc                   = pCtx->eNalPriority;
  pNalHd->eNalUnitType                  = pCtx->eNalType;

  pNalHdExt->uiDependencyId             = kiCurDid;
  pNalHdExt->bDiscardableFlag           = (pCtx->bNeedPrefixNalFlag) ? (pNalHd->uiNalRefIdc == NRI_PRI_LOWEST) : false;
  pNalHdExt->bIdrFlag                   = (pParamInternal->iFrameNum == 0)
                                          && ((pCtx->eNalType == NAL_UNIT_CODED_SLICE_IDR)
                                              || (pCtx->eSliceType == I_SLICE));
  pNalHdExt->uiTemporalId               = pCtx->uiTemporalId;

  // pEncPic pData
  pCurDq->pEncData[0]   = pEncPic->pData[0];
  pCurDq->pEncData[1]   = pEncPic->pData[1];
  pCurDq->pEncData[2]   = pEncPic->pData[2];
  pCurDq->iEncStride[0] = pEncPic->iLineSize[0];
  pCurDq->iEncStride[1] = pEncPic->iLineSize[1];
  pCurDq->iEncStride[2] = pEncPic->iLineSize[2];
  // cs pData
  pCurDq->pCsData[0]    = pDecPic->pData[0];
  pCurDq->pCsData[1]    = pDecPic->pData[1];
  pCurDq->pCsData[2]    = pDecPic->pData[2];
  pCurDq->iCsStride[0]  = pDecPic->iLineSize[0];
  pCurDq->iCsStride[1]  = pDecPic->iLineSize[1];
  pCurDq->iCsStride[2]  = pDecPic->iLineSize[2];

  if (pCurDq->pRefLayer != NULL) {
    pCurDq->bBaseLayerAvailableFlag = true;
  } else {
    pCurDq->bBaseLayerAvailableFlag = false;
  }

}

static inline void SetFastCodingFunc (SWelsFuncPtrList* pFuncList) {
  pFuncList->pfIntraFineMd = WelsMdIntraFinePartitionVaa;
  pFuncList->sSampleDealingFuncs.pfMdCost = pFuncList->sSampleDealingFuncs.pfSampleSad;
  pFuncList->sSampleDealingFuncs.pfIntra16x16Combined3 = pFuncList->sSampleDealingFuncs.pfIntra16x16Combined3Sad;
  pFuncList->sSampleDealingFuncs.pfIntra8x8Combined3 = pFuncList->sSampleDealingFuncs.pfIntra8x8Combined3Sad;
}
static inline void SetNormalCodingFunc (SWelsFuncPtrList* pFuncList) {
  pFuncList->pfIntraFineMd = WelsMdIntraFinePartition;
  pFuncList->sSampleDealingFuncs.pfMdCost = pFuncList->sSampleDealingFuncs.pfSampleSatd;
  pFuncList->sSampleDealingFuncs.pfIntra16x16Combined3 =
    pFuncList->sSampleDealingFuncs.pfIntra16x16Combined3Satd;
  pFuncList->sSampleDealingFuncs.pfIntra8x8Combined3 =
    pFuncList->sSampleDealingFuncs.pfIntra8x8Combined3Satd;
  pFuncList->sSampleDealingFuncs.pfIntra4x4Combined3 =
    pFuncList->sSampleDealingFuncs.pfIntra4x4Combined3Satd;
}



void PreprocessSliceCoding (sWelsEncCtx* pCtx) {
  SDqLayer* pCurLayer           = pCtx->pCurDqLayer;
  //const bool kbBaseAvail      = pCurLayer->bBaseLayerAvailableFlag;
  SWelsFuncPtrList* pFuncList = pCtx->pFuncList;
  SLogContext* pLogCtx = & (pCtx->sLogCtx);
  /* function pointers conditional assignment under sWelsEncCtx, layer_mb_enc_rec (in stack) is exclusive */
  if (P_SLICE == pCtx->eSliceType) {
    SetFastCodingFunc (pFuncList);
  } else {
    SetNormalCodingFunc (pFuncList);
  }

  if (P_SLICE == pCtx->eSliceType) {
    for (int i = 0; i < BLOCK_STATIC_IDC_ALL; i++) {
      pFuncList->pfMotionSearch[i] = WelsMotionEstimateSearch;
    }
    pFuncList->pfSearchMethod[BLOCK_16x16]  =
      pFuncList->pfSearchMethod[BLOCK_16x8] =
        pFuncList->pfSearchMethod[BLOCK_8x16] =
          pFuncList->pfSearchMethod[BLOCK_8x8] =
            pFuncList->pfSearchMethod[BLOCK_4x4] =
              pFuncList->pfSearchMethod[BLOCK_8x4] =
                pFuncList->pfSearchMethod[BLOCK_4x8] = WelsDiamondSearch;
    pFuncList->pfFirstIntraMode = WelsMdFirstIntraMode;
    pFuncList->sSampleDealingFuncs.pfMeCost = pCtx->pFuncList->sSampleDealingFuncs.pfSampleSatd;
    pFuncList->pfSetScrollingMv = SetScrollingMvToMdNull;
    pFuncList->pfCalculateSatd = NotCalculateSatdCost;
  } else {
    pFuncList->sSampleDealingFuncs.pfMeCost = NULL;
  }

  //to init at each frame will be needed when dealing with hybrid content (camera+screen)
  if (P_SLICE == pCtx->eSliceType) {
    //MD related func pointers
    pFuncList->pfInterFineMd = WelsMdInterFinePartitionVaaOnScreen;

    //ME related func pointers
    SVAAFrameInfoExt* pVaaExt = static_cast<SVAAFrameInfoExt*> (pCtx->pVaa);
    if (pVaaExt->sScrollDetectInfo.bScrollDetectFlag
        && (pVaaExt->sScrollDetectInfo.iScrollMvX | pVaaExt->sScrollDetectInfo.iScrollMvY)) {
      pFuncList->pfSetScrollingMv = SetScrollingMvToMd;
    } else {
      pFuncList->pfSetScrollingMv = SetScrollingMvToMdNull;
    }

    pFuncList->pfMotionSearch[NO_STATIC] = WelsMotionEstimateSearch;
    pFuncList->pfMotionSearch[COLLOCATED_STATIC] = WelsMotionEstimateSearchStatic;
    pFuncList->pfMotionSearch[SCROLLED_STATIC] = WelsMotionEstimateSearchScrolled;
    //ME16x16
    pFuncList->pfSearchMethod[BLOCK_16x16] = WelsDiamondCrossSearch;
    // jumpseat: on scrolling frames, the cross search (16x16 and 8x8
    // partitions) finds content moved along an axis. OpenH264 enabled it
    // only by building a per-pixel feature index of the reference frame,
    // whose lookups never changed a motion vector on desktop content: we
    // set the cross search's threshold directly instead (scroll CPU -16%,
    // same bits). Other frames keep to diamond search and 16x16
    // (WelsMdInterFinePartitionVaaOnScreen): partitions there cost CPU and
    // saved nothing.
    SScreenBlockFeatureStorage* pRefStorage = pCurLayer->pRefPic->pScreenBlockFeatureStorage;
    const bool kbScrolling = pVaaExt->sScrollDetectInfo.bScrollDetectFlag
                             && (pVaaExt->sScrollDetectInfo.iScrollMvX | pVaaExt->sScrollDetectInfo.iScrollMvY);
    if (pRefStorage) {
      SetCrossSearchThresholds (pCurLayer->pRefPic, pRefStorage, kbScrolling);
    }
    if (kbScrolling) {
      pFuncList->pfSearchMethod[BLOCK_8x8] = WelsDiamondCrossSearch;
    }
  }

  // update some layer dependent variable to save judgements in mb-level
  pCurLayer->bSatdInMdFlag = ((pFuncList->sSampleDealingFuncs.pfMeCost == pFuncList->sSampleDealingFuncs.pfSampleSatd)
                              && (pFuncList->sSampleDealingFuncs.pfMdCost == pFuncList->sSampleDealingFuncs.pfSampleSatd));

  const int32_t kiCurDid            = pCtx->uiDependencyId;
  const int32_t kiCurTid            = pCtx->uiTemporalId;
}

/*!
 * \brief   swap pDq layers between current pDq layer and reference pDq layer
 */

static inline void WelsSwapDqLayers (sWelsEncCtx* pCtx, const int32_t kiNextDqIdx) {
  // swap and assign reference
  SDqLayer* pTmpLayer           = pCtx->ppDqLayerList[kiNextDqIdx];
  SDqLayer* pRefLayer           = pCtx->pCurDqLayer;
  pCtx->pCurDqLayer             = pTmpLayer;
  pCtx->pCurDqLayer->pRefLayer  = pRefLayer;
}

/*!
 * \brief   prefetch reference picture after WelsBuildRefList
 */
static inline void PrefetchReferencePicture (sWelsEncCtx* pCtx, const EVideoFrameType keFrameType) {
  const int32_t kiSliceCount = pCtx->pCurDqLayer->iMaxSliceNum;
  int32_t iIdx = 0;
  uint8_t uiRefIdx = -1;

  assert (kiSliceCount > 0);
  if (keFrameType != videoFrameTypeIDR) {
    assert (pCtx->iNumRef0 > 0);
    pCtx->pRefPic               = pCtx->pRefList0[0];   // always get item 0 due to reordering done
    pCtx->pCurDqLayer->pRefPic  = pCtx->pRefPic;
    uiRefIdx                    = 0;                    // reordered reference iIndex
  } else { // safe for IDR coding
    pCtx->pRefPic               = NULL;
    pCtx->pCurDqLayer->pRefPic  = NULL;
  }

  iIdx = 0;
  while (iIdx < kiSliceCount) {
    pCtx->pCurDqLayer->ppSliceInLayer[iIdx]->sSliceHeaderExt.sSliceHeader.uiRefIndex = uiRefIdx;
    ++ iIdx;
  }
}

int32_t WelsWriteOneSPS (sWelsEncCtx* pCtx, const int32_t kiSpsIdx, int32_t& iNalSize) {
  int iNal = pCtx->pOut->iNalIndex;
  WelsLoadNal (pCtx->pOut, NAL_UNIT_SPS, NRI_PRI_HIGHEST);

  WelsWriteSpsNal (&pCtx->pSpsArray[kiSpsIdx], &pCtx->pOut->sBsWrite,
                   pCtx->pFuncList->pParametersetStrategy->GetSpsIdOffsetList (PARA_SET_TYPE_AVCSPS));
  WelsUnloadNal (pCtx->pOut);

  int32_t iReturn = WelsEncodeNal (&pCtx->pOut->sNalList[iNal], NULL,
                                   pCtx->iFrameBsSize - pCtx->iPosBsBuffer,//available buffer to be written, so need to substract the used length
                                   pCtx->pFrameBs + pCtx->iPosBsBuffer,
                                   &iNalSize);
  WELS_VERIFY_RETURN_IFNEQ (iReturn, ENC_RETURN_SUCCESS)

  pCtx->iPosBsBuffer += iNalSize;
  return ENC_RETURN_SUCCESS;
}

int32_t WelsWriteOnePPS (sWelsEncCtx* pCtx, const int32_t kiPpsIdx, int32_t& iNalSize) {
  //TODO
  int32_t iNal = pCtx->pOut->iNalIndex;
  /* generate picture parameter set */
  WelsLoadNal (pCtx->pOut, NAL_UNIT_PPS, NRI_PRI_HIGHEST);

  WelsWritePpsSyntax (&pCtx->pPPSArray[kiPpsIdx], &pCtx->pOut->sBsWrite,
                      pCtx->pFuncList->pParametersetStrategy);
  WelsUnloadNal (pCtx->pOut);

  int32_t iReturn = WelsEncodeNal (&pCtx->pOut->sNalList[iNal], NULL,
                                   pCtx->iFrameBsSize - pCtx->iPosBsBuffer,
                                   pCtx->pFrameBs + pCtx->iPosBsBuffer,
                                   &iNalSize);
  WELS_VERIFY_RETURN_IFNEQ (iReturn, ENC_RETURN_SUCCESS)

  pCtx->iPosBsBuffer += iNalSize;
  return ENC_RETURN_SUCCESS;
}


/*!
 * \brief   write all parameter sets introduced in SVC extension
 * \return  writing results, success or error
 */
int32_t WelsWriteParameterSets (sWelsEncCtx* pCtx, int32_t* pNalLen, int32_t* pNumNal, int32_t* pTotalLength) {
  int32_t iSize = 0;
  int32_t iNal  = 0;
  int32_t iIdx  = 0;
  int32_t iId   = 0;
  int32_t iCountNal     = 0;
  int32_t iNalLength    = 0;
  int32_t iReturn = ENC_RETURN_SUCCESS;

  if (NULL == pCtx || NULL == pNalLen || NULL == pNumNal || NULL == pCtx->pFuncList->pParametersetStrategy)
    return ENC_RETURN_UNEXPECTED;

  *pTotalLength = 0;
  /* write all SPS */
  iIdx = 0;
  while (iIdx < pCtx->iSpsNum) {
    pCtx->pFuncList->pParametersetStrategy->Update (pCtx->pSpsArray[iIdx].uiSpsId, PARA_SET_TYPE_AVCSPS);
    /* generate sequence parameters set */
    iId = pCtx->pFuncList->pParametersetStrategy->GetSpsIdx (iIdx);

    WelsWriteOneSPS (pCtx, iId, iNalLength);

    pNalLen[iCountNal] = iNalLength;
    iSize += iNalLength;

    ++ iIdx;
    ++ iCountNal;
  }

  /* write all Subset SPS */
  iIdx = 0;
  while (iIdx < pCtx->iSubsetSpsNum) {
    iNal = pCtx->pOut->iNalIndex;

    pCtx->pFuncList->pParametersetStrategy->Update (pCtx->pSubsetArray[iIdx].pSps.uiSpsId, PARA_SET_TYPE_SUBSETSPS);

    iId = iIdx;

    /* generate Subset SPS */
    WelsLoadNal (pCtx->pOut, NAL_UNIT_SUBSET_SPS, NRI_PRI_HIGHEST);

    WelsWriteSubsetSpsSyntax (&pCtx->pSubsetArray[iId], &pCtx->pOut->sBsWrite,
                              pCtx->pFuncList->pParametersetStrategy->GetSpsIdOffsetList (PARA_SET_TYPE_SUBSETSPS));
    WelsUnloadNal (pCtx->pOut);

    iReturn = WelsEncodeNal (&pCtx->pOut->sNalList[iNal], NULL,
                             pCtx->iFrameBsSize - pCtx->iPosBsBuffer,//available buffer to be written, so need to substract the used length
                             pCtx->pFrameBs + pCtx->iPosBsBuffer,
                             &iNalLength);
    WELS_VERIFY_RETURN_IFNEQ (iReturn, ENC_RETURN_SUCCESS)
    pNalLen[iCountNal] = iNalLength;

    pCtx->iPosBsBuffer  += iNalLength;
    iSize               += iNalLength;

    ++ iIdx;
    ++ iCountNal;
  }

  pCtx->pFuncList->pParametersetStrategy->UpdatePpsList (pCtx);

  iIdx = 0;
  while (iIdx < pCtx->iPpsNum) {
    pCtx->pFuncList->pParametersetStrategy->Update (pCtx->pPPSArray[iIdx].iPpsId, PARA_SET_TYPE_PPS);

    WelsWriteOnePPS (pCtx, iIdx, iNalLength);

    pNalLen[iCountNal] = iNalLength;
    iSize += iNalLength;

    ++ iIdx;
    ++ iCountNal;
  }

  *pNumNal = iCountNal;
  *pTotalLength = iSize;

  return ENC_RETURN_SUCCESS;
}

static inline int32_t AddPrefixNal (sWelsEncCtx* pCtx,
                                    SLayerBSInfo* pLayerBsInfo,
                                    int32_t* pNalLen,
                                    int32_t* pNalIdxInLayer,
                                    const EWelsNalUnitType keNalType,
                                    const EWelsNalRefIdc keNalRefIdc,
                                    int32_t& iPayloadSize) {
  int32_t iReturn = ENC_RETURN_SUCCESS;
  iPayloadSize = 0;

  if (keNalRefIdc != NRI_PRI_LOWEST) {
    WelsLoadNal (pCtx->pOut, NAL_UNIT_PREFIX, keNalRefIdc);

    WelsWriteSVCPrefixNal (&pCtx->pOut->sBsWrite, keNalRefIdc, (NAL_UNIT_CODED_SLICE_IDR == keNalType));

    WelsUnloadNal (pCtx->pOut);

    iReturn = WelsEncodeNal (&pCtx->pOut->sNalList[pCtx->pOut->iNalIndex - 1],
                             &pCtx->pCurDqLayer->sLayerInfo.sNalHeaderExt,
                             pCtx->iFrameBsSize - pCtx->iPosBsBuffer,
                             pCtx->pFrameBs + pCtx->iPosBsBuffer,
                             &pNalLen[*pNalIdxInLayer]);
    WELS_VERIFY_RETURN_IFNEQ (iReturn, ENC_RETURN_SUCCESS)
    iPayloadSize = pNalLen[*pNalIdxInLayer];

    pCtx->iPosBsBuffer += iPayloadSize;

    (*pNalIdxInLayer) ++;
  } else { // No Prefix NAL Unit RBSP syntax here, but need add NAL Unit Header extension
    WelsLoadNal (pCtx->pOut, NAL_UNIT_PREFIX, keNalRefIdc);
    // No need write any syntax of prefix NAL Unit RBSP here
    WelsUnloadNal (pCtx->pOut);

    iReturn = WelsEncodeNal (&pCtx->pOut->sNalList[pCtx->pOut->iNalIndex - 1],
                             &pCtx->pCurDqLayer->sLayerInfo.sNalHeaderExt,
                             pCtx->iFrameBsSize - pCtx->iPosBsBuffer,
                             pCtx->pFrameBs + pCtx->iPosBsBuffer,
                             &pNalLen[*pNalIdxInLayer]);
    WELS_VERIFY_RETURN_IFNEQ (iReturn, ENC_RETURN_SUCCESS)
    iPayloadSize = pNalLen[*pNalIdxInLayer];

    pCtx->iPosBsBuffer += iPayloadSize;

    (*pNalIdxInLayer) ++;
  }

  return ENC_RETURN_SUCCESS;
}


/*
 * Force coding IDR as follows
 */
int32_t ForceCodingIDR (sWelsEncCtx* pCtx, int32_t iLayerId) {
  if (NULL == pCtx)
    return 1;
  for (int32_t iDid = 0; iDid < pCtx->pSvcParam->iSpatialLayerNum; iDid++) {
    SSpatialLayerInternal* pParamInternal = &pCtx->pSvcParam->sDependencyLayers[iDid];
    pParamInternal->iCodingIndex = 0;
    pParamInternal->iFrameIndex = 0;
    pParamInternal->iFrameNum = 0;
    pParamInternal->iPOC = 0;
    pParamInternal->bEncCurFrmAsIdrFlag = true;
    pCtx->sEncoderStatistics[0].uiIDRReqNum++;
  }
  WelsLog (&pCtx->sLogCtx, WELS_LOG_INFO, "ForceCodingIDR(iDid 0-%d)at InputFrameCount=%d\n",
           pCtx->pSvcParam->iSpatialLayerNum - 1, pCtx->sEncoderStatistics[0].uiInputFrameCount);
  pCtx->bCheckWindowStatusRefreshFlag = false;


  return 0;
}


int32_t GetSubSequenceId (sWelsEncCtx* pCtx, EVideoFrameType eFrameType) {
  int32_t iSubSeqId = 0;
  if (eFrameType == videoFrameTypeIDR)
    iSubSeqId = 0;
  else if (eFrameType == videoFrameTypeI)
    iSubSeqId = 1;
  else if (eFrameType == videoFrameTypeP) {
    if (pCtx->bCurFrameMarkedAsSceneLtr)
      iSubSeqId = 2;
    else
      iSubSeqId = 3 + pCtx->uiTemporalId; //T0:3 T1:4 T2:5 T3:6
  } else
    iSubSeqId = 3 + MAX_TEMPORAL_LAYER_NUM;
  return iSubSeqId;
}

// writing parasets for (simulcast) svc
int32_t WriteSsvcParaset (sWelsEncCtx* pCtx, const int32_t kiSpatialNum,
                          SLayerBSInfo*& pLayerBsInfo, int32_t& iLayerNum, int32_t& iFrameSize) {
  int32_t iNonVclSize = 0, iCountNal = 0, iReturn = ENC_RETURN_SUCCESS;
  iReturn = WelsWriteParameterSets (pCtx, &pLayerBsInfo->pNalLengthInByte[0], &iCountNal, &iNonVclSize);
  WELS_VERIFY_RETURN_IFNEQ (iReturn, ENC_RETURN_SUCCESS)
  for (int32_t iSpatialId = 0; iSpatialId < kiSpatialNum; iSpatialId++) {
    SSpatialLayerInternal* pParamInternal = &pCtx->pSvcParam->sDependencyLayers[iSpatialId];
    if (pParamInternal->uiIdrPicId < 65535) {
      ++ pParamInternal->uiIdrPicId;
    } else {
      pParamInternal->uiIdrPicId = 0;
    }
  }
  pLayerBsInfo->uiSpatialId     = 0;
  pLayerBsInfo->uiTemporalId    = 0;
  pLayerBsInfo->uiQualityId     = 0;
  pLayerBsInfo->uiLayerType     = NON_VIDEO_CODING_LAYER;
  pLayerBsInfo->iNalCount       = iCountNal;
  pLayerBsInfo->eFrameType      = videoFrameTypeIDR;
  pLayerBsInfo->iSubSeqId = GetSubSequenceId (pCtx, videoFrameTypeIDR);
  //point to next pLayerBsInfo
  ++ pLayerBsInfo;
  ++ pCtx->pOut->iLayerBsIndex;
  pLayerBsInfo->pBsBuf           = pCtx->pFrameBs + pCtx->iPosBsBuffer;
  pLayerBsInfo->pNalLengthInByte = (pLayerBsInfo - 1)->pNalLengthInByte + iCountNal;

  //update for external countings
  ++ iLayerNum;
  iFrameSize += iNonVclSize;
  return iReturn;
}


//cover the logic of simulcast avc + sps_pps_listing
int32_t WriteSavcParaset_Listing (sWelsEncCtx* pCtx, const int32_t kiSpatialNum,
                                  SLayerBSInfo*& pLayerBsInfo, int32_t& iLayerNum, int32_t& iFrameSize) {
  int32_t iNonVclSize = 0, iCountNal = 0, iReturn = ENC_RETURN_SUCCESS;

  // write SPS
  iNonVclSize = 0;

  for (int32_t iSpatialId = 0; iSpatialId < kiSpatialNum; iSpatialId++) {
    SSpatialLayerInternal* pParamInternal = &pCtx->pSvcParam->sDependencyLayers[iSpatialId];
    if (pParamInternal->uiIdrPicId < 65535) {
      ++ pParamInternal->uiIdrPicId;
    } else {
      pParamInternal->uiIdrPicId = 0;
    }

    iCountNal = 0;

    for (int32_t iIdx = 0; iIdx < pCtx->iSpsNum; iIdx++) {
      //writing one NAL
      int32_t iNalSize = 0;
      iReturn = WelsWriteOneSPS (pCtx, iIdx, iNalSize);
      WELS_VERIFY_RETURN_IFNEQ (iReturn, ENC_RETURN_SUCCESS)

      pLayerBsInfo->pNalLengthInByte[iCountNal] = iNalSize;
      iNonVclSize += iNalSize;
      iCountNal ++;
      //finish writing one NAL
    }

    pLayerBsInfo->uiSpatialId   = iSpatialId;
    pLayerBsInfo->uiTemporalId  = 0;
    pLayerBsInfo->uiQualityId   = 0;
    pLayerBsInfo->uiLayerType   = NON_VIDEO_CODING_LAYER;
    pLayerBsInfo->iNalCount     = iCountNal;
    pLayerBsInfo->eFrameType    = videoFrameTypeIDR;
    pLayerBsInfo->iSubSeqId     = GetSubSequenceId (pCtx, videoFrameTypeIDR);
    //point to next pLayerBsInfo
    ++ pLayerBsInfo;
    ++ pCtx->pOut->iLayerBsIndex;
    pLayerBsInfo->pBsBuf           = pCtx->pFrameBs + pCtx->iPosBsBuffer;
    pLayerBsInfo->pNalLengthInByte = (pLayerBsInfo - 1)->pNalLengthInByte + iCountNal;
    //update for external countings
    ++ iLayerNum;
  }

  // write PPS
  pCtx->pFuncList->pParametersetStrategy->UpdatePpsList (pCtx);

  //TODO: under new strategy, will PPS be correctly updated?
  for (int32_t iSpatialId = 0; iSpatialId < kiSpatialNum; iSpatialId++) {
    iCountNal = 0;
    for (int32_t iIdx = 0; iIdx < pCtx->iPpsNum; iIdx++) {
      //writing one NAL
      int32_t iNalSize = 0;
      iReturn = WelsWriteOnePPS (pCtx, iIdx, iNalSize);
      WELS_VERIFY_RETURN_IFNEQ (iReturn, ENC_RETURN_SUCCESS)

      pLayerBsInfo->pNalLengthInByte[iCountNal] = iNalSize;
      iNonVclSize += iNalSize;
      iCountNal ++;
      //finish writing one NAL
    }

    pLayerBsInfo->uiSpatialId   = iSpatialId;
    pLayerBsInfo->uiTemporalId  = 0;
    pLayerBsInfo->uiQualityId   = 0;
    pLayerBsInfo->uiLayerType   = NON_VIDEO_CODING_LAYER;
    pLayerBsInfo->iNalCount     = iCountNal;
    pLayerBsInfo->eFrameType    = videoFrameTypeIDR;
    pLayerBsInfo->iSubSeqId     = GetSubSequenceId (pCtx, videoFrameTypeIDR);
    //point to next pLayerBsInfo
    ++ pLayerBsInfo;
    ++ pCtx->pOut->iLayerBsIndex;
    pLayerBsInfo->pBsBuf           = pCtx->pFrameBs + pCtx->iPosBsBuffer;
    pLayerBsInfo->pNalLengthInByte = (pLayerBsInfo - 1)->pNalLengthInByte + iCountNal;
    //update for external countings
    ++ iLayerNum;
  }

  // to check number of layers / nals / slices dependencies
  if (iLayerNum > MAX_LAYER_NUM_OF_FRAME) {
    WelsLog (& pCtx->sLogCtx, WELS_LOG_ERROR, "WriteSavcParaset(), iLayerNum(%d) > MAX_LAYER_NUM_OF_FRAME(%d)!",
             iLayerNum, MAX_LAYER_NUM_OF_FRAME);
    return ENC_RETURN_UNEXPECTED;
  }

  iFrameSize += iNonVclSize;
  return iReturn;
}


EVideoFrameType PrepareEncodeFrame (sWelsEncCtx* pCtx, SLayerBSInfo*& pLayerBsInfo, int32_t iSpatialNum,
                                    int8_t& iCurDid, int32_t& iCurTid,
                                    int32_t& iLayerNum, int32_t& iFrameSize, long long uiTimeStamp) {
  SWelsSvcCodingParam* pSvcParam        = pCtx->pSvcParam;
  SSpatialPicIndex* pSpatialIndexMap = &pCtx->sSpatialIndexMap[0];

  bool bSkipFrameFlag =  WelsRcCheckFrameStatus (pCtx, uiTimeStamp, iSpatialNum, iCurDid);
  EVideoFrameType eFrameType = DecideFrameType (pCtx, iSpatialNum, iCurDid, bSkipFrameFlag);
  if (eFrameType == videoFrameTypeSkip) {
    for (int32_t i = 0; i < iSpatialNum; i++) {
      UpdateBufferWhenFrameSkipped (pCtx, (pSpatialIndexMap + i)->iDid);
    }
    WelsLog (& (pCtx->sLogCtx), WELS_LOG_DEBUG,
             "[Rc] Frame timestamp = %lld, iDid = %d,skip one frame due to target_br, continual skipped %d frames",
             uiTimeStamp, iCurDid, pCtx->pWelsSvcRc[iCurDid].iContinualSkipFrames);

  } else {
    SSpatialLayerInternal* pParamInternal = &pSvcParam->sDependencyLayers[iCurDid];

    iCurTid = GetTemporalLevel (pParamInternal, pParamInternal->iCodingIndex,
                                pSvcParam->uiGopSize);
    pCtx->uiTemporalId = iCurTid;
    if (eFrameType == videoFrameTypeIDR) {
      // write parameter sets bitstream or SEI/SSEI (if any) here
      // TODO: use function pointer instead
      if (! (SPS_LISTING & pCtx->pSvcParam->eSpsPpsIdStrategy)) {
        pCtx->iEncoderError = WriteSsvcParaset (pCtx, iSpatialNum, pLayerBsInfo, iLayerNum, iFrameSize);
      } else {
        pCtx->iEncoderError = WriteSavcParaset_Listing (pCtx, iSpatialNum, pLayerBsInfo, iLayerNum, iFrameSize);


      }
    }
  }
  return eFrameType;
}
/*!
 * \brief   core svc encoding process
 *
 * \pParam  pCtx            sWelsEncCtx*, encoder context
 * \pParam  pFbi            FrameBSInfo*
 * \pParam  pSrcPic         Source Picture
 * \return  EFrameType (videoFrameTypeIDR/videoFrameTypeI/videoFrameTypeP)
 */
int32_t WelsEncoderEncodeExt (sWelsEncCtx* pCtx, SFrameBSInfo* pFbi, const SSourcePicture* pSrcPic) {
  if (pCtx == NULL) {
    return ENC_RETURN_MEMALLOCERR;
  }
  SLayerBSInfo* pLayerBsInfo            = &pFbi->sLayerInfo[0];
  SWelsSvcCodingParam* pSvcParam        = pCtx->pSvcParam;
  SSpatialPicIndex* pSpatialIndexMap = &pCtx->sSpatialIndexMap[0];
  SPicture* fsnr                = NULL;
  SPicture* pEncPic             = NULL; // to be decided later
  int32_t iLayerNum             = 0;
  int32_t iLayerSize            = 0;
  int32_t iSpatialNum           =
    0; // available count number of spatial layers due to frame size changed in this given frame
  int32_t iSpatialIdx           = 0; // iIndex of spatial layers due to frame size changed in this given frame
  int32_t iFrameSize            = 0;
  int32_t iNalIdxInLayer        = 0;
  int32_t iCountNal             = 0;
  EVideoFrameType eFrameType    = videoFrameTypeInvalid;
  int32_t iCurWidth             = 0;
  int32_t iCurHeight            = 0;
  EWelsNalUnitType eNalType     = NAL_UNIT_UNSPEC_0;
  EWelsNalRefIdc eNalRefIdc     = NRI_PRI_LOWEST;
  int8_t iCurDid                = 0;
  int32_t iCurTid                = 0;
  bool bAvcBased                = false;
  SLogContext* pLogCtx = & (pCtx->sLogCtx);
  float fSnrY = .0f, fSnrU = .0f, fSnrV = .0f;

  pCtx->iEncoderError = ENC_RETURN_SUCCESS;
  pCtx->bCurFrameMarkedAsSceneLtr = false;
  pFbi->eFrameType = videoFrameTypeSkip;
  pFbi->iLayerNum = 0; // for initialization
  pFbi->uiTimeStamp = GetTimestampForRc (pSrcPic->uiTimeStamp, pCtx->uiLastTimestamp,
                                         pCtx->pSvcParam->sSpatialLayers[pCtx->pSvcParam->iSpatialLayerNum - 1].fFrameRate);
  for (int32_t iNalIdx = 0; iNalIdx < MAX_LAYER_NUM_OF_FRAME; iNalIdx++) {
    pFbi->sLayerInfo[iNalIdx].eFrameType = videoFrameTypeSkip;
    pFbi->sLayerInfo[iNalIdx].iNalCount  = 0;
  }
  // perform csc/denoise/downsample/padding, generate spatial layers
  iSpatialNum = pCtx->pVpp->BuildSpatialPicList (pCtx, pSrcPic);
  if (iSpatialNum == -1) {
    WelsLog (& (pCtx->sLogCtx), WELS_LOG_ERROR, "Failed in allocating memory in BuildSpatialPicList");
    return ENC_RETURN_MEMALLOCERR;
  }

  UpdateMaxBrCheckWindowStatus (pCtx, iSpatialNum, pFbi->uiTimeStamp);

  if (iSpatialNum < 1) {
    for (int32_t iDidIdx = 0; iDidIdx < pSvcParam->iSpatialLayerNum; iDidIdx++) {
      SSpatialLayerInternal* pParamInternal = &pSvcParam->sDependencyLayers[iDidIdx];
      pParamInternal->iCodingIndex ++;
    }
    pFbi->eFrameType = videoFrameTypeSkip;
    pLayerBsInfo->eFrameType = videoFrameTypeSkip;
    WelsLog (& (pCtx->sLogCtx), WELS_LOG_DEBUG,
             "[Rc] Frame timestamp = %lld, skip one frame due to preprocessing return (temporal layer settings or else)",
             pSrcPic->uiTimeStamp);
    return ENC_RETURN_SUCCESS;
  }

  InitBitStream (pCtx);
  pLayerBsInfo->pBsBuf = pCtx->pFrameBs ;
  pLayerBsInfo->pNalLengthInByte = pCtx->pOut->pNalLen;
  iCurDid = pSpatialIndexMap->iDid;
  pCtx->pCurDqLayer             = pCtx->ppDqLayerList[iCurDid];
  pCtx->pCurDqLayer->pRefLayer  = NULL;
  eFrameType = PrepareEncodeFrame (pCtx, pLayerBsInfo, iSpatialNum, iCurDid, iCurTid, iLayerNum, iFrameSize,
                                   pFbi->uiTimeStamp);
  if (eFrameType == videoFrameTypeSkip) {
    pFbi->eFrameType = videoFrameTypeSkip;
    pLayerBsInfo->eFrameType = videoFrameTypeSkip;
    return ENC_RETURN_SUCCESS;
  }

  while (iSpatialIdx < iSpatialNum) {
    iCurDid  = (pSpatialIndexMap + iSpatialIdx)->iDid;
    SSpatialLayerConfig* pParam = &pSvcParam->sSpatialLayers[iCurDid];
    SSpatialLayerInternal* pParamInternal = &pSvcParam->sDependencyLayers[iCurDid];
    int32_t  iDecompositionStages = pSvcParam->sDependencyLayers[iCurDid].iDecompositionStages;
    pCtx->pCurDqLayer           = pCtx->ppDqLayerList[iCurDid];
    pCtx->uiDependencyId        =  iCurDid;

    InitFrameCoding (pCtx, eFrameType, iCurDid);
    pCtx->pVpp->AnalyzeSpatialPic (pCtx, iCurDid);

    pCtx->pEncPic               = pEncPic = (pSpatialIndexMap + iSpatialIdx)->pSrc;
    pCtx->pEncPic->iPictureType = pCtx->eSliceType;
    pCtx->pEncPic->iFramePoc    = pParamInternal->iPOC;

    iCurWidth   = pParam->iVideoWidth;
    iCurHeight  = pParam->iVideoHeight;
    /* coding each spatial layer, only one sQualityStat layer within spatial support */
    int32_t iSliceCount = 1;
    if (iLayerNum >= MAX_LAYER_NUM_OF_FRAME) { // check available layer_bs_info writing as follows
      WelsLog (pLogCtx, WELS_LOG_ERROR, "WelsEncoderEncodeExt(), iLayerNum(%d) overflow(max:%d)!", iLayerNum,
               MAX_LAYER_NUM_OF_FRAME);
      return ENC_RETURN_UNSUPPORTED_PARA;
    }

    iNalIdxInLayer  = 0;
    bAvcBased       = ((pSvcParam->bSimulcastAVC) || (iCurDid == BASE_DEPENDENCY_ID));
    pCtx->bNeedPrefixNalFlag    = ((!pSvcParam->bSimulcastAVC) && (bAvcBased &&
                                   (pSvcParam->bPrefixNalAddingCtrl ||
                                    (pSvcParam->iSpatialLayerNum > 1))));

    if (eFrameType == videoFrameTypeP) {
      eNalType = bAvcBased ? NAL_UNIT_CODED_SLICE : NAL_UNIT_CODED_SLICE_EXT;
    } else if (eFrameType == videoFrameTypeIDR) {
      eNalType = bAvcBased ? NAL_UNIT_CODED_SLICE_IDR : NAL_UNIT_CODED_SLICE_EXT;
    }
    if (iCurTid == 0 || pCtx->eSliceType == I_SLICE)
      eNalRefIdc = NRI_PRI_HIGHEST;
    else if (iCurTid == iDecompositionStages)
      eNalRefIdc = NRI_PRI_LOWEST;
    else if (1 + iCurTid == iDecompositionStages)
      eNalRefIdc = NRI_PRI_LOW;
    else if (2 + iCurTid == iDecompositionStages)
      eNalRefIdc = NRI_PRI_HIGH;
    else // more details for other temporal layers?
      eNalRefIdc = NRI_PRI_HIGHEST;
    pCtx->eNalType = eNalType;
    pCtx->eNalPriority = eNalRefIdc;

    pCtx->pDecPic               = pCtx->ppRefPicListExt[iCurDid]->pNextBuffer;
    fsnr                        = pCtx->pDecPic;
    pCtx->pDecPic->iPictureType = pCtx->eSliceType;
    pCtx->pDecPic->iFramePoc    = pParamInternal->iPOC;

    WelsInitCurrentLayer (pCtx, iCurWidth, iCurHeight);

    WelsMarkPic (pCtx);
    if (!WelsBuildRefList (pCtx, pParamInternal->iPOC, 0)) {
      WelsLog (pLogCtx, WELS_LOG_WARNING,
               "WelsEncoderEncodeExt(), WelsBuildRefList failed for P frames, pCtx->iNumRef0= %d. ForceCodingIDR!",
               pCtx->iNumRef0);
      eFrameType = videoFrameTypeIDR;
      pCtx->iEncoderError = ENC_RETURN_CORRECTED;
      break;
    }
    if (pCtx->eSliceType != I_SLICE) {
      UpdateBlockStatic (pCtx);
    }
    pCtx->pVpp->AnalyzePictureComplexity (pCtx, pCtx->pEncPic, ((pCtx->eSliceType == P_SLICE)
                                            && (pCtx->iNumRef0 > 0)) ? pCtx->pRefList0[0] : NULL,
                                            iCurDid, (pCtx->eSliceType == P_SLICE) && pSvcParam->bEnableBackgroundDetection);
    WelsUpdateRefSyntax (pCtx,  pParamInternal->iPOC,
                         eFrameType); //get reordering syntax used for writing slice header and transmit to encoder.
    PrefetchReferencePicture (pCtx, eFrameType); // update reference picture for current pDq layer
    WelsRcPictureInitGom (pCtx, pFbi->uiTimeStamp);
    PreprocessSliceCoding (pCtx); // MUST be called after pfWelsRcPictureInit() and WelsInitCurrentLayer()

    //TODO Complexity Calculation here for screen content
    iLayerSize = 0;
    // jumpseat: one slice, one thread (the multi-slice and threaded branches are removed)
    int32_t iSliceSize   = 0;
    int32_t iPayloadSize = 0;
    SSlice* pCurSlice    = &pCtx->pCurDqLayer->sSliceBufferInfo[0].pSliceBuffer[0];

    if (pCtx->bNeedPrefixNalFlag) {
      pCtx->iEncoderError = AddPrefixNal (pCtx, pLayerBsInfo, &pLayerBsInfo->pNalLengthInByte[0], &iNalIdxInLayer, eNalType,
                                          eNalRefIdc,
                                          iPayloadSize);
      WELS_VERIFY_RETURN_IFNEQ (pCtx->iEncoderError, ENC_RETURN_SUCCESS)
      iLayerSize += iPayloadSize;
    }

    WelsLoadNal (pCtx->pOut, eNalType, eNalRefIdc);
    assert (0 == (int) pCurSlice->iSliceIdx);
    pCtx->iEncoderError   = SetSliceBoundaryInfo (pCtx->pCurDqLayer, pCurSlice, 0);
    WELS_VERIFY_RETURN_IFNEQ (pCtx->iEncoderError, ENC_RETURN_SUCCESS)

    pCtx->iEncoderError   = WelsCodeOneSlice (pCtx, pCurSlice, eNalType);
    WELS_VERIFY_RETURN_IFNEQ (pCtx->iEncoderError, ENC_RETURN_SUCCESS)

    WelsUnloadNal (pCtx->pOut);

    pCtx->iEncoderError = WelsEncodeNal (&pCtx->pOut->sNalList[pCtx->pOut->iNalIndex - 1],
                                         &pCtx->pCurDqLayer->sLayerInfo.sNalHeaderExt,
                                         pCtx->iFrameBsSize - pCtx->iPosBsBuffer,
                                         pCtx->pFrameBs + pCtx->iPosBsBuffer,
                                         &pLayerBsInfo->pNalLengthInByte[iNalIdxInLayer]);
    WELS_VERIFY_RETURN_IFNEQ (pCtx->iEncoderError, ENC_RETURN_SUCCESS)
    iSliceSize = pLayerBsInfo->pNalLengthInByte[iNalIdxInLayer];

    iLayerSize += iSliceSize;
    pCtx->iPosBsBuffer               += iSliceSize;
    pLayerBsInfo->uiLayerType         = VIDEO_CODING_LAYER;
    pLayerBsInfo->uiSpatialId         = iCurDid;
    pLayerBsInfo->uiTemporalId        = iCurTid;
    pLayerBsInfo->uiQualityId         = 0;
    pLayerBsInfo->iNalCount           = ++ iNalIdxInLayer;
    pLayerBsInfo->eFrameType          = eFrameType;
    pLayerBsInfo->iSubSeqId = GetSubSequenceId (pCtx, eFrameType);



    WelsRcPictureInfoUpdateGom (pCtx, iLayerSize);
    iFrameSize += iLayerSize;
    RcTraceFrameBits (pCtx, pFbi->uiTimeStamp, iFrameSize);
    pCtx->pDecPic->iFrameAverageQp = pCtx->pWelsSvcRc[iCurDid].iAverageFrameQp;

    //update scc related

    // reference picture list update
    if (eNalRefIdc != NRI_PRI_LOWEST) {
      if (!WelsUpdateRefList (pCtx)) {
        WelsLog (pLogCtx, WELS_LOG_WARNING, "WelsEncoderEncodeExt(), WelsUpdateRefList failed. ForceCodingIDR!");
        //the above is to set the next frame to be IDR
        pCtx->iEncoderError = ENC_RETURN_CORRECTED;
        break;
      }
    }


    //check MinCr
    {
      int32_t iMinCrFrameSize = (pParam->iVideoWidth * pParam->iVideoHeight * 3) >> 2; //MinCr = 2;
      if (pParam->uiLevelIdc == LEVEL_3_1 || pParam->uiLevelIdc == LEVEL_3_2 || pParam->uiLevelIdc == LEVEL_4_0)
        iMinCrFrameSize >>= 1; //MinCr = 4
      if (iFrameSize > iMinCrFrameSize)
        WelsLog (pLogCtx, WELS_LOG_WARNING,
                 "WelsEncoderEncodeExt()MinCr Checking,codec bitstream size is larger than Level limitation");
    }

    if (fsnr && (pSvcParam->bPsnrY || pSrcPic->bPsnrY)) {
      fSnrY = WelsCalcPsnr (fsnr->pData[0],
                            fsnr->iLineSize[0],
                            pEncPic->pData[0],
                            pEncPic->iLineSize[0],
                            iCurWidth,
                            iCurHeight);
    }
    if (fsnr && (pSvcParam->bPsnrU || pSrcPic->bPsnrU)) {
      fSnrU = WelsCalcPsnr (fsnr->pData[1],
                            fsnr->iLineSize[1],
                            pEncPic->pData[1],
                            pEncPic->iLineSize[1],
                            (iCurWidth >> 1),
                            (iCurHeight >> 1));
    }
    if (fsnr && (pSvcParam->bPsnrV || pSrcPic->bPsnrV)) {
      fSnrV = WelsCalcPsnr (fsnr->pData[2],
                            fsnr->iLineSize[2],
                            pEncPic->pData[2],
                            pEncPic->iLineSize[2],
                            (iCurWidth >> 1),
                            (iCurHeight >> 1));
    }


    pLayerBsInfo->rPsnr[0] = 0;
    pLayerBsInfo->rPsnr[1] = 0;
    pLayerBsInfo->rPsnr[2] = 0;
    if (pSrcPic->bPsnrY) {
      pLayerBsInfo->rPsnr[0] = fSnrY;
    }
    if (pSrcPic->bPsnrU) {
      pLayerBsInfo->rPsnr[1] = fSnrU;
    }
    if (pSrcPic->bPsnrV) {
      pLayerBsInfo->rPsnr[2] = fSnrV;
    }


    iCountNal = pLayerBsInfo->iNalCount;
    ++ iLayerNum;
    ++ pLayerBsInfo;
    ++ pCtx->pOut->iLayerBsIndex;
    pLayerBsInfo->pBsBuf = pCtx->pFrameBs + pCtx->iPosBsBuffer;
    pLayerBsInfo->pNalLengthInByte = (pLayerBsInfo - 1)->pNalLengthInByte + iCountNal;


    pCtx->eLastNalPriority[iCurDid] = eNalRefIdc;
    ++ iSpatialIdx;

    if (iCurDid + 1 < pSvcParam->iSpatialLayerNum) {
      //for next layer, note that iSpatialIdx has been ++ so it is pointer to next layer
      WelsSwapDqLayers (pCtx, (pSpatialIndexMap + iSpatialIdx)->iDid);
    }

    if (pCtx->pVpp->UpdateSpatialPictures (pCtx, pSvcParam, iCurTid, iCurDid) != 0) {
      ForceCodingIDR (pCtx, iCurDid);
      WelsLog (pLogCtx, WELS_LOG_WARNING,
               "WelsEncoderEncodeExt(), Logic Error Found in Preprocess updating. ForceCodingIDR!");
      //the above is to set the next frame IDR
      pFbi->eFrameType = eFrameType;
      pLayerBsInfo->eFrameType = eFrameType;
      return ENC_RETURN_CORRECTED;
    }

  }//end of (iSpatialIdx/iSpatialNum)

  for (int32_t i = 0; i < pSvcParam->iSpatialLayerNum; i++) {
    SSpatialLayerInternal* pParamInternal = &pSvcParam->sDependencyLayers[i];
    pParamInternal->iCodingIndex ++;
  }

  if (ENC_RETURN_CORRECTED == pCtx->iEncoderError) {
    pCtx->pVpp->UpdateSpatialPictures (pCtx, pSvcParam, iCurTid, (pSpatialIndexMap + iSpatialIdx)->iDid);
    ForceCodingIDR (pCtx, (pSpatialIndexMap + iSpatialIdx)->iDid);
    WelsLog (pLogCtx, WELS_LOG_ERROR, "WelsEncoderEncodeExt(), Logic Error Found in temporal level. ForceCodingIDR!");
    //the above is to set the next frame IDR
    pFbi->eFrameType = eFrameType;
    pLayerBsInfo->eFrameType = eFrameType;
    return ENC_RETURN_CORRECTED;
  }


  // to check number of layers / nals / slices dependencies
  if (iLayerNum > MAX_LAYER_NUM_OF_FRAME) {
    WelsLog (& pCtx->sLogCtx, WELS_LOG_ERROR, "WelsEncoderEncodeExt(), iLayerNum(%d) > MAX_LAYER_NUM_OF_FRAME(%d)!",
             iLayerNum, MAX_LAYER_NUM_OF_FRAME);
    return 1;
  }


  pFbi->iLayerNum = iLayerNum;

  WelsLog (pLogCtx, WELS_LOG_DEBUG, "WelsEncoderEncodeExt() OutputInfo iLayerNum = %d,iFrameSize = %d",
           iLayerNum, iFrameSize);
  for (int32_t i = 0; i < iLayerNum; i++)
    WelsLog (pLogCtx, WELS_LOG_DEBUG,
             "WelsEncoderEncodeExt() OutputInfo iLayerId = %d,iNalType = %d,iNalCount = %d, first Nal Length=%d,uiSpatialId = %d,uiTemporalId = %d,iSubSeqId = %d",
             i,
             pFbi->sLayerInfo[i].uiLayerType, pFbi->sLayerInfo[i].iNalCount, pFbi->sLayerInfo[i].pNalLengthInByte[0],
             pFbi->sLayerInfo[i].uiSpatialId, pFbi->sLayerInfo[i].uiTemporalId, pFbi->sLayerInfo[i].iSubSeqId);
  WelsEmms();

  pLayerBsInfo->eFrameType = eFrameType;
  pFbi->iFrameSizeInBytes = iFrameSize;
  pFbi->eFrameType = eFrameType;
  for (int32_t k = 0; k < pFbi->iLayerNum; k++) {
    if (pFbi->eFrameType != pFbi->sLayerInfo[k].eFrameType) {
      pFbi->eFrameType = videoFrameTypeIPMixed;
    }
  }
  return ENC_RETURN_SUCCESS;
}




} // namespace WelsEnc
