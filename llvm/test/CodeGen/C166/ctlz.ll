; RUN: llc -mtriple=c166 -O2 < %s | FileCheck %s

; PRIOR reports how far the leftmost set bit is from the top, which is the
; count of leading zeroes.  It used to be a twenty five instruction bit smear
; and a population count.
;
; It leaves zero behind for a source of zero, where CTLZ wants sixteen, so only
; the poison-on-zero form is the instruction on its own; the full one is that
; instruction and a test, which is still most of the sequence gone.

define i16 @clz_nonzero(i16 %x) {
; CHECK-LABEL: clz_nonzero:
; CHECK:      prior r2, r2
; CHECK-NEXT: ret
  %r = call i16 @llvm.ctlz.i16(i16 %x, i1 true)
  ret i16 %r
}

define i16 @clz_any(i16 %x) {
; CHECK-LABEL: clz_any:
; CHECK-NOT:  shr r{{[0-9]+}}, #8
; CHECK:      prior
  %r = call i16 @llvm.ctlz.i16(i16 %x, i1 false)
  ret i16 %r
}

declare i16 @llvm.ctlz.i16(i16, i1)
