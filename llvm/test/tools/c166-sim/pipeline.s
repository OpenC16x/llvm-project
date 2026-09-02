# REQUIRES: c166-registered-target
# RUN: rm -rf %t && split-file %s %t

## The particular pipeline effects of section 4.2 of the C167CR Derivatives
## User's Manual V3.1, which are the cases where "the circumstance that the
## C167CR is a pipelined machine requires attention by the programmer".
##
## Three of them are a rule about what not to write, and those are checked
## rather than emulated: the part does not report them, it uses the value the
## pipeline still holds and carries on, so the wrong answer a program gets is
## not one this could reproduce without inventing what "mostly not capable"
## covers.  What can be checked exactly is the sequence the manual says must
## not appear, and that is what stops the program here.
##
## Until this existed, the two gaps the compiler leaves - the NOP after SCXT CP
## in a banked handler's prologue, and the one after crt0's write to DPP3 -
## were put there on the strength of reading the manual, and nothing in this
## tree could tell whether they were needed or whether they worked.  Taking
## either out now stops the program with the sentence that asked for it.

## Context Pointer Updating.  "An instruction, which calculates a physical GPR
## operand address via the CP register, is mostly not capable of using a new CP
## value, which is to be updated by an immediately preceding instruction."
# RUN: llvm-mc -filetype=obj -triple=c166 %t/cp-bad.s -o %t/cp-bad.o
# RUN: llvm-objcopy -O binary %t/cp-bad.o %t/cp-bad.bin
# RUN: not %c166_sim --binary %t/cp-bad.bin 2>&1 | FileCheck %s --check-prefix=CP
# CP: error: a general purpose register is read or written by the instruction after the one that wrote CP
# CP-SAME: Context Pointer Updating

## The same sequence with the gap the manual asks for, which is what the
## banked handler prologue emits.
# RUN: llvm-mc -filetype=obj -triple=c166 %t/cp-good.s -o %t/cp-good.o
# RUN: llvm-objcopy -O binary %t/cp-good.o %t/cp-good.bin
# RUN: %c166_sim --binary --dump-state %t/cp-good.bin 2>&1 \
# RUN:   | FileCheck %s --check-prefix=CPOK
# CPOK: R2=0000 R3=1234

## Data Page Pointer Updating, which is the one crt0.S leaves a NOP for.
# RUN: llvm-mc -filetype=obj -triple=c166 %t/dpp-bad.s -o %t/dpp-bad.o
# RUN: llvm-objcopy -O binary %t/dpp-bad.o %t/dpp-bad.bin
# RUN: not %c166_sim --binary %t/dpp-bad.bin 2>&1 \
# RUN:   | FileCheck %s --check-prefix=DPP
# DPP: error: a long or indirect address goes through the data page pointer that the instruction before this one wrote
# DPP-SAME: Data Page Pointer Updating

## A different pointer is a different dependency: writing DPP0 and then
## addressing through DPP3 is not this at all.
# RUN: llvm-mc -filetype=obj -triple=c166 %t/dpp-other.s -o %t/dpp-other.o
# RUN: llvm-objcopy -O binary %t/dpp-other.o %t/dpp-other.bin
# RUN: %c166_sim --binary --dump-state %t/dpp-other.bin 2>&1 \
# RUN:   | FileCheck %s --check-prefix=DPPOK
# DPPOK: R4=00aa

## Explicit Stack Pointer Updating.  "None of the RET, RETI, RETS, RETP or POP
## instructions is capable of correctly using a new SP register value, which is
## to be updated by an immediately preceding instruction."
# RUN: llvm-mc -filetype=obj -triple=c166 %t/sp-bad.s -o %t/sp-bad.o
# RUN: llvm-objcopy -O binary %t/sp-bad.o %t/sp-bad.bin
# RUN: not %c166_sim --binary %t/sp-bad.bin 2>&1 | FileCheck %s --check-prefix=SP
# SP: error: this pops the system stack immediately after SP was written
# SP-SAME: Explicit Stack Pointer Updating

## "Note: Conflicts with instructions writing to the stack (PUSH, CALL, SCXT)
## are solved internally by the CPU logic" - so a PUSH straight after a write
## to SP is not one of these, and neither is the implicit update a PUSH makes
## to SP itself.
# RUN: llvm-mc -filetype=obj -triple=c166 %t/sp-push.s -o %t/sp-push.o
# RUN: llvm-objcopy -O binary %t/sp-push.o %t/sp-push.bin
# RUN: %c166_sim --binary --dump-state %t/sp-push.bin 2>&1 \
# RUN:   | FileCheck %s --check-prefix=SPOK
# SPOK: R6=0077

