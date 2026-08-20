; RUN: llvm-mc -triple=c166 -filetype=obj < %s -o %t.o
; RUN: llvm-objdump -d --triple=c166 %t.o | FileCheck %s

; An operator over an address the assembler already knows is folded rather
; than relocated.  0x123456 is segment 0x12, offset 0x3456, page 0x48.

; CHECK: d7 00 12 00 exts #18, #1
        exts    #seg(0x123456), #1
; CHECK: f2 f2 56 34 mov r2, 13398
        mov     r2, sof(0x123456)
; CHECK: d7 40 48 00 extp #72, #1
        extp    #pag(0x123456), #1
; CHECK: f2 f3 56 34 mov r3, 13398
        mov     r3, pof(0x123456)
