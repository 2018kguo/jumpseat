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
;*      vaa.asm
;*
;*      Abstract
;*      sse2 for pVaa routines
;*
;*  History
;*      04/14/2010      Created
;*              06/07/2010      Added AnalysisVaaInfoIntra_sse2(ssse3)
;*              06/10/2010      Tune rc_sad_frame_sse2 and got about 40% improvement
;*              08/11/2010      Added abs_difference_mbrow_sse2 & sum_sqrsum_mbrow_sse2
;*
;*************************************************************************/
%include "asm_inc.asm"

;***********************************************************************
; Macros and other preprocessor constants
;***********************************************************************
%macro SUM_SQR_SSE2     3       ; dst, pSrc, zero
    movdqa %1, %2
    punpcklbw %1, %3
    punpckhbw %2, %3
    pmaddwd %1, %1
    pmaddwd %2, %2
    paddd %1, %2
    pshufd %2, %1, 04Eh   ; 01001110 B
    paddd %1, %2
    pshufd %2, %1, 0B1h   ; 10110001 B
    paddd %1, %2
%endmacro       ; END OF SUM_SQR_SSE2

%macro WELS_SAD_16x2_SSE2  3 ;esi :%1 edi:%2 ebx:%3
    movdqa        xmm1,   [%1]
    movdqa        xmm2,   [%2]
    movdqa        xmm3,   [%1+%3]
    movdqa        xmm4,   [%2+%3]
    psadbw        xmm1,   xmm2
    psadbw        xmm3,   xmm4
    paddd xmm6,   xmm1
    paddd xmm6,   xmm3
    lea           %1,     [%1+%3*2]
    lea           %2,     [%2+%3*2]
%endmacro

; by comparing it outperforms than phaddw(SSSE3) sets
%macro SUM_WORD_8x2_SSE2        2       ; dst(pSrc), tmp
    ; @sum_8x2 begin
    pshufd %2, %1, 04Eh   ; 01001110 B
    paddw %1, %2
    pshuflw %2, %1, 04Eh  ; 01001110 B
    paddw %1, %2
    pshuflw %2, %1, 0B1h  ; 10110001 B
    paddw %1, %2
    ; end of @sum_8x2
%endmacro       ; END of SUM_WORD_8x2_SSE2

%macro WELS_SAD_SUM_SQSUM_16x1_SSE2 3 ;esi:%1,edi:%2,ebx:%3
    movdqa        xmm1,   [%1]
    movdqa        xmm2,   [%2]
    movdqa        xmm3,   xmm1
    psadbw        xmm3,   xmm2
    paddd         xmm6,   xmm3

    movdqa        xmm3,   xmm1
    psadbw        xmm3,   xmm0
    paddd         xmm5,   xmm3

    movdqa        xmm2,   xmm1
    punpcklbw     xmm1,   xmm0
    punpckhbw     xmm2,   xmm0
    pmaddwd               xmm1,   xmm1
    pmaddwd               xmm2,   xmm2
    paddd         xmm4,   xmm1
    paddd         xmm4,   xmm2

    add           %1,     %3
    add           %2,     %3
%endmacro

%macro WELS_SAD_SUM_SQSUM_SQDIFF_16x1_SSE2 3 ;esi:%1 edi:%2 ebx:%3
    movdqa        xmm1,   [%1]
    movdqa        xmm2,   [%2]
    movdqa        xmm3,   xmm1
    psadbw        xmm3,   xmm2
    paddd         xmm7,   xmm3    ; sad

    movdqa        xmm3,   xmm1
    pmaxub        xmm3,   xmm2
    pminub        xmm2,   xmm1
    psubb xmm3,   xmm2    ; diff

    movdqa        xmm2,   xmm1
    psadbw        xmm2,   xmm0
    paddd xmm6,   xmm2    ; sum

    movdqa                xmm2,   xmm1
    punpcklbw     xmm1,   xmm0
    punpckhbw     xmm2,   xmm0
    pmaddwd               xmm1,   xmm1
    pmaddwd               xmm2,   xmm2
    paddd         xmm5,   xmm1
    paddd         xmm5,   xmm2    ; sqsum

    movdqa                xmm1,   xmm3
    punpcklbw     xmm1,   xmm0
    punpckhbw     xmm3,   xmm0
    pmaddwd               xmm1,   xmm1
    pmaddwd               xmm3,   xmm3
    paddd         xmm4,   xmm1
    paddd         xmm4,   xmm3    ; sqdiff

    add           %1,     %3
    add           %2,     %3
%endmacro

%macro WELS_SAD_SD_MAD_16x1_SSE2       7 ;esi:%5 edi:%6 ebx:%7
%define sad_reg                 %1
%define sum_cur_reg             %2
%define sum_ref_reg             %3
%define mad_reg                 %4
    movdqa        xmm1,           [%5]
    movdqa        xmm2,           [%6]
    movdqa        xmm3,           xmm1
    psadbw        xmm3,           xmm0
    paddd         sum_cur_reg,    xmm3    ; sum_cur
    movdqa        xmm3,           xmm2
    psadbw        xmm3,           xmm0
    paddd sum_ref_reg,                    xmm3    ; sum_ref

    movdqa        xmm3,           xmm1
    pmaxub        xmm3,           xmm2
    pminub        xmm2,           xmm1
    psubb xmm3,           xmm2    ; abs diff
    pmaxub        mad_reg,        xmm3    ; max abs diff

    psadbw        xmm3,           xmm0
    paddd sad_reg,        xmm3    ; sad

    add                   %5,             %7
    add                   %6,             %7
%endmacro

%macro WELS_MAX_REG_SSE2       1       ; xmm1, xmm2, xmm3 can be used
%define max_reg  %1
    movdqa        xmm1,           max_reg
    psrldq        xmm1,           4
    pmaxub        max_reg,        xmm1
    movdqa        xmm1,           max_reg
    psrldq        xmm1,           2
    pmaxub        max_reg,        xmm1
    movdqa        xmm1,           max_reg
    psrldq        xmm1,           1
    pmaxub        max_reg,        xmm1
%endmacro

%macro WELS_SAD_BGD_SQDIFF_16x1_SSE2   7 ;esi:%5 edi:%6 ebx:%7
%define sad_reg         %1
%define sum_reg         %2
%define mad_reg         %3
%define sqdiff_reg      %4
    movdqa                xmm1,           [%5]
    movdqa                xmm2,           xmm1
    movdqa                xmm3,           xmm1
    punpcklbw     xmm2,           xmm0
    punpckhbw     xmm3,           xmm0
    pmaddwd               xmm2,           xmm2
    pmaddwd               xmm3,           xmm3
    paddd         xmm2,           xmm3
    movdqa                xmm3,           xmm2
    psllq         xmm2,           32
    psrlq         xmm3,           32
    psllq         xmm3,           32
    paddd         xmm2,           xmm3
    paddd         sad_reg,        xmm2            ; sqsum

    movdqa        xmm2,           [%6]
    movdqa        xmm3,           xmm1
    psadbw        xmm3,           xmm0
    paddd sum_reg,                        xmm3    ; sum_cur
    movdqa        xmm3,           xmm2
    psadbw        xmm3,           xmm0
    pslldq        xmm3,           4
    paddd sum_reg,                        xmm3    ; sum_ref

    movdqa        xmm3,           xmm1
    pmaxub        xmm3,           xmm2
    pminub        xmm2,           xmm1
    psubb xmm3,           xmm2    ; abs diff
    pmaxub        mad_reg,        xmm3    ; max abs diff

    movdqa        xmm1,           xmm3
    psadbw        xmm3,           xmm0
    paddd sad_reg,        xmm3    ; sad

    movdqa                xmm3,   xmm1
    punpcklbw     xmm1,   xmm0
    punpckhbw     xmm3,   xmm0
    pmaddwd               xmm1,   xmm1
    pmaddwd               xmm3,   xmm3
    paddd         sqdiff_reg,     xmm1
    paddd         sqdiff_reg,     xmm3    ; sqdiff

    add           %5,     %7
    add           %6,     %7
