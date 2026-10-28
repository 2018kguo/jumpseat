/*!
 *@page License
 *
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

#ifndef WELS_VIDEO_CODEC_SVC_API_H__
#define WELS_VIDEO_CODEC_SVC_API_H__

#ifndef __cplusplus
#include <stdbool.h>
#endif

#include "codec_app_def.h"
#include "codec_def.h"

#if defined(_WIN32) || defined(__cdecl)
#define EXTAPI __cdecl
#else
#define EXTAPI
#endif

// jumpseat: the encoder interface (OpenH264's, without the decoder).

#ifdef __cplusplus
/**
* @brief Endocder definition
*/
class ISVCEncoder {
 public:
  /**
  * @brief  Initialize the encoder
  * @param  pParam  basic encoder parameter
  * @return CM_RETURN: 0 - success; otherwise - failed;
  */
  virtual int EXTAPI Initialize (const SEncParamBase* pParam) = 0;

  /**
  * @brief  Initilaize encoder by using extension parameters.
  * @param  pParam  extension parameter for encoder
  * @return CM_RETURN: 0 - success; otherwise - failed;
  */
  virtual int EXTAPI InitializeExt (const SEncParamExt* pParam) = 0;

  /**
  * @brief   Get the default extension parameters.
  *          If you want to change some parameters of encoder, firstly you need to get the default encoding parameters,
  *          after that you can change part of parameters you want to.
  * @param   pParam  extension parameter for encoder
  * @return  CM_RETURN: 0 - success; otherwise - failed;
  * */
  virtual int EXTAPI GetDefaultParams (SEncParamExt* pParam) = 0;
  /// uninitialize the encoder
  virtual int EXTAPI Uninitialize() = 0;

  /**
  * @brief Encode one frame
  * @param kpSrcPic the pointer to the source luminance plane
  *        chrominance data:
  *        CbData = kpSrc  +  m_iMaxPicWidth * m_iMaxPicHeight;
  *        CrData = CbData + (m_iMaxPicWidth * m_iMaxPicHeight)/4;
  *        the application calling this interface needs to ensure the data validation between the location
  * @param pBsInfo output bit stream
  * @return  0 - success; otherwise -failed;
  */
  virtual int EXTAPI EncodeFrame (const SSourcePicture* kpSrcPic, SFrameBSInfo* pBsInfo) = 0;

  /**
  * @brief  Encode the parameters from output bit stream
  * @param  pBsInfo output bit stream
  * @return 0 - success; otherwise - failed;
  */
  virtual int EXTAPI EncodeParameterSets (SFrameBSInfo* pBsInfo) = 0;

  /**
  * @brief  Force encoder to encoder frame as IDR if bIDR set as true
  * @param  bIDR true: force encoder to encode frame as IDR frame;false, return 1 and nothing to do
  * @return 0 - success; otherwise - failed;
  */
  virtual int EXTAPI ForceIntraFrame (bool bIDR, int iLayerId = -1) = 0;

  /**
  * @brief   Set option for encoder, detail option type, please refer to enumurate ENCODER_OPTION.
  * @param   pOption option for encoder such as InDataFormat, IDRInterval, SVC Encode Param, Frame Rate, Bitrate,...
  * @return  CM_RETURN: 0 - success; otherwise - failed;
  */
  virtual int EXTAPI SetOption (ENCODER_OPTION eOptionId, void* pOption) = 0;

  /**
  * @brief   Get option for encoder, detail option type, please refer to enumurate ENCODER_OPTION.
  * @param   pOption option for encoder such as InDataFormat, IDRInterval, SVC Encode Param, Frame Rate, Bitrate,...
  * @return  CM_RETURN: 0 - success; otherwise - failed;
  */
  virtual int EXTAPI GetOption (ENCODER_OPTION eOptionId, void* pOption) = 0;
  virtual ~ISVCEncoder() {}
};

extern "C"
{
#else

typedef struct ISVCEncoderVtbl ISVCEncoderVtbl;
typedef const ISVCEncoderVtbl* ISVCEncoder;
struct ISVCEncoderVtbl {

int (*Initialize) (ISVCEncoder*, const SEncParamBase* pParam);
int (*InitializeExt) (ISVCEncoder*, const SEncParamExt* pParam);

int (*GetDefaultParams) (ISVCEncoder*, SEncParamExt* pParam);

int (*Uninitialize) (ISVCEncoder*);

int (*EncodeFrame) (ISVCEncoder*, const SSourcePicture* kpSrcPic, SFrameBSInfo* pBsInfo);
int (*EncodeParameterSets) (ISVCEncoder*, SFrameBSInfo* pBsInfo);

int (*ForceIntraFrame) (ISVCEncoder*, bool bIDR);

int (*SetOption) (ISVCEncoder*, ENCODER_OPTION eOptionId, void* pOption);
int (*GetOption) (ISVCEncoder*, ENCODER_OPTION eOptionId, void* pOption);
};

#endif

typedef void (*WelsTraceCallback) (void* ctx, int level, const char* string);

/** @brief   Create encoder
 *  @param   ppEncoder encoder
 *  @return  0 - success; otherwise - failed;
*/
int  WelsCreateSVCEncoder (ISVCEncoder** ppEncoder);

/** @brief   Destroy encoder
*   @param   pEncoder encoder
 *  @return  void
*/
void WelsDestroySVCEncoder (ISVCEncoder* pEncoder);

#ifdef __cplusplus
}
#endif

#endif//WELS_VIDEO_CODEC_SVC_API_H__
