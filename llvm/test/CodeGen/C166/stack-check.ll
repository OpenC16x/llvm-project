; RUN: llc -mtriple=c166 < %s | FileCheck %s --check-prefix=OFF
; RUN: llc -mtriple=c166 -mattr=+stack-check < %s | FileCheck %s
; RUN: llc -mtriple=c166 -mattr=+stack-check -c166-stack-check-threshold=256 < %s \
; RUN:   | FileCheck %s --check-prefix=HIGH

; The ABI stack is the one the part does not watch.  SP, which holds return
; addresses, is compared against STKOV and STKUN by the hardware; R0, which
; holds frames, spills and locals, is an ordinary register with nothing looking
; at it.  -mstack-check is two instructions in the prologue that say the frame
; just allocated is still above __user_stack_limit, and jump to
; __c166_stack_overflow when it is not.
;
; The comparison goes before the allocation and against the limit plus the
; frame size, so that a checked program never puts R0 below its stack even for
; the two instructions it would take to notice.  The addition is the linker's -
; it rides in the addend of the relocation - so it is still one CMP.

define void @big() {
; CHECK-LABEL: big:
; CHECK:      cmp r0, #__user_stack_limit+128
; CHECK-NEXT: jmpa cc_ULT, __c166_stack_overflow
; CHECK-NEXT: sub r0, #128
;
; A threshold above this frame leaves it alone.
; HIGH-LABEL: big:
; HIGH:      sub r0, #128
; HIGH-NOT:  __user_stack_limit
;
; And the feature is off unless it is asked for.
; OFF-LABEL: big:
; OFF-NOT:   __user_stack_limit
  %a = alloca [64 x i16]
  call void @use(ptr %a)
  ret void
}

; A frame of four bytes or less is the hardware's to catch: every call pushes
; two bytes onto the 512 byte system stack, so a recursion through frames that
; small trips STKOV before it reaches the bottom of the 1 KByte ABI stack.  The
; default threshold is the first size above that.
define void @four() {
; CHECK-LABEL: four:
; CHECK:     sub r0, #4
; CHECK-NOT: __user_stack_limit
  %a = alloca [2 x i16]
  call void @use(ptr %a)
  ret void
}

define void @six() {
; CHECK-LABEL: six:
; CHECK:      cmp r0, #__user_stack_limit+6
; CHECK-NEXT: jmpa cc_ULT, __c166_stack_overflow
; CHECK-NEXT: sub r0, #6
  %a = alloca [3 x i16]
  call void @use(ptr %a)
  ret void
}

; A function that allocates nothing cannot be the one that runs out, whatever
; the threshold is.  That is also what keeps the handler from checking itself.
define void @none() {
; CHECK-LABEL: none:
; CHECK-NOT: __user_stack_limit
  call void @use(ptr null)
  ret void
}

declare void @use(ptr)
