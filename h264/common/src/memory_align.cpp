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

#include <stdlib.h>
#include <string.h>
#include "memory_align.h"
#include "macros.h"

namespace WelsCommon {



CMemoryAlign::CMemoryAlign (const uint32_t kuiCacheLineSize)
  : m_nMemoryUsageInBytes (0)
{
  if ((kuiCacheLineSize == 0) || (kuiCacheLineSize & 0x0f))
    m_nCacheLineSize = 0x10;
  else
    m_nCacheLineSize = kuiCacheLineSize;
}

CMemoryAlign::~CMemoryAlign() {
  assert (m_nMemoryUsageInBytes == 0);
}

void* WelsMalloc (const uint32_t kuiSize, const char* kpTag, const uint32_t kiAlign) {
  const uint32_t kiSizeOfVoidPointer     = sizeof (void**);
  const uint32_t kiSizeOfInt             = sizeof (int32_t);
  const uint32_t kiAlignedBytes          = kiAlign - 1;
  const uint32_t kiTrialRequestedSize    = kuiSize + kiAlignedBytes + kiSizeOfVoidPointer + kiSizeOfInt;
  const uint32_t kiActualRequestedSize   = kiTrialRequestedSize;
  const uint32_t kiPayloadSize          = kuiSize;

  uint8_t* pBuf = (uint8_t*) malloc (kiActualRequestedSize);
  if (NULL == pBuf)
    return NULL;

  uint8_t* pAlignedBuffer;
  pAlignedBuffer = pBuf + kiAlignedBytes + kiSizeOfVoidPointer + kiSizeOfInt;
  pAlignedBuffer -= ((uintptr_t) pAlignedBuffer & kiAlignedBytes);
  * ((void**) (pAlignedBuffer - kiSizeOfVoidPointer)) = pBuf;
  * ((int32_t*) (pAlignedBuffer - (kiSizeOfVoidPointer + kiSizeOfInt))) = kiPayloadSize;

  return pAlignedBuffer;
}

void WelsFree (void* pPointer, const char* kpTag) {
  if (pPointer) {
    free (* (((void**) pPointer) - 1));
  }
}

void* CMemoryAlign::WelsMallocz (const uint32_t kuiSize, const char* kpTag) {
  void* pPointer = WelsMalloc (kuiSize, kpTag);
  if (NULL == pPointer) {
    return NULL;
  }
  // zero memory
  memset (pPointer, 0, kuiSize);

  return pPointer;
}

void* CMemoryAlign::WelsMalloc (const uint32_t kuiSize, const char* kpTag) {
  void* pPointer = WelsCommon::WelsMalloc (kuiSize, kpTag, m_nCacheLineSize);
  if (pPointer != NULL) {
    const int32_t kiMemoryLength = * ((int32_t*) ((uint8_t*)pPointer - sizeof (void**) - sizeof (
                                        int32_t))) + m_nCacheLineSize - 1 + sizeof (void**) + sizeof (int32_t);
    m_nMemoryUsageInBytes += kiMemoryLength;
  }
  return pPointer;
}

void CMemoryAlign::WelsFree (void* pPointer, const char* kpTag) {
  if (pPointer) {
    const int32_t kiMemoryLength = * ((int32_t*) ((uint8_t*)pPointer - sizeof (void**) - sizeof (
                                        int32_t))) + m_nCacheLineSize - 1 + sizeof (void**) + sizeof (int32_t);
    m_nMemoryUsageInBytes -= kiMemoryLength;
  }
  WelsCommon::WelsFree (pPointer, kpTag);
}

void* WelsMallocz (const uint32_t kuiSize, const char* kpTag) {
  void* pPointer = WelsMalloc (kuiSize, kpTag, 16);
  if (NULL == pPointer) {
    return NULL;
  }
  memset (pPointer, 0, kuiSize);
  return pPointer;
}

const uint32_t CMemoryAlign::WelsGetCacheLineSize() const {
  return m_nCacheLineSize;
}

const uint32_t CMemoryAlign::WelsGetMemoryUsage() const {
  return m_nMemoryUsageInBytes;
}

} // end of namespace WelsCommon