## Controlling Interrupts is the fourth effect and the only one of the shape
## the manual describes as behaviour rather than as a rule, so it is modelled
## rather than checked: "the current interrupt prioritization round does not
## consider these changes ... The described delay of 1 instruction also applies
## for enabling the interrupts system i.e. no interrupt requests are
## acknowledged until the instruction following the enabling instruction."
##
## The request below is raised long before interrupts are enabled and so is
## waiting when BSET PSW.11 runs.  R6 is the counter as the handler found it:
## one, because the ADD after the BSET ran first.  Without the delay it would
## be zero, which is what --no-pipeline-effects shows.
# RUN: llvm-mc -filetype=obj -triple=c166 %t/delay.s -o %t/delay.o
# RUN: llvm-objcopy -O binary %t/delay.o %t/delay.bin
# RUN: %c166_sim --binary --dump-state --interrupt-at=2:2:8 %t/delay.bin 2>&1 \
# RUN:   | FileCheck %s --check-prefix=DELAY
# DELAY: R2=0003 R3=0001 R4=0000 R5=0000 R6=0001
# RUN: %c166_sim --binary --dump-state --no-pipeline-effects \
# RUN:   --interrupt-at=2:2:8 %t/delay.bin 2>&1 | FileCheck %s --check-prefix=NODELAY
# NODELAY: R2=0003 R3=0001 R4=0000 R5=0000 R6=0000

## And the checks go with it, so a program written before any of this can still
## be run - the part would run it wrongly rather than refuse it, and saying so
## is the whole point, but it is not this simulator's place to make it
## unrunnable.
# RUN: %c166_sim --binary --dump-state --no-pipeline-effects %t/cp-bad.bin 2>&1 \
# RUN:   | FileCheck %s --check-prefix=CPOFF
# CPOFF: R3=1234

#--- cp-bad.s
        .text
        jmps    #0, 0x100
        .org    0x100
## A bank at FC20H, which is inside the dual-port RAM where one has to be.
        scxt    cp, #0xfc20
## The very next instruction addresses a GPR, so it addresses the old bank.
        mov     r3, #0x1234
        mov     r5, #0
        exts    #0xFF, #1
        mov     0x0002, r5

#--- cp-good.s
        .text
        jmps    #0, 0x100
        .org    0x100
        scxt    cp, #0xfc20
        nop
        mov     r3, #0x1234
## Back to the old bank, where R3 was never written and R2 is the exit code.
        pop     cp
        nop
        mov     r2, #0
        mov     r3, #0x1234
        exts    #0xFF, #1
        mov     0x0002, r2

#--- dpp-bad.s
        .text
        jmps    #0, 0x100
        .org    0x100
        mov     dpp3, #3
## C000H and up is the DPP3 quarter of a near address, so this goes through the
## pointer the instruction before it wrote.
        mov     r4, 0xc002
        mov     r5, #0
        exts    #0xFF, #1
        mov     0x0002, r5

#--- dpp-other.s
        .text
        jmps    #0, 0x100
        .org    0x100
## Put something where DPP3 already points, then write a different pointer and
## read it back.  DPP0 is not the one this address goes through.
        mov     r4, #0x00aa
        mov     0xc002, r4
        mov     r4, #0
        mov     dpp0, #0
        mov     r4, 0xc002
        mov     r5, #0
        exts    #0xFF, #1
        mov     0x0002, r5

#--- sp-bad.s
        .text
        jmps    #0, 0x100
        .org    0x100
        mov     sp, #0xfb00
        pop     r4
        mov     r5, #0
        exts    #0xFF, #1
        mov     0x0002, r5

#--- sp-push.s
        .text
        jmps    #0, 0x100
        .org    0x100
        mov     r6, #0x0077
        mov     sp, #0xfb00
## A PUSH straight after, which the manual says the CPU sorts out itself, and
## then a POP two instructions after the SP write rather than one.
        push    r6
        mov     r6, #0
        pop     r6
        mov     r5, #0
        exts    #0xFF, #1
        mov     0x0002, r5

#--- delay.s
        .text
        jmps    #0, 0x100
        .org    8
        jmps    #0, 0x160

        .org    0x100
        mov     r3, #0
        mov     r6, #0xffff
        mov     r2, #0
        bset    psw.11
        add     r2, #1
        add     r2, #1
        add     r2, #1
        mov     r5, #0
        exts    #0xFF, #1
        mov     0x0002, r5

        .org    0x160
        add     r3, #1
        mov     r6, r2
        reti