%endmacro

;***********************************************************************
; Code
;***********************************************************************

SECTION .text

;*************************************************************************************************************
;void VAACalcSad_sse2( const uint8_t *cur_data, const uint8_t *ref_data, int32_t iPicWidth, int32_t iPicHeight
;                                                               int32_t iPicStride, int32_t *psadframe, int32_t *psad8x8)
;*************************************************************************************************************

WELS_EXTERN VAACalcSad_sse2
%define         cur_data                        r0
%define         ref_data                        r1
%define         iPicWidth                       r2
%define         iPicHeight              r3
%define         iPicStride              r4
%define         psadframe                       r5
%define         psad8x8                         r6

    push r12
    push r13
    %assign push_num 2
    LOAD_7_PARA
    PUSH_XMM 8
    SIGN_EXTENSION r2,r2d
    SIGN_EXTENSION r3,r3d
    SIGN_EXTENSION r4,r4d

    mov   r12,r4
    shr           r2,     4                                       ; iPicWidth/16
    shr           r3,     4                                       ; iPicHeight/16

    shl           r12,    4                                                               ; iPicStride*16
    pxor  xmm0,   xmm0
    pxor  xmm7,   xmm7            ; iFrameSad
height_loop:
    mov           r13,    r2
    push  r0
    push  r1
width_loop:
    pxor  xmm6,   xmm6
    WELS_SAD_16x2_SSE2 r0,r1,r4
    WELS_SAD_16x2_SSE2 r0,r1,r4
    WELS_SAD_16x2_SSE2 r0,r1,r4
    WELS_SAD_16x2_SSE2 r0,r1,r4
    paddd xmm7,           xmm6
    movd  [r6],           xmm6
    psrldq        xmm6,           8
    movd  [r6+4], xmm6

    pxor  xmm6,   xmm6
    WELS_SAD_16x2_SSE2 r0,r1,r4
    WELS_SAD_16x2_SSE2 r0,r1,r4
    WELS_SAD_16x2_SSE2 r0,r1,r4
    WELS_SAD_16x2_SSE2 r0,r1,r4
    paddd xmm7,           xmm6
    movd  [r6+8], xmm6
    psrldq        xmm6,           8
    movd  [r6+12],        xmm6

    add           r6,     16
    sub           r0,     r12
    sub           r1,     r12
    add           r0,     16
    add           r1,     16

    dec           r13
    jnz           width_loop

    pop           r1
    pop           r0
    add           r0,     r12
    add           r1,     r12

    dec   r3
    jnz           height_loop

    ;mov          r13,    [psadframe]
    movdqa        xmm5,   xmm7
    psrldq        xmm7,   8
    paddd xmm7,   xmm5
    movd  [psadframe],    xmm7

%undef          cur_data
%undef          ref_data
%undef          iPicWidth
%undef          iPicHeight
%undef          iPicStride
%undef          psadframe
%undef          psad8x8
%undef          pushsize
    POP_XMM
    LOAD_7_PARA_POP
    pop r13
    pop r12
    %assign push_num 0
    ret

%define ptrword qword

%define xmm_width 16
%define ymm_width 32

%macro PUSHM 1-*
    %rep %0
        push           %1
        %rotate 1
    %endrep
    %assign push_num push_num + %0
%endmacro

%macro POPM 1-*
    %rep %0
        %rotate -1
        pop            %1
    %endrep
    %assign push_num push_num - %0
%endmacro

%define stack_alloc_min 8

; Allocate aligned stack space.
; address_out=%1 size=%2 alignment=%3
%macro STACK_ALLOC 3
%if (%3) & ((%3) - 1)
    %error non-power-of-2 alignment requested.
%endif
%if (%3) > 0
    %assign stack_alloc_align ((%3) + stack_alloc_min - 1) / stack_alloc_min
%else
    %assign stack_alloc_align 1
%endif
    %assign stack_alloc_num ((%2) + stack_alloc_min - 1) / stack_alloc_min + stack_alloc_align - 1
    %assign push_num push_num + stack_alloc_num
    sub            r7, stack_alloc_min * stack_alloc_num
%if stack_alloc_align == 1
    mov            %1, r7
%else
    lea            %1, [r7 + stack_alloc_min * (stack_alloc_align - 1)]
    and            %1, -(stack_alloc_min * stack_alloc_align)
%endif
%endmacro

; Deallocate stack space allocated with STACK_ALLOC.
%macro STACK_DEALLOC 0
    add            r7, stack_alloc_min * stack_alloc_num
    %assign push_num push_num - stack_alloc_num
%endmacro

; Max unsigned byte per quadword
; out=%1 in=%2 tmp=%3
%macro AVX2_Maxubq 3
    vpsrlq         %3, %2, 32
    vpmaxub        %1, %2, %3
    vpsrlq         %3, %1, 16
    vpmaxub        %1, %1, %3
    vpsrlq         %3, %1,  8
    vpmaxub        %1, %1, %3
%endmacro

; Max unsigned byte per quadword. 2 register input.
; Results interleaved as least significant byte of even/odd doublewords.
; out=%1 in_a=%2 in_b=%3 tmp=%4
%macro AVX2_Maxubq2 4
    vpblendd       %4, %2, %3, 10101010b
    vpshufd        %4, %4, 10110001b
    vpblendd       %1, %2, %3, 01010101b
    vpmaxub        %1, %4, %1
    vpsrld         %4, %1, 16
    vpmaxub        %1, %1, %4
    vpsrld         %4, %1,  8
    vpmaxub        %1, %1, %4
%endmacro

; res=%1 src=%2 zero=%3 tmp=%4 add_to_res=%5
%macro AVX2_Sqsumbdw 5
    vpunpcklbw     %4, %2, %3
%if %5
    vpmaddwd       %4, %4, %4
    vpaddd         %1, %1, %4
%else
    vpmaddwd       %1, %4, %4
%endif
    vpunpckhbw     %4, %2, %3
    vpmaddwd       %4, %4, %4
    vpaddd         %1, %1, %4
%endmacro

; res=%1 src=%2 zero=%3 tmp=%4 add_to_res=%5
%macro AVX2_Sumbdw 5
%if %5
    vpsadbw        %4, %2, %3
    vpaddd         %1, %1, %4
%else
    vpsadbw        %1, %2, %3
%endif
%endmacro

; res=%1 a=%2 b=%3 a=%4 tmp=%5
%macro AVX2_AbsDiffub 5
    vpsubusb       %5, %2, %3
    vpsubusb       %1, %3, %4
    vpor           %1, %5, %1
%endmacro

; sad=%1 cur_data=%2 ref_data=%3 tmp=%4 accumulate_results=%5
%macro AVX2_Sadbdw 5
%if %5
    vpsadbw        %4, %2, %3
    vpaddd         %1, %1, %4
%else
    vpsadbw        %1, %2, %3
%endif
%endmacro

; sad=%1 sum_cur=%2 sqsum_cur=%3 cur_data=%4 ref_data=%5 zero=%6 tmp=%7 accumulate_results=%8
%macro AVX2_SadSumSqsumbdw 8
    AVX2_Sadbdw    %1, %4, %5, %7, %8
    AVX2_Sumbdw    %2, %4, %6, %7, %8
    AVX2_Sqsumbdw  %3, %4, %6, %7, %8
