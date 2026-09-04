; RUN: llc -mtriple=c166 -mcpu=xc16x -verify-machineinstrs < %s | FileCheck %s

; A near walk folds its step into the access - "mov r5, [r4+]" - and until this
; existed a confined far walk did not, so the __seg loop carried an "add r5, #2"
; the near one did not need.  Two states an element, which is what separates a
; confined walk from a near one apart from the EXTS itself.
;
; The generic post-index machinery cannot do it.  The pointer here is 32 bits
; and C166LowerSegPointers has rewritten the arithmetic into an add on its low
; half wrapped back up with an OR, so there is no ADD on a pointer for
; getPostIndexedAddressParts() to recognise.  After the load is lowered there
; is one again, on the offset half, and that is where the fold happens.

define i32 @seg_walk(ptr addrspace(2) %p, i16 %n) {
; CHECK-LABEL: seg_walk:
; CHECK:         exts r3, #1
; CHECK-NEXT:    mov r7, [r5+]
; CHECK-NOT:     add r5, #2
; CHECK:         jmpr cc_NE
entry:
  %cmp = icmp eq i16 %n, 0
  br i1 %cmp, label %done, label %loop

loop:
  %i = phi i16 [ 0, %entry ], [ %inc, %loop ]
  %s = phi i32 [ 0, %entry ], [ %add, %loop ]
  %ptr = phi ptr addrspace(2) [ %p, %entry ], [ %next, %loop ]
  %v = load i16, ptr addrspace(2) %ptr, align 2
  %z = zext i16 %v to i32
  %add = add i32 %s, %z
  %next = getelementptr inbounds i16, ptr addrspace(2) %ptr, i16 1
  %inc = add i16 %i, 1
  %ec = icmp eq i16 %inc, %n
  br i1 %ec, label %done, label %loop

done:
  %r = phi i32 [ 0, %entry ], [ %add, %loop ]
  ret i32 %r
}

; A byte walk steps by one, and gets the byte form of the same instruction.
define i16 @seg_bytes(ptr addrspace(2) %p, i16 %n) {
; CHECK-LABEL: seg_bytes:
; CHECK:         exts r3, #1
; CHECK-NEXT:    movb rl6, [r5+]
; CHECK-NOT:     add r5, #1
; CHECK:         jmpr cc_NE
entry:
  %cmp = icmp eq i16 %n, 0
  br i1 %cmp, label %done, label %loop

loop:
  %i = phi i16 [ 0, %entry ], [ %inc, %loop ]
  %s = phi i16 [ 0, %entry ], [ %add, %loop ]
  %ptr = phi ptr addrspace(2) [ %p, %entry ], [ %next, %loop ]
  %v = load i8, ptr addrspace(2) %ptr, align 1
  %z = zext i8 %v to i16
  %add = add i16 %s, %z
  %next = getelementptr inbounds i8, ptr addrspace(2) %ptr, i16 1
  %inc = add i16 %i, 1
  %ec = icmp eq i16 %inc, %n
  br i1 %ec, label %done, label %loop

done:
  %r = phi i16 [ 0, %entry ], [ %add, %loop ]
  ret i16 %r
}

; An unconfined far walk is left alone: its arithmetic carries into the
; segment, so stepping the offset on its own would be a different address.
define i32 @far_walk(ptr addrspace(1) %p, i16 %n) {
; CHECK-LABEL: far_walk:
; CHECK:         exts r3, #1
; CHECK-NEXT:    mov r7, [r5]
; CHECK:         add r5, #2
; CHECK:         addc r3, #0
entry:
  %cmp = icmp eq i16 %n, 0
  br i1 %cmp, label %done, label %loop

loop:
  %i = phi i16 [ 0, %entry ], [ %inc, %loop ]
  %s = phi i32 [ 0, %entry ], [ %add, %loop ]
  %ptr = phi ptr addrspace(1) [ %p, %entry ], [ %next, %loop ]
  %v = load i16, ptr addrspace(1) %ptr, align 2
  %z = zext i16 %v to i32
  %add = add i32 %s, %z
  %next = getelementptr inbounds i16, ptr addrspace(1) %ptr, i16 1
  %inc = add i16 %i, 1
  %ec = icmp eq i16 %inc, %n
  br i1 %ec, label %done, label %loop

done:
  %r = phi i32 [ 0, %entry ], [ %add, %loop ]
  ret i32 %r
}
