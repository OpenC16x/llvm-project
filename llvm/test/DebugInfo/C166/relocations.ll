; Reading the debug information out of an object file means applying its
; relocations, which llvm/lib/Object/RelocationResolver.cpp is what knows how to
; do.  Without a C166 entry there every debug relocation fails and the tools
; report nothing but warnings, so this checks that an unlinked object comes
; apart cleanly - it is what llvm-dwarfdump and llvm-symbolizer both go through.
;
; RUN: llc -filetype=obj -mtriple=c166 < %s -o %t.o
; RUN: llvm-dwarfdump --debug-line %t.o 2>&1 | FileCheck %s

; CHECK-NOT: failed to compute relocation
; CHECK: file_names[ 0]:
; CHECK: name: "dbg.c"

; The rows are section relative in an object, so the first is at zero; what
; matters is that there are rows at all and that they carry line numbers.
; CHECK: Address Line
; CHECK: 0x0000000000000000 1
; CHECK: end_sequence
; CHECK-NOT: failed to compute relocation

; ModuleID = 'dbg.c'
source_filename = "dbg.c"
target datalayout = "e-m:e-p:16:16-p1:32:16-i32:16-i64:16-f32:16-f64:16-a:8-n8:16-S16"
target triple = "c166"

; Function Attrs: noinline nounwind optnone
define dso_local i16 @add(i16 noundef %a, i16 noundef %b) #0 !dbg !6 {
entry:
  %a.addr = alloca i16, align 2
  %b.addr = alloca i16, align 2
  store i16 %a, ptr %a.addr, align 2
    #dbg_declare(ptr %a.addr, !11, !DIExpression(), !12)
  store i16 %b, ptr %b.addr, align 2
    #dbg_declare(ptr %b.addr, !13, !DIExpression(), !14)
  %0 = load i16, ptr %a.addr, align 2, !dbg !15
  %1 = load i16, ptr %b.addr, align 2, !dbg !16
  %add = add nsw i16 %0, %1, !dbg !17
  ret i16 %add, !dbg !18
}

; Function Attrs: noinline nounwind optnone
define dso_local i16 @main() #0 !dbg !19 {
entry:
  %retval = alloca i16, align 2
  store i16 0, ptr %retval, align 2
  %call = call i16 @add(i16 noundef 3, i16 noundef 4), !dbg !22
  ret i16 %call, !dbg !23
}

attributes #0 = { noinline nounwind optnone "no-trapping-math"="true" "stack-protector-buffer-size"="8" }

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!2, !3, !4}

!0 = distinct !DICompileUnit(language: DW_LANG_C11, file: !1, producer: "clang version 24.0.0git (https://github.com/dwzg/llvm-project 63619e33ca71498b118e430f53ebc815864a07d4)", isOptimized: false, runtimeVersion: 0, emissionKind: FullDebug, splitDebugInlining: false, nameTableKind: None)
!1 = !DIFile(filename: "dbg.c", directory: "/tmp/probe", checksumkind: CSK_MD5, checksum: "a63b97779e37de0816a7e575fdf00407")
!2 = !{i32 7, !"Dwarf Version", i32 5}
!3 = !{i32 2, !"Debug Info Version", i32 3}
!4 = !{i32 1, !"wchar_size", i32 2}
!6 = distinct !DISubprogram(name: "add", scope: !1, file: !1, line: 1, type: !7, scopeLine: 1, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition, unit: !0, retainedNodes: !10)
!7 = !DISubroutineType(types: !8)
!8 = !{!9, !9, !9}
!9 = !DIBasicType(name: "int", size: 16, encoding: DW_ATE_signed)
!10 = !{}
!11 = !DILocalVariable(name: "a", arg: 1, scope: !6, file: !1, line: 1, type: !9)
!12 = !DILocation(line: 1, column: 13, scope: !6)
!13 = !DILocalVariable(name: "b", arg: 2, scope: !6, file: !1, line: 1, type: !9)
!14 = !DILocation(line: 1, column: 20, scope: !6)
!15 = !DILocation(line: 1, column: 32, scope: !6)
!16 = !DILocation(line: 1, column: 36, scope: !6)
!17 = !DILocation(line: 1, column: 34, scope: !6)
!18 = !DILocation(line: 1, column: 25, scope: !6)
!19 = distinct !DISubprogram(name: "main", scope: !1, file: !1, line: 2, type: !20, scopeLine: 2, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition, unit: !0)
!20 = !DISubroutineType(types: !21)
!21 = !{!9}
!22 = !DILocation(line: 2, column: 25, scope: !19)
!23 = !DILocation(line: 2, column: 18, scope: !19)