%endmacro

; sad=%1 pCur=%2 pRef=%3 tmp=%4 accumulate_results=%5
%macro AVX2_Sad 5
    vmovdqu        %4, [%2]
    AVX2_Sadbdw    %1, %4, [%3], %4, %5
%endmacro

; sad=%1 sum_cur=%2 sqsum_cur=%3 pCur=%4 pRef=%5 zero=%6 tmp=%7,%8 accumulate_results=%9
%macro AVX2_SadSumSqsum 9
    vmovdqu        %7, [%4]
    AVX2_SadSumSqsumbdw %1, %2, %3, %7, [%5], %6, %8, %9
%endmacro

; sad=%1 sum_cur=%2 sqsum_cur=%3 sqdiff=%4 pCur=%5 pRef=%6 zero=%7 tmp=%8,%9,%10 accumulate_results=%11
%macro AVX2_SadSumSqsumSqdiff 11
    vmovdqu        %8,  [%5]
    vmovdqu        %9,  [%6]
    AVX2_SadSumSqsumbdw %1, %2, %3, %8, %9, %7, %10, %11
    AVX2_AbsDiffub %9,  %8,  %9,  %8,  %10
    AVX2_Sqsumbdw  %4,  %9,  %7,  %10, %11
%endmacro

; sad=%1 sum_cur=%2 sum_ref=%3 mad=%4 pCur=%5 pRef=%6 zero=%7 tmp=%8,%9,%10 accumulate_results=%11
%macro AVX2_SadSdMad 11
    vmovdqu        %8,  [%5]
    vmovdqu        %9,  [%6]
    AVX2_Sumbdw    %2,  %8,  %7,  %10, %11
    AVX2_Sumbdw    %3,  %9,  %7,  %10, %11
    AVX2_Sadbdw    %1,  %8,  %9,  %10, %11
%if %11
    AVX2_AbsDiffub %9,  %8,  %9,  %8, %10
    vpmaxub        %4,  %4,  %9
%else
    AVX2_AbsDiffub %4,  %8,  %9,  %8, %10
%endif
%endmacro

; sad=%1 sum_cur=%2 sum_ref=%3 mad=%4 sqdiff=%5 sqsum_cur=%6 pCur=%7 pRef=%8 zero=%9 tmp=%10,%11,%12 accumulate_results=%13
%macro AVX2_SadBgdSqdiff 13
%ifidn %12, 0
    vmovdqu        %10, [%7]
    AVX2_Sumbdw    %2,  %10, %9,  %11, %13
    AVX2_Sqsumbdw  %6,  %10, %9,  %11, %13
    vmovdqu        %11, [%8]
    AVX2_Sadbdw    %1,  %10, %11, %10, %13
    AVX2_Sumbdw    %3,  %11, %9,  %10, %13
    vmovdqu        %10, [%7]
%if %13
    AVX2_AbsDiffub %11, %10, %11, [%7], %10
    vpmaxub        %4,  %4,  %11
    AVX2_Sqsumbdw  %5,  %11, %9,  %10, %13
%else
    AVX2_AbsDiffub %4,  %10, %11, [%7], %10
    AVX2_Sqsumbdw  %5,  %4,  %9,  %10, %13
%endif
%else
    vmovdqu        %10, [%7]
    vmovdqu        %11, [%8]
    AVX2_Sadbdw    %1,  %10, %11, %12, %13
    AVX2_Sumbdw    %2,  %10, %9,  %12, %13
    AVX2_Sumbdw    %3,  %11, %9,  %12, %13
    AVX2_Sqsumbdw  %6,  %10, %9,  %12, %13
%if %13
    AVX2_AbsDiffub %11, %10, %11, %10, %12
    vpmaxub        %4,  %4,  %11
    AVX2_Sqsumbdw  %5,  %11, %9,  %10, %13
%else
    AVX2_AbsDiffub %4,  %10, %11, %10, %12
    AVX2_Sqsumbdw  %5,  %4,  %9,  %10, %13
%endif
%endif
%endmacro

; p_dst=%1 mmreg_prefix=%2 data=%3 tmp=%4 second_blocks=%5
%macro AVX2_Store8x8Accdw 5
    vpshufd        %2%4, %2%3, 1000b
%ifidni %2, x
    vmovlps        [%1 + 8 * %5], x%4
%elif %5 == 0
    vmovdqu        [%1], %2%4
%else
    vmovlps        [%1 +  8], x%4
    vextracti128   x%4, %2%4, 1
    vmovlps        [%1 + 24], x%4
%endif
%endmacro

; p_dst=%1 mmreg_prefix=%2 data=%3 tmp=%4 second_blocks=%5
%macro AVX2_Store8x8Accb 5
    vpunpckhqdq    %2%4, %2%3, %2%3
    vpunpcklbw     %2%4, %2%3, %2%4
%if %5 == 0
    vmovd          [%1 + 0], x%4
%ifidni %2, y
    vextracti128   x%4, %2%4, 1
    vmovd          [%1 + 4], x%4
%endif
%else
    vpextrw        [%1 + 2], x%4, 0
%ifidni %2, y
    vextracti128   x%4, %2%4, 1
    vpextrw        [%1 + 6], x%4, 0
%endif
%endif
%endmacro

; p_dst=%1 data=%2 tmp=%3,%4 second_blocks=%5
%macro AVX2_Store2x8x8Accb 5
    vpunpckhqdq    y%3, y%2, y%2
    vpunpcklbw     y%3, y%2, y%3
    vextracti128   x%4, y%3, 1
    vpsllq         x%4, x%4, 32
    vpblendd       x%4, x%3, x%4, 1010b
%if %5
    vpslld         x%4, x%4, 16
    vpblendw       x%4, x%4, [%1], 01010101b
%endif
    vmovdqu        [%1], x%4
%endmacro

; p_dst=%1 mmreg_prefix=%2 data=%3 tmp=%4 add_to_dst=%5
%macro AVX2_Store16x16Accdw 5
%ifidni %2, x
%if %5
    vmovd          x%4, [%1 + 0]
    vpaddd         x%3, x%4, x%3
%endif
    vmovd          [%1 + 0], x%3
%elif %5 == 0
    vmovd          [%1 + 0], x%3
    vextracti128   x%3, %2%3, 1
    vmovd          [%1 + 4], x%3
%else
    vextracti128   x%4, %2%3, 1
    vpunpckldq     x%4, x%3, x%4
    vmovq          x%3, [%1 + 0]
    vpaddd         x%3, x%3, x%4
    vmovlps        [%1 + 0], x%3
%endif
%endmacro

; p_dst1=%1 p_dst2=%2 i_dst_offset=%3 gpr_tmp=%4 mmreg_prefix=%5 data=%6 mm_tmp=%7 add_to_dst=%8
%macro AVX2_Store2x16x16Accdw 8
%ifidni %5, x
    mov            %4, %1
%if %8 == 0
    vmovd          [%4 + %3], x%6
    mov            %4, %2
    vpextrd        [%4 + %3], x%6, 2
%else
    vmovd          x%7, [%4 + %3]
    vpaddd         x%7, x%7, x%6
    vmovd          [%4 + %3], x%7
    mov            %4, %2
    vpbroadcastd   x%7, [%4 + %3]
    vpaddd         x%7, x%7, x%6
    vpextrd        [%4 + %3], x%7, 2
