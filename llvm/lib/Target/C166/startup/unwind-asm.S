//===-- unwind-asm.S - Register capture for the C166 unwinder -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Snapshotting the machine is not something C can do, so it is here.
//
//===----------------------------------------------------------------------===//

        .text

// void __unw_getcontext(_Unwind_Context *ctx)   ctx in R2
//
// Fills ctx with the state of whoever called this, so that the first frame a
// walk reports is the caller and not this function - which has no call frame
// information of its own and could not be walked out of.
//
// "The caller's state" is this one with the call undone: the general purpose
// registers as they stand, because nothing here has changed them yet; the
// system stack pointer stepped past the return address a near CALL pushed;
// and the program counter that return address, with the segment we are
// running in, since a near call cannot have changed it.
        .globl  __unw_getcontext
        .type   __unw_getcontext,@function
__unw_getcontext:
        mov     [r2+#0],  r0
        mov     [r2+#2],  r1
        mov     [r2+#4],  r2
        mov     [r2+#6],  r3
        mov     [r2+#8],  r4
        mov     [r2+#10], r5
        mov     [r2+#12], r6
        mov     [r2+#14], r7
        mov     [r2+#16], r8
        mov     [r2+#18], r9
        mov     [r2+#20], r10
        mov     [r2+#22], r11
        mov     [r2+#24], r12
        mov     [r2+#26], r13
        mov     [r2+#28], r14
        mov     [r2+#30], r15

        // R3 is saved, so it is free to use as a pointer to the system stack.
        mov     r3, sp
        mov     r4, [r3]                // the return offset the CALL pushed
        mov     [r2+#36], r4            // ctx->pc, low half
        mov     r4, csp
        mov     [r2+#38], r4            // ctx->pc, high half: the segment
        mov     [r2+#34], r4            // ctx->csp
        add     r3, #2                  // past the return address
        mov     [r2+#32], r3            // ctx->syssp

        ret
.Lgetcontext_end:
        .size   __unw_getcontext, .Lgetcontext_end-__unw_getcontext

// void __unw_resume(_Unwind_Context *ctx)   ctx in R2, and never returns
//
// Puts the machine into the state ctx describes and carries on there, which is
// what installing a landing pad comes to.
//
// The jump is a RETS rather than a branch because the address has two halves
// and only a return instruction sets both.  A fabricated frame goes on the
// system stack - the offset where RETS looks for it and the segment above -
// and the system stack pointer is moved to it before the general purpose
// registers are put back.  R2 is restored last because it is the pointer
// everything else is read through.
        .globl  __unw_resume
        .type   __unw_resume,@function
__unw_resume:
        mov     r3, [r2+#32]            // ctx->syssp, the caller's
        sub     r3, #4                  // room for the segment and the offset
        mov     r4, [r2+#36]            // ctx->pc, low half: the offset
        mov     [r3], r4
        mov     r4, [r2+#38]            // ctx->pc, high half: the segment
        mov     [r3+#2], r4
        mov     sp, r3

        mov     r0,  [r2+#0]
        mov     r1,  [r2+#2]
        mov     r3,  [r2+#6]
        mov     r4,  [r2+#8]
        mov     r5,  [r2+#10]
        mov     r6,  [r2+#12]
        mov     r7,  [r2+#14]
        mov     r8,  [r2+#16]
        mov     r9,  [r2+#18]
        mov     r10, [r2+#20]
        mov     r11, [r2+#22]
        mov     r12, [r2+#24]
        mov     r13, [r2+#26]
        mov     r14, [r2+#28]
        mov     r15, [r2+#30]
        mov     r2,  [r2+#4]

        rets
.Lresume_end:
        .size   __unw_resume, .Lresume_end-__unw_resume
