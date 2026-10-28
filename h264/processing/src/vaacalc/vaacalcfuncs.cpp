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

#include "util.h"

WELSVP_NAMESPACE_BEGIN



void VAACalcSad_c (const uint8_t* pCurData, const uint8_t* pRefData, int32_t iPicWidth, int32_t iPicHeight,
                   int32_t iPicStride,
                   int32_t* pFrameSad, int32_t* pSad8x8) {
  const uint8_t* tmp_ref = pRefData;
  const uint8_t* tmp_cur = pCurData;
  int32_t iMbWidth = (iPicWidth >> 4);
  int32_t mb_height = (iPicHeight >> 4);
  int32_t mb_index = 0;
  int32_t pic_stride_x8 = iPicStride << 3;
  int32_t step = (iPicStride << 4) - iPicWidth;

  *pFrameSad = 0;
  for (int32_t i = 0; i < mb_height; i ++) {
    for (int32_t j = 0; j < iMbWidth; j ++) {
      int32_t k, l;
      int32_t l_sad;
      const uint8_t* tmp_cur_row;
      const uint8_t* tmp_ref_row;

      l_sad =  0;
      tmp_cur_row = tmp_cur;
      tmp_ref_row = tmp_ref;
      for (k = 0; k < 8; k ++) {
        for (l = 0; l < 8; l ++) {
          int32_t diff = WELS_ABS (tmp_cur_row[l] - tmp_ref_row[l]);
          l_sad += diff;
        }
        tmp_cur_row += iPicStride;
        tmp_ref_row += iPicStride;
      }
      *pFrameSad += l_sad;
      pSad8x8[ (mb_index << 2) + 0] = l_sad;

      l_sad =  0;
      tmp_cur_row = tmp_cur + 8;
      tmp_ref_row = tmp_ref + 8;
      for (k = 0; k < 8; k ++) {
        for (l = 0; l < 8; l ++) {
          int32_t diff = WELS_ABS (tmp_cur_row[l] - tmp_ref_row[l]);
          l_sad += diff;
        }
        tmp_cur_row += iPicStride;
        tmp_ref_row += iPicStride;
      }
      *pFrameSad += l_sad;
      pSad8x8[ (mb_index << 2) + 1] = l_sad;

      l_sad =  0;
      tmp_cur_row = tmp_cur + pic_stride_x8;
      tmp_ref_row = tmp_ref + pic_stride_x8;
      for (k = 0; k < 8; k ++) {
        for (l = 0; l < 8; l ++) {
          int32_t diff = WELS_ABS (tmp_cur_row[l] - tmp_ref_row[l]);
          l_sad += diff;
        }
        tmp_cur_row += iPicStride;
        tmp_ref_row += iPicStride;
      }
      *pFrameSad += l_sad;
      pSad8x8[ (mb_index << 2) + 2] = l_sad;

      l_sad =  0;
      tmp_cur_row = tmp_cur + pic_stride_x8 + 8;
      tmp_ref_row = tmp_ref + pic_stride_x8 + 8;
      for (k = 0; k < 8; k ++) {
        for (l = 0; l < 8; l ++) {
          int32_t diff = WELS_ABS (tmp_cur_row[l] - tmp_ref_row[l]);
          l_sad += diff;
        }
        tmp_cur_row += iPicStride;
        tmp_ref_row += iPicStride;
      }
      *pFrameSad += l_sad;
      pSad8x8[ (mb_index << 2) + 3] = l_sad;

      tmp_ref += 16;
      tmp_cur += 16;
      ++mb_index;
    }
    tmp_ref += step;
    tmp_cur += step;
  }
}



WELSVP_NAMESPACE_END
