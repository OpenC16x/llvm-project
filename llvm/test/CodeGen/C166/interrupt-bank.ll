; RUN: llc -mtriple=c166 < %s | FileCheck %s

; R0 to R15 are a window into internal RAM at the address CP holds, so moving
; CP moves the whole window.  A handler with a bank of its own enters with
; SCXT, which pushes CP and loads the new one, and leaves with POP CP; the
; interrupted code's registers are never touched, so nothing is spilled and
; nothing is reloaded.

@counter = external global i16
declare i16 @work(i16)

; A handler that calls something is the case that saves the most: without a
; bank it spills R2 to R11 before it can do anything.
define void @calls_out() #0 {
  %v = load volatile i16, ptr @counter
  %r = call i16 @work(i16 %v)
  store volatile i16 %r, ptr @counter
  ret void
}

; CHECK-LABEL: calls_out:
; CHECK:      scxt cp, #__c166_bank_calls_out
; Whether the next instruction already sees the new window is not settled by
; any manual to hand, and being wrong about it would put this handler's first
; register write in the interrupted code's bank.
; CHECK-NEXT: nop
; R0 is the ABI stack pointer and belongs to the interrupted code rather than
; to the window, so it is brought across: SP names the word SCXT just pushed
; the old CP into, and R0 is the first word of the bank that names.
; CHECK-NEXT: mov r1, sp
; CHECK-NEXT: mov r1, [r1]
; CHECK-NEXT: mov r0, [r1]
; The multiply/divide unit is not part of a bank, so a handler that calls
; anything still saves it.
; CHECK-NEXT: push mdc
; CHECK-NOT:  Folded Spill
; CHECK:      calla cc_UC, work
; CHECK-NOT:  Folded Reload
; CHECK:      pop mdc
; CHECK-NEXT: pop cp
; The unwind rules go back to what they were before the prologue, between the
; last pop and the RETI.
; CHECK:      reti

; A leaf with no frame never looks at R0, so it pays nothing for it: the whole
; prologue is one instruction.
define void @leaf() #0 {
  %v = load volatile i16, ptr @counter
  %n = add i16 %v, 1
  store volatile i16 %n, ptr @counter
  ret void
}

; CHECK-LABEL: leaf:
; CHECK:      scxt cp, #__c166_bank_leaf
; CHECK-NEXT: nop
; CHECK-NOT:  mov r1, sp
; CHECK:      pop cp
; CHECK:      reti

; Without the attribute the handler saves what it uses, which is what it has to
; do when the registers are the interrupted code's.
define void @no_bank() #1 {
  %v = load volatile i16, ptr @counter
  %r = call i16 @work(i16 %v)
  store volatile i16 %r, ptr @counter
  ret void
}

; CHECK-LABEL: no_bank:
; CHECK-NOT:  scxt
; CHECK:      Folded Spill

attributes #0 = { noinline "interrupt" "c166-bank" }
attributes #1 = { noinline "interrupt" }

; Thirty-two bytes apiece, one section each so that a handler nothing keeps
; takes its bank with it, and NOBITS because nothing initialises them.
; CHECK: .section .c166_banks.calls_out,"aw",@nobits
; CHECK: __c166_bank_calls_out:
; CHECK: .zero 32
; CHECK: .section .c166_banks.leaf,"aw",@nobits
; CHECK: __c166_bank_leaf:
; CHECK-NOT: .c166_banks.no_bank
