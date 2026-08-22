;; The unwind information, run rather than read.  The simulator walks the stack
;; with the call frame information the compiler emitted, so a rule that is wrong
;; makes a wrong backtrace - and on this target the rule that finds the return
;; address is a DWARF expression over the hardware stack, which is not something
;; reading the assembly would catch.
;;
;; The chain is deliberately mixed: leaf and outer are near, farmid is far and
;; was entered with CALLS, so the walk crosses both shapes of frame.  Both text
;; sections are in segment C0, which is what c166.ld does - a near call out of
;; far code stays in whatever segment it is running in.
;;
;; The IR below was compiled from this, which is what the line numbers name.
;; The argument is -3 so that leaf is reached with zero in R2, which is what the
;; simulator exits with.
;;
;;   1  volatile int sink;
;;   2  __attribute__((noinline)) int leaf(int x) { sink = x; return x + 1; }
;;   3  __attribute__((far, noinline)) int farmid(int x) { return leaf(x*2)+1; }
;;   4  __attribute__((noinline)) int outer(int x) { return farmid(x + 3) * 2; }
;;   5  void _start(void) { sink = outer(-3); for (;;) ; }
;;
; REQUIRES: c166
; RUN: llc -filetype=obj -mtriple=c166 %s -o %t.o
; RUN: ld.lld -e _start -Ttext=0xc00100 --section-start=.fartext=0xc0c000 -Tdata=0xc000 %t.o -o %t.exe
; RUN: llvm-nm %t.exe | FileCheck %s --check-prefix=SYM
; RUN: c166-sim --exit-symbol=leaf --backtrace %t.exe 2>&1 | FileCheck %s

;; Stopping at leaf leaves the whole chain on the stack.
; SYM: 00c0c000 T farmid
; SYM: 00c00100 T leaf
; SYM: 00c00108 T outer

;; Every frame but the innermost names the address its call will return to, so
;; those addresses are inside the caller rather than at its entry.  Frame 1 in
;; particular is in the far section, reached by unwinding the four byte record
;; CALLS left behind - two words, one of which is the caller's segment.
; CHECK:      #0 c00100 in leaf at {{.*}}backtrace.c:2
; CHECK-NEXT: #1 c0c006 in farmid at {{.*}}backtrace.c:3
; CHECK-NEXT: #2 c0010e in outer at {{.*}}backtrace.c:4
; CHECK-NEXT: #3 c0011a in _start at {{.*}}backtrace.c:5

; ModuleID = 'backtrace.c'
source_filename = "backtrace.c"
target datalayout = "e-m:e-p:16:16-p1:32:16-i32:16-i64:16-f32:16-f64:16-a:8-n8:16-S16"
target triple = "c166"

@sink = dso_local global i16 0, align 2, !dbg !0

; Function Attrs: nofree noinline norecurse nosync nounwind memory(readwrite, argmem: none, target_mem: none)
define dso_local range(i16 -32767, -32768) i16 @leaf(i16 noundef %x) local_unnamed_addr #0 !dbg !17 {
entry:
    #dbg_value(i16 %x, !21, !DIExpression(), !22)
  store volatile i16 %x, ptr @sink, align 2, !dbg !23, !tbaa !24
  %add = add nsw i16 %x, 1, !dbg !25
  ret i16 %add, !dbg !26
}

; Function Attrs: nofree noinline norecurse nosync nounwind memory(readwrite, target_mem: none)
define dso_local range(i16 -32766, -32768) i16 @farmid(i16 noundef %x) local_unnamed_addr #1 !dbg !27 {
entry:
    #dbg_value(i16 %x, !29, !DIExpression(), !30)
  %mul = shl nsw i16 %x, 1, !dbg !31
  %call = tail call i16 @leaf(i16 noundef %mul), !dbg !32
  %add = add nsw i16 %call, 1, !dbg !33
  ret i16 %add, !dbg !34
}

; Function Attrs: nofree noinline norecurse nosync nounwind memory(readwrite, target_mem: none)
define dso_local range(i16 -32768, 32767) i16 @outer(i16 noundef %x) local_unnamed_addr #2 !dbg !35 {
entry:
    #dbg_value(i16 %x, !37, !DIExpression(), !38)
  %add = add nsw i16 %x, 3, !dbg !39
  %call = tail call i16 @farmid(i16 noundef %add), !dbg !40
  %mul = shl nsw i16 %call, 1, !dbg !41
  ret i16 %mul, !dbg !42
}

; Function Attrs: nofree norecurse noreturn nosync nounwind memory(readwrite, target_mem: none)
define dso_local void @_start() local_unnamed_addr #3 !dbg !43 {
entry:
  %call = tail call i16 @outer(i16 noundef -3), !dbg !46
  store volatile i16 %call, ptr @sink, align 2, !dbg !47, !tbaa !24
  br label %for.cond, !dbg !48

