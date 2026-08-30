; RUN: llc -mtriple=c166 -O2 -verify-machineinstrs < %s | FileCheck %s

;; __builtin_mul_overflow, answered out of the flag the multiply already set.
;;
;; MUL and MULU set V when the product will not fit in a word, which is the
;; question being asked.  So the whole of the overflow test is a branch on
;; cc_V: no read of MDH, and for the signed case no sign extension of MDL to
;; compare against it.
;;
;; What has to hold for this to be right is that V is still the multiply's
;; when the branch reads it.  Only the move out of MDL sits in between, and a
;; move leaves V alone - its row in the manual is "* * - - *".  The CHECK-NEXT
;; run below is what pins that: anything inserted between the MUL and the JMPR
;; that is not that move fails here.

declare {i16, i1} @llvm.smul.with.overflow.i16(i16, i16)
declare {i16, i1} @llvm.umul.with.overflow.i16(i16, i16)

define i16 @smulo(i16 %a, i16 %b, ptr %p) {
; CHECK-LABEL: smulo:
; CHECK-NOT:     ashr
; CHECK:         mul r{{[0-9]+}}, r{{[0-9]+}}
; CHECK-NEXT:    mov r{{[0-9]+}}, mdl
; CHECK-NEXT:    jmpr cc_V,
  %r = call {i16, i1} @llvm.smul.with.overflow.i16(i16 %a, i16 %b)
  %v = extractvalue {i16, i1} %r, 0
  %o = extractvalue {i16, i1} %r, 1
  store i16 %v, ptr %p
  %z = zext i1 %o to i16
  ret i16 %z
}

define i16 @umulo(i16 %a, i16 %b, ptr %p) {
; CHECK-LABEL: umulo:
; CHECK-NOT:     mdh
; CHECK:         mulu r{{[0-9]+}}, r{{[0-9]+}}
; CHECK-NEXT:    mov r{{[0-9]+}}, mdl
; CHECK-NEXT:    jmpr cc_V,
  %r = call {i16, i1} @llvm.umul.with.overflow.i16(i16 %a, i16 %b)
  %v = extractvalue {i16, i1} %r, 0
  %o = extractvalue {i16, i1} %r, 1
  store i16 %v, ptr %p
  %z = zext i1 %o to i16
  ret i16 %z
}
