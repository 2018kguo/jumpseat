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

#include "au_set.h"
#include "encoder_context.h"
#include "paraset_strategy.h"

namespace WelsEnc {


IWelsParametersetStrategy*   IWelsParametersetStrategy::CreateParametersetStrategy (
  EParameterSetStrategy eSpsPpsIdStrategy, bool bSimulcastAVC,
  const int32_t kiSpatialLayerNum) {

  IWelsParametersetStrategy* pParametersetStrategy = NULL;
  // jumpseat: constant ids only; the id-increasing and listing strategies
  // (for SVC meetings and stream switching) are removed.
  pParametersetStrategy = WELS_NEW_OP (CWelsParametersetIdConstant (bSimulcastAVC, kiSpatialLayerNum),
                                       CWelsParametersetIdConstant);
  WELS_VERIFY_RETURN_IF (NULL, NULL == pParametersetStrategy)

  return pParametersetStrategy;
}


static int32_t WelsGenerateNewSps (sWelsEncCtx* pCtx, const bool kbUseSubsetSps, const int32_t iDlayerIndex,
                                   const int32_t iDlayerCount, const int32_t kiSpsId,
                                   SWelsSPS*& pSps, SSubsetSps*& pSubsetSps, bool bSVCBaselayer) {
  int32_t iRet = 0;

  if (!kbUseSubsetSps) {
    pSps        = & (pCtx->pSpsArray[kiSpsId]);
  } else {
    pSubsetSps  = & (pCtx->pSubsetArray[kiSpsId]);
    pSps        = &pSubsetSps->pSps;
  }

  SWelsSvcCodingParam* pParam = pCtx->pSvcParam;
  SSpatialLayerConfig* pDlayerParam = &pParam->sSpatialLayers[iDlayerIndex];
  // Need port pSps/pPps initialization due to spatial scalability changed
  if (!kbUseSubsetSps) {
    iRet = WelsInitSps (pSps, pDlayerParam, &pParam->sDependencyLayers[iDlayerIndex], pParam->uiIntraPeriod,
                        pParam->iMaxNumRefFrame,
                        kiSpsId, pParam->bEnableFrameCroppingFlag, pParam->iRCMode != RC_OFF_MODE, iDlayerCount,
                        bSVCBaselayer);
  } else {
    iRet = WelsInitSubsetSps (pSubsetSps, pDlayerParam, &pParam->sDependencyLayers[iDlayerIndex], pParam->uiIntraPeriod,
                              pParam->iMaxNumRefFrame,
                              kiSpsId, pParam->bEnableFrameCroppingFlag, pParam->iRCMode != RC_OFF_MODE, iDlayerCount);
  }
  return iRet;
}



/*!
 * \brief   check if the current parameter can found a presenting sps
 * \param   pParam          the current encoding paramter in SWelsSvcCodingParam
 * \param   kbUseSubsetSps  bool
 * \param   iDlayerIndex    int, the index of current D layer
 * \param   iDlayerCount    int, the number of total D layer
 * \param pSpsArray         array of all the stored SPSs
 * \param   pSubsetArray    array of all the stored Subset-SPSs
 * \return  0 - successful
 *         -1 - cannot find existing SPS for current encoder parameter
 */
CWelsParametersetIdConstant::CWelsParametersetIdConstant (const bool bSimulcastAVC, const int32_t kiSpatialLayerNum) {
  memset (&m_sParaSetOffset, 0, sizeof (m_sParaSetOffset));

  m_bSimulcastAVC = bSimulcastAVC;
  m_iSpatialLayerNum = kiSpatialLayerNum;

  m_iBasicNeededSpsNum = 1;
  m_iBasicNeededPpsNum = (1 + m_iSpatialLayerNum);
}

CWelsParametersetIdConstant::~CWelsParametersetIdConstant() {
}

int32_t CWelsParametersetIdConstant::GetPpsIdOffset (const int32_t iPpsId) {
  return 0;
};
int32_t CWelsParametersetIdConstant::GetSpsIdOffset (const int32_t iPpsId, const int32_t iSpsId) {
  return 0;
};

int32_t* CWelsParametersetIdConstant::GetSpsIdOffsetList (const int iParasetType) {
  return & (m_sParaSetOffset.sParaSetOffsetVariable[iParasetType].iParaSetIdDelta[0]);
}

uint32_t CWelsParametersetIdConstant::GetAllNeededParasetNum() {
  return (GetNeededSpsNum()
          + GetNeededSubsetSpsNum()
          + GetNeededPpsNum());
}

uint32_t CWelsParametersetIdConstant::GetNeededSpsNum() {
  if (0 >= m_sParaSetOffset.uiNeededSpsNum) {
    m_sParaSetOffset.uiNeededSpsNum = m_iBasicNeededSpsNum * ((m_bSimulcastAVC) ? (m_iSpatialLayerNum) : (1));
  }
  return m_sParaSetOffset.uiNeededSpsNum;
}


uint32_t CWelsParametersetIdConstant::GetNeededSubsetSpsNum() {
  if (0 >= m_sParaSetOffset.uiNeededSubsetSpsNum) {
    m_sParaSetOffset.uiNeededSubsetSpsNum = (m_bSimulcastAVC ? 0 : (m_iSpatialLayerNum - 1));
  }
  return m_sParaSetOffset.uiNeededSubsetSpsNum;
}

uint32_t CWelsParametersetIdConstant::GetNeededPpsNum() {
  if (0 == m_sParaSetOffset.uiNeededPpsNum) {
    m_sParaSetOffset.uiNeededPpsNum = m_iBasicNeededPpsNum * ((m_bSimulcastAVC) ? (m_iSpatialLayerNum) :
                                      (1));
  }
  return m_sParaSetOffset.uiNeededPpsNum;
}

void CWelsParametersetIdConstant::LoadPrevious (SExistingParasetList* pExistingParasetList,  SWelsSPS* pSpsArray,
    SSubsetSps*       pSubsetArray, SWelsPPS* pPpsArray) {
  return;
}

void CWelsParametersetIdConstant::Update (const uint32_t kuiId, const int iParasetType) {
  memset (&m_sParaSetOffset, 0, sizeof (SParaSetOffset));
}

uint32_t CWelsParametersetIdConstant::GenerateNewSps (sWelsEncCtx* pCtx, const bool kbUseSubsetSps,
    const int32_t iDlayerIndex,
    const int32_t iDlayerCount, uint32_t kuiSpsId,
    SWelsSPS*& pSps, SSubsetSps*& pSubsetSps, bool bSVCBaselayer) {
  WelsGenerateNewSps (pCtx, kbUseSubsetSps, iDlayerIndex,
                      iDlayerCount, kuiSpsId,
                      pSps, pSubsetSps, bSVCBaselayer);
  return kuiSpsId;
}


uint32_t CWelsParametersetIdConstant::InitPps (sWelsEncCtx* pCtx, uint32_t kiSpsId,
    SWelsSPS* pSps,
    SSubsetSps* pSubsetSps,
    uint32_t kuiPpsId,
    const bool kbDeblockingFilterPresentFlag,
    const bool kbUsingSubsetSps,
    const bool kbEntropyCodingModeFlag) {
  WelsInitPps (& pCtx->pPPSArray[kuiPpsId], pSps, pSubsetSps, kuiPpsId, true, kbUsingSubsetSps, kbEntropyCodingModeFlag);
  SetUseSubsetFlag (kuiPpsId, kbUsingSubsetSps);
  return kuiPpsId;
}

void CWelsParametersetIdConstant::SetUseSubsetFlag (const uint32_t iPpsId, const bool bUseSubsetSps) {
  m_sParaSetOffset.bPpsIdMappingIntoSubsetsps[iPpsId] = bUseSubsetSps;
}

}
