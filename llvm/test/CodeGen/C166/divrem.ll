; RUN: llc -mtriple=c166 -O2 < %s | FileCheck %s

; One DIV answers both questions.  The generic combiner will not pair them
; when plain division is legal, so the target does it: DIV is the slowest
; instruction on the core and leaves the remainder in MDH either way.

define i16 @udivrem(i16 %a, i16 %b) {
; CHECK-LABEL: udivrem:
; CHECK:         mov mdl, r2
; CHECK-NEXT:    divu r3
; CHECK-NEXT:    mov [[Q:r[0-9]+]], mdl
; CHECK-NEXT:    mov [[R:r[0-9]+]], mdh
; CHECK-NOT:     divu
  %q = udiv i16 %a, %b
  %r = urem i16 %a, %b
  %s = add i16 %q, %r
  ret i16 %s
}

define i16 @sdivrem(i16 %a, i16 %b) {
; CHECK-LABEL: sdivrem:
; CHECK:         div r3
; CHECK-NOT:     div r3
  %q = sdiv i16 %a, %b
  %r = srem i16 %a, %b
  %s = add i16 %q, %r
  ret i16 %s
}

; Only one half wanted: still one divide, and the move that would read the
; other half is not made at all.

define i16 @justdiv(i16 %a, i16 %b) {
; CHECK-LABEL: justdiv:
; CHECK:         divu r3
; CHECK-NEXT:    mov r2, mdl
; CHECK-NEXT:    ret
  %q = udiv i16 %a, %b
  ret i16 %q
}

define i16 @justrem(i16 %a, i16 %b) {
; CHECK-LABEL: justrem:
; CHECK:         divu r3
; CHECK-NEXT:    mov r2, mdh
; CHECK-NEXT:    ret
  %r = urem i16 %a, %b
  ret i16 %r
}
