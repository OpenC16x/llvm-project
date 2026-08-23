; RUN: llc -mtriple=c166 -O2 < %s | FileCheck %s

; Testing one bit and branching on it is a single instruction.  Masking the
; value, comparing it against zero and jumping is three, and it needs a
; register to hold the masked copy; JB and JNB leave what they test alone, and
; on a bit-addressable word in memory they read it themselves.
;
; They also have no long form, so one whose target is too far away has to be
; turned into the opposite branch over a jump that can reach - @too_far below.
; Without that this would not be a worse instruction but a build failure.

declare void @act()

; A branch taken when the bit is set is the branch over it that goes on clear.
define void @sfr_set() {
; CHECK-LABEL: sfr_set:
; CHECK:      jnb p3.8, .LBB0_2
  %v = load volatile i16, ptr inttoptr (i16 -60 to ptr)
  %b = and i16 %v, 256
  %c = icmp ne i16 %b, 0
  br i1 %c, label %t, label %e
t:
  call void @act()
  br label %e
e:
  ret void
}

define void @sfr_clear() {
; CHECK-LABEL: sfr_clear:
; CHECK:      jb p3.3, .LBB1_2
  %v = load volatile i16, ptr inttoptr (i16 -60 to ptr)
  %b = and i16 %v, 8
  %c = icmp eq i16 %b, 0
  br i1 %c, label %t, label %e
t:
  call void @act()
  br label %e
e:
  ret void
}

; The bitoff of a register is F0H + its number, so no address is involved.
define void @reg() {
; CHECK-LABEL: reg:
; CHECK:      jnb r2.5, .LBB2_2
  %x = call i16 @get()
  %b = and i16 %x, 32
  %c = icmp ne i16 %b, 0
  br i1 %c, label %t, label %e
t:
  call void @act()
  br label %e
e:
  ret void
}
declare i16 @get()

; Waiting on a flag in the top bit reaches the backend as a sign test, because
; the combiner rewrites "x & 8000H" against zero into "x" against zero signed.
; It is the same bit, and this is the shape a busy wait on a peripheral takes.
define void @busy_wait() {
; CHECK-LABEL: busy_wait:
; CHECK:      .LBB3_1:
; CHECK:      jb ssccon.15, .LBB3_1
  br label %loop
loop:
  %v = load volatile i16, ptr inttoptr (i16 -78 to ptr)
  %c = icmp slt i16 %v, 0
  br i1 %c, label %loop, label %done
done:
  ret void
}

; The combiner spells the sign test either way round depending on how the
; branch was written - here as "x <= -1" rather than "x < 0" - and both are the
; same bit.
define void @top_bit_other_spelling() {
; CHECK-LABEL: top_bit_other_spelling:
; CHECK: jb ssccon.15
  %v = load volatile i16, ptr inttoptr (i16 -78 to ptr)
  %c = icmp slt i16 %v, 0
  br i1 %c, label %t, label %e
t:
  call void @act()
  br label %e
e:
  ret void
}

; The same sign test on a register is not worth it: a compare and a jump are
; already the four bytes the bit branch would be, with no load to save.
define void @sign_test_register(i16 %x) {
; CHECK-LABEL: sign_test_register:
; CHECK: cmp r2, #-1
; CHECK: jmpr cc_SLE
  %c = icmp slt i16 %x, 0
  br i1 %c, label %t, label %e
t:
  call void @act()
  br label %e
e:
  ret void
}

; Two bits is not one bit.
define void @two_bits(i16 %x) {
; CHECK-LABEL: two_bits:
; CHECK: and r2, #96
; CHECK: jmpr
  %b = and i16 %x, 96
  %c = icmp ne i16 %b, 0
  br i1 %c, label %t, label %e
t:
  call void @act()
  br label %e
e:
  ret void
}

