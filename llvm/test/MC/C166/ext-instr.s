; RUN: llvm-mc -triple=c166 -mcpu=c167 -show-encoding < %s | FileCheck %s
; RUN: llvm-mc -triple=c166 -show-encoding < %s | FileCheck %s
; RUN: not llvm-mc -triple=c166 -mcpu=c166 < %s 2>&1 \
; RUN:   | FileCheck %s --check-prefix=NOEXT

;; ATOMIC and the EXTend instructions are the second generation's.  A listing
;; for an 80C166 that contains one is not a listing of these instructions, it
;; is a listing of whatever that part does with an opcode it has never heard
;; of, so the assembler refuses rather than encoding it.
;;
;; The default is the second generation, which is what -mmcu= names and what
;; the startup code and linker script in this tree are written for.

; CHECK: exts r3, #1              ; encoding: [0xdc,0x03]
; NOEXT: [[@LINE+1]]:{{[0-9]+}}: error: instruction requires a feature the selected processor does not have
        exts    r3, #1

; CHECK: extp r3, #2              ; encoding: [0xdc,0x53]
; NOEXT: [[@LINE+1]]:{{[0-9]+}}: error: instruction requires a feature the selected processor does not have
        extp    r3, #2

; CHECK: extsr r3, #3             ; encoding: [0xdc,0xa3]
; NOEXT: [[@LINE+1]]:{{[0-9]+}}: error: instruction requires a feature the selected processor does not have
        extsr   r3, #3

; CHECK: extpr r3, #4             ; encoding: [0xdc,0xf3]
; NOEXT: [[@LINE+1]]:{{[0-9]+}}: error: instruction requires a feature the selected processor does not have
        extpr   r3, #4

; CHECK: exts #3, #1              ; encoding: [0xd7,0x00,0x03,0x00]
; NOEXT: [[@LINE+1]]:{{[0-9]+}}: error: instruction requires a feature the selected processor does not have
        exts    #3, #1

; CHECK: extp #3, #2              ; encoding: [0xd7,0x50,0x03,0x00]
; NOEXT: [[@LINE+1]]:{{[0-9]+}}: error: instruction requires a feature the selected processor does not have
        extp    #3, #2

; CHECK: extr #2                  ; encoding: [0xd1,0x90]
; NOEXT: [[@LINE+1]]:{{[0-9]+}}: error: instruction requires a feature the selected processor does not have
        extr    #2

; CHECK: atomic #4                ; encoding: [0xd1,0x30]
; NOEXT: [[@LINE+1]]:{{[0-9]+}}: error: instruction requires a feature the selected processor does not have
        atomic  #4

;; Everything either generation has still assembles for both, so the feature
;; gates what it should and nothing more.

; CHECK: mov r2, r3               ; encoding: [0xf0,0x23]
; NOEXT-NOT: error
        mov     r2, r3