%endif
%else
    vextracti128   x%7, %5%6, 1
    vpblendd       x%6, x%6, x%7, 1010b
    mov            %4, %1
%if %8 == 0
    vmovlps        [%4 + %3], x%6
    mov            %4, %2
    vmovhps        [%4 + %3], x%6
%else
    vmovq          x%7, [%4 + %3]
    vpaddd         x%7, x%7, x%6
    vmovlps        [%4 + %3], x%7
    mov            %4, %2
    vpbroadcastq   x%7, [%4 + %3]
    vpaddd         x%7, x%7, x%6
    vmovhps        [%4 + %3], x%7
%endif
%endif
%endmacro

; x/y-mm_prefix=%1 mm_clobber=%2,%3,%4,%5,%6 b_second_blocks=%7
%macro AVX2_CalcSad_8Lines 7
%define mm_tmp0    %2
%define mm_sad     %3
%define mm_sad2    %4
%define mm_sad3    %5
%define mm_sad4    %6
%define b_second_blocks %7
%ifdef i_stride5
    %define i_stride5_ i_stride5
%else
    lea            r_tmp, [5 * i_stride]
    %define i_stride5_ r_tmp
%endif
    ; Use multiple accumulators to shorten dependency chains and enable more parallelism.
    AVX2_Sad       %1 %+ mm_sad,  p_cur,                  p_ref,                  %1 %+ mm_tmp0, 0
    AVX2_Sad       %1 %+ mm_sad2, p_cur + 1 * i_stride,   p_ref + 1 * i_stride,   %1 %+ mm_tmp0, 0
    AVX2_Sad       %1 %+ mm_sad3, p_cur + 2 * i_stride,   p_ref + 2 * i_stride,   %1 %+ mm_tmp0, 0
    AVX2_Sad       %1 %+ mm_sad4, p_cur + 1 * i_stride3,  p_ref + 1 * i_stride3,  %1 %+ mm_tmp0, 0
    AVX2_Sad       %1 %+ mm_sad,  p_cur + 4 * i_stride,   p_ref + 4 * i_stride,   %1 %+ mm_tmp0, 1
    AVX2_Sad       %1 %+ mm_sad2, p_cur + 1 * i_stride5_, p_ref + 1 * i_stride5_, %1 %+ mm_tmp0, 1
%ifdef i_stride7
    %define i_stride7_ i_stride7
%else
    lea            r_tmp, [i_stride + 2 * i_stride3]
    %define i_stride7_ r_tmp
%endif
    AVX2_Sad       %1 %+ mm_sad3, p_cur + 2 * i_stride3,  p_ref + 2 * i_stride3,  %1 %+ mm_tmp0, 1
    AVX2_Sad       %1 %+ mm_sad4, p_cur + 1 * i_stride7_, p_ref + 1 * i_stride7_, %1 %+ mm_tmp0, 1
%undef i_stride5_
%undef i_stride7_
    ; Increment addresses for the next iteration. Doing this early is beneficial on Haswell.
    add            p_cur, %1 %+ mm_width
    add            p_ref, %1 %+ mm_width
    ; Collapse accumulators.
    vpaddd         %1 %+ mm_sad,  %1 %+ mm_sad,  %1 %+ mm_sad2
    vpaddd         %1 %+ mm_sad3, %1 %+ mm_sad3, %1 %+ mm_sad4
    vpaddd         %1 %+ mm_sad,  %1 %+ mm_sad,  %1 %+ mm_sad3
    AVX2_Store8x8Accdw p_sad8x8 + xcnt_unit * i_xcnt, %1, mm_sad, mm_tmp0, b_second_blocks
    vpaddd         y %+ mm_sadframe, y %+ mm_sadframe, y %+ mm_sad
%undef mm_tmp0
%undef mm_sad
%undef mm_sad2
%undef mm_sad3
%undef mm_sad4
%undef b_second_blocks
%endmacro

;*************************************************************************************************************
;void VAACalcSad_avx2( const uint8_t *cur_data, const uint8_t *ref_data, int32_t iPicWidth, int32_t iPicHeight
;                                                               int32_t iPicStride, int32_t *psadframe, int32_t *psad8x8)
;*************************************************************************************************************

WELS_EXTERN VAACalcSad_avx2
%define          p_sadframe                    ptrword arg6
%define          p_sad8x8                      ptrword arg7
%define          saveregs                      rbx, rbp, r12

%assign push_num 0
    LOAD_5_PARA
    PUSH_XMM 7
    SIGN_EXTENSION r2, r2d
    SIGN_EXTENSION r3, r3d
    SIGN_EXTENSION r4, r4d
    PUSHM          saveregs

%define mm_zero mm0
%define mm_sadframe mm6
    vpxor          x %+ mm_zero, x %+ mm_zero, x %+ mm_zero
    vmovdqa        y %+ mm_sadframe, y %+ mm_zero

    and            r2, -16                     ; iPicWidth &= -16
    jle            .done                       ; bail if iPicWidth < 16
    sar            r3, 4                       ; iPicHeight / 16
    jle            .done                       ; bail if iPicHeight < 16
    shr            r2, 2                       ; iPicWidth / 4

%define p_cur     r0
%define p_ref     r1
%define i_xcnt    r2
%define i_ycnt    ptrword arg4
%define i_stride  r4
%define xcnt_unit 4
    mov            rbp, p_sad8x8
    %define i_stride3 rbx
    %define i_stride5 r12
    %define i_stride7 r6
    %undef  p_sad8x8
    %define p_sad8x8  rbp
    lea            i_stride3, [3 * i_stride]
    lea            i_stride5, [5 * i_stride]
    lea            i_stride7, [i_stride + 2 * i_stride3]

    ; offset pointer so as to compensate for the i_xcnt offset below.
    sub            p_sad8x8, 4 * 16 / xcnt_unit

    push           i_xcnt
%assign push_num push_num + 1
%define i_xcnt_load ptrword [r7]

.height_loop:
    ; use end-of-line pointers so as to enable use of a negative counter as index.
    lea            p_sad8x8, [p_sad8x8 + xcnt_unit * i_xcnt]
    ; use a negative loop counter so as to enable counting toward zero and indexing with the same counter.
    neg            i_xcnt
    add            i_xcnt, 16 / xcnt_unit
    jz             .width_loop_upper8_remaining16
.width_loop_upper8:
    AVX2_CalcSad_8Lines y, mm1, mm2, mm3, mm4, mm5, 0
    add            i_xcnt, 32 / xcnt_unit
    jl             .width_loop_upper8
    jg             .width_loop_upper8_end
.width_loop_upper8_remaining16:
    AVX2_CalcSad_8Lines x, mm1, mm2, mm3, mm4, mm5, 0
.width_loop_upper8_end:
    lea            p_cur, [p_cur + 8 * i_stride]
    lea            p_ref, [p_ref + 8 * i_stride]
    xor            i_xcnt, i_xcnt
    sub            i_xcnt, i_xcnt_load
    lea            p_cur, [p_cur + xcnt_unit * i_xcnt]
    lea            p_ref, [p_ref + xcnt_unit * i_xcnt]
    add            i_xcnt, 16 / xcnt_unit
    jz             .width_loop_lower8_remaining16
.width_loop_lower8:
    AVX2_CalcSad_8Lines y, mm1, mm2, mm3, mm4, mm5, 1
    add            i_xcnt, 32 / xcnt_unit
    jl             .width_loop_lower8
    jg             .width_loop_lower8_end
.width_loop_lower8_remaining16:
    AVX2_CalcSad_8Lines x, mm1, mm2, mm3, mm4, mm5, 1
