# REQUIRES: c166-registered-target
# RUN: llvm-mc -filetype=obj -triple=c166 -mattr=+mac %s -o %t.o
# RUN: llvm-objcopy -O binary %t.o %t.bin
# RUN: %c166_sim --binary --dump-state %t.bin 2>&1 | FileCheck %s

## CoMAX and CoMIN compare a 40 bit operand against the accumulator, signed.
## The operand is the two registers concatenated and sign extended, which is
## how CoLOAD builds one too, so a pair of sign extended 32 bit values compare
## as 32 bit signed values.
##
## The case that says the comparison is signed rather than unsigned is a
## negative against a positive: as unsigned words FFFF5678H is the larger, and
## as signed values it is the smaller.
##
##   CoLOAD r1, r2      ACC = 0001 1234H
##   CoMAX  r3, r4      max(0001 1234H, FFFF 5678H) = 0001 1234H
##   CoSTORE r5, MAL    R5 = 1234
##   CoSTORE r6, MAH    R6 = 0001
##
##   CoLOAD r1, r2      ACC = 0001 1234H
##   CoMIN  r3, r4      min(...) = FFFF 5678H, sign extended
##   CoSTORE r7, MAL    R7 = 5678
##   CoSTORE r8, MAH    R8 = ffff

## The dump prints eight registers to a line, so the pair from CoMIN is split
## across two of them.
# CHECK: R5=1234 R6=0001 R7=5678
# CHECK-NEXT: R8=ffff

        .text
        jmps    #0, 0x20

        .org    0x20
        mov     r1, #0x1234
        mov     r2, #0x0001
        mov     r3, #0x5678
        mov     r4, #0xFFFF

        coload  r1, r2
        comax   r3, r4
        costore r5, mal
        costore r6, mah

        coload  r1, r2
        comin   r3, r4
        costore r7, mal
        costore r8, mah

        mov     r9, #0
        exts    #0xFF, #1
        mov     0x0002, r9
