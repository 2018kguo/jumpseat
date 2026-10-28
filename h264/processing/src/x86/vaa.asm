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

; Deallocate stack space allocated with STACK_ALLOC.

; Max unsigned byte per quadword
; out=%1 in=%2 tmp=%3

; Max unsigned byte per quadword. 2 register input.
; Results interleaved as least significant byte of even/odd doublewords.
; out=%1 in_a=%2 in_b=%3 tmp=%4

; res=%1 src=%2 zero=%3 tmp=%4 add_to_res=%5

; res=%1 src=%2 zero=%3 tmp=%4 add_to_res=%5

; res=%1 a=%2 b=%3 a=%4 tmp=%5

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

; sad=%1 pCur=%2 pRef=%3 tmp=%4 accumulate_results=%5
%macro AVX2_Sad 5
    vmovdqu        %4, [%2]
    AVX2_Sadbdw    %1, %4, [%3], %4, %5
%endmacro

; sad=%1 sum_cur=%2 sqsum_cur=%3 pCur=%4 pRef=%5 zero=%6 tmp=%7,%8 accumulate_results=%9

; sad=%1 sum_cur=%2 sqsum_cur=%3 sqdiff=%4 pCur=%5 pRef=%6 zero=%7 tmp=%8,%9,%10 accumulate_results=%11

; sad=%1 sum_cur=%2 sum_ref=%3 mad=%4 pCur=%5 pRef=%6 zero=%7 tmp=%8,%9,%10 accumulate_results=%11

; sad=%1 sum_cur=%2 sum_ref=%3 mad=%4 sqdiff=%5 sqsum_cur=%6 pCur=%7 pRef=%8 zero=%9 tmp=%10,%11,%12 accumulate_results=%13

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

; p_dst=%1 data=%2 tmp=%3,%4 second_blocks=%5

; p_dst=%1 mmreg_prefix=%2 data=%3 tmp=%4 add_to_dst=%5

; p_dst1=%1 p_dst2=%2 i_dst_offset=%3 gpr_tmp=%4 mmreg_prefix=%5 data=%6 mm_tmp=%7 add_to_dst=%8

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

; x/y-mm_prefix=%1 mm_clobber=%2,%3,%4,%5,%6,%7,%8 b_second_blocks=%9

; x/y-mm_prefix=%1 mm_clobber=%2,%3,%4,%5,%6,%7,%8 b_second_blocks=%9

; Store remaining MAD accumulator for width & 32 cases.
; width/xcnt_unit=%1 mm_tmp=%2,%3 b_second_blocks=%4

; x/y-mm_prefix=%1 mm_clobber=%2,%3,%4,%5,%6,%7,%8,%9,%10 b_second_blocks=%11

