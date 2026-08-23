; RUN: llvm-mc -triple=c166 -filetype=obj %s -o %t.o
; RUN: llvm-objdump -d -r %t.o | FileCheck %s
; RUN: llvm-nm %t.o | FileCheck %s --check-prefix=SYM

;; A register name is not something a symbol can be called here, because the
;; assembler reads it as the register before it considers a symbol: "mov r2, t2"
;; is a load from the T2 timer at FE40H and there is nothing to say otherwise,
;; since a symbol may be defined after it is used.
;;
;; So a symbol that would collide is written quoted.  A quoted name reaches the
;; parser as a string rather than an identifier and can only be a symbol, which
;; makes the two spellings say different things on purpose.  The compiler quotes
;; such a name for exactly this reason; see C166MCTargetDesc.cpp.

        .text
;; Bare: the special function register, resolved to its address with no
;; relocation left behind.
; CHECK: mov r2, mdl
        mov     r2, mdl
; CHECK-NEXT: mov r3, t2
        mov     r3, t2

;; Quoted: the symbol, with a relocation.
; CHECK-NEXT: mov r4, 0
; CHECK-NEXT: R_C166_SOF16 t2
        mov     r4, "t2"

;; The same for a general purpose register, the stack pointer and a condition
;; code, all of which the parser would otherwise take for what they are named
;; after.  cc_z is one of the four conditions the architecture spells two ways.
; CHECK: mov r5, 0
; CHECK-NEXT: R_C166_SOF16 r5
        mov     r5, "r5"
; CHECK: mov r6, 0
; CHECK-NEXT: R_C166_SOF16 sp
        mov     r6, "sp"
; CHECK: mov r7, 0
; CHECK-NEXT: R_C166_SOF16 cc_z
        mov     r7, "cc_z"

        .data
        .globl "t2"
"t2":   .short 0
        .globl "r5"
"r5":   .short 0
        .globl "sp"
"sp":   .short 0
        .globl "cc_z"
"cc_z": .short 0

;; The names in the object file are the plain ones: the quotes are how the
;; assembly says which of the two meanings it wants, not part of the name.
; SYM: D cc_z
; SYM: D r5
; SYM: D sp
; SYM: D t2
