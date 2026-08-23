;; Where a frame's return address is, which on a C166 is the question DWARF is
;; least ready for: the return address is not in a register and it is not in
;; this frame.  It is on the hardware stack, a second stack addressed by SP that
;; the compiler never touches, while the canonical frame address is measured
;; from R0 on the other one.  So no offset from the CFA finds it and the rule
;; has to be an expression.
;;
;; The functions here are the shapes that expression comes in.
;;
; RUN: llc -mtriple=c166 < %s | FileCheck %s
; RUN: llc -filetype=obj -mtriple=c166 < %s -o %t.o
; RUN: llvm-dwarfdump --debug-frame %t.o | FileCheck %s --check-prefix=CIE

;; A near call put one word on the hardware stack, the offset half of the
;; return address; the segment half is the CSP we are running with, because a
;; near call cannot have changed it.  That is what almost every function looks
;; like, so it goes in the CIE and no function repeats it.  PC is the column
;; the return address is described in, and is not a register a program can
;; name - see C166RegisterInfo.td.
; CIE: Return address column: 45
; CIE: DW_CFA_def_cfa: R0 +0
; CIE-NEXT: DW_CFA_val_expression: PC DW_OP_bregx CSP+0, DW_OP_const1u 0x10, DW_OP_shl, DW_OP_bregx SYSSP+0, DW_OP_deref_size 0x2, DW_OP_or
; CIE-NEXT: DW_CFA_val_expression: SYSSP DW_OP_bregx SYSSP+2
; CIE-NEXT: DW_CFA_same_value: CSP

;; So a near function has nothing of its own to say and goes straight to work.
; CHECK-LABEL: nearfn:
; CHECK-NOT: .cfi_escape
; CHECK: add r2, #2

;; A far function was entered with CALLS, which pushed the caller's CSP as well,
;; so both halves of the return address come off the stack and the caller's SP
;; is four bytes up rather than two.
;;
;;   16          DW_CFA_val_expression
;;   2d          register 45, the return address column
;;   0e          fourteen bytes of expression
;;   92 24 02    DW_OP_bregx 36, 2        two bytes up the hardware stack
;;   94 02       DW_OP_deref_size 2       the caller's segment
;;   08 10 24    DW_OP_const1u 16, DW_OP_shl
;;   92 24 00    DW_OP_bregx 36, 0
;;   94 02       DW_OP_deref_size 2       the offset within it
;;   21          DW_OP_or
; CHECK-LABEL: farfn:
; CHECK: .cfi_escape 0x16, 0x2d, 0x0e, 0x92, 0x24, 0x02, 0x94, 0x02, 0x08, 0x10, 0x24, 0x92, 0x24, 0x00, 0x94, 0x02, 0x21
; CHECK-NEXT: .cfi_escape 0x16, 0x24, 0x03, 0x92, 0x24, 0x04
; CHECK-NEXT: .cfi_escape 0x10, 0x2c, 0x03, 0x92, 0x24, 0x02

;; An interrupt handler was entered by the hardware, which pushed PSW, CSP and
;; IP - six bytes, with the PSW recoverable as well.
; CHECK-LABEL: isr:
; CHECK: .cfi_escape 0x16, 0x2d, 0x0e, 0x92, 0x24, 0x02, 0x94, 0x02, 0x08, 0x10, 0x24, 0x92, 0x24, 0x00, 0x94, 0x02, 0x21
; CHECK-NEXT: .cfi_escape 0x16, 0x24, 0x03, 0x92, 0x24, 0x06
; CHECK-NEXT: .cfi_escape 0x10, 0x2c, 0x03, 0x92, 0x24, 0x02
; CHECK-NEXT: .cfi_escape 0x10, 0x20, 0x03, 0x92, 0x24, 0x04

;; And then this one saves the multiply/divide unit, which is three more words
;; on that same stack, so every offset above moves down by six and the rules are
;; emitted again saying so.
; CHECK: push mdc
; CHECK-NEXT: push mdl
; CHECK-NEXT: push mdh
; CHECK-NEXT: .cfi_escape 0x16, 0x2d, 0x0e, 0x92, 0x24, 0x08, 0x94, 0x02, 0x08, 0x10, 0x24, 0x92, 0x24, 0x06, 0x94, 0x02, 0x21
; CHECK-NEXT: .cfi_escape 0x16, 0x24, 0x03, 0x92, 0x24, 0x0c
; CHECK-NEXT: .cfi_escape 0x10, 0x2c, 0x03, 0x92, 0x24, 0x08
; CHECK-NEXT: .cfi_escape 0x10, 0x20, 0x03, 0x92, 0x24, 0x0a

