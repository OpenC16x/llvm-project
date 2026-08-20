; RUN: not llc -mtriple=c166 < %s 2>&1 | FileCheck %s
; RUN: not llc -mtriple=c166 < %S/Inputs/far-function-pointer.ll 2>&1 \
; RUN:   | FileCheck %s --check-prefix=INIT

; A far function can only be called by name: CALLI stays inside the current
; segment, and the RETS at the far end would pop a segment nobody pushed.

declare i16 @far_callee(i16) "far"

; CHECK: error: {{.*}}in function take_address ptr (): cannot take the address of a far function
define ptr @take_address() {
  ret ptr @far_callee
}

; INIT: error: cannot take the address of far function 'far_callee'
