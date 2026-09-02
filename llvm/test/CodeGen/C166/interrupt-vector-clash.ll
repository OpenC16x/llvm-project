; RUN: not llc -mtriple=c166 < %s 2>&1 | FileCheck %s

; Two handlers asking for one slot.  Left alone, the second jump would land
; four bytes above the first and take the next slot with it, which is a wrong
; program that assembles, links and runs until the wrong interrupt arrives.

define void @early() #0 {
  ret void
}

define void @late() #0 {
  ret void
}

attributes #0 = { noinline "interrupt" "c166-interrupt-vector"="26" }

; CHECK: error: interrupt vector 26 is claimed by both 'early' and 'late'
