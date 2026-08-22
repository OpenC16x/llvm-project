; RUN: llc -mtriple=c166 -O2 < %s | FileCheck %s

;; A far access carries its segment in an EXTend covering the one instruction
;; after it.  Two that name the same object fold into one EXTend covering the
;; run, which is four bytes and a cycle less.

@s = external addrspace(1) global { i16, i16 }
@a = external addrspace(1) global i16
@near = external global i16

;; Two fields of the same object: "seg(s)" and "seg(s + 2)" are the same
;; segment, which c166.ld asserts by keeping every region inside one.
; CHECK-LABEL: fields:
; CHECK:      exts #seg(s{{(\+2)?}}), #3
; CHECK-NOT:  exts
; CHECK:      ret
define void @fields() {
  %x = getelementptr inbounds { i16, i16 }, ptr addrspace(1) @s, i32 0, i32 0
  %y = getelementptr inbounds { i16, i16 }, ptr addrspace(1) @s, i32 0, i32 1
  store volatile i16 4, ptr addrspace(1) %x
  store volatile i16 5, ptr addrspace(1) %y
  ret void
}

;; The same word read and written, with arithmetic in between that touches no
;; memory and so is safe to bring under the override.
; CHECK-LABEL: readmodifywrite:
; CHECK:      exts #seg(a), #3
; CHECK-NOT:  exts
; CHECK:      ret
define void @readmodifywrite() {
  %v = load i16, ptr addrspace(1) @a
  %n = add i16 %v, 1
  store i16 %n, ptr addrspace(1) @a
  ret void
}

;; A near store between the two has an address of its own, and widening the
;; first EXTend over it would send it to the far segment instead.  So this one
;; keeps both.
; CHECK-LABEL: nearbetween:
; CHECK:      exts #seg(a), #1
; CHECK:      mov near, r{{[0-9]+}}
; CHECK:      exts #seg(a), #1
define void @nearbetween() {
  store volatile i16 1, ptr addrspace(1) @a
  store volatile i16 2, ptr @near
  store volatile i16 3, ptr addrspace(1) @a
  ret void
}

;; Two different objects say different things, so nothing folds.
; CHECK-LABEL: different:
; CHECK:      exts #seg({{a|s}}), #1
; CHECK:      exts #seg({{a|s}}), #1
define void @different() {
  store volatile i16 1, ptr addrspace(1) @a
  store volatile i16 2, ptr addrspace(1) @s
  ret void
}