;; The callee saved registers are the ordinary case: they are on the ABI stack,
;; so they are at an offset from the CFA like anywhere else.  R0 is dropped by
;; the frame size and the CFA follows it, which is what makes -2 the slot at the
;; top of the frame.
; CHECK-LABEL: spills:
; CHECK: sub r0, #8
; CHECK-NEXT: .cfi_def_cfa_offset 8
; CHECK: mov [r0+#6], r12
; CHECK-NEXT: mov [r0+#4], r13
; CHECK-NEXT: mov [r0+#2], r14
; CHECK-NEXT: mov [r0], r15
; CHECK-NEXT: .cfi_offset r12, -2
; CHECK-NEXT: .cfi_offset r13, -4
; CHECK-NEXT: .cfi_offset r14, -6
; CHECK-NEXT: .cfi_offset r15, -8

; ModuleID = 'cfikinds.c'
source_filename = "cfikinds.c"
target datalayout = "e-m:e-p:16:16-p1:32:16-i32:16-i64:16-f32:16-f64:16-a:8-n8:16-S16"
target triple = "c166"

@sink = dso_local global i16 0, align 2, !dbg !0

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(none)
define dso_local range(i16 -32766, -32768) i16 @nearfn(i16 noundef %x) local_unnamed_addr #0 !dbg !17 {
entry:
    #dbg_value(i16 %x, !21, !DIExpression(), !22)
  %add = add nsw i16 %x, 2, !dbg !23
  ret i16 %add, !dbg !24
}

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(none)
define dso_local range(i16 -32767, -32768) i16 @farfn(i16 noundef %x) local_unnamed_addr #1 !dbg !25 {
entry:
    #dbg_value(i16 %x, !27, !DIExpression(), !28)
  %add = add nsw i16 %x, 1, !dbg !29
  ret i16 %add, !dbg !30
}

; Function Attrs: nofree noinline norecurse nosync nounwind memory(readwrite, argmem: none, target_mem: none)
define dso_local void @isr() local_unnamed_addr #2 !dbg !31 {
entry:
  %0 = load volatile i16, ptr @sink, align 2, !dbg !34, !tbaa !35
  %mul = mul nsw i16 %0, 3, !dbg !36
  store volatile i16 %mul, ptr @sink, align 2, !dbg !37, !tbaa !35
  ret void, !dbg !38
}

; Function Attrs: nounwind
define dso_local i16 @spills(i16 noundef %a, i16 noundef %b, i16 noundef %c, i16 noundef %d, i16 noundef %e) local_unnamed_addr #3 !dbg !39 {
entry:
    #dbg_value(i16 %a, !43, !DIExpression(), !51)
    #dbg_value(i16 %b, !44, !DIExpression(), !51)
    #dbg_value(i16 %c, !45, !DIExpression(), !51)
    #dbg_value(i16 %d, !46, !DIExpression(), !51)
    #dbg_value(i16 %e, !47, !DIExpression(), !51)
  %mul = mul nsw i16 %b, %a, !dbg !52
    #dbg_value(i16 %mul, !48, !DIExpression(), !51)
  %mul1 = mul nsw i16 %d, %c, !dbg !53
    #dbg_value(i16 %mul1, !49, !DIExpression(), !51)
  %add = add nsw i16 %e, 1, !dbg !54
    #dbg_value(i16 %add, !50, !DIExpression(), !51)
  %call = tail call i16 @use(i16 noundef %mul) #5, !dbg !55
  %call2 = tail call i16 @use(i16 noundef %mul1) #5, !dbg !56
  %add3 = add nsw i16 %call2, %call, !dbg !57
  %call4 = tail call i16 @use(i16 noundef %add) #5, !dbg !58
  %add5 = add nsw i16 %add3, %call4, !dbg !59
  store volatile i16 %add5, ptr @sink, align 2, !dbg !60, !tbaa !35
  %add6 = add nsw i16 %mul1, %mul, !dbg !61
  %add7 = add nsw i16 %add6, %add, !dbg !62
  ret i16 %add7, !dbg !63
}

declare !dbg !64 dso_local i16 @use(i16 noundef) local_unnamed_addr #4

attributes #0 = { mustprogress nofree norecurse nosync nounwind willreturn memory(none) "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #1 = { mustprogress nofree norecurse nosync nounwind willreturn memory(none) "far" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #2 = { nofree noinline norecurse nosync nounwind memory(readwrite, argmem: none, target_mem: none) "interrupt" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #3 = { nounwind "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #4 = { "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #5 = { nounwind }

!llvm.dbg.cu = !{!2}
!llvm.module.flags = !{!7, !8, !9, !10}
!llvm.errno.tbaa = !{!12}

