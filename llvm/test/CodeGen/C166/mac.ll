; RUN: llc -mtriple=c166 -mattr=+mac -O2 < %s | FileCheck %s --check-prefix=MAC
; RUN: llc -mtriple=c166 -O2 < %s | FileCheck %s --check-prefix=NOMAC

; "acc += a * b" with signed words onto a 32 bit accumulator.  The multiply is
; ten states and CoMAC is two, so this wins even with the accumulator loaded
; and stored around every one - eight states against eighteen.

define i32 @mac(i32 %acc, i16 %a, i16 %b) {
; MAC-LABEL: mac:
; MAC:         coload r2, r3
; MAC-NEXT:    comac r4, r5
; MAC-NEXT:    costore r2, mal
; MAC-NEXT:    costore r3, mah
; MAC-NOT:     mul
;
; NOMAC-LABEL: mac:
; NOMAC:         mul r4, r5
; NOMAC-NOT:     comac
  %sa = sext i16 %a to i32
  %sb = sext i16 %b to i32
  %m = mul i32 %sa, %sb
  %r = add i32 %acc, %m
  ret i32 %r
}

; The multiply has to feed nothing else.  Here the product is returned as well
; as accumulated, so a MAC would have to compute it twice.

define i32 @product_used_twice(i32 %acc, i16 %a, i16 %b, ptr %out) {
; MAC-LABEL: product_used_twice:
; MAC:         mul
; MAC-NOT:     comac
  %sa = sext i16 %a to i32
  %sb = sext i16 %b to i32
  %m = mul i32 %sa, %sb
  store i32 %m, ptr %out
  %r = add i32 %acc, %m
  ret i32 %r
}

; An unsigned widening multiply is a different instruction, and the unit has
; it: which of SMUL_LOHI and UMUL_LOHI the type legalizer built is what picks
; between CoMAC and CoMACu.

define i32 @unsigned_mac(i32 %acc, i16 %a, i16 %b) {
; MAC-LABEL: unsigned_mac:
; MAC:         coload r2, r3
; MAC-NEXT:    comacu r4, r5
; MAC-NEXT:    costore r2, mal
; MAC-NEXT:    costore r3, mah
; MAC-NOT:     mul
;
; NOMAC-LABEL: unsigned_mac:
; NOMAC:         mul r4, r5
; NOMAC-NOT:     comacu
  %za = zext i16 %a to i32
  %zb = zext i16 %b to i32
  %m = mul i32 %za, %zb
  %r = add i32 %acc, %m
  ret i32 %r
}

; "acc -= a * b" is the same shape in SUBC and SUBE, and the unit has the
; negating form of each multiply.

define i32 @mac_subtract(i32 %acc, i16 %a, i16 %b) {
; MAC-LABEL: mac_subtract:
; MAC:         coload r2, r3
; MAC-NEXT:    comac- r4, r5
; MAC-NEXT:    costore r2, mal
; MAC-NEXT:    costore r3, mah
; MAC-NOT:     mul
  %sa = sext i16 %a to i32
  %sb = sext i16 %b to i32
  %m = mul i32 %sa, %sb
  %r = sub i32 %acc, %m
  ret i32 %r
}

define i32 @unsigned_mac_subtract(i32 %acc, i16 %a, i16 %b) {
; MAC-LABEL: unsigned_mac_subtract:
; MAC:         coload r2, r3
; MAC-NEXT:    comacu- r4, r5
; MAC-NEXT:    costore r2, mal
; MAC-NEXT:    costore r3, mah
; MAC-NOT:     mul
  %za = zext i16 %a to i32
  %zb = zext i16 %b to i32
  %m = mul i32 %za, %zb
  %r = sub i32 %acc, %m
  ret i32 %r
}

; Subtraction does not commute, so the product has to be what is taken away.
; "a * b - acc" is not a multiply-accumulate and must not become one.

define i32 @product_minus_acc(i32 %acc, i16 %a, i16 %b) {
; MAC-LABEL: product_minus_acc:
; MAC:         mul r4, r5
; MAC-NOT:     comac
  %sa = sext i16 %a to i32
  %sb = sext i16 %b to i32
  %m = mul i32 %sa, %sb
  %r = sub i32 %m, %acc
  ret i32 %r
}

; One operand sign extended and the other zero extended is a mixed sign
; product.  There is no widening multiply node for one, and the legalizer does
; not build a signed pair either - a value zero extended from a word has
; sixteen sign bits, one short of what that check wants - so CoMACsu has no
; shape here to match.

define i32 @mixed_sign_mac(i32 %acc, i16 %a, i16 %b) {
; MAC-LABEL: mixed_sign_mac:
; MAC-NOT:     comac
  %sa = sext i16 %a to i32
  %zb = zext i16 %b to i32
  %m = mul i32 %sa, %zb
  %r = add i32 %acc, %m
  ret i32 %r
}