for.cond:                                         ; preds = %for.cond, %entry
  br label %for.cond, !dbg !49, !llvm.loop !52
}

attributes #0 = { nofree noinline norecurse nosync nounwind memory(readwrite, argmem: none, target_mem: none) "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #1 = { nofree noinline norecurse nosync nounwind memory(readwrite, target_mem: none) "far" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #2 = { nofree noinline norecurse nosync nounwind memory(readwrite, target_mem: none) "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #3 = { nofree norecurse noreturn nosync nounwind memory(readwrite, target_mem: none) "no-trapping-math"="true" "stack-protector-buffer-size"="8" }

!llvm.dbg.cu = !{!2}
!llvm.module.flags = !{!7, !8, !9, !10}
!llvm.errno.tbaa = !{!12}

!0 = !DIGlobalVariableExpression(var: !1, expr: !DIExpression())
!1 = distinct !DIGlobalVariable(name: "sink", scope: !2, file: !3, line: 1, type: !5, isLocal: false, isDefinition: true)
!2 = distinct !DICompileUnit(language: DW_LANG_C11, file: !3, producer: "clang", isOptimized: true, runtimeVersion: 0, emissionKind: FullDebug, globals: !4, splitDebugInlining: false, nameTableKind: None)
!3 = !DIFile(filename: "backtrace.c", directory: "/tmp", checksumkind: CSK_MD5, checksum: "d25e4395218d1ff635afa19228859ee6")
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
!17 = distinct !DISubprogram(name: "leaf", scope: !3, file: !3, line: 2, type: !18, scopeLine: 2, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !20, keyInstructions: true)
!18 = !DISubroutineType(types: !19)
!19 = !{!6, !6}
!20 = !{!21}
!21 = !DILocalVariable(name: "x", arg: 1, scope: !17, file: !3, line: 2, type: !6)
!22 = !DILocation(line: 0, scope: !17)
!23 = !DILocation(line: 2, column: 50, scope: !17, atomGroup: 1, atomRank: 1)
!24 = !{!14, !14, i64 0}
!25 = !DILocation(line: 2, column: 64, scope: !17, atomGroup: 2, atomRank: 2)
!26 = !DILocation(line: 2, column: 55, scope: !17, atomGroup: 2, atomRank: 1)
!27 = distinct !DISubprogram(name: "farmid", scope: !3, file: !3, line: 3, type: !18, scopeLine: 3, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !28, keyInstructions: true)
!28 = !{!29}
!29 = !DILocalVariable(name: "x", arg: 1, scope: !27, file: !3, line: 3, type: !6)
!30 = !DILocation(line: 0, scope: !27)
!31 = !DILocation(line: 3, column: 66, scope: !27)
!32 = !DILocation(line: 3, column: 59, scope: !27)
!33 = !DILocation(line: 3, column: 71, scope: !27, atomGroup: 1, atomRank: 2)
!34 = !DILocation(line: 3, column: 52, scope: !27, atomGroup: 1, atomRank: 1)
!35 = distinct !DISubprogram(name: "outer", scope: !3, file: !3, line: 4, type: !18, scopeLine: 4, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, retainedNodes: !36, keyInstructions: true)
!36 = !{!37}
!37 = !DILocalVariable(name: "x", arg: 1, scope: !35, file: !3, line: 4, type: !6)
!38 = !DILocation(line: 0, scope: !35)
!39 = !DILocation(line: 4, column: 62, scope: !35)
!40 = !DILocation(line: 4, column: 53, scope: !35)
!41 = !DILocation(line: 4, column: 67, scope: !35, atomGroup: 1, atomRank: 2)
!42 = !DILocation(line: 4, column: 46, scope: !35, atomGroup: 1, atomRank: 1)
!43 = distinct !DISubprogram(name: "_start", scope: !3, file: !3, line: 5, type: !44, scopeLine: 5, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !2, keyInstructions: true)
!44 = !DISubroutineType(types: !45)
!45 = !{null}
!46 = !DILocation(line: 5, column: 28, scope: !43, atomGroup: 1, atomRank: 2)
!47 = !DILocation(line: 5, column: 26, scope: !43, atomGroup: 1, atomRank: 1)
!48 = !DILocation(line: 5, column: 39, scope: !43)
!49 = !DILocation(line: 5, column: 39, scope: !50, atomGroup: 2, atomRank: 1)
!50 = distinct !DILexicalBlock(scope: !51, file: !3, line: 5, column: 39)
!51 = distinct !DILexicalBlock(scope: !43, file: !3, line: 5, column: 39)
!52 = distinct !{!52, !53, !54, !55}
!53 = !DILocation(line: 5, column: 39, scope: !51)
!54 = !DILocation(line: 5, column: 48, scope: !51)
!55 = !{!"llvm.loop.unroll.disable"}
