## The ST10's memory map, held against what its data sheets say rather than
## against what the C167 script would have produced.
##
## The Flash is the part that has to be checked by placing something in it:
## 256 KByte arrives as 32 KByte at 00'0000H, a hole from 00'8000H to
## 01'7FFFH, then 32 KByte at 01'8000H and 64 KByte each at 02'0000H,
## 03'0000H and 04'0000H.  A script that divided one length up from zero -
## which is what c167.ld does, correctly for a C167 - would put .fartext at
## 00'8000H, in the hole, and nothing would say so: the link succeeds and the
## bytes are in a region the part reads as erased.

# REQUIRES: c166
# RUN: llvm-mc -filetype=obj -triple=c166 -mcpu=st10 %s -o %t.o
## The same source with __far data in it, which only a part that has far RAM
## can link - so the ST10F269 run below deliberately does not use it.
# RUN: llvm-mc -filetype=obj -triple=c166 -mcpu=st10 --defsym=HASFAR=1 %s \
# RUN:   -o %tfar.o

## An ST10F269: 10 KByte of extension RAM in one region at 00'C000H, because
## its XRAM2 runs up to 00'DFFFH and its XRAM1 starts at 00'E000H.
# RUN: ld.lld -T %S/../../../llvm/lib/Target/C166/startup/st10.ld \
# RUN:   --defsym=__c166_rom_length=262144 --defsym=__c166_iram_length=2048 \
# RUN:   --defsym=__c166_xram_length=10240 --defsym=__c166_xram_origin=0xC000 \
# RUN:   %t.o -o %t269.exe
# RUN: llvm-readelf -S %t269.exe | FileCheck %s --check-prefix=F269

## An ST10F272E: the C167's 2 KByte at 00'E000H and 16 KByte more at 09'0000H,
## which is a far region because no data page pointer here holds that page.
# RUN: ld.lld -T %S/../../../llvm/lib/Target/C166/startup/st10.ld \
# RUN:   --defsym=__c166_rom_length=262144 --defsym=__c166_iram_length=2048 \
# RUN:   --defsym=__c166_xram_length=2048 --defsym=__c166_xram_origin=0xE000 \
# RUN:   --defsym=__c166_xram2_length=16384 \
# RUN:   --defsym=__c166_xram2_origin=0x90000 \
# RUN:   %tfar.o -o %t272.exe
# RUN: llvm-readelf -S %t272.exe | FileCheck %s --check-prefix=F272E

## Where the XPERCON decision is checked: clang/test/Driver/c166-mmcu.c.  It
## is the driver's rather than this script's, because a script symbol written
## "X = DEFINED(X) ? X : default" reads as its default to anything derived from
## it here - so the script carries a constant for the part it defaults to and
## the driver decides for the part on the command line.
# RUN: llvm-nm %t269.exe | FileCheck %s --check-prefix=F269SYM
# F269SYM-DAG: 0000f024 A __c166_xpercon_addr

## The Flash blocks are the same on both parts, so both runs check them.
# F269:  .reset            PROGBITS        00000000
# F269:  .text             PROGBITS        00000004
# F269:  .fartext          PROGBITS        00018000
# F269:  .fartext2         PROGBITS        00020000
# F269:  .fartext3         PROGBITS        00030000
# F269:  .fartext4         PROGBITS        00040000
## and the extension RAM is one region low in page 3.
# F269:  .data             PROGBITS        0000c000

# F272E: .reset            PROGBITS        00000000
# F272E: .fartext          PROGBITS        00018000
# F272E: .fartext2         PROGBITS        00020000
# F272E: .fartext3         PROGBITS        00030000
# F272E: .fartext4         PROGBITS        00040000
## Near data is the 2 KByte at 00'E000H and __far data is the far RAM.
# F272E: .data             PROGBITS        0000e000
# F272E: .fardata          PROGBITS        00090000

        .section .reset,"ax",@progbits
__reset_vector:
        jmpa    cc_UC, _start

        .text
        .globl  _start
_start:
        nop
        nop

        .section .fartext,"ax",@progbits
        nop
        .section .fartext2,"ax",@progbits
        nop
        .section .fartext3,"ax",@progbits
        nop
        .section .fartext4,"ax",@progbits
        nop

        .data
        .short  1

## An ST10F269's extension RAM is all near, so there is no region for __far
## data and a program with any fails to link - which is the right answer for
## that part and is why this is behind a flag.
.ifdef HASFAR
        .section .fardata,"aw",@progbits
        .short  2
.endif
