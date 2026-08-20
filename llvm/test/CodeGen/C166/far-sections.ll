; RUN: llc -mtriple=c166 -verify-machineinstrs < %s | FileCheck %s

; Far objects and far functions only pay for themselves if the linker can put
; them outside segment 0, so they get sections of their own.  A section the
; user asked for still wins.

@fd = addrspace(1) global i16 7
@fb = addrspace(1) global i16 0
@fr = addrspace(1) constant i16 9
@fu = addrspace(1) global i16 3, section ".mine"
@nd = global i16 7

define void @f() "far" {
  ret void
}

define void @n() {
  ret void
}

; CHECK:      .section .fartext,"ax",@progbits
; CHECK:      f:
; CHECK:      .text
; CHECK:      n:
; CHECK:      .section .fardata,"aw",@progbits
; CHECK:      fd:
; CHECK:      .section .farbss,"aw",@nobits
; CHECK:      fb:
; CHECK:      .section .farrodata,"a",@progbits
; CHECK:      fr:
; CHECK:      .section .mine,"aw",@progbits
; CHECK:      fu:
; CHECK:      .data
; CHECK:      nd:
