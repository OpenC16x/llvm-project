; RUN: llc -mtriple=c166 -mattr=+mac -O2 < %s | FileCheck %s --check-prefix=MAC
; RUN: llc -mtriple=c166 -O2 < %s | FileCheck %s --check-prefix=NOMAC

; A 32 bit signed minimum or maximum is one comparison against the
; coprocessor's 40 bit accumulator.  CoLOAD sign extends the first operand
; into 40 bits, CoMAX or CoMIN takes whichever of it and the second is wanted,
; and the two CoSTOREs bring the answer back.  The extension is what makes the
; 40 bit comparison the 32 bit one asked for, and truncating on the way out
; loses nothing because the answer is one of the two operands.
;
; Without the unit the type legalizer compares the two words in order and
; carries the equal case between them, which is five basic blocks.

define i32 @max32(i32 %a, i32 %b) {
; MAC-LABEL: max32:
; MAC:         coload r2, r3
; MAC-NEXT:    comax r4, r5
; MAC-NEXT:    costore r2, mal
; MAC-NEXT:    costore r3, mah
; MAC-NEXT:    ret
;
; NOMAC-LABEL: max32:
; NOMAC-NOT:   comax
; NOMAC:       cmp
  %r = call i32 @llvm.smax.i32(i32 %a, i32 %b)
  ret i32 %r
}

define i32 @min32(i32 %a, i32 %b) {
; MAC-LABEL: min32:
; MAC:         coload r2, r3
; MAC-NEXT:    comin r4, r5
; MAC-NEXT:    costore r2, mal
; MAC-NEXT:    costore r3, mah
  %r = call i32 @llvm.smin.i32(i32 %a, i32 %b)
  ret i32 %r
}

; Unsigned has no form here: CoMIN and CoMAX compare signed and there is no
; unsigned pair of them, so this stays what it was.
define i32 @umax32(i32 %a, i32 %b) {
; MAC-LABEL: umax32:
; MAC-NOT:     comax
; MAC:         cmp
  %r = call i32 @llvm.umax.i32(i32 %a, i32 %b)
  ret i32 %r
}

; And 16 bits is left alone: a compare and a conditional move is already three
; instructions and six bytes, where going through the unit would be four
; four-byte ones.
define i16 @max16(i16 %a, i16 %b) {
; MAC-LABEL: max16:
; MAC-NOT:     comax
; MAC:         cmp r2, r3
; MAC-NEXT:    jmpr cc_SGT
  %r = call i16 @llvm.smax.i16(i16 %a, i16 %b)
  ret i16 %r
}

declare i32 @llvm.smax.i32(i32, i32)
declare i32 @llvm.smin.i32(i32, i32)
declare i32 @llvm.umax.i32(i32, i32)
declare i16 @llvm.smax.i16(i16, i16)
