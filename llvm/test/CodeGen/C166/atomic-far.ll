; RUN: not llc -mtriple=c166 -O2 < %s 2>&1 | FileCheck %s

; Every atomic sequence here holds together because of what surrounds it, and a
; far access does not fit inside either kind of surround: reaching one needs an
; EXTend, and the hardware keeps a single instruction counter that an ATOMIC
; sequence is already using.  Saying so is better than the type legalizer
; falling over on the 32 bit pointer, and much better than a sequence that
; looks atomic and is not.

; CHECK: cannot make a far access atomic: a read-modify-write on a far pointer
define i16 @rmw(ptr addrspace(1) %p, i16 %v) {
  %r = atomicrmw add ptr addrspace(1) %p, i16 %v seq_cst
  ret i16 %r
}
