; RUN: llc -mtriple=c166 -verify-machineinstrs < %s | FileCheck %s

; The same dot product on a part without the coprocessor.  There is no
; instruction to repeat and no IDX0 to walk with, so the loop stays a loop -
; and __dpram, which is a placement and not a promise about the core, does not
; on its own make one of these.

; CHECK-LABEL: dot8:
; CHECK-NOT: comac
; CHECK-NOT: idx0
; CHECK: jmpr

@dp = global [64 x i16] zeroinitializer, align 2 #0
@pl = global [64 x i16] zeroinitializer, align 2

define i32 @dot8() {
entry:
  br label %for.body

for.cond.cleanup:
  ret i32 %add

for.body:
  %i = phi i16 [ 0, %entry ], [ %inc, %for.body ]
  %a = phi i32 [ 0, %entry ], [ %add, %for.body ]
  %px = getelementptr inbounds nuw [2 x i8], ptr @dp, i16 %i
  %x = load i16, ptr %px, align 2
  %sx = sext i16 %x to i32
  %py = getelementptr inbounds nuw [2 x i8], ptr @pl, i16 %i
  %y = load i16, ptr %py, align 2
  %sy = sext i16 %y to i32
  %mul = mul nsw i32 %sy, %sx
  %add = add nsw i32 %mul, %a
  %inc = add nuw nsw i16 %i, 1
  %done = icmp eq i16 %inc, 8
  br i1 %done, label %for.cond.cleanup, label %for.body
}

attributes #0 = { "c166-dpram" }
