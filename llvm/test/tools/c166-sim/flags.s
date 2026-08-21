# REQUIRES: c166-registered-target
# RUN: llvm-mc -filetype=obj -triple=c166 %s -o %t.o
# RUN: llvm-objcopy -O binary %t.o %t.bin
# RUN: %c166_sim --binary --dump-state %t.bin 2>&1 | FileCheck %s
## R2 is the set of checks that failed, so it must be zero.
# CHECK: R2=0000

## The three flag behaviours that are not what a reader used to other machines
## would guess.  Each check leaves a bit set in R3 on failure, and R3 is the
## result, so a passing run exits 0.

        mov     r3, #0

## 1. SUB sets C when a borrow is generated, so C is set exactly when an
##    unsigned subtraction wrapped.  1 - 2 borrows.
        mov     r2, #1
        sub     r2, #2
        jb      psw.1, .Lborrow_ok
        or      r3, #1
.Lborrow_ok:
##    and 2 - 1 does not.
        mov     r2, #2
        sub     r2, #1
        jnb     psw.1, .Lnoborrow_ok
        or      r3, #2
.Lnoborrow_ok:

## 2. ADDC keeps Z set only if it was already set, which is how a wide value
##    tests as zero exactly when every word of it did.  Note that MOV sets Z
##    itself, so the instruction that clears it has to come after the operands
##    are loaded.
##    A zero result with Z previously clear must leave Z clear.
        mov     r2, #0
        mov     r5, #0
        add     r5, #3          ; r5 = 3, so Z is cleared and C is clear
        addc    r2, #0          ; result 0, but Z was clear, so Z stays clear
        jnb     psw.3, .Lsticky_ok
        or      r3, #4
.Lsticky_ok:
##    and a zero result with Z previously set must leave Z set.
        mov     r2, #0
        mov     r5, #0
        add     r5, #0          ; r5 = 0, so Z is set and C is clear
        addc    r2, #0          ; result 0 and Z was set
        jb      psw.3, .Lsticky2_ok
        or      r3, #32
.Lsticky2_ok:

## 3. BAND reports the two bits rather than the result: C is their AND and N
##    is their XOR.  Take one set bit and one clear bit.
        mov     r2, #1
        bset    r4.0
        bclr    r4.1
        band    r4.0, r4.1      ; 1 AND 0
        jnb     psw.1, .Lband_c_ok  ; C is the AND, so clear
        or      r3, #8
.Lband_c_ok:
        jb      psw.0, .Lband_n_ok  ; N is the XOR, so set
        or      r3, #16
.Lband_n_ok:

        mov     r2, r3
        exts    #0xFF, #1
        mov     0x0002, r2
