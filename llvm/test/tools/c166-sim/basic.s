# REQUIRES: c166-registered-target
# RUN: llvm-mc -filetype=obj -triple=c166 %s -o %t.o
# RUN: llvm-objcopy -O binary %t.o %t.bin
# RUN: %c166_sim --binary --dump-state %t.bin 2>&1 | FileCheck %s
## R2 holds the result, which is 40 + 2.
# CHECK: R2=002a

## A flat image needs no linker and no symbols: it stops by writing to the
## simulator's exit port, which is in the top segment and so is reached
## through an EXTS.  The answer is left in R2 and the exit code is zero, since
## a non-zero one would fail the test whatever the answer was.

        mov     r2, #40
        add     r2, #2
        mov     r5, #0
        exts    #0xFF, #1
        mov     0x0002, r5