.width_loop_lower8_end:
    lea            p_cur, [p_cur + 8 * i_stride]
    lea            p_ref, [p_ref + 8 * i_stride]
    xor            i_xcnt, i_xcnt
    sub            i_xcnt, i_xcnt_load
    lea            p_cur, [p_cur + xcnt_unit * i_xcnt]
    lea            p_ref, [p_ref + xcnt_unit * i_xcnt]
    neg            i_xcnt
    sub            i_ycnt, 1
    jnz            .height_loop

    pop            i_xcnt
%assign push_num push_num - 1
%undef i_xcnt_load

.done:
    mov            r6, p_sadframe
    vextracti128   xmm2, y %+ mm_sadframe, 1
    vpaddd         xmm2, x %+ mm_sadframe, xmm2
    vpunpckhqdq    xmm1, xmm2, xmm2
    vpaddd         xmm2, xmm2, xmm1
    vmovd          [r6], xmm2
    vzeroupper

    POPM           saveregs
    POP_XMM
    LOAD_5_PARA_POP
%undef           p_cur
%undef           p_ref
%undef           i_xcnt
%undef           i_ycnt
%undef           i_stride
%undef           r_tmp
%undef           xcnt_unit
%undef           i_stride3
%undef           i_stride5
%undef           i_stride7
%undef           mm_sadframe
%undef           mm_zero
%undef           saveregs
%undef           p_sadframe
%undef           p_sad8x8
    ret

; x/y-mm_prefix=%1 mm_clobber=%2,%3,%4,%5,%6 b_second_blocks=%7
%macro AVX2_CalcSadVar_8Lines 7
%define mm_tmp0    %2
%define mm_tmp1    %3
%define mm_sad     %4
%define mm_sum     %5
%define mm_sqsum   %6
%define b_second_blocks %7
    ; Unroll for better performance on Haswell.
    ; Avoid unrolling for the 16 px case so as to reduce the code footprint.
%ifidni %1, y
    lea            r_tmp, [5 * i_stride]
    AVX2_SadSumSqsum %1 %+ mm_sad, %1 %+ mm_sum, %1 %+ mm_sqsum, p_cur,                 p_ref,                 %1 %+ mm_zero, %1 %+ mm_tmp0, %1 %+ mm_tmp1, 0
    AVX2_SadSumSqsum %1 %+ mm_sad, %1 %+ mm_sum, %1 %+ mm_sqsum, p_cur + 1 * i_stride,  p_ref + 1 * i_stride,  %1 %+ mm_zero, %1 %+ mm_tmp0, %1 %+ mm_tmp1, 1
    AVX2_SadSumSqsum %1 %+ mm_sad, %1 %+ mm_sum, %1 %+ mm_sqsum, p_cur + 2 * i_stride,  p_ref + 2 * i_stride,  %1 %+ mm_zero, %1 %+ mm_tmp0, %1 %+ mm_tmp1, 1
    AVX2_SadSumSqsum %1 %+ mm_sad, %1 %+ mm_sum, %1 %+ mm_sqsum, p_cur + 1 * i_stride3, p_ref + 1 * i_stride3, %1 %+ mm_zero, %1 %+ mm_tmp0, %1 %+ mm_tmp1, 1
    AVX2_SadSumSqsum %1 %+ mm_sad, %1 %+ mm_sum, %1 %+ mm_sqsum, p_cur + 4 * i_stride,  p_ref + 4 * i_stride,  %1 %+ mm_zero, %1 %+ mm_tmp0, %1 %+ mm_tmp1, 1
    AVX2_SadSumSqsum %1 %+ mm_sad, %1 %+ mm_sum, %1 %+ mm_sqsum, p_cur + r_tmp,         p_ref + r_tmp,         %1 %+ mm_zero, %1 %+ mm_tmp0, %1 %+ mm_tmp1, 1
    lea            r_tmp, [i_stride + 2 * i_stride3]
    AVX2_SadSumSqsum %1 %+ mm_sad, %1 %+ mm_sum, %1 %+ mm_sqsum, p_cur + 2 * i_stride3, p_ref + 2 * i_stride3, %1 %+ mm_zero, %1 %+ mm_tmp0, %1 %+ mm_tmp1, 1
    AVX2_SadSumSqsum %1 %+ mm_sad, %1 %+ mm_sum, %1 %+ mm_sqsum, p_cur + r_tmp,         p_ref + r_tmp,         %1 %+ mm_zero, %1 %+ mm_tmp0, %1 %+ mm_tmp1, 1
    ; Increment addresses for the next iteration. Doing this early is beneficial on Haswell.
    add            p_cur, %1 %+ mm_width
    add            p_ref, %1 %+ mm_width
%else
    vpxor          x %+ mm_sad, x %+ mm_sad, x %+ mm_sad
    vpxor          x %+ mm_sum, x %+ mm_sum, x %+ mm_sum
    vpxor          x %+ mm_sqsum, x %+ mm_sqsum, x %+ mm_sqsum
    lea            r_tmp, [8 * i_stride]
    add            p_cur, r_tmp
    add            p_ref, r_tmp
    neg            r_tmp
%%loop:
    AVX2_SadSumSqsum %1 %+ mm_sad, %1 %+ mm_sum, %1 %+ mm_sqsum, p_cur + r_tmp, p_ref + r_tmp, %1 %+ mm_zero, %1 %+ mm_tmp0, %1 %+ mm_tmp1, 1
    add            r_tmp, i_stride
    jl             %%loop
    ; Increment addresses for the next iteration. Doing this early is beneficial on Haswell.
    lea            r_tmp, [8 * i_stride - %1 %+ mm_width]
    sub            p_cur, r_tmp
    sub            p_ref, r_tmp
%endif
    AVX2_Store8x8Accdw p_sad8x8 + 4 * i_xcnt, %1, mm_sad, mm_tmp1, b_second_blocks
    vpaddd         y %+ mm_sadframe, y %+ mm_sadframe, y %+ mm_sad
    vpunpcklqdq    %1 %+ mm_tmp0, %1 %+ mm_sum, %1 %+ mm_sqsum
    vpunpckhqdq    %1 %+ mm_tmp1, %1 %+ mm_sum, %1 %+ mm_sqsum
    vpaddd         %1 %+ mm_tmp0, %1 %+ mm_tmp0, %1 %+ mm_tmp1
    vpshufd        %1 %+ mm_tmp1, %1 %+ mm_tmp0, 10110001b
    vpaddd         %1 %+ mm_tmp0, %1 %+ mm_tmp0, %1 %+ mm_tmp1
    AVX2_Store2x16x16Accdw p_sum16x16, p_sqsum16x16, i_xcnt, r_tmp, %1, mm_tmp0, mm_tmp1, b_second_blocks
%undef mm_tmp0
%undef mm_tmp1
%undef mm_sad
%undef mm_sum
%undef mm_sqsum
%undef b_second_blocks
%endmacro

; x/y-mm_prefix=%1 mm_clobber=%2,%3,%4,%5,%6,%7,%8 b_second_blocks=%9
%macro AVX2_CalcSadSsd_8Lines 9
%define mm_tmp0    %2
%define mm_tmp1    %3
%define mm_tmp2    %4
%define mm_sad     %5
%define mm_sum     %6
%define mm_sqsum   %7
%define mm_sqdiff  %8
%define b_second_blocks %9
    ; Unroll for better performance on Haswell.
    ; Avoid unrolling for the 16 px case so as to reduce the code footprint.
%ifidni %1, y
%ifdef i_stride5
    lea            r_tmp, [i_stride + 2 * i_stride3]
    %define i_stride5_ i_stride5
