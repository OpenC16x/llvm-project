# REQUIRES: c166-registered-target
# RUN: llvm-mc -filetype=obj -triple=c166 %s -o %t.o
# RUN: llvm-objcopy -O binary %t.o %t.bin
# RUN: %c166_sim --binary --dump-state %t.bin 2>&1 | FileCheck %s

## How far apart the vector table's entries are is not fixed on this core:
## CPUCON1's VECSC field scales it, so trap 2 is at 8 with the reset value of
## that field and at 16 once it says four words instead of two.  Both handlers
## are in place and each adds a different amount, so R2 says which ran.
## 1 + 40 = 41.
# CHECK: R2=0029

        .text
## Trap 0 is reset, which is where a flat image starts.
        jmps    #0, 0x20
## Trap 2 at two words a vector.
        .org    8
        jmps    #0, 0x40
## Trap 2 at four words a vector.
        .org    16
        jmps    #0, 0x50

        .org    0x20
        mov     r2, #0
        trap    #2
## VECSC is bits 6 and 5, so 01 there is four words between vectors.  The rest
## of the reset value, 0007H, is left alone.
        mov     cpucon1, #0x27
        trap    #2

        mov     r5, #0
        exts    #0xFF, #1
        mov     0x0002, r5

        .org    0x40
        add     r2, #1
        reti

        .org    0x50
        add     r2, #40
        reti
