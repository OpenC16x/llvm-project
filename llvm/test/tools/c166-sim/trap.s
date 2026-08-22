# REQUIRES: c166-registered-target
# RUN: llvm-mc -filetype=obj -triple=c166 %s -o %t.o
# RUN: llvm-objcopy -O binary %t.o %t.bin
# RUN: %c166_sim --binary --dump-state %t.bin 2>&1 | FileCheck %s

## TRAP does not read a vector through the table: it branches to the table
## entry itself, at 4 * the trap number, so the entry has to be a jump.  This
## puts one at 8, traps to it, and comes back with RETI, which undoes the three
## things TRAP pushed.  R2 counts the handler (40) and the instruction after
## the trap (+2), so 42 says both halves ran and in the right order.
# CHECK: R2=002a

## A flat image has no linker, so every address here is written out rather than
## named; .org puts the two jumps in the slots they have to be in.
        .text
## Trap 0 is reset, which is where a flat image starts.
        jmps    #0, 0x20
        .org    8
## Trap 2.
        jmps    #0, 0x40

        .org    0x20
        mov     r2, #0
        trap    #2
## The handler returns here, so this runs after it.
        add     r2, #2

        mov     r5, #0
        exts    #0xFF, #1
        mov     0x0002, r5

        .org    0x40
        mov     r2, #40
        reti
