; RUN: llc -mtriple=c166 -O2 < %s | FileCheck %s

; AND and OR set Z and N from what they wrote, so a compare against zero
; directly after one asks a question that is already answered.

define i16 @and_eq(i16 %x) {
; CHECK-LABEL: and_eq:
; CHECK:         and r2, #16
; CHECK-NEXT:    jmpr cc_NE,
; CHECK-NOT:     cmp
entry:
  %a = and i16 %x, 16
  %c = icmp eq i16 %a, 0
  br i1 %c, label %t, label %f
t:
  ret i16 1
f:
  ret i16 %a
}

define i16 @or_ne(i16 %x, i16 %y) {
; CHECK-LABEL: or_ne:
; CHECK:         or r2, r3
; CHECK-NEXT:    jmpr cc_EQ,
; CHECK-NOT:     cmp
entry:
  %a = or i16 %x, %y
  %c = icmp ne i16 %a, 0
  br i1 %c, label %t, label %f
t:
  ret i16 %a
f:
  ret i16 7
}

; A compare against something other than zero is a different question.

define i16 @against_five(i16 %x) {
; CHECK-LABEL: against_five:
; CHECK:         cmp r2, #5
; CHECK-NEXT:    jmpr cc_NE,
entry:
  %c = icmp eq i16 %x, 5
  br i1 %c, label %t, label %f
t:
  ret i16 1
f:
  ret i16 0
}

; The register compared has to be the one the instruction before it wrote.

define i16 @other_register(i16 %x, i16 %y) {
; CHECK-LABEL: other_register:
; CHECK:         cmp r3, #0
; CHECK-NEXT:    jmpr cc_EQ,
entry:
  %a = and i16 %x, 16
  %c = icmp eq i16 %y, 0
  br i1 %c, label %t, label %f
t:
  ret i16 %a
f:
  ret i16 0
}

; A load does not set the flags from what it loaded as far as the machine
; description is concerned, so the compare after one stays.

define i16 @after_load(ptr %p) {
; CHECK-LABEL: after_load:
; CHECK:         mov r2, [r2]
; CHECK-NEXT:    cmp r2, #0
entry:
  %v = load i16, ptr %p
  %c = icmp eq i16 %v, 0
  br i1 %c, label %t, label %f
t:
  ret i16 1
f:
  ret i16 %v
}
