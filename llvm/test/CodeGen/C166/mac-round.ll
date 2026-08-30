; RUN: llc -mtriple=c166 -mattr=+mac -O2 < %s | FileCheck %s --check-prefix=MAC
; RUN: llc -mtriple=c166 -O2 < %s | FileCheck %s --check-prefix=NOMAC

; "(a * b + 0x8000) >> 16" is a fixed point product rounded to its high word.
; The coprocessor's rounding forms add 00 0000 8000H to the accumulator and
; clear MAL, so MAH alone is the answer: two instructions where the accumulate
; onto a materialised constant that this used to become is six.

define i16 @round_signed(i16 %a, i16 %b) {
; MAC-LABEL: round_signed:
; MAC:         comul r2, r3, rnd
; MAC-NEXT:    costore r2, mah
; MAC-NOT:     coload
; MAC-NOT:     costore r{{[0-9]+}}, mal
;
; NOMAC-LABEL: round_signed:
; NOMAC:         mul r2, r3
; NOMAC-NOT:     comul
  %sa = sext i16 %a to i32
  %sb = sext i16 %b to i32
  %m = mul i32 %sa, %sb
  %r = add i32 %m, 32768
  %s = ashr i32 %r, 16
  %t = trunc i32 %s to i16
  ret i16 %t
}

; The bits MAH holds are the same whichever way the shift was written, so a
; logical shift right reaches the same instruction.

define i16 @round_unsigned(i16 %a, i16 %b) {
; MAC-LABEL: round_unsigned:
; MAC:         comulu r2, r3, rnd
; MAC-NEXT:    costore r2, mah
; MAC-NOT:     coload
;
; NOMAC-LABEL: round_unsigned:
; NOMAC:         mulu r2, r3
; NOMAC-NOT:     comulu
  %za = zext i16 %a to i32
  %zb = zext i16 %b to i32
  %m = mul i32 %za, %zb
  %r = add i32 %m, 32768
  %s = lshr i32 %r, 16
  %t = trunc i32 %s to i16
  ret i16 %t
}

; The rounding instruction clears MAL, so it can only stand in where the low
; word is thrown away.  Here the whole sum is wanted and it must not.

define i32 @round_keeping_low(i16 %a, i16 %b) {
; MAC-LABEL: round_keeping_low:
; MAC:         costore r{{[0-9]+}}, mal
; MAC-NOT:     rnd
  %sa = sext i16 %a to i32
  %sb = sext i16 %b to i32
  %m = mul i32 %sa, %sb
  %r = add i32 %m, 32768
  ret i32 %r
}

; A different constant is a different answer, and only 0x8000 is the one the
; instruction adds.

define i16 @round_wrong_constant(i16 %a, i16 %b) {
; MAC-LABEL: round_wrong_constant:
; MAC-NOT:     rnd
  %sa = sext i16 %a to i32
  %sb = sext i16 %b to i32
  %m = mul i32 %sa, %sb
  %r = add i32 %m, 16384
  %s = ashr i32 %r, 16
  %t = trunc i32 %s to i16
  ret i16 %t
}

; An accumulator in play is not this shape and must not become it.  The unit's
; accumulator is forty bits, so a sum that a 32 bit one would have wrapped is
; still whole in there, and reading MAH with no CoSTORE of MAL to truncate
; through would not be what the program asked for.

define i16 @round_with_accumulator(i32 %acc, i16 %a, i16 %b) {
; MAC-LABEL: round_with_accumulator:
; MAC:         coload
; MAC-NOT:     rnd
  %sa = sext i16 %a to i32
  %sb = sext i16 %b to i32
  %m = mul i32 %sa, %sb
  %s1 = add i32 %acc, %m
  %r = add i32 %s1, 32768
  %s = ashr i32 %r, 16
  %t = trunc i32 %s to i16
  ret i16 %t
}
