; RUN: llc -mtriple=c166 -O2 < %s | FileCheck %s

;; The shape the runtime division helpers depend on, and the shape that looks
;; like it but is not.  compiler-rt/lib/builtins/c166/int_divmod.h uses the
;; divide unit for a divisor that fits in a word, and it is written the way it
;; is because of what is below; if that stops holding, the helpers stop being
;; fast, or - in the third case - stop terminating.

;; What the helper looks like: the divisor arrives as a word and is widened, so
;; the divisor's high half is zero as a property of the value.  Two divides,
;; no call.  This is what c166_udiv_word compiles to.
define i32 @widened_word_divisor(i32 %n, i16 %d) {
; CHECK-LABEL: widened_word_divisor:
; CHECK-NOT:     __udivsi3
; CHECK:         divu
; CHECK:         divlu
  %z = zext i16 %d to i32
  %q = udiv i32 %n, %z
  ret i32 %q
}

;; A mask says the same thing and is honoured the same way, so the backend is
;; not the fragile part here.
define i32 @masked_divisor(i32 %n, i32 %d) {
; CHECK-LABEL: masked_divisor:
; CHECK-NOT:     __udivsi3
; CHECK:         divu
; CHECK:         divlu
  %m = and i32 %d, 65535
  %q = udiv i32 %n, %m
  ret i32 %q
}

;; The trap.  This is what clang hands the backend for
;;
;;     if (d.s.high == 0) return n / (su_int)d.s.low;
;;
;; written inside the helper: the narrowing is gone, because the branch it sat
;; under already proved it redundant, and what is left is a plain 32 by 32
;; division.  The fact is in the control flow and the backend's test is on the
;; value, so this is a call - and inside __udivsi3 that call is to itself.
;;
;; That is why the fast path is a separate noinline function taking the divisor
;; as a 16 bit parameter: it puts the fact back in the value, where it can be
;; seen.  If this case ever starts emitting the divides, the helper's noinline
;; and its narrow parameter can go.
define i32 @divisor_narrow_only_by_branch(i32 %n, i32 %d) {
; CHECK-LABEL: divisor_narrow_only_by_branch:
; CHECK:         __udivsi3
entry:
  %fits = icmp ult i32 %d, 65536
  br i1 %fits, label %fast, label %slow
fast:
  %q = udiv i32 %n, %d
  ret i32 %q
slow:
  ret i32 0
}
