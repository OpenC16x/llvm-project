;; The same call frame information, in the section an exception unwinds
;; through.  A program built with unwind tables has .eh_frame and no
;; .debug_frame, so this is the only copy of the rules it carries - and the
;; rules are the two-stack ones, which is what makes an exception on this part
;; able to find its caller at all.
;;
;; Reading it back also exercises the frame description entry's own pointer,
;; which is a PC relative relocation in a section that holds data.  Without one
;; that can be resolved the whole section is unreadable, and the warning about
;; it is the only sign.
;;
; RUN: llc -filetype=obj -mtriple=c166 < %s -o %t.o
; RUN: llvm-dwarfdump --eh-frame %t.o 2>&1 | FileCheck %s

;; No warning, and the entry locates the code it describes rather than leaving
;; the raw section offset behind.
; CHECK-NOT: warning
; CHECK: .eh_frame contents:

; CHECK: Return address column: 45
; CHECK: DW_CFA_def_cfa: R0 +0
;; And that the caller's R0 is that address: cfi-two-stacks.ll says why.
; CHECK-NEXT: DW_CFA_val_offset: R0 0
; CHECK-NEXT: DW_CFA_val_expression: PC DW_OP_bregx CSP+0, DW_OP_const1u 0x10, DW_OP_shl, DW_OP_bregx SYSSP+0, DW_OP_deref_size 0x2, DW_OP_or
; CHECK-NEXT: DW_CFA_val_expression: SYSSP DW_OP_bregx SYSSP+2
; CHECK-NEXT: DW_CFA_same_value: CSP

; CHECK: FDE cie={{[0-9a-f]+}} pc=00000000...{{[0-9a-f]+}}
; CHECK-NOT: warning

declare i32 @callee(i32)

define i32 @caller(i32 %n) uwtable {
  %r = call i32 @callee(i32 %n)
  %s = add i32 %r, 1
  ret i32 %s
}
