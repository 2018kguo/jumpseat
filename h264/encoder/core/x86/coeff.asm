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
;*  memzero.asm
;*
;*  Abstract
;*     cavlc
;*
;*  History
;*      09/08/2010 Created
;*
;*
;*************************************************************************/

%include "asm_inc.asm"

SECTION .rodata align=16

align 16

wels_shufb_rev:
    db 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0

; 4-bit table giving number of preceding zeros for each set bit as well as the
; eventual next bit. For the case where all 4 bits are set, this requires 5
; zeros. The 5th zero can either be read from beyond the final table entry or
; implied via zero-initializing the location being read into.
wels_cavlc_param_cal_run_lut:
    db 4, 0, 0, 0
    db 0, 3, 0, 0
    db 1, 2, 0, 0
    db 0, 0, 2, 0
    db 2, 1, 0, 0
    db 0, 1, 1, 0
    db 1, 0, 1, 0
    db 0, 0, 0, 1
    db 3, 0, 0, 0
    db 0, 2, 0, 0
    db 1, 1, 0, 0
    db 0, 0, 1, 0
    db 2, 0, 0, 0
    db 0, 1, 0, 0
    db 1, 0, 0, 0
    db 0, 0, 0, 0
;   db 0
; 4-bit table giving pshufb vectors for compacting 4-word vectors by removing
; the words that match zero bits and concatenating in reverse order.
wels_cavlc_param_cal_shufb_lut:
    db 0, 0, 0, 0, 0, 0, 0, 0
    db 6, 7, 0, 0, 0, 0, 0, 0
    db 4, 5, 0, 0, 0, 0, 0, 0
    db 6, 7, 4, 5, 0, 0, 0, 0
    db 2, 3, 0, 0, 0, 0, 0, 0
    db 6, 7, 2, 3, 0, 0, 0, 0
    db 4, 5, 2, 3, 0, 0, 0, 0
    db 6, 7, 4, 5, 2, 3, 0, 0
    db 0, 1, 0, 0, 0, 0, 0, 0
    db 6, 7, 0, 1, 0, 0, 0, 0
    db 4, 5, 0, 1, 0, 0, 0, 0
    db 6, 7, 4, 5, 0, 1, 0, 0
    db 2, 3, 0, 1, 0, 0, 0, 0
    db 6, 7, 2, 3, 0, 1, 0, 0
    db 4, 5, 2, 3, 0, 1, 0, 0
    db 6, 7, 4, 5, 2, 3, 0, 1



;***********************************************************************
; Code
;***********************************************************************
SECTION .text



;***********************************************************************
;int32_t CavlcParamCal_sse42(int16_t*coffLevel, uint8_t* run, int16_t *Level, int32_t* total_coeffs , int32_t endIdx);
;***********************************************************************

WELS_EXTERN CavlcParamCal_sse42
%define i_endidxd      dword arg5d

    %assign push_num 0
    %define p_coeff_level r0
    %define p_run r1
    %define p_level r2
    %define p_total_coeffs r3
    %define r_mask  rax
    %define r_maskd eax
    %define p_shufb_lut r5
    %define i_total_zeros r6
    %define p_run_lut (p_shufb_lut + (wels_cavlc_param_cal_run_lut - wels_cavlc_param_cal_shufb_lut))
    lea             p_shufb_lut, [wels_cavlc_param_cal_shufb_lut]
    INIT_X86_32_PIC_NOPRESERVE r0

    ; Acquire a bitmask indicating which words are non-zero.
    ; Assume p_coeff_level is 16-byte-aligned and at least 32 bytes if endIdx > 3.
    ; Otherwise, assume 8 bytes available. Assume that input beyond endIdx is zero.
    ; Assumptions are taken from previous implementations.
    pxor            xmm1, xmm1
    cmp             i_endidxd, 3
    jg              .load16
    movq            xmm0, [p_coeff_level]
    packsswb        xmm0, xmm1
    jmp             .load_done
.load16:
    movdqa          xmm0, [p_coeff_level]
    packsswb        xmm0, [p_coeff_level + 16]
.load_done:
    movdqa          [p_run], xmm1                           ; Zero-initialize because we may read back implied zeros.
    pcmpeqb         xmm0, xmm1
    pshufb          xmm0, [pic(wels_shufb_rev)]
    pmovmskb        r_maskd, xmm0
    xor             r_maskd, 0FFFFh
%undef i_endidxd
%define r_tmp2  r4
%define r_tmp2d r4d
    popcnt          r_tmp2d, r_maskd
    mov             [p_total_coeffs], r_tmp2d
    ; Recycle p_total_coeffs.
%ifidni p_total_coeffs, rcx
    %define r_tmp rcx
    %define r_tmpd ecx
    %define r_tmpb cl
%else
    %xdefine i_total_zeros p_total_coeffs
%endif
%undef p_total_coeffs
    mov             i_total_zeros, r_tmp2
    jz              .done
    bsf             r_tmpd, r_maskd                         ; Find first set bit.
    lea             r_tmp2, [r_tmp2 + r_tmp - 16]
    neg             r_tmp2
    mov             i_total_zeros, r_tmp2
    ; Skip trailing zeros.
    ; Restrict to multiples of 4 to retain alignment and avoid out-of-bound stores.
    and             r_tmpd, -4
    shr             r_maskd, r_tmpb
    add             r_tmpd, r_tmpd
    sub             p_coeff_level, r_tmp
    ; Handle first quadruple containing a non-zero value.
    mov             r_tmp, r_mask
    and             r_tmpd, 0Fh
    movq            xmm0, [p_coeff_level + 24]
    movq            xmm1, [p_shufb_lut + 8 * r_tmp]
    pshufb          xmm0, xmm1
    mov             r_tmp2d, [p_run_lut + 4 * r_tmp]
    shr             r_tmp2d, 8                              ; Skip initial zero run.
    movlps          [p_level], xmm0                         ; Store levels for the first quadruple.
    mov             [p_run], r_tmp2d                        ; Store accompanying zero runs thus far.
    shr             r_maskd, 4
    jz              .done
.loop:
    ; Increment pointers.
    popcnt          r_tmpd, r_tmpd                          ; Number of non-zero values handled.
    lea             p_level, [p_level + 2 * r_tmp]
    add             p_run, r_tmp
    ; Handle next quadruple.
    mov             r_tmp, r_mask
    and             r_tmpd, 0Fh
    movq            xmm0, [p_coeff_level + 16]
    sub             p_coeff_level, 8
    movq            xmm1, [p_shufb_lut + 8 * r_tmp]
    pshufb          xmm0, xmm1
    movzx           r_tmp2d, byte [p_run - 1]
    add             r_tmp2d, [p_run_lut + 4 * r_tmp]        ; Add to previous run and get eventual new runs.
    movlps          [p_level], xmm0                         ; Store levels (potentially none).
    mov             [p_run - 1], r_tmp2d                    ; Update previous run and store eventual new runs.
    shr             r_maskd, 4
    jnz             .loop
.done:
%ifnidni retrq, i_total_zeros
    mov             retrq, i_total_zeros
%endif
    DEINIT_X86_32_PIC
    ret
%undef p_coeff_level
%undef p_run
%undef p_level
%undef i_total_zeros
%undef r_mask
%undef r_maskd
%undef r_tmp
%undef r_tmpd
%undef r_tmpb
%undef r_tmp2
%undef r_tmp2d
%undef p_shufb_lut
%undef p_run_lut
