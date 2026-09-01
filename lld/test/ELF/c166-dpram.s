## Where __dpram data lands, which is the dual-port RAM at 00'F600H: the only
## memory the coprocessor's IDX0 and IDX1 can address, per the ST10 Family
## Programming Manual (PM0036) section 2.1.  All three scripts have to agree
## about that, and the interesting one is the part with no extension RAM, where
## the static data is already in this same memory and the two have to sit one
## after the other rather than on top of each other.

# REQUIRES: c166
# RUN: llvm-mc -filetype=obj -triple=c166 -mcpu=xc16x %s -o %t.o

## An XC164CM-8F: 2 KByte of extension RAM at 00'C000H for the static data, so
## the dual-port RAM holds only what __dpram puts there and the ABI stack.
# RUN: ld.lld -T %S/../../../llvm/lib/Target/C166/startup/c166.ld %t.o -o %t8f.exe
# RUN: llvm-nm %t8f.exe | FileCheck %s --check-prefix=WITH

# WITH-DAG: 0000c000 {{[bB]}} ordinary
# WITH-DAG: 0000f600 {{[dD]}} seeded
# WITH-DAG: 0000f608 {{[bB]}} delay

## An XC164CM-4F, which has none: the script puts the static data at the bottom
## of the dual-port RAM, and the __dpram data has to follow it rather than
## start underneath it.  Getting that wrong is silent - two regions over one
## memory and the second overwrites the first - so it is checked by address.
# RUN: ld.lld -T %S/../../../llvm/lib/Target/C166/startup/c166.ld \
# RUN:   --defsym=__c166_dsram_length=0 %t.o -o %t4f.exe
# RUN: llvm-nm %t4f.exe | FileCheck %s --check-prefix=WITHOUT

# WITHOUT-DAG: 0000f600 {{[bB]}} ordinary
# WITHOUT-DAG: 0000f610 {{[dD]}} seeded
# WITHOUT-DAG: 0000f618 {{[bB]}} delay

## The image carries the initialised half and not the zeroed one, which is what
## the two sections are for: a delay line is 2 KByte of nothing.
# RUN: llvm-readelf -S %t8f.exe | FileCheck %s --check-prefix=SECT
# SECT: .dpramdata        PROGBITS        0000f600
# SECT: .dprambss         NOBITS          0000f608

        .text
        .globl _start
_start:
        ret

        .section .dprambss,"aw",@nobits
        .globl delay
delay:  .zero 16

        .section .dpramdata,"aw",@progbits
        .globl seeded
seeded: .short 1, 2, 3, 4

        .bss
        .globl ordinary
ordinary: .zero 16
