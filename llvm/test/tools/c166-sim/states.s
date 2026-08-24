# REQUIRES: c166-registered-target
# RUN: llvm-mc -filetype=obj -triple=c166 %s -o %t.o
# RUN: llvm-objcopy -O binary %t.o %t.bin
# RUN: %c166_sim --binary --count-states %t.bin 2>&1 | FileCheck %s

## The state counts, worked out by hand from Table 11 of the instruction set
## manual so that the simulator is checked against the document rather than
## against itself.  Every instruction below is named in that table or falls
## under its last row, "all other instructions: 2".
##
##   jmps  #0, 0x20        4   JMPS is always four
##   mov   r1, #4          2   all other instructions
##   mov   r2, #7          2
##   mul   r1, r2         10   MUL, MULU
##   mov   mdl, r1         2
##   div   r2             20   DIV, DIVL, DIVU, DIVLU
##   mov   r3, mdl         2
##   loop: sub r1, #1      2   four times round, so eight
##   jmpr  cc_NE, loop         taken three times at 4, not taken once at 2,
##                             and one state more every time because the SUB
##                             wrote PSW immediately before it (section 7.3,
##                             "Testing Branch Conditions")
##   mov   r5, #0          2
##   exts  #0xFF, #1       2
##   mov   0x0002, r5      2   the write to the exit port stops the program
##
## Instructions: 1 + 2 + 1 + 1 + 1 + 1 + 4 + 4 + 3 = 18
## States: 4 + 2 + 2 + 10 + 2 + 20 + 2          = 42
##         + 4 x 2 for the SUB                  =  8
##         + 3 x (4 + 1) + 1 x (2 + 1)          = 18
##         + 2 + 2 + 2                          =  6
##         + 6 for the solitary pipeline fill   =  6
##                                                80

# CHECK: instructions 18  states 80

        .text
        jmps    #0, 0x20

        .org    0x20
        mov     r1, #4
        mov     r2, #7
        mul     r1, r2
        mov     mdl, r1
        div     r2
        mov     r3, mdl
loop:
        sub     r1, #1
        jmpr    cc_NE, loop
        mov     r5, #0
        exts    #0xFF, #1
        mov     0x0002, r5