%else
    lea            r_tmp, [5 * i_stride]
    %define i_stride5_ r_tmp
%endif
    AVX2_SadSumSqsumSqdiff %1 %+ mm_sad, %1 %+ mm_sum, %1 %+ mm_sqsum, %1 %+ mm_sqdiff, p_cur,                  p_ref,                  %1 %+ mm_zero, %1 %+ mm_tmp0, %1 %+ mm_tmp1, %1 %+ mm_tmp2, 0
    AVX2_SadSumSqsumSqdiff %1 %+ mm_sad, %1 %+ mm_sum, %1 %+ mm_sqsum, %1 %+ mm_sqdiff, p_cur + 1 * i_stride,   p_ref + 1 * i_stride,   %1 %+ mm_zero, %1 %+ mm_tmp0, %1 %+ mm_tmp1, %1 %+ mm_tmp2, 1
    AVX2_SadSumSqsumSqdiff %1 %+ mm_sad, %1 %+ mm_sum, %1 %+ mm_sqsum, %1 %+ mm_sqdiff, p_cur + 2 * i_stride,   p_ref + 2 * i_stride,   %1 %+ mm_zero, %1 %+ mm_tmp0, %1 %+ mm_tmp1, %1 %+ mm_tmp2, 1
    AVX2_SadSumSqsumSqdiff %1 %+ mm_sad, %1 %+ mm_sum, %1 %+ mm_sqsum, %1 %+ mm_sqdiff, p_cur + 1 * i_stride3,  p_ref + 1 * i_stride3,  %1 %+ mm_zero, %1 %+ mm_tmp0, %1 %+ mm_tmp1, %1 %+ mm_tmp2, 1
    AVX2_SadSumSqsumSqdiff %1 %+ mm_sad, %1 %+ mm_sum, %1 %+ mm_sqsum, %1 %+ mm_sqdiff, p_cur + 4 * i_stride,   p_ref + 4 * i_stride,   %1 %+ mm_zero, %1 %+ mm_tmp0, %1 %+ mm_tmp1, %1 %+ mm_tmp2, 1
    AVX2_SadSumSqsumSqdiff %1 %+ mm_sad, %1 %+ mm_sum, %1 %+ mm_sqsum, %1 %+ mm_sqdiff, p_cur + 1 * i_stride5_, p_ref + 1 * i_stride5_, %1 %+ mm_zero, %1 %+ mm_tmp0, %1 %+ mm_tmp1, %1 %+ mm_tmp2, 1
%ifndef i_stride5
    lea            r_tmp, [i_stride + 2 * i_stride3]
%endif
%undef i_stride5_
    AVX2_SadSumSqsumSqdiff %1 %+ mm_sad, %1 %+ mm_sum, %1 %+ mm_sqsum, %1 %+ mm_sqdiff, p_cur + 2 * i_stride3,  p_ref + 2 * i_stride3,  %1 %+ mm_zero, %1 %+ mm_tmp0, %1 %+ mm_tmp1, %1 %+ mm_tmp2, 1
    AVX2_SadSumSqsumSqdiff %1 %+ mm_sad, %1 %+ mm_sum, %1 %+ mm_sqsum, %1 %+ mm_sqdiff, p_cur + r_tmp,          p_ref + r_tmp,          %1 %+ mm_zero, %1 %+ mm_tmp0, %1 %+ mm_tmp1, %1 %+ mm_tmp2, 1
    ; Increment addresses for the next iteration. Doing this early is beneficial on Haswell.
    add            p_cur, %1 %+ mm_width
    add            p_ref, %1 %+ mm_width
%else
    vpxor          x %+ mm_sad, x %+ mm_sad, x %+ mm_sad
    vpxor          x %+ mm_sum, x %+ mm_sum, x %+ mm_sum
    vpxor          x %+ mm_sqsum, x %+ mm_sqsum, x %+ mm_sqsum
    vpxor          x %+ mm_sqdiff, x %+ mm_sqdiff, x %+ mm_sqdiff
    lea            r_tmp, [8 * i_stride]
    add            p_cur, r_tmp
    add            p_ref, r_tmp
    neg            r_tmp
%%loop:
    AVX2_SadSumSqsumSqdiff %1 %+ mm_sad, %1 %+ mm_sum, %1 %+ mm_sqsum, %1 %+ mm_sqdiff, p_cur + r_tmp, p_ref + r_tmp, %1 %+ mm_zero, %1 %+ mm_tmp0, %1 %+ mm_tmp1, %1 %+ mm_tmp2, 1
    add            r_tmp, i_stride
    jl             %%loop
    ; Increment addresses for the next iteration. Doing this early is beneficial on Haswell.
    lea            r_tmp, [8 * i_stride - %1 %+ mm_width]
    sub            p_cur, r_tmp
    sub            p_ref, r_tmp
%endif
    mov            r_tmp, p_sad8x8
    AVX2_Store8x8Accdw r_tmp + 4 * i_xcnt, %1, mm_sad, mm_tmp1, b_second_blocks
    vpaddd         sadframe_acc, sadframe_acc, y %+ mm_sad
    mov            r_tmp, i_xcnt
    add            r_tmp, p_sum16x16
    vpunpckhqdq    %1 %+ mm_tmp1, %1 %+ mm_sum, %1 %+ mm_sum
    vpaddd         %1 %+ mm_tmp0, %1 %+ mm_sum, %1 %+ mm_tmp1
    AVX2_Store16x16Accdw r_tmp, %1, mm_tmp0, mm_tmp1, b_second_blocks
    vpunpcklqdq    %1 %+ mm_tmp0, %1 %+ mm_sqsum, %1 %+ mm_sqdiff
    vpunpckhqdq    %1 %+ mm_tmp1, %1 %+ mm_sqsum, %1 %+ mm_sqdiff
    vpaddd         %1 %+ mm_tmp0, %1 %+ mm_tmp0, %1 %+ mm_tmp1
    vpshufd        %1 %+ mm_tmp1, %1 %+ mm_tmp0, 10110001b
    vpaddd         %1 %+ mm_tmp0, %1 %+ mm_tmp0, %1 %+ mm_tmp1
    AVX2_Store2x16x16Accdw p_sqsum16x16, p_sqdiff16x16, i_xcnt, r_tmp, %1, mm_tmp0, mm_tmp1, b_second_blocks
%undef mm_tmp0
%undef mm_tmp1
%undef mm_tmp2
%undef mm_sad
%undef mm_sum
%undef mm_sqsum
%undef mm_sqdiff
%undef b_second_blocks
%endmacro

; x/y-mm_prefix=%1 mm_clobber=%2,%3,%4,%5,%6,%7,%8 b_second_blocks=%9
%macro AVX2_CalcSadBgd_8Lines 9
%define mm_tmp0    %2
%define mm_tmp1    %3
%define mm_tmp2    %8
%define mm_mad     %4
%define mm_sumcur  %5
%define mm_sumref  %6
%define mm_sad     %7
%define b_second_blocks %9
    ; Unroll for better performance on Haswell.
    ; Avoid unrolling for the 16 px case so as to reduce the code footprint.
