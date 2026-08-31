; RUN: llc -mtriple=c166 -O2 < %s | FileCheck %s

; A variable in the bit-addressable RAM, which is what clang's __bitaddr
; places.  The instruction takes an 8 bit word number of that space rather than
; an address, and which word the variable is is the linker's answer, so the
; symbol goes in the instruction and the byte is a relocation.

@flags = global i16 0, align 2 #0
@byteflags = global i8 0, align 2 #0
@ordinary = global i16 0, align 2

declare void @act()

; Setting one bit: two bytes, and one indivisible bus operation rather than a
; read and a write an interrupt can arrive between.
define void @arm() {
; CHECK-LABEL: arm:
; CHECK: bset flags.3
  %v = load i16, ptr @flags, align 2
  %r = or i16 %v, 8
  store i16 %r, ptr @flags, align 2
  ret void
}

define void @disarm() {
; CHECK-LABEL: disarm:
; CHECK: bclr flags.3
  %v = load i16, ptr @flags, align 2
  %r = and i16 %v, -9
  store i16 %r, ptr @flags, align 2
  ret void
}

; And testing one, on a word the instruction reads for itself.
define void @poll() {
; CHECK-LABEL: poll:
; CHECK: jnb flags.3
entry:
  %v = load volatile i16, ptr @flags, align 2
  %b = and i16 %v, 8
  %c = icmp eq i16 %b, 0
  br i1 %c, label %out, label %hit
hit:
  call void @act()
  br label %out
out:
  ret void
}

; A byte of the word is the same word: the low byte is the same bit position
; and the high byte is eight bits up, which is where the odd offset goes.
define void @poll_high_byte() {
; CHECK-LABEL: poll_high_byte:
; CHECK: jnb flags.11
entry:
  %p = getelementptr inbounds i8, ptr @flags, i16 1
  %v = load volatile i8, ptr %p, align 1
  %b = and i8 %v, 8
  %c = icmp eq i8 %b, 0
  br i1 %c, label %out, label %hit
hit:
  call void @act()
  br label %out
out:
  ret void
}

; A variable that is a byte names its own word.
define void @poll_byte() {
; CHECK-LABEL: poll_byte:
; CHECK: jnb byteflags.5
entry:
  %v = load volatile i8, ptr @byteflags, align 2
  %b = and i8 %v, 32
  %c = icmp eq i8 %b, 0
  br i1 %c, label %out, label %hit
hit:
  call void @act()
  br label %out
out:
  ret void
}

; Without the attribute there is no bit address to name, so it stays an ALU
; operation on a word that could be anywhere.
define void @not_bit_addressable() {
; CHECK-LABEL: not_bit_addressable:
; CHECK-NOT: bset
; CHECK: or ordinary
  %v = load i16, ptr @ordinary, align 2
  %r = or i16 %v, 8
  store i16 %r, ptr @ordinary, align 2
  ret void
}

attributes #0 = { "c166-bitaddr" }
