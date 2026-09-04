## A program for the stub tests to stop in.  It does nothing: what is under
## test is the conversation rather than what the machine computes.
        .text
        .globl  _start
_start:
        nop
        nop
        mov     r5, #0
        exts    #0xFF, #1
        mov     0x0002, r5
