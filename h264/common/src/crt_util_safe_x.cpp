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
 * \file    crt_utils_safe_x.cpp
 *
 * \brief   common tool/function utilization
 *
 * \date    03/10/2009 Created
 *
 *************************************************************************************
 */

#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>

#include "macros.h"
#include "crt_util_safe_x.h" // Safe CRT routines like utils for cross platforms


int32_t WelsSnprintf (char* pBuffer,  int32_t iSizeOfBuffer, const char* kpFormat, ...) {
  va_list pArgPtr;
  int32_t iRc;

  va_start (pArgPtr, kpFormat);

  iRc = vsnprintf (pBuffer, iSizeOfBuffer, kpFormat, pArgPtr);

  va_end (pArgPtr);

  return iRc;
}

char* WelsStrncpy (char* pDest, int32_t iSizeInBytes, const char* kpSrc) {
  strncpy (pDest, kpSrc, iSizeInBytes); //confirmed_safe_unsafe_usage
  pDest[iSizeInBytes - 1] = '\0';
  return pDest;
}

int32_t WelsVsnprintf (char* pBuffer, int32_t iSizeOfBuffer, const char* kpFormat, va_list pArgPtr) {
  return vsnprintf (pBuffer, iSizeOfBuffer, kpFormat, pArgPtr); //confirmed_safe_unsafe_usage
}







char* WelsStrcat (char* pDest, uint32_t uiSizeInBytes, const char* kpSrc) {
  uint32_t uiCurLen = (uint32_t) strlen (pDest);
  if (uiSizeInBytes > uiCurLen)
    return WelsStrncpy (pDest + uiCurLen, uiSizeInBytes - uiCurLen, kpSrc);
  return pDest;
}




