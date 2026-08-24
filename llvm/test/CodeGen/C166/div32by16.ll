; RUN: llc -mtriple=c166 -O2 < %s | FileCheck %s

; A 32 bit division whose divisor is known to fit in a word is two divides
; rather than a call to __udivsi3.  The high word goes through DIVU, whose
; remainder DIVLU then takes together with the low word - so nothing moves
; the remainder between them, and neither divide can overflow.

define i32 @udiv32by16(i32 %a, i16 %b) {
; CHECK-LABEL: udiv32by16:
; CHECK-NOT:     __udivsi3
; CHECK:         mov mdl, r3
; CHECK-NEXT:    divu r4
; CHECK-NEXT:    mov [[QHI:r[0-9]+]], mdl
; CHECK-NEXT:    mov mdl, r2
; CHECK-NEXT:    divlu r4
  %z = zext i16 %b to i32
  %r = udiv i32 %a, %z
  ret i32 %r
}

; Only the remainder wanted, so neither half of the quotient is read out.

define i32 @urem32by16(i32 %a, i16 %b) {
; CHECK-LABEL: urem32by16:
; CHECK-NOT:     __umodsi3
; CHECK:         mov mdl, r3
; CHECK-NEXT:    divu r4
; CHECK-NEXT:    mov mdl, r2
; CHECK-NEXT:    divlu r4
; CHECK-NEXT:    mov r2, mdh
  %z = zext i16 %b to i32
  %r = urem i32 %a, %z
  ret i32 %r
}

; A divisor that is 16 bits wide because it was masked, not because it was
; extended, takes the same path: what matters is that the high word is zero.

define i32 @udiv32masked(i32 %a, i32 %b) {
; CHECK-LABEL: udiv32masked:
; CHECK-NOT:     __udivsi3
; CHECK:         divlu
  %m = and i32 %b, 65535
  %r = udiv i32 %a, %m
  ret i32 %r
}

; A divisor that really is 32 bits keeps the library call: DIVLU on a general
; 32 bit dividend can overflow, and nothing here can rule that out.

define i32 @udiv32general(i32 %a, i32 %b) {
; CHECK-LABEL: udiv32general:
; CHECK:         calla cc_UC, __udivsi3
; CHECK-NOT:     divlu
  %r = udiv i32 %a, %b
  ret i32 %r
}

; Signed division by a word is not done this way: the two step decomposition
; is an unsigned argument, so the call stays.

define i32 @sdiv32by16(i32 %a, i16 %b) {
; CHECK-LABEL: sdiv32by16:
; CHECK:         calla cc_UC, __divsi3
  %z = sext i16 %b to i32
  %r = sdiv i32 %a, %z
  ret i32 %r
}
