; RUN: not llc -mtriple=c166 < %s 2>&1 | FileCheck %s

; Placing an object in the far address space would need a relocation for its
; segment, which the object writer does not have yet.  Until it does, say so
; instead of failing somewhere inside the type legalizer.

@fg = addrspace(1) global i16 7

; CHECK: error: {{.*}}in function read_far_global i16 (): cannot take the address of a symbol in the far address space
define i16 @read_far_global() {
  %v = load i16, ptr addrspace(1) @fg
  ret i16 %v
}
