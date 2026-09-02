; RUN: llc -mtriple=c166 < %s | FileCheck %s
; RUN: llc -mtriple=c166 -filetype=obj < %s -o %t.o
; RUN: llvm-objdump -dr --section=.vectors.026 %t.o | FileCheck %s --check-prefix=OBJ

; A vector table slot holds a jump rather than an address, because TRAP and a
; hardware interrupt both branch to the slot instead of reading through it.
; __attribute__((interrupt(n))) asks for one, and it comes out in a section
; named for the trap number so that startup/vectors.ld can place it at 4n.

define void @t3_isr() #0 {
  ret void
}

define void @adc_isr() #1 {
  ret void
}

; No trap number, so no slot: the handler is still a handler, and how it is
; reached is left to a hand written vector.
define void @dispatched_isr() #2 {
  ret void
}

; A declaration cannot have a body to jump to and must not claim a slot.
declare void @elsewhere() #3

attributes #0 = { noinline "interrupt" "c166-interrupt-vector"="26" }
attributes #1 = { noinline "interrupt" "c166-interrupt-vector"="1" }
attributes #2 = { noinline "interrupt" }
attributes #3 = { noinline "interrupt" "c166-interrupt-vector"="99" }

; The number is padded to three digits so that the section name sorts the way
; the trap number does.
; CHECK:      .section .vectors.026,"ax",@progbits
; CHECK-NEXT: jmps #seg(t3_isr), sof(t3_isr)
; CHECK:      .section .vectors.001,"ax",@progbits
; CHECK-NEXT: jmps #seg(adc_isr), sof(adc_isr)
; CHECK-NOT:  .vectors.099

; The two halves of the address are two relocations against the handler, which
; is what lets it live in any segment.
; OBJ:      <.vectors.026>:
; OBJ-NEXT: jmps
; OBJ-NEXT: R_C166_SEG8 t3_isr
; OBJ-NEXT: R_C166_SOF16 t3_isr