!0 = !DIGlobalVariableExpression(var: !1, expr: !DIExpression())
!1 = distinct !DIGlobalVariable(name: "sink", scope: !2, file: !3, line: 1, type: !5, isLocal: false, isDefinition: true)
!2 = distinct !DICompileUnit(language: DW_LANG_C11, file: !3, producer: "clang", isOptimized: true, runtimeVersion: 0, emissionKind: FullDebug, globals: !4, splitDebugInlining: false, nameTableKind: None)
!3 = !DIFile(filename: "cfikinds.c", directory: "/tmp")
!4 = !{!0}
!5 = !DIDerivedType(tag: DW_TAG_volatile_type, baseType: !6)
!6 = !DIBasicType(name: "int", size: 16, encoding: DW_ATE_signed)
!7 = !{i32 7, !"Dwarf Version", i32 5}
!8 = !{i32 2, !"Debug Info Version", i32 3}
!9 = !{i32 1, !"wchar_size", i32 2}
!10 = !{i32 7, !"debug-info-assignment-tracking", i1 true}
!12 = !{!13, !14, i64 0}
!13 = !{!"__libc_errno", !14, i64 0}
!14 = !{!"int", !15, i64 0}
!15 = !{!"omnipotent char", !16, i64 0}
!16 = !{!"Simple C/C++ TBAA"}
!17 = distinct !DISubprogram(name: "nearfn", scope: !3, file: !3, line: 4, type: !18, scopeLine: 4, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !20, keyInstructions: true)
!18 = !DISubroutineType(types: !19)
!19 = !{!6, !6}
!20 = !{!21}
!21 = !DILocalVariable(name: "x", arg: 1, scope: !17, file: !3, line: 4, type: !6)
!22 = !DILocation(line: 0, scope: !17)
!23 = !DILocation(line: 4, column: 30, scope: !17, atomGroup: 1, atomRank: 2)
!24 = !DILocation(line: 4, column: 21, scope: !17, atomGroup: 1, atomRank: 1)
!25 = distinct !DISubprogram(name: "farfn", scope: !3, file: !3, line: 6, type: !18, scopeLine: 6, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !26, keyInstructions: true)
!26 = !{!27}
!27 = !DILocalVariable(name: "x", arg: 1, scope: !25, file: !3, line: 6, type: !6)
!28 = !DILocation(line: 0, scope: !25)
!29 = !DILocation(line: 6, column: 50, scope: !25, atomGroup: 1, atomRank: 2)
!30 = !DILocation(line: 6, column: 41, scope: !25, atomGroup: 1, atomRank: 1)
!31 = distinct !DISubprogram(name: "isr", scope: !3, file: !3, line: 8, type: !32, scopeLine: 8, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, keyInstructions: true)
!32 = !DISubroutineType(types: !33)
!33 = !{null}
!34 = !DILocation(line: 8, column: 52, scope: !31)
!35 = !{!14, !14, i64 0}
!36 = !DILocation(line: 8, column: 57, scope: !31, atomGroup: 1, atomRank: 2)
!37 = !DILocation(line: 8, column: 50, scope: !31, atomGroup: 1, atomRank: 1)
!38 = !DILocation(line: 8, column: 62, scope: !31, atomGroup: 2, atomRank: 1)
!39 = distinct !DISubprogram(name: "spills", scope: !3, file: !3, line: 10, type: !40, scopeLine: 10, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !42, keyInstructions: true)
!40 = !DISubroutineType(types: !41)
!41 = !{!6, !6, !6, !6, !6, !6}
!42 = !{!43, !44, !45, !46, !47, !48, !49, !50}
!43 = !DILocalVariable(name: "a", arg: 1, scope: !39, file: !3, line: 10, type: !6)
!44 = !DILocalVariable(name: "b", arg: 2, scope: !39, file: !3, line: 10, type: !6)
!45 = !DILocalVariable(name: "c", arg: 3, scope: !39, file: !3, line: 10, type: !6)
!46 = !DILocalVariable(name: "d", arg: 4, scope: !39, file: !3, line: 10, type: !6)
!47 = !DILocalVariable(name: "e", arg: 5, scope: !39, file: !3, line: 10, type: !6)
!48 = !DILocalVariable(name: "x", scope: !39, file: !3, line: 11, type: !6)
!49 = !DILocalVariable(name: "y", scope: !39, file: !3, line: 11, type: !6)
!50 = !DILocalVariable(name: "z", scope: !39, file: !3, line: 11, type: !6)
!51 = !DILocation(line: 0, scope: !39)
!52 = !DILocation(line: 11, column: 13, scope: !39, atomGroup: 1, atomRank: 2)
!53 = !DILocation(line: 11, column: 24, scope: !39, atomGroup: 2, atomRank: 2)
!54 = !DILocation(line: 11, column: 35, scope: !39, atomGroup: 3, atomRank: 2)
!55 = !DILocation(line: 12, column: 10, scope: !39)
!56 = !DILocation(line: 12, column: 19, scope: !39)
!57 = !DILocation(line: 12, column: 17, scope: !39)
!58 = !DILocation(line: 12, column: 28, scope: !39)
!59 = !DILocation(line: 12, column: 26, scope: !39, atomGroup: 4, atomRank: 2)
!60 = !DILocation(line: 12, column: 8, scope: !39, atomGroup: 4, atomRank: 1)
!61 = !DILocation(line: 13, column: 12, scope: !39)
!62 = !DILocation(line: 13, column: 16, scope: !39, atomGroup: 6, atomRank: 2)
!63 = !DILocation(line: 13, column: 3, scope: !39, atomGroup: 6, atomRank: 1)
!64 = !DISubprogram(name: "use", scope: !3, file: !3, line: 2, type: !18, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
