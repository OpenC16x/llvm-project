# REQUIRES: c166-registered-target
# RUN: llvm-mc -filetype=obj -triple=c166 %s -o %t.o
# RUN: llvm-objcopy -O binary %t.o %t.bin
# RUN: %c166_sim --binary --dump-state %t.bin 2>&1 | FileCheck %s

## The 8 bit "reg" field of a byte instruction names a special function
## register the same way a word instruction does, except that it reaches only
## the register's low byte.  Writing that byte does not leave the high byte
## alone: the manual says a byte write through the field "can only access the
## low byte of an SFR and force zeros in the high byte", so MDL comes out as
## 0012H rather than FF12H.
# CHECK: MDL=0012 MDH=0044

        mov     mdl, #0xFFFF
        movb    mdl, #0x12

## Byte arithmetic on the field works on that low byte too, and the high byte
## of MDH goes the same way: 0033H + 11H, not FF33H + 11H.
        mov     mdh, #0xFF33
        addb    mdh, #0x11

        mov     r5, #0
        exts    #0xFF, #1
        mov     0x0002, r5