%ifidni %1, y
    lea            r_tmp, [5 * i_stride]
    AVX2_SadSdMad  %1 %+ mm_sad, %1 %+ mm_sumcur, %1 %+ mm_sumref, %1 %+ mm_mad, p_cur,                 p_ref,                 %1 %+ mm_zero, %1 %+ mm_tmp0, %1 %+ mm_tmp1, %1 %+ mm_tmp2, 0
    AVX2_SadSdMad  %1 %+ mm_sad, %1 %+ mm_sumcur, %1 %+ mm_sumref, %1 %+ mm_mad, p_cur + 1 * i_stride,  p_ref + 1 * i_stride,  %1 %+ mm_zero, %1 %+ mm_tmp0, %1 %+ mm_tmp1, %1 %+ mm_tmp2, 1
    AVX2_SadSdMad  %1 %+ mm_sad, %1 %+ mm_sumcur, %1 %+ mm_sumref, %1 %+ mm_mad, p_cur + 2 * i_stride,  p_ref + 2 * i_stride,  %1 %+ mm_zero, %1 %+ mm_tmp0, %1 %+ mm_tmp1, %1 %+ mm_tmp2, 1
    AVX2_SadSdMad  %1 %+ mm_sad, %1 %+ mm_sumcur, %1 %+ mm_sumref, %1 %+ mm_mad, p_cur + 1 * i_stride3, p_ref + 1 * i_stride3, %1 %+ mm_zero, %1 %+ mm_tmp0, %1 %+ mm_tmp1, %1 %+ mm_tmp2, 1
    AVX2_SadSdMad  %1 %+ mm_sad, %1 %+ mm_sumcur, %1 %+ mm_sumref, %1 %+ mm_mad, p_cur + 4 * i_stride,  p_ref + 4 * i_stride,  %1 %+ mm_zero, %1 %+ mm_tmp0, %1 %+ mm_tmp1, %1 %+ mm_tmp2, 1
    AVX2_SadSdMad  %1 %+ mm_sad, %1 %+ mm_sumcur, %1 %+ mm_sumref, %1 %+ mm_mad, p_cur + r_tmp,         p_ref + r_tmp,         %1 %+ mm_zero, %1 %+ mm_tmp0, %1 %+ mm_tmp1, %1 %+ mm_tmp2, 1
    lea            r_tmp, [i_stride + 2 * i_stride3]
    AVX2_SadSdMad  %1 %+ mm_sad, %1 %+ mm_sumcur, %1 %+ mm_sumref, %1 %+ mm_mad, p_cur + 2 * i_stride3, p_ref + 2 * i_stride3, %1 %+ mm_zero, %1 %+ mm_tmp0, %1 %+ mm_tmp1, %1 %+ mm_tmp2, 1
    AVX2_SadSdMad  %1 %+ mm_sad, %1 %+ mm_sumcur, %1 %+ mm_sumref, %1 %+ mm_mad, p_cur + r_tmp,         p_ref + r_tmp,         %1 %+ mm_zero, %1 %+ mm_tmp0, %1 %+ mm_tmp1, %1 %+ mm_tmp2, 1
    ; Increment addresses for the next iteration. Doing this early is beneficial on Haswell.
    add            p_cur, %1 %+ mm_width
    add            p_ref, %1 %+ mm_width
%else
    vpxor          x %+ mm_sad, x %+ mm_sad, x %+ mm_sad
    vpxor          x %+ mm_sumcur, x %+ mm_sumcur, x %+ mm_sumcur
    vpxor          x %+ mm_sumref, x %+ mm_sumref, x %+ mm_sumref
    vpxor          x %+ mm_mad, x %+ mm_mad, x %+ mm_mad
    lea            r_tmp, [8 * i_stride]
    add            p_cur, r_tmp
    add            p_ref, r_tmp
    neg            r_tmp
%%loop:
    AVX2_SadSdMad  %1 %+ mm_sad, %1 %+ mm_sumcur, %1 %+ mm_sumref, %1 %+ mm_mad, p_cur + r_tmp, p_ref + r_tmp, %1 %+ mm_zero, %1 %+ mm_tmp0, %1 %+ mm_tmp1, %1 %+ mm_tmp2, 1
    add            r_tmp, i_stride
    jl             %%loop
    ; Increment addresses for the next iteration. Doing this early is beneficial on Haswell.
    lea            r_tmp, [8 * i_stride - %1 %+ mm_width]
    sub            p_cur, r_tmp
    sub            p_ref, r_tmp
%endif
    mov            r_tmp, p_sad8x8
    AVX2_Store8x8Accdw r_tmp + 4 * i_xcnt, %1, mm_sad, mm_tmp1, b_second_blocks
    vpaddd         sadframe_acc, sadframe_acc, y %+ mm_sad
    mov            r_tmp, p_sd8x8
    vpsubd         %1 %+ mm_tmp0, %1 %+ mm_sumcur, %1 %+ mm_sumref
    AVX2_Store8x8Accdw r_tmp + 4 * i_xcnt, %1, mm_tmp0, mm_tmp1, b_second_blocks
    ; Coalesce store and horizontal reduction of MAD accumulator for even and
    ; odd iterations so as to enable more parallelism.
%ifidni %1, y
    test           i_xcnt, 32 / xcnt_unit
    jz             %%preserve_mad
    mov            r_tmp, p_mad8x8
    AVX2_Maxubq2   y %+ mm_mad, y %+ mm_mad, prev_mad, y %+ mm_tmp0
    AVX2_Store2x8x8Accb r_tmp + i_xcnt - 8, mm_mad, mm_tmp0, mm_tmp1, b_second_blocks
%%preserve_mad:
    vmovdqa        prev_mad, y %+ mm_mad
%else
    mov            r_tmp, p_mad8x8
    AVX2_Maxubq    %1 %+ mm_mad, %1 %+ mm_mad, %1 %+ mm_tmp0
    AVX2_Store8x8Accb r_tmp + i_xcnt, %1, mm_mad, mm_tmp0, b_second_blocks
%endif
%undef mm_tmp0
%undef mm_tmp1
%undef mm_tmp2
%undef mm_mad
%undef mm_sumcur
%undef mm_sumref
%undef mm_sad
%undef b_second_blocks
%endmacro

; Store remaining MAD accumulator for width & 32 cases.
; width/xcnt_unit=%1 mm_tmp=%2,%3 b_second_blocks=%4
%macro AVX2_StoreRemainingSingleMad 4
    test           %1, 32 / xcnt_unit
    jz             %%skip
    mov            r_tmp, p_mad8x8
    vmovdqa        y%2, prev_mad
    AVX2_Maxubq    y%2, y%2, y%3
    AVX2_Store8x8Accb r_tmp + i_xcnt - 8, y, %2, %3, %4
%%skip:
%endmacro

; x/y-mm_prefix=%1 mm_clobber=%2,%3,%4,%5,%6,%7,%8,%9,%10 b_second_blocks=%11
%macro AVX2_CalcSadSsdBgd_8Lines 11
%define mm_tmp0    %2
%define mm_tmp1    %3
%define mm_sad     %4
%define mm_sum     %5
%define mm_sumref  %6
%define mm_mad     %7
%define mm_sqsum   %8
%define mm_sqdiff  %9
%ifidn %10, 0
%define tmp2       0
%else
%define tmp2       %1 %+ %10
%endif
%define b_second_blocks %11
    ; Unroll for better performance on Haswell.
    ; Avoid unrolling for the 16 px case so as to reduce the code footprint.
