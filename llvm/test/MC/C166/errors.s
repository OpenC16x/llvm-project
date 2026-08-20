; RUN: not llvm-mc -triple=c166 %s 2>&1 | FileCheck %s

; CHECK: [[@LINE+1]]:{{[0-9]+}}: error: expected '#' before a displacement
        mov     r2, [r3+4]

; CHECK: [[@LINE+1]]:{{[0-9]+}}: error: expected ']'
        mov     r2, [r3+#4

; CHECK: [[@LINE+1]]:{{[0-9]+}}: error: invalid condition code
        jmpa    cc_BOGUS, foo

; CHECK: [[@LINE+1]]:{{[0-9]+}}: error: invalid instruction mnemonic
        frobnicate r1

; A shift count has to fit in four bits, and is rejected rather than truncated.
; CHECK: [[@LINE+1]]:{{[0-9]+}}: error: expected an immediate in the range [0, 15]
        shl     r2, #99

; The indirect call form wants a register in brackets.
; CHECK: [[@LINE+1]]:{{[0-9]+}}: error: expected a register
        calli   cc_UC, [1234]

; The instruction range of an extend instruction is written 1 to 4, so the
; encodable range 0 to 3 is not what the assembler accepts.
; CHECK: [[@LINE+1]]:{{[0-9]+}}: error: expected an instruction range in [1, 4]
        exts    r5, #0
; CHECK: [[@LINE+1]]:{{[0-9]+}}: error: expected an instruction range in [1, 4]
        atomic  #5

; A segment is eight bits and a page is ten.
; CHECK: [[@LINE+1]]:{{[0-9]+}}: error: expected an 8 bit segment number
        exts    #256, #1
; CHECK: [[@LINE+1]]:{{[0-9]+}}: error: expected a 10 bit page number
        extp    #1024, #1
