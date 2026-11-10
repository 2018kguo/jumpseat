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

// exp_data.c
// export date cross various modules (.c)
#include "md.h"
namespace WelsEnc {
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// extern at mb_cache.h
const uint8_t g_kuiSmb4AddrIn256[16] = {
  0,           4,            16 * 4,      16 * 4 + 4,
  8,           12,           16 * 4 + 8,  16 * 4 + 12,
  16 * 8,      16 * 8 + 4,   16 * 12,     16 * 12 + 4,
  16 * 8 + 8,  16 * 8 + 12,  16 * 12 + 8, 16 * 12 + 12
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// extern at md.h
const int32_t g_kiQpCostTable[52] = {
  1, 1, 1, 1, 1, 1, 1, 1,  /*  0-7 */
  1, 1, 1, 1,              /*  8-11 */
  1, 1, 1, 1, 2, 2, 2, 2,  /* 12-19 */
  3, 3, 3, 4, 4, 4, 5, 6,  /* 20-27 */
  6, 7, 8, 9, 10, 11, 13, 14, /* 28-35 */
  16, 18, 20, 23, 25, 29, 32, 36, /* 36-43 */
  40, 45, 51, 57, 64, 72, 81, 91 /* 44-51 */
};
const int8_t g_kiMapModeI16x16[7] = {
  0, 1, 2, 3, 2, 2, 2
};//{I16_PRED_V, I16_PRED_H, I16_PRED_DC, I16_PRED_P, I16_PRED_DC, I16_PRED_DC, I16_PRED_DC};

const int8_t g_kiMapModeIntraChroma[7] = {
  0, 1, 2, 3, 0, 0, 0
};//{C_PRED_DC, C_PRED_H, C_PRED_V, C_PRED_P, C_PRED_DC_L, C_PRED_DC_T, C_PRED_DC_128};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

const uint8_t   g_kuiTemporalIdListTable[MAX_TEMPORAL_LEVEL][MAX_GOP_SIZE + 1] = {
  {
    0, 0, 0, 0, 0, 0, 0, 0,
    0
  },  // uiGopSize = 1
  {
    0, 1, 0, 0, 0, 0, 0, 0,
    0
  },  // uiGopSize = 2
  {
    0, 2, 1, 2, 0, 0, 0, 0,
    0
  },  // uiGopSize = 4
  {
    0, 3, 2, 3, 1, 3, 2, 3,
    0
  }  //uiGopSize = 8
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// extern at svc_encode_slice.h
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
}
