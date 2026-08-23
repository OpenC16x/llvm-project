; RUN: llc -mtriple=c166 -O2 < %s | FileCheck %s

; Setting or clearing one bit of a value the register allocator has placed is a
; single two byte instruction: bitoff F0H + n names R0 to R15, so the register
; is the bit-addressable word.  Anything touching more than one bit stays an
; ALU operation.

define i16 @set_low(i16 %x) {
; CHECK-LABEL: set_low:
; CHECK: bset r2.0
  %r = or i16 %x, 1
  ret i16 %r
}

; Bit 8 does not fit #data3, so without this it is the four byte #data16 form.
define i16 @set_mid(i16 %x) {
; CHECK-LABEL: set_mid:
; CHECK: bset r2.8
  %r = or i16 %x, 256
  ret i16 %r
}

; The mask arrives sign extended - bit 15 alone is -32768 - which is why the
; predicate and the transform both work on the truncated value.
define i16 @set_high(i16 %x) {
; CHECK-LABEL: set_high:
; CHECK: bset r2.15
  %r = or i16 %x, 32768
  ret i16 %r
}

; Clearing is an "and" with fifteen bits set, so it never fits a short constant
; and the bit form is always the smaller one.
define i16 @clear(i16 %x) {
; CHECK-LABEL: clear:
; CHECK: bclr r2.9
  %r = and i16 %x, -513
  ret i16 %r
}

define i16 @two_bits(i16 %x) {
; CHECK-LABEL: two_bits:
; CHECK: or r2, #768
  %r = or i16 %x, 768
  ret i16 %r
}

; The instruction reads and writes the same register, so a source that is still
; needed afterwards has to survive - here by storing it first.
define i16 @live_after(i16 %x, ptr %p) {
; CHECK-LABEL: live_after:
; CHECK: mov [r3], r2
; CHECK: bset r2.4
  %r = or i16 %x, 16
  store i16 %x, ptr %p
  ret i16 %r
}