; Eight bytes to jump over is well inside the reach of the displacement.
define void @near_enough(i16 %x) {
; CHECK-LABEL: near_enough:
; CHECK:      jnb r2.5, .LBB7_2
; CHECK-NOT:  jmpr
  %b = and i16 %x, 32
  %c = icmp ne i16 %b, 0
  br i1 %c, label %t, label %e
t:
  call void asm sideeffect ".space 8", ""()
  br label %e
e:
  ret void
}

; Three hundred is not: the displacement is a signed 8 bit count of words, so
; this becomes the opposite branch over a jump, and the jump is a JMPR the
; assembler grows into a JMPA by itself.
define void @too_far(i16 %x) {
; CHECK-LABEL: too_far:
; CHECK:      jb r2.5, .LBB8_1
; CHECK-NEXT: jmpr cc_UC, .LBB8_2
  %b = and i16 %x, 32
  %c = icmp ne i16 %b, 0
  br i1 %c, label %t, label %e
t:
  call void asm sideeffect ".space 300", ""()
  br label %e
e:
  ret void
}

; Measuring the code is what decides whether a branch can reach, and a bundle -
; here an EXTend and the far access it covers - carries no size of its own.
; Counting one as nothing would make this body look tiny, leave the short
; branch in place, and fail to assemble.
@fd = addrspace(1) global i16 7
define void @over_bundles(i16 %x) {
; CHECK-LABEL: over_bundles:
; CHECK:      jb r2.5, .LBB9_1
; CHECK-NEXT: jmpr cc_UC, .LBB9_2
  %b = and i16 %x, 32
  %c = icmp ne i16 %b, 0
  br i1 %c, label %t, label %e
t:
  store volatile i16 0, ptr addrspace(1) @fd
  store volatile i16 1, ptr addrspace(1) @fd
  store volatile i16 2, ptr addrspace(1) @fd
  store volatile i16 3, ptr addrspace(1) @fd
  store volatile i16 4, ptr addrspace(1) @fd
  store volatile i16 5, ptr addrspace(1) @fd
  store volatile i16 6, ptr addrspace(1) @fd
  store volatile i16 7, ptr addrspace(1) @fd
  store volatile i16 8, ptr addrspace(1) @fd
  store volatile i16 9, ptr addrspace(1) @fd
  store volatile i16 10, ptr addrspace(1) @fd
  store volatile i16 11, ptr addrspace(1) @fd
  store volatile i16 12, ptr addrspace(1) @fd
  store volatile i16 13, ptr addrspace(1) @fd
  store volatile i16 14, ptr addrspace(1) @fd
  store volatile i16 15, ptr addrspace(1) @fd
  store volatile i16 16, ptr addrspace(1) @fd
  store volatile i16 17, ptr addrspace(1) @fd
  store volatile i16 18, ptr addrspace(1) @fd
  store volatile i16 19, ptr addrspace(1) @fd
  store volatile i16 20, ptr addrspace(1) @fd
  store volatile i16 21, ptr addrspace(1) @fd
  store volatile i16 22, ptr addrspace(1) @fd
  store volatile i16 23, ptr addrspace(1) @fd
  store volatile i16 24, ptr addrspace(1) @fd
  store volatile i16 25, ptr addrspace(1) @fd
  store volatile i16 26, ptr addrspace(1) @fd
  store volatile i16 27, ptr addrspace(1) @fd
  store volatile i16 28, ptr addrspace(1) @fd
  store volatile i16 29, ptr addrspace(1) @fd
  store volatile i16 30, ptr addrspace(1) @fd
  store volatile i16 31, ptr addrspace(1) @fd
  store volatile i16 32, ptr addrspace(1) @fd
  store volatile i16 33, ptr addrspace(1) @fd
  store volatile i16 34, ptr addrspace(1) @fd
  store volatile i16 35, ptr addrspace(1) @fd
  store volatile i16 36, ptr addrspace(1) @fd
  store volatile i16 37, ptr addrspace(1) @fd
  store volatile i16 38, ptr addrspace(1) @fd
  store volatile i16 39, ptr addrspace(1) @fd
  br label %e
e:
  ret void
}
