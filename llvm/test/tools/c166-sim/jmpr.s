# REQUIRES: c166-registered-target
# RUN: llvm-mc -filetype=obj -triple=c166 %s -o %t.o
# RUN: llvm-objcopy -O binary %t.o %t.bin
# RUN: %c166_sim --binary --dump-state %t.bin 2>&1 | FileCheck %s
## R2 counts the checks that ran, so a run that took every branch it should
## and no branch it should not leaves it at 3.
# CHECK: R2=0003

## JMPR is the two byte relative jump.  Its displacement counts words from the
## instruction after it, so it is the one branch here whose target moves when
## the instruction in front of it changes size.

        mov     r2, #0

## Taken unconditionally, forwards.
        jmpr    cc_UC, .Lone
        mov     r2, #100                ; must not run
.Lone:
        add     r2, #1

## Taken on a condition that holds.
        cmp     r2, #1
        jmpr    cc_z, .Ltwo
        mov     r2, #100                ; must not run
.Ltwo:
        add     r2, #1

## Not taken on a condition that does not.
        cmp     r2, #99
        jmpr    cc_z, .Lbad
        add     r2, #1
        jmpr    cc_UC, .Lout
.Lbad:
        mov     r2, #100
.Lout:

## And backwards: run the loop body twice, using R3 as the counter so that R2
## is left alone.
        mov     r3, #2
.Lloop:
        sub     r3, #1
        jmpr    cc_nz, .Lloop

        mov     r5, #0
        exts    #0xFF, #1
        mov     0x0002, r5