%ifidni %1, y
    lea            r_tmp, [5 * i_stride]
    AVX2_SadBgdSqdiff %1 %+ mm_sad, %1 %+ mm_sum, %1 %+ mm_sumref, %1 %+ mm_mad, %1 %+ mm_sqdiff, %1 %+ mm_sqsum, p_cur,                 p_ref,                 %1 %+ mm_zero, %1 %+ mm_tmp0, %1 %+ mm_tmp1, tmp2, 0
    AVX2_SadBgdSqdiff %1 %+ mm_sad, %1 %+ mm_sum, %1 %+ mm_sumref, %1 %+ mm_mad, %1 %+ mm_sqdiff, %1 %+ mm_sqsum, p_cur + 1 * i_stride,  p_ref + 1 * i_stride,  %1 %+ mm_zero, %1 %+ mm_tmp0, %1 %+ mm_tmp1, tmp2, 1
    AVX2_SadBgdSqdiff %1 %+ mm_sad, %1 %+ mm_sum, %1 %+ mm_sumref, %1 %+ mm_mad, %1 %+ mm_sqdiff, %1 %+ mm_sqsum, p_cur + 2 * i_stride,  p_ref + 2 * i_stride,  %1 %+ mm_zero, %1 %+ mm_tmp0, %1 %+ mm_tmp1, tmp2, 1
    AVX2_SadBgdSqdiff %1 %+ mm_sad, %1 %+ mm_sum, %1 %+ mm_sumref, %1 %+ mm_mad, %1 %+ mm_sqdiff, %1 %+ mm_sqsum, p_cur + 1 * i_stride3, p_ref + 1 * i_stride3, %1 %+ mm_zero, %1 %+ mm_tmp0, %1 %+ mm_tmp1, tmp2, 1
    AVX2_SadBgdSqdiff %1 %+ mm_sad, %1 %+ mm_sum, %1 %+ mm_sumref, %1 %+ mm_mad, %1 %+ mm_sqdiff, %1 %+ mm_sqsum, p_cur + 4 * i_stride,  p_ref + 4 * i_stride,  %1 %+ mm_zero, %1 %+ mm_tmp0, %1 %+ mm_tmp1, tmp2, 1
    AVX2_SadBgdSqdiff %1 %+ mm_sad, %1 %+ mm_sum, %1 %+ mm_sumref, %1 %+ mm_mad, %1 %+ mm_sqdiff, %1 %+ mm_sqsum, p_cur + r_tmp,         p_ref + r_tmp,         %1 %+ mm_zero, %1 %+ mm_tmp0, %1 %+ mm_tmp1, tmp2, 1
    lea            r_tmp, [i_stride + 2 * i_stride3]
    AVX2_SadBgdSqdiff %1 %+ mm_sad, %1 %+ mm_sum, %1 %+ mm_sumref, %1 %+ mm_mad, %1 %+ mm_sqdiff, %1 %+ mm_sqsum, p_cur + 2 * i_stride3, p_ref + 2 * i_stride3, %1 %+ mm_zero, %1 %+ mm_tmp0, %1 %+ mm_tmp1, tmp2, 1
    AVX2_SadBgdSqdiff %1 %+ mm_sad, %1 %+ mm_sum, %1 %+ mm_sumref, %1 %+ mm_mad, %1 %+ mm_sqdiff, %1 %+ mm_sqsum, p_cur + r_tmp,         p_ref + r_tmp,         %1 %+ mm_zero, %1 %+ mm_tmp0, %1 %+ mm_tmp1, tmp2, 1
    ; Increment addresses for the next iteration. Doing this early is beneficial on Haswell.
    add            p_cur, %1 %+ mm_width
    add            p_ref, %1 %+ mm_width
%else
    vpxor          x %+ mm_sad, x %+ mm_sad, x %+ mm_sad
    vpxor          x %+ mm_sum, x %+ mm_sum, x %+ mm_sum
    vpxor          x %+ mm_sumref, x %+ mm_sumref, x %+ mm_sumref
    vpxor          x %+ mm_mad, x %+ mm_mad, x %+ mm_mad
    vpxor          x %+ mm_sqsum, x %+ mm_sqsum, x %+ mm_sqsum
    vpxor          x %+ mm_sqdiff, x %+ mm_sqdiff, x %+ mm_sqdiff
    lea            r_tmp, [8 * i_stride]
    add            p_cur, r_tmp
    add            p_ref, r_tmp
    neg            r_tmp
%%loop:
    AVX2_SadBgdSqdiff %1 %+ mm_sad, %1 %+ mm_sum, %1 %+ mm_sumref, %1 %+ mm_mad, %1 %+ mm_sqdiff, %1 %+ mm_sqsum, p_cur + r_tmp, p_ref + r_tmp, %1 %+ mm_zero, %1 %+ mm_tmp0, %1 %+ mm_tmp1, tmp2, 1
    add            r_tmp, i_stride
    jl             %%loop
    ; Increment addresses for the next iteration. Doing this early is beneficial on Haswell.
    lea            r_tmp, [8 * i_stride - %1 %+ mm_width]
    sub            p_cur, r_tmp
    sub            p_ref, r_tmp
%endif
    mov            r_tmp, p_sad8x8
    AVX2_Store8x8Accdw r_tmp + 4 * i_xcnt, %1, mm_sad, mm_tmp1, b_second_blocks
    vpaddd         sadframe_acc, sadframe_acc, y %+ mm_sad
    mov            r_tmp, i_xcnt
    add            r_tmp, p_sum16x16
    vpunpckhqdq    %1 %+ mm_tmp1, %1 %+ mm_sum, %1 %+ mm_sum
    vpaddd         %1 %+ mm_tmp0, %1 %+ mm_sum, %1 %+ mm_tmp1
    AVX2_Store16x16Accdw r_tmp, %1, mm_tmp0, mm_tmp1, b_second_blocks
    mov            r_tmp, p_sd8x8
    vpsubd         %1 %+ mm_sum,  %1 %+ mm_sum, %1 %+ mm_sumref
    AVX2_Store8x8Accdw r_tmp + 4 * i_xcnt, %1, mm_sum, mm_tmp0, b_second_blocks
    ; Coalesce store and horizontal reduction of MAD accumulator for even and
    ; odd iterations so as to enable more parallelism.
%ifidni %1, y
    test           i_xcnt, 32 / xcnt_unit
    jz             %%preserve_mad
    mov            r_tmp, p_mad8x8
    AVX2_Maxubq2   y %+ mm_mad, y %+ mm_mad, prev_mad, y %+ mm_tmp0
    AVX2_Store2x8x8Accb r_tmp + i_xcnt - 8, mm_mad, mm_tmp0, mm_tmp1, b_second_blocks
%%preserve_mad:
    vmovdqa        prev_mad, y %+ mm_mad
%else
    mov            r_tmp, p_mad8x8
    AVX2_Maxubq    %1 %+ mm_mad, %1 %+ mm_mad, %1 %+ mm_tmp0
    AVX2_Store8x8Accb r_tmp + i_xcnt, %1, mm_mad, mm_tmp0, b_second_blocks
%endif
    vpunpcklqdq    %1 %+ mm_tmp0, %1 %+ mm_sqsum, %1 %+ mm_sqdiff
    vpunpckhqdq    %1 %+ mm_tmp1, %1 %+ mm_sqsum, %1 %+ mm_sqdiff
    vpaddd         %1 %+ mm_tmp0, %1 %+ mm_tmp0,  %1 %+ mm_tmp1
    vpshufd        %1 %+ mm_tmp1, %1 %+ mm_tmp0,  10110001b
    vpaddd         %1 %+ mm_tmp0, %1 %+ mm_tmp0,  %1 %+ mm_tmp1
    AVX2_Store2x16x16Accdw p_sqsum16x16, p_sqdiff16x16, i_xcnt, r_tmp, %1, mm_tmp0, mm_tmp1, b_second_blocks
%undef mm_tmp0
%undef mm_tmp1
%undef mm_sqsum
%undef mm_sqdiff
%undef mm_mad
%undef mm_sum
%undef mm_sumref
%undef mm_sad
%undef tmp2
%undef b_second_blocks
%endmacro

