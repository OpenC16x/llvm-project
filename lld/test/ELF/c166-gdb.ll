;; The GDB remote serial protocol, driven end to end.  No debugger knows the
;; C166 architecture, so what drives it here is a client of our own - see
;; llvm/utils/C166Sim/tools/rsp-client.py - which speaks the framing and reads
;; the registers out of the target description the stub serves rather than
;; assuming a layout.
;;
;; The program is the same mixed near and far chain the backtrace test uses.
;;
; REQUIRES: c166
; RUN: rm -rf %t && split-file %s %t
; RUN: llc -filetype=obj -mtriple=c166 %t/prog.ll -o %t/prog.o
; RUN: ld.lld -e _start -Ttext=0xc00100 --section-start=.fartext=0xc0c000 -Tdata=0xc000 %t/prog.o -o %t/prog.exe
; RUN: %python %llvm_src_root/utils/C166Sim/tools/rsp-client.py -- c166-sim --gdb %t/prog.exe < %t/script.rsp | FileCheck %s

;; The stub says what it can do, and can describe its own registers.
; CHECK: qSupported -> PacketSize={{[0-9a-f]+}};qXfer:features:read+

;; Before anything runs the machine is at the entry point, which is where the
;; ELF header said to start.
; CHECK: pc = 00c00112

;; A breakpoint stops the program exactly on the address asked for, having
;; passed through the far function on the way.
; CHECK: c -> S05
; CHECK: pc = 00c00100

;; The first instruction of leaf is "mov sink, r2", four bytes, and stepping
;; over it moves the program counter by exactly that.
; CHECK: mc00100,4 -> f6f20ad0
; CHECK: pc = 00c00100
; CHECK: s -> S05
; CHECK: pc = 00c00104

;; Registers go both ways.  The value is written least significant byte first,
;; which is the protocol's rule and the machine's byte order.
; CHECK: P2=3412 -> OK
; CHECK: r2 = 1234

;; And the register the whole two stack problem is about: the hardware stack
;; pointer, which is where the return addresses are.  It came up at FC00H and
;; three calls have pushed onto it - two bytes for the near call to outer, four
;; for the CALLS into farmid, two more for the near call to leaf - so it is
;; eight bytes down.
; CHECK: sp = fbf8

;--- script.rsp
print --- what the stub supports
send qSupported
match qXfer:features:read+

print --- where we are before anything runs
send ?
expect S05
reg pc
expect 00c00112

print --- break on leaf, run to it
send Z0,c00100,2
expect OK
send c
expect S05
reg pc
expect 00c00100

print --- the instruction there, and one step over it
send mc00100,4
reg pc
send s
reg pc
expect 00c00104

print --- a register written and read back
send P2=3412
expect OK
reg r2
expect 1234

print --- the hardware stack pointer, three calls deep
reg sp

;--- prog.ll
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
