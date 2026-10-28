;*!
;* \copy
;*     Copyright (c)  2009-2013, Cisco Systems
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
;*  sse2inc.asm
;*
;*  Abstract
;*      macro and constant
;*
;*  History
;*      8/5/2009 Created
;*
;*
;*************************************************************************/
;***********************************************************************
; Options, for DEBUG
;***********************************************************************

%if 1
    %define MOVDQ movdqa
%else
    %define MOVDQ movdqu
%endif

%if 1
    %define WELSEMMS emms
%else
    %define WELSEMMS
%endif

;***********************************************************************
; Macros
;***********************************************************************

DEFAULT REL

BITS 64

%ifidn __OUTPUT_FORMAT__,elf64
SECTION .note.GNU-stack noalloc noexec nowrite progbits ; Mark the stack as non-executable
%endif

%define arg1 rdi
%define arg2 rsi
%define arg3 rdx
%define arg4 rcx
%define arg5 r8
%define arg6 r9
%define arg7 [rsp + push_num*8 + 8]
%define arg8 [rsp + push_num*8 + 16]
%define arg9 [rsp + push_num*8 + 24]
%define arg10 [rsp + push_num*8 + 32]
%define arg11 [rsp + push_num*8 + 40]
%define arg12 [rsp + push_num*8 + 48]

%define arg1d edi
%define arg2d esi
%define arg3d edx
%define arg4d ecx
%define arg5d r8d
%define arg6d r9d
%define arg7d arg7
%define arg8d arg8
%define arg9d arg9
%define arg10d arg10
%define arg11d arg11
%define arg12d arg12

%define r0 rdi
%define r1 rsi
%define r2 rdx
%define r3 rcx
%define r4 r8
%define r5 r9
%define r6 r10
%define r7 rsp

%define r0d edi
%define r1d esi
%define r2d edx
%define r3d ecx
%define r4d r8d
%define r5d r9d
%define r6d r10d

%define r0w  di
%define r1w  si
%define r2w  dx
%define r3w  cx
%define r4w  r8w
%define r6w  r10w

%define r0b  dil
%define r1b  sil
%define r2b  dl
%define r3b  cl

%define  PUSHRFLAGS     pushfq
%define  POPRFLAGS      popfq
%define  retrq          rax
%define  retrd          eax

%macro LOAD_1_PARA 0
%endmacro

%macro LOAD_2_PARA 0
%endmacro

%macro LOAD_3_PARA 0
%endmacro

%macro LOAD_4_PARA 0
%endmacro

%macro LOAD_5_PARA 0
%endmacro

%macro LOAD_6_PARA 0
%endmacro

%macro LOAD_7_PARA 0
        mov r6, [rsp + push_num*8 + 8]
%endmacro

%macro LOAD_4_PARA_POP 0
%endmacro

%macro LOAD_5_PARA_POP 0
%endmacro

%macro LOAD_6_PARA_POP 0
%endmacro

%macro LOAD_7_PARA_POP 0
%endmacro

%macro PUSH_XMM 1
%endmacro

%macro POP_XMM 0
%endmacro

%macro SIGN_EXTENSION 2
        movsxd %1, %2
%endmacro

%macro SIGN_EXTENSIONW 2
        movsx %1, %2
%endmacro

%macro WELS_EXTERN 1
    ALIGN 16, nop
        %ifdef WELS_PRIVATE_EXTERN
            global %1: WELS_PRIVATE_EXTERN
        %else
            global %1
        %endif
    %1:
%endmacro

%macro WELS_AbsW 2
    pxor        %2, %2
    psubw       %2, %1
    pmaxsw      %1, %2
%endmacro

%macro MMX_XSwap  4
    movq        %4, %2
    punpckh%1   %4, %3
    punpckl%1   %2, %3
%endmacro

; pOut mm1, mm4, mm5, mm3

;for TRANSPOSE
%macro SSE2_XSawp 4
    movdqa      %4, %2
    punpckl%1   %2, %3
    punpckh%1   %4, %3
