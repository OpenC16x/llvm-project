## Where __attribute__((interrupt(n))) puts a handler, checked all the way
## through: the section the AsmPrinter emits, the address the stock linker
## script gives it, and the handler actually being entered when the vector is
## taken.  The script is c166.ld itself rather than one written for the test,
## because whether that one carries the table is the thing being checked.

# REQUIRES: c166
# RUN: rm -rf %t && split-file %s %t
# RUN: llc -filetype=obj -mtriple=c166 %t/isr.ll -o %t/isr.o
# RUN: llvm-mc -filetype=obj -triple=c166 %t/start.s -o %t/start.o
# RUN: ld.lld -T %S/../../../llvm/lib/Target/C166/startup/c166.ld \
# RUN:   %t/start.o %t/isr.o -o %t/prog.exe

## Slot n is at 4n into the segment the table lives in, which is where the ROM
## starts.  Slot 1 is the four bytes after the reset vector and slot 26 is at
## 68H, and each holds a jump rather than an address because that is what the
## part branches to.
# RUN: llvm-objdump -d --section=.reset %t/prog.exe | FileCheck %s --check-prefix=TABLE

# TABLE:      c00000: {{.*}} jmps #192,
# TABLE-NEXT: c00004: {{.*}} jmps #192,
## Everything between the two claimed slots is LLD's trap pattern, so a
## spurious interrupt stops rather than running into whatever follows.
# TABLE-NEXT: c00008: {{.*}} jmpr cc_UC
# TABLE:      c00068: {{.*}} jmps #192,

## The whole table is there once anything claims a slot: 512 bytes, ending
## just before .text.
# RUN: llvm-readelf -S %t/prog.exe | FileCheck %s --check-prefix=SIZE
# SIZE: .reset PROGBITS 00c00000 {{[0-9a-f]+}} 000200

## That each slot holds a jump to the right handler is what running it shows,
## and shows better than comparing addresses would: the two are entered
## through the table by the hardware path, and R5 counts what ran - 1 for slot
## 1 and 2 more for slot 26, so 3 says both, and either one alone would not.
# RUN: c166-sim --dump-state --interrupt-at=40:1:8 --interrupt-at=80:26:9 \
# RUN:   %t/prog.exe 2>&1 | FileCheck %s --check-prefix=RAN
# RAN: R5=0003

## And with nothing injected neither runs, so the count above is the handlers
## and not something the program did to itself.
# RUN: c166-sim --dump-state %t/prog.exe 2>&1 | FileCheck %s --check-prefix=QUIET
# QUIET: R5=0000

## A program with no handlers pays nothing for the table, which is why the
## rows of it are conditional in the first place.
# RUN: ld.lld -T %S/../../../llvm/lib/Target/C166/startup/c166.ld \
# RUN:   %t/start.o -o %t/bare.exe
# RUN: llvm-readelf -S %t/bare.exe | FileCheck %s --check-prefix=BARE
# BARE: .reset PROGBITS 00c00000 {{[0-9a-f]+}} 000004

#--- isr.ll
target triple = "c166"

@counter = external global i16

define void @adc_isr() #0 {
  %v = load volatile i16, ptr @counter
  %n = add i16 %v, 1
  store volatile i16 %n, ptr @counter
  ret void
}

define void @t3_isr() #1 {
  %v = load volatile i16, ptr @counter
  %n = add i16 %v, 2
  store volatile i16 %n, ptr @counter
  ret void
}

attributes #0 = { noinline "interrupt" "c166-interrupt-vector"="1" }
attributes #1 = { noinline "interrupt" "c166-interrupt-vector"="26" }

#--- start.s
        .section .reset,"ax",@progbits
        .globl __reset_vector
__reset_vector:
        jmps    #seg(_begin), sof(_begin)

        .text
        .globl _begin
_begin:
## Both handlers write through this, and R5 is what the run is read from.
        mov     r2, #0
        mov     r3, #counter
        mov     [r3], r2
        bset    psw.11
        mov     r4, #60
.Lwait:
        sub     r4, #1
        jmpr    cc_NZ, .Lwait
## R5 carries the count and R2 is the program's exit code, which has to be
## zero for the run to be a pass rather than a failure.
        mov     r5, [r3]
        mov     r2, #0
        .globl  __c166_exit
__c166_exit:
        nop

        .data
        .globl counter
counter:
        .short  0
