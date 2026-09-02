## A handler with a register bank of its own: where the bank lands, and that
## the interrupted code's registers really do come back untouched.

# REQUIRES: c166
# RUN: rm -rf %t && split-file %s %t
# RUN: llc -filetype=obj -mtriple=c166 %t/isr.ll -o %t/isr.o
# RUN: llvm-mc -filetype=obj -triple=c166 %t/start.s -o %t/start.o
# RUN: ld.lld -T %S/../../../llvm/lib/Target/C166/startup/c166.ld \
# RUN:   %t/start.o %t/isr.o -o %t/prog.exe

## The bank has to be in internal RAM, which is the only memory a context
## pointer may name.  The reset register bank is the sixteen words at FC00H, so
## the banks region starts at FC20H and runs to the bit-addressable RAM at
## FD00H.
# RUN: llvm-nm %t/prog.exe | FileCheck %s --check-prefix=WHERE
# WHERE: 0000fc20 {{[bB]}} __c166_bank_clobber_isr

## It is NOBITS, so it costs nothing in the image.
# RUN: llvm-readelf -S %t/prog.exe | FileCheck %s --check-prefix=NOBITS
# NOBITS: .c166_banks NOBITS 0000fc20 {{[0-9a-f]+}} 000020

## And the proof.  The handler is written to use as much of a register bank as
## the allocator will give it, and it saves none of it - so if the switch were
## not happening, what it writes would land on the interrupted code's R6 to
## R11.  Those hold 1111H to 6666H across the interrupt and R5 is their sum,
## which comes to 6665H only if every one of them survived.
# RUN: c166-sim --dump-state --interrupt-at=60:26:8 %t/prog.exe 2>&1 \
# RUN:   | FileCheck %s --check-prefix=RAN
# RAN: R5=6665
## The handler did run: it counts itself in a static, and main copies that to
## R12 at the end.
# RAN: R12=0001

#--- isr.ll
target triple = "c166"

@ticks = external global i16
@slots = external global [12 x i16]

; Twelve live values at once, so the allocator has to use most of a bank.
define void @clobber_isr() #0 {
  %p0 = getelementptr [12 x i16], ptr @slots, i16 0, i16 0
  %p1 = getelementptr [12 x i16], ptr @slots, i16 0, i16 1
  %p2 = getelementptr [12 x i16], ptr @slots, i16 0, i16 2
  %p3 = getelementptr [12 x i16], ptr @slots, i16 0, i16 3
  %p4 = getelementptr [12 x i16], ptr @slots, i16 0, i16 4
  %p5 = getelementptr [12 x i16], ptr @slots, i16 0, i16 5
  %v0 = load volatile i16, ptr %p0
  %v1 = load volatile i16, ptr %p1
  %v2 = load volatile i16, ptr %p2
  %v3 = load volatile i16, ptr %p3
  %v4 = load volatile i16, ptr %p4
  %v5 = load volatile i16, ptr %p5
  %a = add i16 %v0, %v3
  %b = xor i16 %v1, %v4
  %c = sub i16 %v2, %v5
  store volatile i16 %a, ptr %p0
  store volatile i16 %b, ptr %p1
  store volatile i16 %c, ptr %p2
  store volatile i16 %v0, ptr %p3
  store volatile i16 %v1, ptr %p4
  store volatile i16 %v2, ptr %p5
  %t = load volatile i16, ptr @ticks
  %n = add i16 %t, 1
  store volatile i16 %n, ptr @ticks
  ret void
}

attributes #0 = { noinline "interrupt" "c166-bank" "c166-interrupt-vector"="26" }

#--- start.s
        .section .reset,"ax",@progbits
        .globl __reset_vector
__reset_vector:
        jmps    #seg(_begin), sof(_begin)

        .text
        .globl _begin
_begin:
        mov     r6, #0x1111
        mov     r7, #0x2222
        mov     r8, #0x3333
        mov     r9, #0x4444
        mov     r10, #0x5555
        mov     r11, #0x6666
        bset    psw.11
        mov     r4, #60
.Lwait:
        sub     r4, #1
        jmpr    cc_NZ, .Lwait
## R5 is the sum of the six, and R12 says the handler ran at all - a sum that
## is right because nothing happened would prove nothing.
        mov     r5, r6
        add     r5, r7
        add     r5, r8
        add     r5, r9
        add     r5, r10
        add     r5, r11
        mov     r12, ticks
        mov     r2, #0
        .globl  __c166_exit
__c166_exit:
        nop

        .data
        .globl ticks
ticks:
        .short  0
        .globl slots
slots:
        .short  1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12
