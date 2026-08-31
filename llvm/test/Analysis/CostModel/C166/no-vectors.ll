; REQUIRES: asserts
; RUN: opt < %s -passes=loop-vectorize -debug-only=loop-vectorize \
; RUN:   -disable-output 2>&1 | FileCheck %s --allow-empty --check-prefix=LV
; RUN: opt < %s -passes=slp-vectorizer -debug-only=SLP \
; RUN:   -disable-output 2>&1 | FileCheck %s --check-prefix=SLP

;; There are no vector registers, and the way a target says so is to answer
;; zero for the vector register class.  Both vectorisers read that and return
;; before doing anything, which is the point: without it they run in full on
;; every loop, cost each candidate, and arrive at "not beneficial" - the right
;; answer, reached the expensive way, on a machine that has no vector
;; instruction to select even if they had decided otherwise.
;;
;; The loop below is the easy case they would otherwise take furthest: unit
;; stride, no dependence to speak of, and a type two of which would fit in the
;; 32 bit register the default model believes in.

; LV-NOT: LV:
; SLP: Didn't find any vector registers for target

target datalayout = "e-m:e-p:16:16-p1:32:16-i32:16-i64:16-f32:16-f64:16-a:8-n8:16-S16"
target triple = "c166"

define void @add(ptr noalias %a, ptr noalias %b, ptr noalias %c, i16 %n) {
entry:
  %cmp = icmp sgt i16 %n, 0
  br i1 %cmp, label %body, label %exit

body:
  %i = phi i16 [ 0, %entry ], [ %inc, %body ]
  %pb = getelementptr inbounds i16, ptr %b, i16 %i
  %vb = load i16, ptr %pb, align 2
  %pc = getelementptr inbounds i16, ptr %c, i16 %i
  %vc = load i16, ptr %pc, align 2
  %sum = add i16 %vb, %vc
  %pa = getelementptr inbounds i16, ptr %a, i16 %i
  store i16 %sum, ptr %pa, align 2
  %inc = add nuw nsw i16 %i, 1
  %done = icmp eq i16 %inc, %n
  br i1 %done, label %exit, label %body

exit:
  ret void
}
