;; A global whose name is a register's is written quoted, because unquoted it
;; would not mean itself.  The assembler reads "t2" as the T2 timer at FE40H
;; before it considers a symbol, and it has no way to know better: a symbol may
;; be defined after it is used, so there is nothing to consult at the point the
;; name is read.
;;
;; Left alone that was silent and wrong in a way that matters - compiling this
;; to assembly and assembling that back produced a load from FE40H with no
;; relocation and no diagnostic, so the program went through the compiler
;; unharmed and through the assembler broken.  The quotes are what make the two
;; spellings say different things; see C166MCTargetDesc.cpp.
;;
; RUN: llc -mtriple=c166 < %s | FileCheck %s
;; And the whole point of them: what comes out assembles back to what went in.
; RUN: llc -filetype=obj -mtriple=c166 < %s -o %t.direct.o
; RUN: llc -mtriple=c166 < %s | llvm-mc -triple=c166 -filetype=obj -o %t.viaasm.o
;; The file name is the only thing that legitimately differs between the two.
; RUN: llvm-objdump -d -r %t.direct.o | grep -v "file format" > %t.direct.txt
; RUN: llvm-objdump -d -r %t.viaasm.o | grep -v "file format" > %t.viaasm.txt
; RUN: diff %t.direct.txt %t.viaasm.txt
; RUN: llvm-nm %t.direct.o > %t.direct.nm
; RUN: llvm-nm %t.viaasm.o > %t.viaasm.nm
; RUN: diff %t.direct.nm %t.viaasm.nm

;; Every one of these names is something the assembler reads as itself: a
;; special function register, the stack pointer, a general purpose register, a
;; condition code and one of the two constant registers.
; CHECK-LABEL: f:
; CHECK: mov r2, "sp"
; CHECK-NEXT: add r2, "t2"
; CHECK-NEXT: add r2, "r5"
; CHECK-NEXT: add r2, "cc_z"
; CHECK-NEXT: add r2, "ones"

;; A store goes the same way round.
; CHECK-LABEL: g:
; CHECK: mov "t2", r2

;; And so do the directives that declare them, since the name is the name
;; wherever it appears.
; CHECK: .type "t2",@object
; CHECK: .globl "t2"

; ModuleID = 'resv.c'
source_filename = "resv.c"
target datalayout = "e-m:e-p:16:16-p1:32:16-i32:16-i64:16-f32:16-f64:16-a:8-n8:16-S16"
target triple = "c166"

@t2 = dso_local local_unnamed_addr global i16 0, align 2
@sp = dso_local local_unnamed_addr global i16 0, align 2
@r5 = dso_local local_unnamed_addr global i16 0, align 2
@cc_z = dso_local local_unnamed_addr global i16 0, align 2
@ones = dso_local local_unnamed_addr global i16 0, align 2

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(read, argmem: none, inaccessiblemem: none, target_mem: none)
define dso_local i16 @f() local_unnamed_addr #0 {
entry:
  %0 = load i16, ptr @t2, align 2, !tbaa !7
  %1 = load i16, ptr @sp, align 2, !tbaa !7
  %add = add nsw i16 %1, %0
  %2 = load i16, ptr @r5, align 2, !tbaa !7
  %add1 = add nsw i16 %add, %2
  %3 = load i16, ptr @cc_z, align 2, !tbaa !7
  %add2 = add nsw i16 %add1, %3
  %4 = load i16, ptr @ones, align 2, !tbaa !7
  %add3 = add nsw i16 %add2, %4
  ret i16 %add3
}

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(write, argmem: none, inaccessiblemem: none, target_mem: none)
define dso_local void @g(i16 noundef %v) local_unnamed_addr #1 {
entry:
  store i16 %v, ptr @t2, align 2, !tbaa !7
  ret void
}

attributes #0 = { mustprogress nofree norecurse nosync nounwind willreturn memory(read, argmem: none, inaccessiblemem: none, target_mem: none) "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #1 = { mustprogress nofree norecurse nosync nounwind willreturn memory(write, argmem: none, inaccessiblemem: none, target_mem: none) "no-trapping-math"="true" "stack-protector-buffer-size"="8" }

!llvm.module.flags = !{!0}
!llvm.errno.tbaa = !{!2}

!0 = !{i32 1, !"wchar_size", i32 2}
!2 = !{!3, !4, i64 0}
!3 = !{!"__libc_errno", !4, i64 0}
!4 = !{!"int", !5, i64 0}
!5 = !{!"omnipotent char", !6, i64 0}
!6 = !{!"Simple C/C++ TBAA"}
!7 = !{!4, !4, i64 0}
