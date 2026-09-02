# REQUIRES: c166-registered-target
# RUN: llvm-mc -filetype=obj -triple=c166 -mcpu=xc16x %s -o %t.o
# RUN: llvm-objcopy -O binary %t.o %t.bin

## The coprocessor is a feature rather than a core: an ST10 may have one or
## not, which is why the compiler takes -mmac on top of -mcpu=st10.  The
## decoder here has to be told the same thing the same way, or it refuses the
## instructions the compiler just emitted for that part.

# RUN: %c166_sim --binary --mcpu=st10 --mattr=+mac --dump-state %t.bin 2>&1 \
# RUN:   | FileCheck %s
## 3 * 5 + 7 * 11 = 92, which is 5CH.
# CHECK: R6=005c

## Without it the same bytes are not an instruction on that part, and saying so
## is better than running something else.
# RUN: not %c166_sim --binary --mcpu=st10 %t.bin 2>&1 \
# RUN:   | FileCheck %s --check-prefix=NOMAC
# NOMAC: error: cannot decode the bytes at

## The default part has one, so it needs no help.
# RUN: %c166_sim --binary --dump-state %t.bin 2>&1 | FileCheck %s

        .text
        jmps    #0, 0x20
        .org    0x20
        mov     r2, #3
        mov     r3, #5
        mov     r4, #7
        mov     r5, #11
        comul   r2, r3
        comac   r4, r5
        costore r6, mal
        mov     r5, #0
        exts    #0xFF, #1
        mov     0x0002, r5
