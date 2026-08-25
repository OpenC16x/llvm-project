;; A function whose early return is followed by more code.  The epilogue says
;; the canonical frame address is the stack pointer again, and that rule must
;; not be left standing over what comes after it: the code below the return is
;; still running with the frame the prologue set up.
;;
;; Getting this wrong is not a worse backtrace, it is a wrong one - an unwinder
;; lands two bytes out, which on this part is one word of a caller's saved
;; registers.  A C++ exception thrown from past an early return is the way it
;; shows up.
;;
; RUN: llc -mtriple=c166 < %s | FileCheck %s
; RUN: llc -filetype=obj -mtriple=c166 < %s -o %t.o
; RUN: llvm-dwarfdump --debug-frame %t.o | FileCheck %s --check-prefix=CFA

declare void @sink(i32)

define i32 @early(i32 %n) {
entry:
  %small = icmp slt i32 %n, 3
  br i1 %small, label %ret, label %work

ret:
  ret i32 %n

work:
  call void @sink(i32 %n)
  %r = add i32 %n, 1
  ret i32 %r
}

;; The epilogue puts the rule back, and the block after it says what the frame
;; really is rather than inheriting that.
; CHECK: .cfi_def_cfa r0, 0
; CHECK: .cfi_def_cfa_offset 4

;; Read back, every row from the call onwards has the frame four bytes above the
;; stack pointer, which is where the prologue put it.
; CFA: DW_CFA_def_cfa_offset: +4
; CFA: DW_CFA_def_cfa: R0 +0
; CFA: DW_CFA_def_cfa_offset: +4
