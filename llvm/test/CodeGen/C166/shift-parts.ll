; RUN: llc -mtriple=c166 -O2 < %s | FileCheck %s

; A shift of a value two words wide by an amount only known at run time is a
; handful of instructions rather than a call.  That matters beyond the speed:
; code placed in the PSRAM cannot reach the compiler-rt builtins, which are in
; the flash and a segment away, so a variable shift used to be something such
; code had to be written around.

; The crossing bits are brought over by shifting by "15 - amount" and then once
; more, rather than by "16 - amount".  The shift instructions read their count
; as its low four bits, so a shift by sixteen is a shift by nothing: at an
; amount of zero the direct form would contribute the whole word instead of
; none of it.  Two shifts get it right there without a test for it.
define i32 @shl(i32 %v, i16 %n) {
; CHECK-LABEL: shl:
; CHECK-NOT:   __ashlsi3
; CHECK:       and r{{[0-9]+}}, #16
; CHECK:       mov r{{[0-9]+}}, #15
; CHECK:       sub r{{[0-9]+}}, r{{[0-9]+}}
; CHECK:       shr r{{[0-9]+}}, r{{[0-9]+}}
; CHECK-NEXT:  shr r{{[0-9]+}}, #1
  %a = zext i16 %n to i32
  %r = shl i32 %v, %a
  ret i32 %r
}

define i32 @lshr(i32 %v, i16 %n) {
; CHECK-LABEL: lshr:
; CHECK-NOT:   __lshrsi3
; CHECK:       shl r{{[0-9]+}}, r{{[0-9]+}}
; CHECK-NEXT:  shl r{{[0-9]+}}, #1
  %a = zext i16 %n to i32
  %r = lshr i32 %v, %a
  ret i32 %r
}

; The arithmetic one differs in two places: the high half comes down with ASHR,
; and when the shift is wide enough to empty it what is left above is the sign,
; which is the high half shifted right by fifteen.
define i32 @ashr(i32 %v, i16 %n) {
; CHECK-LABEL: ashr:
; CHECK-NOT:   __ashrsi3
; CHECK:       ashr r{{[0-9]+}}, r{{[0-9]+}}
; CHECK:       ashr r{{[0-9]+}}, #15
  %a = zext i16 %n to i32
  %r = ashr i32 %v, %a
  ret i32 %r
}

; A constant amount was always inline, and stays the cheaper sequence: no test
; for a wide shift and no crossing arithmetic, because both are known.
define i32 @shl_const(i32 %v) {
; CHECK-LABEL: shl_const:
; CHECK-NOT:   __ashlsi3
; CHECK-NOT:   #16
; CHECK:       shl r{{[0-9]+}}, #4
  %r = shl i32 %v, 4
  ret i32 %r
}
