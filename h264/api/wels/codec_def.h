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

#ifndef WELS_VIDEO_CODEC_DEFINITION_H__
#define WELS_VIDEO_CODEC_DEFINITION_H__

/**
* @brief Enumerate the type of video format
*/
typedef enum {
  videoFormatRGB        = 1,             ///< rgb color formats
  videoFormatRGBA       = 2,
  videoFormatRGB555     = 3,
  videoFormatRGB565     = 4,
  videoFormatBGR        = 5,
  videoFormatBGRA       = 6,
  videoFormatABGR       = 7,
  videoFormatARGB       = 8,

  videoFormatYUY2       = 20,            ///< yuv color formats
  videoFormatYVYU       = 21,
  videoFormatUYVY       = 22,
  videoFormatI420       = 23,            ///< the same as IYUV
  videoFormatYV12       = 24,
  videoFormatInternal   = 25,            ///< only used in SVC decoder testbed

  videoFormatNV12       = 26,            ///< new format for output by DXVA decoding

  videoFormatVFlip      = 0x80000000
} EVideoFormatType;

/**
* @brief Enumerate  video frame type
*/
typedef enum {
  videoFrameTypeInvalid,    ///< encoder not ready or parameters are invalidate
  videoFrameTypeIDR,        ///< IDR frame in H.264
  videoFrameTypeI,          ///< I frame type
  videoFrameTypeP,          ///< P frame type
  videoFrameTypeSkip,       ///< skip the frame based encoder kernel
  videoFrameTypeIPMixed     ///< a frame where I and P slices are mixing, not supported yet
} EVideoFrameType;

/**
* @brief Enumerate  return type
*/
typedef enum {
  cmResultSuccess,          ///< successful
  cmInitParaError,          ///< parameters are invalid
  cmUnknownReason,
  cmMallocMemeError,        ///< malloc a memory error
  cmInitExpected,           ///< initial action is expected
  cmUnsupportedData
} CM_RETURN;

/**
* @brief Enumulate the nal unit type
*/
enum ENalUnitType {
  NAL_UNKNOWN     = 0,
  NAL_SLICE       = 1,
  NAL_SLICE_DPA   = 2,
  NAL_SLICE_DPB   = 3,
  NAL_SLICE_DPC   = 4,
  NAL_SLICE_IDR   = 5,      ///< ref_idc != 0
  NAL_SEI         = 6,      ///< ref_idc == 0
  NAL_SPS         = 7,
  NAL_PPS         = 8
                    ///< ref_idc == 0 for 6,9,10,11,12
};

/**
* @brief NRI: eNalRefIdc
*/
enum ENalPriority {
  NAL_PRIORITY_DISPOSABLE = 0,
  NAL_PRIORITY_LOW        = 1,
  NAL_PRIORITY_HIGH       = 2,
  NAL_PRIORITY_HIGHEST    = 3
};

/**
 * @brief eDeblockingIdc
 */
enum {
  DEBLOCKING_IDC_0 = 0,
  DEBLOCKING_IDC_1 = 1,
  DEBLOCKING_IDC_2 = 2
};
#define DEBLOCKING_OFFSET (6)
#define DEBLOCKING_OFFSET_MINUS (-6)

/* Error Tools definition */
typedef unsigned short ERR_TOOL;

/**
 @brief to do
*/
enum {
  ET_NONE = 0x00,           ///< NONE Error Tools
  ET_IP_SCALE = 0x01,       ///< IP Scalable
  ET_FMO = 0x02,            ///< Flexible Macroblock Ordering
  ET_IR_R1 = 0x04,          ///< Intra Refresh in predifined 2% MB
  ET_IR_R2 = 0x08,          ///< Intra Refresh in predifined 5% MB
  ET_IR_R3 = 0x10,          ///< Intra Refresh in predifined 10% MB
  ET_FEC_HALF = 0x20,       ///< Forward Error Correction in 50% redundency mode
  ET_FEC_FULL = 0x40,       ///< Forward Error Correction in 100% redundency mode
  ET_RFS = 0x80             ///< Reference Frame Selection
};

/**
* @brief In a GOP, multiple of the key frame number, derived from
*        the number of layers(index or array below)
*/
static const char kiKeyNumMultiple[] = {
  1, 1, 2, 4, 8, 16,
};

#endif//WELS_VIDEO_CODEC_DEFINITION_H__
