; RUN: llc -mtriple=c166 < %s | FileCheck %s

; Where a block copy or fill stops being written out and becomes a call.  A
; "store" is one element of whatever width the copy is done in, which here is a
; word: globals and these pointers are word aligned, so eight stores is sixteen
; bytes copied and sixteen stores is thirty-two bytes filled.
;
; The numbers come from measuring against the runtime in startup/mem.c - the
; comment on them in C166ISelLowering.cpp has the figures.  Each pair below is
; the boundary and one size past it.

declare void @llvm.memcpy.p0.p0.i16(ptr, ptr, i16, i1)
declare void @llvm.memset.p0.i16(ptr, i8, i16, i1)

define void @copy_16(ptr %d, ptr %s) {
  call void @llvm.memcpy.p0.p0.i16(ptr align 2 %d, ptr align 2 %s, i16 16, i1 false)
  ret void
}
; CHECK-LABEL: copy_16:
; CHECK-NOT:   memcpy
; CHECK:       ret

define void @copy_18(ptr %d, ptr %s) {
  call void @llvm.memcpy.p0.p0.i16(ptr align 2 %d, ptr align 2 %s, i16 18, i1 false)
  ret void
}
; CHECK-LABEL: copy_18:
; CHECK:       memcpy

define void @fill_32(ptr %d) {
  call void @llvm.memset.p0.i16(ptr align 2 %d, i8 0, i16 32, i1 false)
  ret void
}
; CHECK-LABEL: fill_32:
; CHECK-NOT:   memset
; CHECK:       ret

define void @fill_34(ptr %d) {
  call void @llvm.memset.p0.i16(ptr align 2 %d, i8 0, i16 34, i1 false)
  ret void
}
; CHECK-LABEL: fill_34:
; CHECK:       memset

; Optimising for size moves both boundaries down, because there the trade is
; the inline sequence's bytes against the sixteen a call site costs rather than
; its time: a copy is eight bytes a word and a fill four.

define void @copy_4_optsize(ptr %d, ptr %s) optsize {
  call void @llvm.memcpy.p0.p0.i16(ptr align 2 %d, ptr align 2 %s, i16 4, i1 false)
  ret void
}
; CHECK-LABEL: copy_4_optsize:
; CHECK-NOT:   memcpy
; CHECK:       ret

define void @copy_6_optsize(ptr %d, ptr %s) optsize {
  call void @llvm.memcpy.p0.p0.i16(ptr align 2 %d, ptr align 2 %s, i16 6, i1 false)
  ret void
}
; CHECK-LABEL: copy_6_optsize:
; CHECK:       memcpy

define void @fill_8_optsize(ptr %d) optsize {
  call void @llvm.memset.p0.i16(ptr align 2 %d, i8 0, i16 8, i1 false)
  ret void
}
; CHECK-LABEL: fill_8_optsize:
; CHECK-NOT:   memset
; CHECK:       ret

define void @fill_10_optsize(ptr %d) optsize {
  call void @llvm.memset.p0.i16(ptr align 2 %d, i8 0, i16 10, i1 false)
  ret void
}
; CHECK-LABEL: fill_10_optsize:
; CHECK:       memset

; A move is a copy the compiler cannot prove does not overlap, so it costs the
; same per word and is cut at the same place.
declare void @llvm.memmove.p0.p0.i16(ptr, ptr, i16, i1)

define void @move_16(ptr %d, ptr %s) {
  call void @llvm.memmove.p0.p0.i16(ptr align 2 %d, ptr align 2 %s, i16 16, i1 false)
  ret void
}
; CHECK-LABEL: move_16:
; CHECK-NOT:   memmove
; CHECK:       ret

define void @move_18(ptr %d, ptr %s) {
  call void @llvm.memmove.p0.p0.i16(ptr align 2 %d, ptr align 2 %s, i16 18, i1 false)
  ret void
}
; CHECK-LABEL: move_18:
; CHECK:       memmove
