; RUN: llc -mtriple=c166 -mattr=+mac -O2 < %s | FileCheck %s

; A special function register is a memory location with a name, so pinning an
; asm operand to one is not a register to register move: it is the absolute
; addressed MOV, the same instruction with the same address in it that the
; assembler emits for "mov r2, idx0".  Nothing selects a copy like this - an
; asm statement naming the register by hand is the only way to ask for one,
; which is also the only way to reach the multiply-accumulate unit from C.

; The coprocessor's pointer, read back out.
define i16 @read_idx0() {
; CHECK-LABEL: read_idx0:
; CHECK:         mov r2, idx0
  %v = call i16 asm sideeffect "nop", "={idx0}"()
  ret i16 %v
}

; And written.
define void @write_idx0(i16 %v) {
; CHECK-LABEL: write_idx0:
; CHECK:         mov idx0, r2
  call void asm sideeffect "nop", "{idx0}"(i16 %v)
  ret void
}

; An extended register is reached the same way and from the other base: QX0 is
; at F000H, not at FE00H plus twice a short address.
define void @write_qx0(i16 %v) {
; CHECK-LABEL: write_qx0:
; CHECK:         mov qx0, r2
  call void asm sideeffect "nop", "{qx0}"(i16 %v)
  ret void
}

; Both halves of the accumulator, which is what a hand written
; multiply-accumulate loop has to save and restore around itself.
define i32 @read_acc() {
; CHECK-LABEL: read_acc:
; CHECK-DAG:     mov r{{[0-9]+}}, mal
; CHECK-DAG:     mov r{{[0-9]+}}, mah
  %lo = call i16 asm sideeffect "nop", "={mal}"()
  %hi = call i16 asm sideeffect "nop", "={mah}"()
  %lo32 = zext i16 %lo to i32
  %hi32 = zext i16 %hi to i32
  %sh = shl i32 %hi32, 16
  %r = or i32 %sh, %lo32
  ret i32 %r
}

; Naming one in a clobber list needs no move at all, and is how an asm
; statement says it has used the unit.
define void @clobber() {
; CHECK-LABEL: clobber:
; CHECK:         comul r2, r3
  call void asm sideeffect "comul r2, r3", "~{mal},~{mah},~{msw}"()
  ret void
}
