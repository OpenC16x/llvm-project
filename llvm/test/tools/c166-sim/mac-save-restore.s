# REQUIRES: c166-registered-target
# RUN: llvm-mc -filetype=obj -triple=c166 -mattr=+mac %s -o %t.o
# RUN: llvm-objcopy -O binary %t.o %t.bin
# RUN: %c166_sim --binary --dump-state %t.bin 2>&1 | FileCheck %s

## Saving and restoring the coprocessor the way an interrupt handler has to.
## The C166S V2 manual says the CPU preserves none of it - "all dedicated MAC
## registers must be saved on the stack if the MAC unit is shared between
## different tasks and interrupts" - and the accumulator is forty bits, so the
## save covers MSW as well: its low byte is MAE, the top eight bits.
##
## Five products of 32767 by 32767 come to 5368381445, which is 1_3FFB_0005H:
## MAE = 01, MAH = 3FFB, MAL = 0005.  A value whose extension byte is not just
## the sign of MAH is the whole point, because it is the part that only comes
## back if MSW came back.
##
## The order is the one the manual forces.  Writing a word to MAH zeroes MAL
## and sign extends the extension byte, so MAH is restored first and MAL and
## MSW land on top of what it cleared.  Pushing is the mirror: MSW, MAL, MAH.

# CHECK: R7=0005
# CHECK-NEXT: R8=3ffb
# CHECK-SAME: R9=0001

        .text
        jmps    #0, 0x20

        .org    0x20
        mov     r0, #0
        coload  r0, r0

        mov     r1, #32767
        comac   r1, r1
        comac   r1, r1
        comac   r1, r1
        comac   r1, r1
        comac   r1, r1

## Save.
        push    msw
        push    mal
        push    mah

## Lose it, as an interrupt handler using the unit for itself would.
        coload  r0, r0
        comac   r1, r1

## Restore.
        pop     mah
        pop     mal
        pop     msw

## And read the whole forty bits back.
        costore r7, mal
        costore r8, mah
        mov     r9, msw

        mov     r10, #0
        exts    #0xFF, #1
        mov     0x0002, r10
