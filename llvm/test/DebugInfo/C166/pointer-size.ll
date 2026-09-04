; A pointer type normally carries no DW_AT_byte_size: DWARF says a consumer
; that finds none uses the compilation unit's address size, and on almost every
; target every pointer is that size, so the attribute would be the same number
; on every pointer in the file.
;
; This target has two pointer widths.  Its address size is four bytes, because
; an address in the debug information is the whole 24 bit physical one - see
; addr-size.ll - so a far pointer matches the default and a near pointer does
; not.  The near ones are the exception here, and they are the ones that have
; to say so; without it a debugger reads four bytes of a two byte pointer.
;
; RUN: llc -filetype=obj -mtriple=c166 < %s -o %t.o
; RUN: llvm-dwarfdump --debug-info %t.o | FileCheck %s

; CHECK: addr_size = 0x04

; The far pointer is four bytes, which is the address size, so it says nothing
; about its size and says which address space it is in instead.  The blank line
; after the address class is what says the entry ends there and carries no
; DW_AT_byte_size.
; CHECK:      DW_TAG_pointer_type
; CHECK-NEXT:   DW_AT_type {{.*}}"char"
; CHECK-NEXT:   DW_AT_address_class (0x00000001)
; CHECK-EMPTY:

; The near one is two, and has to say so.  It has no address class: zero is the
; default and is left out.
; CHECK:      DW_TAG_pointer_type
; CHECK-NEXT:   DW_AT_type {{.*}}"char"
; CHECK-NEXT:   DW_AT_byte_size (0x02)
; CHECK-EMPTY:

target datalayout = "e-m:e-p:16:16-p1:32:16-p2:32:16:16:16-i32:16-i64:16-f32:16-f64:16-a:8-n8:16-S16"
target triple = "c166"

@farp = global ptr addrspace(1) null, align 2, !dbg !0
@nearp = global ptr null, align 2, !dbg !5

!llvm.dbg.cu = !{!2}
!llvm.module.flags = !{!10, !11}

!0 = !DIGlobalVariableExpression(var: !1, expr: !DIExpression())
!1 = distinct !DIGlobalVariable(name: "farp", scope: !2, file: !3, line: 1, type: !7, isLocal: false, isDefinition: true)
!2 = distinct !DICompileUnit(language: DW_LANG_C11, file: !3, producer: "clang", isOptimized: false, runtimeVersion: 0, emissionKind: FullDebug, globals: !4)
!3 = !DIFile(filename: "p.c", directory: "/")
!4 = !{!0, !5}
!5 = !DIGlobalVariableExpression(var: !6, expr: !DIExpression())
!6 = distinct !DIGlobalVariable(name: "nearp", scope: !2, file: !3, line: 2, type: !9, isLocal: false, isDefinition: true)
!7 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !8, size: 32, dwarfAddressSpace: 1)
!8 = !DIBasicType(name: "char", size: 8, encoding: DW_ATE_signed_char)
!9 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !8, size: 16)
!10 = !{i32 7, !"Dwarf Version", i32 5}
!11 = !{i32 2, !"Debug Info Version", i32 3}
