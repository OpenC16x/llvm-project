# REQUIRES: c166-registered-target
# RUN: llvm-mc -filetype=obj -triple=c166 %s -o %t.o
# RUN: llvm-objcopy -O binary %t.o %t.bin
# RUN: %c166_sim --binary %t.bin | FileCheck %s
# CHECK: hi

## A byte written to the console port comes out on stdout, which is how a
## program under test says anything.

        mov     r4, #0x0000
        movb    rl2, #0x68              ; 'h'
        exts    #0xFF, #1
        movb    [r4], rl2
        movb    rl2, #0x69              ; 'i'
        exts    #0xFF, #1
        movb    [r4], rl2
        movb    rl2, #0x0A              ; '\n'
        exts    #0xFF, #1
        movb    [r4], rl2
        mov     r2, #0
        exts    #0xFF, #1
        mov     0x0002, r2
