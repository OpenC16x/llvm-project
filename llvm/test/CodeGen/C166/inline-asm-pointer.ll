; RUN: llc -mtriple=c166 -O2 < %s | FileCheck %s

; The pointer field of an indirect ALU form is two bits wide, so "add Rwn,
; [Rwm]" can only name R0 to R3.  Asking for the pointer with "r" gets whatever
; register was spare and assembles only by luck, which is what the "q"
; constraint is for.

define i16 @indirect(ptr %p, i16 %a) {
; CHECK-LABEL: indirect:
; CHECK:         add r{{[0-9]+}}, [r{{[0-3]}}]
  %r = call i16 asm "add $0, [$1]", "=r,q,0"(ptr %p, i16 %a)
  ret i16 %r
}

; Under enough pressure that the allocator would rather have used a high
; register, the constraint still holds.
define i16 @indirect_pressure(ptr %p, i16 %a, i16 %b, i16 %c, i16 %d, i16 %e) {
; CHECK-LABEL: indirect_pressure:
; CHECK:         add r{{[0-9]+}}, [r{{[0-3]}}]
  %r = call i16 asm "add $0, [$1]", "=r,q,0"(ptr %p, i16 %a)
  %s = add i16 %r, %b
  %t = add i16 %s, %c
  %u = add i16 %t, %d
  %v = add i16 %u, %e
  ret i16 %v
}
