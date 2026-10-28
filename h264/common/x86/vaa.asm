;*!
;* \copy
;*     Copyright (c)  2010-2013, Cisco Systems
;*     All rights reserved.
;*
;*     Redistribution and use in source and binary forms, with or without
;*     modification, are permitted provided that the following conditions
;*     are met:
;*
;*        * Redistributions of source code must retain the above copyright
;*          notice, this list of conditions and the following disclaimer.
;*
;*        * Redistributions in binary form must reproduce the above copyright
;*          notice, this list of conditions and the following disclaimer in
;*          the documentation and/or other materials provided with the
;*          distribution.
;*
;*     THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
;*     "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
;*     LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
;*     FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
;*     COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
;*     INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
;*     BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
;*     LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
;*     CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
;*     LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
;*     ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
;*     POSSIBILITY OF SUCH DAMAGE.
;*
;*
;*  vaa.asm
;*
;*  Abstract
;*      sse2 for pVaa routines
;*
;*  History
;*      04/14/2010  Created
;*      06/07/2010  Added AnalysisVaaInfoIntra_sse2(ssse3)
;*      06/10/2010  Tune rc_sad_frame_sse2 and got about 40% improvement
;*      08/11/2010  Added abs_difference_mbrow_sse2 & sum_sqrsum_mbrow_sse2
;*
;*************************************************************************/
%include "asm_inc.asm"

;***********************************************************************
; Macros and other preprocessor constants
;***********************************************************************

; by comparing it outperforms than phaddw(SSSE3) sets
%macro SUM_WORD_8x2_SSE2    2   ; dst(pSrc), tmp
    ; @sum_8x2 begin
    pshufd %2, %1, 04Eh ; 01001110 B
    paddw %1, %2
    pshuflw %2, %1, 04Eh    ; 01001110 B
    paddw %1, %2
    pshuflw %2, %1, 0B1h    ; 10110001 B
    paddw %1, %2
    ; end of @sum_8x2
%endmacro   ; END of SUM_WORD_8x2_SSE2

;***********************************************************************
; Code
;***********************************************************************

SECTION .text

;***********************************************************************
;   uint8_t MdInterAnalysisVaaInfo_sse41( int32_t *pSad8x8 )
;***********************************************************************
WELS_EXTERN MdInterAnalysisVaaInfo_sse41
    %assign push_num 0
    LOAD_1_PARA
    movdqa xmm0,[r0]
    pshufd xmm1, xmm0, 01Bh
    paddd xmm1, xmm0
    pshufd xmm2, xmm1, 0B1h
    paddd xmm1, xmm2
    psrad xmm1, 02h     ; iAverageSad
    movdqa xmm2, xmm1
    psrad xmm2, 06h
    movdqa xmm3, xmm0   ; iSadBlock
    psrad xmm3, 06h
    psubd xmm3, xmm2
    pmulld xmm3, xmm3   ; [comment]: pmulld from SSE4.1 instruction sets
    pshufd xmm4, xmm3, 01Bh
    paddd xmm4, xmm3
    pshufd xmm3, xmm4, 0B1h
    paddd xmm3, xmm4
    movd r0d, xmm3
    cmp r0d, 20 ; INTER_VARIANCE_SAD_THRESHOLD

    jb near .threshold_exit
    pshufd xmm0, xmm0, 01Bh
    pcmpgtd xmm0, xmm1  ; iSadBlock > iAverageSad
    movmskps retrd, xmm0
    ret
.threshold_exit:
    mov retrd, 15
    ret

