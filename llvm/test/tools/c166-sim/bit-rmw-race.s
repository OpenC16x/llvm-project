# REQUIRES: c166-registered-target
# RUN: llvm-mc -filetype=obj -triple=c166 %s -o %t.o
# RUN: llvm-objcopy -O binary %t.o %t.bin
# RUN: %c166_sim --binary --dump-state %t.bin 2>&1 | FileCheck %s

## What a bit instruction is for, shown rather than asserted.
##
## Two paths share one bit-addressable word: the main one owns bit 0 and a
## handler owns bit 1.  Read the word, change it, write it back, and there is a
## window between the read and the write; a handler that runs in that window
## reads the same old word, and the write-back throws its bit away.  BSET does
## the read and the write as one indivisible operation, so the window does not
## exist and it does not matter where the handler runs.
##
## The interrupt here is a TRAP, which is a software one - this simulator has
## no asynchronous interrupts - but it saves what a hardware interrupt saves
## and vectors the same way, so it lands in the window the same way.
##
## R4 is what survived the read-modify-write and R5 what survived the bit
## instruction.  The handler's bit is missing from the first and present in the
## second, which is the whole of the argument.
# CHECK: R4=0001 R5=0003

        .text
## Trap 0 is reset, which is where a flat image starts.
        jmps    #0, 0x20
        .org    8
## Trap 2, the handler.
        jmps    #0, 0x60

        .org    0x20
## The word both paths share: FD20H is bit-addressable, and its bitoff is
## (FD20H - FD00H) / 2 = 16.
        mov     r2, #0
        mov     0xFD20, r2

## The long form, with the handler arriving in the middle of it.
        mov     r3, 0xFD20
        or      r3, #1
        trap    #2
        mov     0xFD20, r3
        mov     r4, 0xFD20

## The same thing as one instruction.  The handler is put before it here
## because there is nowhere else to put it: BSET has no middle.
        mov     r2, #0
        mov     0xFD20, r2
        trap    #2
        bset    16.0
        mov     r5, 0xFD20

        mov     r2, #0
        exts    #0xFF, #1
        mov     0x0002, r2

        .org    0x60
## The handler sets the bit it owns, and does it the indivisible way.
        bset    16.1
        reti
