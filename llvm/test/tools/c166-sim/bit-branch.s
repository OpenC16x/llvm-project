# REQUIRES: c166-registered-target
# RUN: llvm-mc -filetype=obj -triple=c166 %s -o %t.o
# RUN: llvm-objcopy -O binary %t.o %t.bin
# RUN: %c166_sim --binary --dump-state %t.bin 2>&1 | FileCheck %s

## JB and JNB, taken and not taken, on a register and on a word in memory.
## R3 collects one bit per case that went the right way, so 3F says all six
## did; a case that went the wrong way lands on .Lfail and leaves FFFF, which
## no combination of the six can produce.
# CHECK: R3=003f

        .text
        jmps    #0, 0x20

        .org    0x20
## Bit 1 and bit 8 set, bit 0 clear.
        mov     r2, #0x0102
        mov     r3, #0

## Taken, on a bit that is set.
        jb      r2.1, .L1
        jmpr    cc_UC, .Lfail
.L1:    add     r3, #1

## Taken, on a bit that is clear.
        jnb     r2.0, .L2
        jmpr    cc_UC, .Lfail
.L2:    add     r3, #2

## Not taken, either way round.
        jb      r2.0, .Lfail
        add     r3, #4
        jnb     r2.8, .Lfail
        add     r3, #8

## The same on memory.  FD20H is bit-addressable and its bitoff is
## (FD20H - FD00H) / 2 = 16; bit 15 is set and bit 0 is clear.
        mov     r4, #0x8000
        mov     0xFD20, r4
        jb      16.15, .L3
        jmpr    cc_UC, .Lfail
.L3:    add     r3, #16
        jnb     16.0, .L4
        jmpr    cc_UC, .Lfail
.L4:    add     r3, #32

        jmpr    cc_UC, .Ldone
.Lfail: mov     r3, #0xFFFF
.Ldone:
        mov     r2, #0
        exts    #0xFF, #1
        mov     0x0002, r2