%endmacro

; in: xmm1, xmm2, xmm3, xmm4  pOut:  xmm1, xmm4, xmm5, mm3
%macro SSE2_Trans4x4D 5
    SSE2_XSawp dq,  %1, %2, %5
    SSE2_XSawp dq,  %3, %4, %2
    SSE2_XSawp qdq, %1, %3, %4
    SSE2_XSawp qdq, %5, %2, %3
%endmacro

;in: xmm0, xmm1, xmm2, xmm3  pOut:  xmm0, xmm1, xmm3, xmm4
%macro SSE2_TransTwo4x4W 5
    SSE2_XSawp wd,  %1, %2, %5
    SSE2_XSawp wd,  %3, %4, %2
    SSE2_XSawp dq,  %1, %3, %4
    SSE2_XSawp dq,  %5, %2, %3
    SSE2_XSawp qdq, %1, %5, %2
    SSE2_XSawp qdq, %4, %3, %5
%endmacro

;in:  m1, m2, m3, m4, m5, m6, m7, m8
;pOut: m5, m3, m4, m8, m6, m2, m7, m1

;xmm0, xmm6, xmm7, [eax], [ecx]
;xmm7 = 0, eax = pix1, ecx = pix2, xmm0 save the result

; m2 = m1 + m2, m1 = m1 - m2
%macro SSE2_SumSub 3
    movdqa  %3, %2
    paddw   %2, %1
    psubw   %1, %3
%endmacro

;copy a dw into a xmm for 8 times
%macro SSE2_Copy8Times 2
    movd    %1, %2
    punpcklwd %1, %1
    pshufd  %1,     %1,     0
%endmacro

;copy a db into a xmm for 16 times
%macro SSE2_Copy16Times 2
    movd            %1, %2
    pshuflw         %1, %1, 0
    punpcklqdq      %1, %1
    packuswb        %1,     %1
%endmacro

;***********************************************************************
;preprocessor constants
;***********************************************************************
;dw 32,32,32,32,32,32,32,32 for xmm
;dw 32,32,32,32 for mm
%macro WELS_DW32 1
    pcmpeqw %1,%1
    psrlw %1,15
    psllw %1,5
%endmacro

;dw 1, 1, 1, 1, 1, 1, 1, 1 for xmm
;dw 1, 1, 1, 1 for mm

;all 0 for xmm and mm
%macro WELS_Zero 1
    pxor %1, %1
%endmacro

;dd 1, 1, 1, 1 for xmm
;dd 1, 1 for mm
%macro WELS_DD1 1
    pcmpeqw %1,%1
    psrld %1,31
%endmacro

;dB 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1

%macro WELS_DW1_VEX 1
    vpcmpeqw %1, %1, %1
    vpsrlw   %1, %1, 15
%endmacro

%macro WELS_DW32767_VEX 1
    vpcmpeqw %1, %1, %1
    vpsrlw   %1, %1,  1
%endmacro

;***********************************************************************
; Utility macros for X86_32 PIC support
;***********************************************************************

; Used internally by other macros.
%macro INIT_X86_32_PIC_ 2
    %define pic(data_addr) (data_addr)
%endmacro

; Get program counter and define a helper macro "pic(addr)" to convert absolute
; addresses to program counter-relative addresses if X86_32_PICASM is defined.
; Otherwise define "pic(addr)" as an identity function.
; %1=register to store PC/EIP in.
%macro INIT_X86_32_PIC 1
    INIT_X86_32_PIC_ %1, 1
%endmacro

; Equivalent as above, but without preserving the value of the register argument.
%macro INIT_X86_32_PIC_NOPRESERVE 1
    INIT_X86_32_PIC_ %1, 0
%endmacro

; Clean up after INIT_X86_32_PIC.
; Restore the register used to hold PC/EIP if applicable, and undefine defines.
%macro DEINIT_X86_32_PIC 0
    %undef pic
%endmacro

; Equivalent as above, but without undefining. Useful for functions with
; multiple epilogues.
