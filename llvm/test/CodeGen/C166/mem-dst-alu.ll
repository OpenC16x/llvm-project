; RUN: llc -mtriple=c166 -O2 < %s | FileCheck %s

; A load, an operation and a store back are one instruction when they all name
; the same place.  What was a four byte load, a two byte operation and a four
; byte store is four bytes.

@g = global i16 0
@b = global i8 0
@other = global i16 0

define void @word(i16 %x) {
; CHECK-LABEL: word:
; CHECK:      add g, r2
; CHECK-NEXT: ret
  %v = load i16, ptr @g
  %r = add i16 %v, %x
  store i16 %r, ptr @g
  ret void
}

define void @byte(i8 %x) {
; CHECK-LABEL: byte:
; CHECK:      orb b, rl2
; CHECK-NEXT: ret
  %v = load i8, ptr @b
  %r = or i8 %v, %x
  store i8 %r, ptr @b
  ret void
}

; A volatile word keeps both of its accesses either way: the instruction reads
; it and writes it back, in that order.
define void @volatile_word(i16 %x) {
; CHECK-LABEL: volatile_word:
; CHECK:      and g, r2
; CHECK-NEXT: ret
  %v = load volatile i16, ptr @g
  %r = and i16 %v, %x
  store volatile i16 %r, ptr @g
  ret void
}

; The result is wanted afterwards, so it has to exist in a register and the
; store cannot swallow the operation that produced it.
define i16 @result_reused(i16 %x) {
; CHECK-LABEL: result_reused:
; CHECK:      add r2, g
; CHECK-NEXT: mov g, r2
  %v = load i16, ptr @g
  %r = add i16 %v, %x
  store i16 %r, ptr @g
  ret i16 %r
}

; Read one place, write another: two accesses rather than one.
define void @different_places(i16 %x) {
; CHECK-LABEL: different_places:
; CHECK:      add r2, other
; CHECK:      mov g, r2
  %v = load i16, ptr @other
  %r = add i16 %v, %x
  store i16 %r, ptr @g
  ret void
}

; The carry forms are deliberately left out.  ADDC's carry out is a result the
; rest of a wide chain reads, and folding one into a store would leave that
; result with nothing to say where the chain continues - so a 32 bit add
; through memory stays a pair of register operations with a store each.
define void @wide_add(i32 %x) {
; CHECK-LABEL: wide_add:
; CHECK-NOT:  addc g, r
  %v = load i32, ptr @g
  %r = add i32 %v, %x
  store i32 %r, ptr @g
  ret void
}
