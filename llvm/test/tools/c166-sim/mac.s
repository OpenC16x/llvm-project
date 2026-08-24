# REQUIRES: c166-registered-target
# RUN: llvm-mc -filetype=obj -triple=c166 -mattr=+mac %s -o %t.o
# RUN: llvm-objcopy -O binary %t.o %t.bin
# RUN: %c166_sim --binary --dump-state %t.bin 2>&1 | FileCheck %s

## The MAC unit doing what a dot product needs.  MCW resets to zero, so the
## product shift and the saturation are both off and this is plain signed
## integer multiply-accumulate; nothing configures the unit first.
##
##   CoLOAD r0, r0     ACC = 0
##   CoMAC  r1, r2     ACC += 300 * 400        =    120000
##   CoMAC  r3, r4     ACC += -500 * 600       =   -300000, so ACC = -180000
##   CoMAC  r5, r6     ACC += -700 * -800      =    560000, so ACC =  380000
##
##   380000 = 0x0005CC60, so MAH = 0005 and MAL = cc60.
##
## The negative intermediate is the point: it is what shows the accumulator
## being sign correct across the whole 40 bits rather than wrapping at 32.

# CHECK: R7=cc60
# CHECK-NEXT: R8=0005

        .text
        jmps    #0, 0x20

        .org    0x20
        mov     r0, #0
        coload  r0, r0

        mov     r1, #300
        mov     r2, #400
        comac   r1, r2

        mov     r3, #-500
        mov     r4, #600
        comac   r3, r4

        mov     r5, #-700
        mov     r6, #-800
        comac   r5, r6

        costore r7, mal
        costore r8, mah

        mov     r9, #0
        exts    #0xFF, #1
        mov     0x0002, r9
