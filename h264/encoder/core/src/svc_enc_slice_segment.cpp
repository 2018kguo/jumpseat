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
 * \file    slice_segment.c
 *
 * \brief   SSlice segment routine (Single slice/multiple slice/fmo arrangement exclusive)
 *
 * \date    2/4/2009 Created
 *
 *************************************************************************************
 */
#include <string.h>
#include "rc.h"
#include "svc_enc_frame.h"

namespace WelsEnc {
/*!
 * \brief   Assign MB map for single slice segment
 *
 * \param   pMbMap          overall MB map
 * \param   iCountMbNum     count number of MB
 *
 * \return  0 - successful; none 0 - failed
 */
int32_t AssignMbMapSingleSlice (void* pMbMap, const int32_t kiCountMbNum, const int32_t kiMapUnitSize) {
  if (NULL == pMbMap || kiCountMbNum <= 0)
    return 1;

  memset (pMbMap, 0, kiCountMbNum * kiMapUnitSize);

  return 0;
}


/*!
 *  Check slices assignment settings on MST_INTERLEAVE type
 */









/*!
 *  Get slice count for multiple slice segment
 *
 */
int32_t GetInitialSliceNum (SSliceArgument* pSliceArgument) {
  if (NULL == pSliceArgument)
    return -1;

  return pSliceArgument->uiSliceNum;

  return -1;
}

/*!
 * \brief   Initialize slice segment (Single/multiple slices)
 *
 * \param   pCurDq              current layer which its SSlice segment will be initialized
 * \param   uiSliceMode         SSlice mode
 * \param   multi_slice_argv    Multiple slices argument
 * \param   iMbWidth            MB width
 * \param   iMbHeight           MB height
 *
 * \return  0 - successful; none 0 - failed;
 */
int32_t InitSliceSegment (SDqLayer* pCurDq,
                          CMemoryAlign* pMa,
                          SSliceArgument* pSliceArgument,
                          const int32_t kiMbWidth,
                          const int32_t kiMbHeight) {
  SSliceCtx* pSliceSeg        = &pCurDq->sSliceEncCtx;
  const int32_t kiCountMbNum  = kiMbWidth * kiMbHeight;
  SliceModeEnum uiSliceMode   = SM_SINGLE_SLICE;

  if (NULL == pSliceSeg || NULL == pSliceArgument || kiMbWidth == 0 || kiMbHeight == 0)
    return 1;

  uiSliceMode = pSliceArgument->uiSliceMode;
  if (pSliceSeg->iMbNumInFrame == kiCountMbNum && pSliceSeg->iMbWidth == kiMbWidth
      && pSliceSeg->iMbHeight == kiMbHeight && pSliceSeg->uiSliceMode == uiSliceMode && pSliceSeg->pOverallMbMap != NULL)
    return 0;
  else if (pSliceSeg->iMbNumInFrame != kiCountMbNum) {
    if (NULL != pSliceSeg->pOverallMbMap) {
      pMa->WelsFree (pSliceSeg->pOverallMbMap, "pSliceSeg->pOverallMbMap");

      pSliceSeg->pOverallMbMap = NULL;
    }

    // just for safe
    pSliceSeg->iSliceNumInFrame = 0;
    pSliceSeg->iMbNumInFrame    = 0;
    pSliceSeg->iMbWidth         = 0;
    pSliceSeg->iMbHeight        = 0;
    pSliceSeg->uiSliceMode      = SM_SINGLE_SLICE;      // sigle in default
  }

  pSliceSeg->pOverallMbMap = (uint16_t*)pMa->WelsMallocz (kiCountMbNum * sizeof (uint16_t), "pSliceSeg->pOverallMbMap");

  WELS_VERIFY_RETURN_IF (1, NULL == pSliceSeg->pOverallMbMap)
  pSliceSeg->iSliceNumInFrame = 1;

  pSliceSeg->uiSliceMode              = uiSliceMode;
  pSliceSeg->iMbWidth                 = kiMbWidth;
  pSliceSeg->iMbHeight                = kiMbHeight;
  pSliceSeg->iMbNumInFrame            = kiCountMbNum;

  return AssignMbMapSingleSlice (pSliceSeg->pOverallMbMap, kiCountMbNum, sizeof (pSliceSeg->pOverallMbMap[0]));
  return 0;
}

/*!
 * \brief   Uninitialize slice segment (Single/multiple slices)
 *
 * \param   pCurDq           current layer which its SSlice segment will be uninitialized
 *
 * \return  none;
 */
void UninitSliceSegment (SDqLayer* pCurDq, CMemoryAlign* pMa) {
  SSliceCtx* pSliceSeg = &pCurDq->sSliceEncCtx;
  if (NULL != pSliceSeg) {
    if (NULL != pSliceSeg->pOverallMbMap) {
      pMa->WelsFree (pSliceSeg->pOverallMbMap, "pSliceSeg->pOverallMbMap");

      pSliceSeg->pOverallMbMap = NULL;
    }

    pSliceSeg->uiSliceMode      = SM_SINGLE_SLICE;      // single in default
    pSliceSeg->iMbWidth         = 0;
    pSliceSeg->iMbHeight        = 0;
    pSliceSeg->iSliceNumInFrame = 0;
    pSliceSeg->iMbNumInFrame    = 0;
    pSliceSeg->uiSliceSizeConstraint   = 0;
    pSliceSeg->iMaxSliceNumConstraint  = 0;
  }
}


/*!
 * \brief   Initialize Wels SSlice context (Single/multiple slices and FMO)
 *
 * \param   pCurDq          current layer which its SSlice context will be initialized
 * \param   bFmoUseFlag     flag of using fmo
 * \param   iMbWidth        MB width
 * \param   iMbHeight       MB height
 * \param   uiSliceMode     slice mode
 * \param   mul_slice_arg   argument for multiple slice if it is applicable
 * \param   pPpsArg         argument for pPps parameter
 *
 * \return  0 - successful; none 0 - failed;
 */
int32_t InitSlicePEncCtx (SDqLayer* pCurDq,
                          CMemoryAlign* pMa,
                          bool bFmoUseFlag,
                          int32_t iMbWidth,
                          int32_t iMbHeight,
                          SSliceArgument* pSliceArgument,
                          void* pPpsArg) {
  if (NULL == pCurDq)
    return 1;

  InitSliceSegment (pCurDq,
                    pMa,
                    pSliceArgument,
                    iMbWidth,
                    iMbHeight);
  return 0;
}


/*!
 * \brief   Uninitialize Wels SSlice context (Single/multiple slices and FMO)
 *
 * \param   pCurDq       current layer which its SSlice context will be initialized
 *
 * \return  NONE;
 */
void UninitSlicePEncCtx (SDqLayer* pCurDq, CMemoryAlign* pMa) {
  if (NULL != pCurDq) {
    UninitSliceSegment (pCurDq, pMa);
  }
}

/*!
 * \brief   Get slice idc for given iMbXY (apply in Single/multiple slices and FMO)
 *
 * \param   pCurDq    current layer info
 * \param   kiMbXY    MB xy index
 *
 * \return  uiSliceIdc - successful; -1 - failed;
 */
uint16_t WelsMbToSliceIdc (SDqLayer* pCurDq, const int32_t kiMbXY) {
  if (NULL == pCurDq)
    return (uint16_t) (-1);

  SSliceCtx* pSliceCtx = &pCurDq->sSliceEncCtx;
  if (NULL != pSliceCtx && kiMbXY < pSliceCtx->iMbNumInFrame && kiMbXY >= 0)
    return pSliceCtx->pOverallMbMap[ kiMbXY ];
  return (uint16_t) (-1);
}


/*!
 * \brief   Get successive mb to be processed in slice/slice_group: uiSliceIdc (apply in Single/multiple slices and FMO)
 *
 * \param   pCurDq     current layer info
 * \param   kiMbXY     MB xy index
 *
 * \return  next_mb - successful; -1 - failed;
 */
int32_t WelsGetNextMbOfSlice (SDqLayer* pCurDq, const int32_t kiMbXY) {
  if (NULL != pCurDq) {
    SSliceCtx* pSliceSeg = &pCurDq->sSliceEncCtx;
    if (NULL == pSliceSeg || kiMbXY < 0 || kiMbXY >= pSliceSeg->iMbNumInFrame)
      return -1;
    int32_t iNextMbIdx = kiMbXY;
    ++ iNextMbIdx;
    if (iNextMbIdx >= pSliceSeg->iMbNumInFrame)
      iNextMbIdx = -1;
    return iNextMbIdx;
  } else
    return -1;
}





} // namespace WelsEnc
