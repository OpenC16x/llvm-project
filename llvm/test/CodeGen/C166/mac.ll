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

; An unsigned widening multiply is a different instruction - CoMAC multiplies
; signed - so it is left alone.

define i32 @unsigned_mac(i32 %acc, i16 %a, i16 %b) {
; MAC-LABEL: unsigned_mac:
; MAC-NOT:     comac
  %za = zext i16 %a to i32
  %zb = zext i16 %b to i32
  %m = mul i32 %za, %zb
  %r = add i32 %acc, %m
  ret i32 %r
}
