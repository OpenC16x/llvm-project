;; A select between two 32 bit additions, which on a 16 bit machine is a select
;; between two carry chains.
;;
;; This used to crash in the generic DAG combiner.  foldSelectOfBinops turns
;; "select(c, binop(x, y), binop(z, y))" into "binop(select(c, x, z), y)" and
;; rebuilds the binop from exactly two operands - but ADDE and SUBE, the legacy
;; carry opcodes this target makes legal, take a carry in as a third.  The
;; rebuilt node had no carry in at all, and the next thing to look at it read
;; the operand that was no longer there.
;;
;; Most targets never reach it, because they use UADDO_CARRY, whose carry is an
;; ordinary value rather than glue.
;;
; RUN: llc -mtriple=c166 < %s | FileCheck %s

;; The two additions differ in their constant, so at the point the chains
;; exist there is nothing to merge and each keeps its own carry: two ADDCs.
define i32 @select_of_adds(i1 %c, i32 %a, i32 %b) {
; CHECK-LABEL: select_of_adds:
; CHECK: add r{{[0-9]+}}, #1
; CHECK-NEXT: addc r{{[0-9]+}}, #0
; CHECK: add r{{[0-9]+}}, #512
; CHECK-NEXT: addc r{{[0-9]+}}, #0
entry:
  %x = add i32 %a, 512
  %y = add i32 %b, 1
  %r = select i1 %c, i32 %x, i32 %y
  ret i32 %r
}

;; The same shape for subtraction.  Subtracting a constant is adding its
;; negative, so these are ADDCs too; what matters is that there are two of
;; them rather than one built out of a node with a missing operand.
define i32 @select_of_subs(i1 %c, i32 %a, i32 %b) {
; CHECK-LABEL: select_of_subs:
; CHECK: add r{{[0-9]+}}, #-1
; CHECK-NEXT: addc r{{[0-9]+}}, #-1
; CHECK: add r{{[0-9]+}}, #-512
; CHECK-NEXT: addc r{{[0-9]+}}, #-1
entry:
  %x = sub i32 %a, 512
  %y = sub i32 %b, 1
  %r = select i1 %c, i32 %x, i32 %y
  ret i32 %r
}

;; And the case that shows the guard did not turn the fold off where it does
;; work: here both additions share an operand, so the select is hoisted while
;; this is still one 32 bit add - before it becomes a carry chain at all - and
;; one chain comes out rather than two.
define i32 @select_of_adds_common_lhs(i1 %c, i32 %a, i32 %b, i32 %d) {
; CHECK-LABEL: select_of_adds_common_lhs:
; CHECK-NOT: addc
; CHECK: add r{{[0-9]+}}, r{{[0-9]+}}
; CHECK-NEXT: addc r{{[0-9]+}}, r{{[0-9]+}}
; CHECK-NOT: addc
entry:
  %x = add i32 %a, %b
  %y = add i32 %a, %d
  %r = select i1 %c, i32 %x, i32 %y
  ret i32 %r
}
