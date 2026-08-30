; RUN: llc -mtriple=c166 -O2 < %s | FileCheck %s

;; llvm.trap and llvm.debugtrap both enter the software break vector, 08H.
;; That is the one slot the family names for a break raised by software, and
;; TRAP reaches it on every derivative.  Before this the first was a call to
;; abort() - a function a bare part may not have linked - and the second had
;; no lowering at all, so __builtin_debugtrap() was a compile error.
;;
;; The two are the same instruction; what separates them is what surrounds it,
;; and that comes from the caller rather than from the lowering.  llvm.trap is
;; noreturn, so clang follows __builtin_trap() with unreachable and no return
;; is emitted; llvm.debugtrap is resumable and the return follows it.  Both
;; shapes are below, written the way clang writes them.

declare void @llvm.trap()
declare void @llvm.debugtrap()

define void @t() {
; CHECK-LABEL: t:
; CHECK-NOT:     abort
; CHECK:         trap #8
; CHECK-NOT:     ret
  call void @llvm.trap()
  unreachable
}

define void @d() {
; CHECK-LABEL: d:
; CHECK:         trap #8
; CHECK-NEXT:    ret
  call void @llvm.debugtrap()
  ret void
}
