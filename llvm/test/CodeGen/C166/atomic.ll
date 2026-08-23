; RUN: llc -mtriple=c166 -O2 < %s | FileCheck %s

; Atomics up to a word are instructions rather than library calls.
;
; A byte, or a word at an even address, crosses the bus in one cycle, so a load
; or a store of one is already indivisible and is the ordinary instruction.
; Changing one is not, and that is what ATOMIC is for: it holds interrupts and
; PEC transfers off for the next one to four instructions, which is exactly
; long enough to read a word, change it and write it back.

; CHECK-LABEL: load:
; CHECK:      mov r2, [r2]
; CHECK-NEXT: ret
define i16 @load(ptr %p) {
  %v = load atomic i16, ptr %p seq_cst, align 2
  ret i16 %v
}

; CHECK-LABEL: store:
; CHECK:      mov [r2], r3
; CHECK-NEXT: ret
define void @store(ptr %p, i16 %v) {
  store atomic i16 %v, ptr %p seq_cst, align 2
  ret void
}

; Read, copy so that the old value survives, change, write back: four, which is
; what ATOMIC reaches.
; CHECK-LABEL: add:
; CHECK:      atomic #4
; CHECK-NEXT: mov r4, [r2]
; CHECK-NEXT: mov r5, r4
; CHECK-NEXT: add r5, r3
; CHECK-NEXT: mov [r2], r5
define i16 @add(ptr %p, i16 %v) {
  %r = atomicrmw add ptr %p, i16 %v seq_cst
  ret i16 %r
}

; CHECK-LABEL: and:
; CHECK:      atomic #4
; CHECK-NEXT: mov r4, [r2]
; CHECK-NEXT: mov r5, r4
; CHECK-NEXT: and r5, r3
; CHECK-NEXT: mov [r2], r5
define i16 @and(ptr %p, i16 %v) {
  %r = atomicrmw and ptr %p, i16 %v seq_cst
  ret i16 %r
}

; An exchange needs no copy and no arithmetic, so it is two.
; CHECK-LABEL: xchg:
; CHECK:      atomic #2
; CHECK-NEXT: mov r4, [r2]
; CHECK-NEXT: mov [r2], r3
define i16 @xchg(ptr %p, i16 %v) {
  %r = atomicrmw xchg ptr %p, i16 %v seq_cst
  ret i16 %r
}

; CHECK-LABEL: add8:
; CHECK:      atomic #4
; CHECK-NEXT: movb [[OLD:r[lh][0-9]+]], [r2]
; CHECK-NEXT: movb [[NEW:r[lh][0-9]+]], [[OLD]]
; CHECK-NEXT: addb [[NEW]], r{{[lh][0-9]+}}
; CHECK-NEXT: movb [r2], [[NEW]]
define i8 @add8(ptr %p, i8 %v) {
  %r = atomicrmw add ptr %p, i8 %v seq_cst
  ret i8 %r
}

; A compare and exchange stores only when what it read matched, so it branches,
; and a branch is one of the two things an ATOMIC sequence cannot contain: the
; instruction counter keeps running across it.  This one clears PSW.IEN instead
; and puts the word back afterwards, which restores the bit along with flags
; that are dead by then.
; CHECK-LABEL: cas:
; CHECK:      mov [[SAVE:r[0-9]+]], psw
; CHECK-NEXT: bclr psw.11
; CHECK-NEXT: mov [[OLD:r[0-9]+]], [r2]
; CHECK-NEXT: cmp [[OLD]], r3
; CHECK-NEXT: jmpr cc_NE, [[OUT:\.LBB[0-9_]+]]
; CHECK:      mov [r2], r4
; CHECK:      [[OUT]]:
; CHECK:      mov psw, [[SAVE]]
define i16 @cas(ptr %p, i16 %e, i16 %n) {
  %r = cmpxchg ptr %p, i16 %e, i16 %n seq_cst seq_cst
  %v = extractvalue { i16, i1 } %r, 0
  ret i16 %v
}

; NAND is an AND and a complement, one instruction more than the sequence
; reaches, so it goes round a compare and exchange loop instead.  There is no
; ATOMIC in it at all.
; CHECK-LABEL: nand:
; CHECK-NOT:  atomic #
; CHECK:      cpl
define i16 @nand(ptr %p, i16 %v) {
  %r = atomicrmw nand ptr %p, i16 %v seq_cst
  ret i16 %r
}
