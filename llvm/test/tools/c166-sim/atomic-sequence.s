# REQUIRES: c166-registered-target
# RUN: rm -rf %t && split-file %s %t

## The hardware keeps one instruction counter for ATOMIC and the EXTend
## instructions, and it keeps counting whatever runs next.  So a sequence must
## not contain anything that changes the flow - the rest of the count goes with
## it, protecting instructions nobody meant to protect and leaving the ones
## that needed it uncovered - and must not contain a second extend, which
## overwrites the count the first one is still using.
##
## The compiler has to remember that when it builds an atomic sequence.  This
## is what holds it to it: every program the differential suite runs goes
## through this check, so a sequence that breaks the rule stops rather than
## quietly working.  What the sequence is protecting the code from is in
## interrupt.s, which injects a request into one and shows it held off until
## the last covered instruction has run.

# RUN: llvm-mc -filetype=obj -triple=c166 %t/flow.s -o %t/flow.o
# RUN: llvm-objcopy -O binary %t/flow.o %t/flow.bin
# RUN: not %c166_sim --binary %t/flow.bin 2>&1 | FileCheck %s --check-prefix=FLOW
# FLOW: error: an ATOMIC or EXTend sequence reaches the instruction at {{.*}}, which changes the flow or extends again

# RUN: llvm-mc -filetype=obj -triple=c166 %t/extend.s -o %t/extend.o
# RUN: llvm-objcopy -O binary %t/extend.o %t/extend.bin
# RUN: not %c166_sim --binary %t/extend.bin 2>&1 | FileCheck %s --check-prefix=EXTEND
# EXTEND: error: an ATOMIC or EXTend sequence reaches the instruction at {{.*}}, which changes the flow or extends again

## A sequence that keeps to the rule runs to the end and is not reported.
# RUN: llvm-mc -filetype=obj -triple=c166 %t/good.s -o %t/good.o
# RUN: llvm-objcopy -O binary %t/good.o %t/good.bin
# RUN: %c166_sim --binary --dump-state %t/good.bin 2>&1 | FileCheck %s --check-prefix=GOOD
## R3 is the word as it was and R4 is the word as it now is, so 3 and 7 say
## the read, the change and the write back all happened and in that order.
# GOOD: R3=0003 R4=0007

#--- flow.s
        .text
        jmps    #0, 0x20
        .org    0x20
        mov     r2, #1
        atomic  #3
        mov     r3, r2
        jmpr    cc_UC, .Lx
.Lx:
        mov     r4, r2

#--- extend.s
        .text
        jmps    #0, 0x20
        .org    0x20
        mov     r2, #1
        atomic  #3
        mov     r3, r2
        exts    #0xFF, #1
        mov     r4, r2

#--- good.s
        .text
        jmps    #0, 0x20
        .org    0x20
## The shape the compiler emits: read, copy, change, write back, all covered.
        mov     r2, #0x1000
        mov     r5, #3
        mov     0x1000, r5
        atomic  #4
        mov     r3, [r2]
        mov     r4, r3
        add     r4, #4
        mov     [r2], r4
        mov     r2, #0
        exts    #0xFF, #1
        mov     0x0002, r2
