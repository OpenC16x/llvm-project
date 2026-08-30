; RUN: opt < %s -passes='print<cost-model>' -cost-kind=throughput 2>&1 \
; RUN:   -disable-output | FileCheck %s --check-prefix=TIME
; RUN: opt < %s -passes='print<cost-model>' -cost-kind=code-size 2>&1 \
; RUN:   -disable-output | FileCheck %s --check-prefix=SIZE

;; What the arithmetic costs, and how the numbers were arrived at.
;;
;; Each one is the count of instructions the target actually emits for the
;; operation, measured by compiling it and counting.  C166.td declares
;; NoItineraries, so there is no cycle data in this tree to anchor anything
;; finer; the core is in-order, so a count is a fair proxy for time as well as
;; for space.  To redo a row: compile the operation on its own at -O2, count
;; the instructions in the function, and subtract the RET.
;;
;; Without this the target independent default answers all of these from the
;; legalised type, which is i16 - so a 32 bit divide comes out at two, the
;; same as a 32 bit add, for something that is a call to a bit loop.

target datalayout = "e-m:e-p:16:16-p1:32:16-i32:16-i64:16-f32:16-f64:16-a:8-n8:16-S16"
target triple = "c166"

define void @word(i16 %a, i16 %b) {
;; A word is what this machine computes in, so these are the one instruction
;; each that they look like.
; TIME: cost of 1 for instruction:   %add = add i16
; SIZE: cost of 1 for instruction:   %add = add i16
  %add = add i16 %a, %b
; TIME: cost of 1 for instruction:   %shl = shl i16
  %shl = shl i16 %a, %b
;; MUL, then a MOV to read the low half back out of MDL.
; TIME: cost of 2 for instruction:   %mul = mul i16
  %mul = mul i16 %a, %b
;; The hardware DIV: set up MDL, divide, read the answer back.  This is the
;; one number here that understates, since DIV takes a good deal longer than
;; three instructions' worth of time - but it is the only divide the machine
;; has, so nothing is chosen against it.
; TIME: cost of 3 for instruction:   %div = sdiv i16
  %div = sdiv i16 %a, %b
  ret void
}

define void @wide(i32 %a, i32 %b) {
;; Two words, one carry chain: ADD then ADDC.
; TIME: cost of 2 for instruction:   %add = add i32
  %add = add i32 %a, %b
;; A shift across the word boundary, about four instructions a word plus the
;; count arithmetic.
; TIME: cost of 9 for instruction:   %shl = shl i32
  %shl = shl i32 %a, %b
;; A schoolbook product of the words.
; TIME: cost of 10 for instruction:   %mul = mul i32
  %mul = mul i32 %a, %b
;; A divide by a variable is a call to __udivsi3, which walks the dividend a
;; bit at a time - 143 instructions in the compiler-rt this target builds.
;; That is what it costs to run, and it is not what it costs to emit: the loop
;; is in the builtin and is shared, so the call site is the arguments, the
;; CALLA and the result.  Answering the running cost to a caller asking about
;; size makes a loop body look far bigger than the code it becomes.
; TIME: cost of 144 for instruction:   %udiv = udiv i32
; SIZE: cost of 6 for instruction:   %udiv = udiv i32
  %udiv = udiv i32 %a, %b
;; Signed is that with both signs taken off and one put back.
; TIME: cost of 176 for instruction:   %sdiv = sdiv i32
; SIZE: cost of 6 for instruction:   %sdiv = sdiv i32
  %sdiv = sdiv i32 %a, %b
  ret void
}

define void @wide_by_constant(i32 %a) {
;; A wide divide by a constant never reaches a builtin: the combiner turns it
;; into a multiply and a shift.  Pricing it as a call would be the largest
;; single error this file could make, so both forms are here to hold them
;; apart - 144 above against 8 here.
; TIME: cost of 8 for instruction:   %d13 = udiv i32
; SIZE: cost of 8 for instruction:   %d13 = udiv i32
  %d13 = udiv i32 %a, 13
;; A power of two is just the shift.
; TIME: cost of 5 for instruction:   %d16 = udiv i32
  %d16 = udiv i32 %a, 16
;; A constant multiplier drops the partial products that are zero.
; TIME: cost of 7 for instruction:   %m13 = mul i32
  %m13 = mul i32 %a, 13
; TIME: cost of 5 for instruction:   %m16 = mul i32
  %m16 = mul i32 %a, 16
  ret void
}
