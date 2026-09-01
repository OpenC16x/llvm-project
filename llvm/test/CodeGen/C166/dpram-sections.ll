; RUN: llc -mtriple=c166 -mattr=+mac < %s | FileCheck %s

; The dual-port RAM is the only memory the coprocessor's IDX0 and IDX1 reach -
; PM0036 section 2.1 - so an array a CoMAC walks through [idx0+] has to be
; there.  The "c166-dpram" attribute is what clang's __dpram leaves behind, and
; the section is picked here rather than named in the frontend so that a zero
; initialised object gets a NOBITS section and does not carry its zeroes in the
; image.

; CHECK: .section .dprambss,"aw",@nobits
; CHECK: delay:
@delay = global [8 x i16] zeroinitializer, align 2 #0

; CHECK: .section .dpramdata,"aw",@progbits
; CHECK: seeded:
@seeded = global [4 x i16] [i16 1, i16 2, i16 3, i16 4], align 2 #0

; A read-only one still goes in the writable section: being in the dual-port
; RAM means being in RAM, so it is copied out of the image either way and there
; is nowhere else for it to be.  It follows seeded with no directive of its
; own, which is what says it is in the same section.
; CHECK-NOT: .section
; CHECK: coeffs:
@coeffs = constant [2 x i16] [i16 7, i16 9], align 2 #0

; Without the attribute nothing changes.
; CHECK: .section .bss,"aw",@nobits
; CHECK: ordinary:
@ordinary = global [8 x i16] zeroinitializer, align 2

attributes #0 = { "c166-dpram" }
