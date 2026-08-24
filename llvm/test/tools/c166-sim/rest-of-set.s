# REQUIRES: c166-registered-target
# RUN: llvm-mc -filetype=obj -triple=c166 %s -o %t.o
# RUN: llvm-objcopy -O binary %t.o %t.bin
# RUN: %c166_sim --binary --dump-state %t.bin 2>&1 | FileCheck %s

## The instruction forms nothing generates, run rather than only assembled.
## Their encodings are derived from the structure of the opcode map rather than
## read off the page, so what they do is worth watching happen.
##
##   R3  1234  a memory destination add: FD00 held 1000, 0234 was added to it
##   R5  2234  an indirect source add, through R0
##   R6  1234  the same with the pointer stepping past what it read
##   R7  abcd  written through a pre-decrementing store and read back
##   R8  1000  10000H divided by 16 by DIVLU, which takes MDH:MDL
##   R9  0007  PRIOR on 0100H: seven zeroes above the leftmost set bit
##   R10 0006  CMPI1 compared 5 with 5 and then stepped it
##   R11 0002  SCXT put 2 in DPP2 after saving what was there
##   R12 5555  PCALL and RETP: the register came back with the return
# CHECK: R3=1234 R4=fd02 R5=2234 R6=1234 R7=abcd
# CHECK: R8=1000 R9=0007 R10=0006 R11=0002 R12=5555

        .text
        jmps    #0, 0x20

        .org    0x20
## A memory destination ALU form.
        mov     r2, #0x1000
        mov     0xFD00, r2
        mov     r3, #0x0234
        add     0xFD00, r3
        mov     r3, 0xFD00

## The indirect source forms.  R0 to R3 are the only pointers the two bit
## field can name.
        mov     r0, #0xFD00
        mov     r5, #0x1000
        add     r5, [r0]
        mov     r6, #0
        add     r6, [r0+]
        mov     r4, r0

## The pre-decrementing store, the only auto-stepping store there is.
        mov     r1, #0xFD10
        mov     r7, #0xABCD
        mov     [-r1], r7
        mov     r7, #0
        mov     r7, 0xFD0E

## The 32 by 16 divide.
        mov     r8, #1
        mov     mdh, r8
        mov     r8, #0
        mov     mdl, r8
        mov     r8, #16
        divlu   r8
        mov     r8, mdl

## PRIOR, which is the count of leading zeroes.
        mov     r9, #0x0100
        prior   r9, r9

## Compare and step: the comparison sees the value as it was.
        mov     r10, #5
        cmpi1   r10, #5

## SCXT saves what a register holds and puts something else there.
        mov     r11, #0x1111
        mov     dpp2, r11
        scxt    dpp2, #0x0002
        mov     r11, dpp2

## PCALL pushes a register before the return address; RETP takes them back in
## the other order.
        mov     r12, #0x5555
        pcall   r12, 0x100
        jmpr    cc_UC, .Ldone

        .org    0x100
        mov     r12, #0
        retp    r12

.Ldone:
        mov     r2, #0
        exts    #0xFF, #1
        mov     0x0002, r2
