; RUN: llc -mtriple=c166 -mcpu=xc16x -mattr=+mac -verify-machineinstrs < %s \
; RUN:   | FileCheck %s

; The coprocessor's four offset registers are in the extended register space,
; which has no "push mem" and no way to name one through a "reg" field without
; an EXTR in front of it.  Until there was one they could not be saved at all,
; so an interrupt handler that walked a stream by a stride left the interrupted
; code's stride behind - a silent wrong answer in whatever it interrupted.
;
; They go on the same terms MRW and MCW are on: saved where this handler writes
; them, not merely because it makes a call.  What writes them is the repeat
; expansion in C166InstrInfo.cpp, and its pseudo says so.

@dp = global [64 x i16] zeroinitializer, align 2 #0
@pl = global [64 x i16] zeroinitializer, align 2
@udp = global [64 x i16] zeroinitializer, align 2 #0
@upl = global [64 x i16] zeroinitializer, align 2
@n = global i16 0, align 2
@g = global [64 x i16] zeroinitializer, align 2 #0
@accum = global i32 0

define void @strided_isr() #1 {
; The accumulator first, then the two offset registers, and the mirror on the
; way out.
; CHECK-LABEL: strided_isr:
; CHECK:         push msw
; CHECK-NEXT:    push mal
; CHECK-NEXT:    push mah
; CHECK-NEXT:    push qx0
; CHECK-NEXT:    push qr0
;
; And these are what made them worth saving.
; CHECK:         mov qx0, r5
; CHECK:         mov qr0, r5
;
; CHECK:         pop qr0
; CHECK-NEXT:    pop qx0
; CHECK-NEXT:    pop mah
; CHECK-NEXT:    pop mal
; CHECK-NEXT:    pop msw
; CHECK:         reti
entry:
  br label %for.body

for.cond.cleanup:                                 ; preds = %for.body
  store i32 %add, ptr @accum
  ret void

for.body:                                         ; preds = %entry, %for.body
  %i.011 = phi i16 [ 0, %entry ], [ %inc, %for.body ]
  %a.010 = phi i32 [ 0, %entry ], [ %add, %for.body ]
  %arrayidx.idx = shl nuw nsw i16 %i.011, 3
  %arrayidx = getelementptr inbounds nuw i8, ptr @g, i16 %arrayidx.idx
  %0 = load i16, ptr %arrayidx, align 2
  %conv = sext i16 %0 to i32
  %arrayidx3.idx = mul nuw nsw i16 %i.011, 6
  %arrayidx3 = getelementptr inbounds nuw i8, ptr @pl, i16 %arrayidx3.idx
  %1 = load i16, ptr %arrayidx3, align 2
  %conv4 = sext i16 %1 to i32
  %mul5 = mul nsw i32 %conv4, %conv
  %add = add nsw i32 %mul5, %a.010
  %inc = add nuw nsw i16 %i.011, 1
  %exitcond.not = icmp eq i16 %inc, 16
  br i1 %exitcond.not, label %for.cond.cleanup, label %for.body
}

attributes #0 = { "c166-dpram" }
attributes #1 = { "interrupt"="0" }
