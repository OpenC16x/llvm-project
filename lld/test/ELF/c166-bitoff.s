## R_C166_BITOFF8: the byte a bit instruction carries is not an address but the
## 8 bit word number of the bit-addressable space.  The mapping has two windows
## counted from two bases, so nothing linear will do and the linker has to know
## that is what the byte is.

# REQUIRES: c166
# RUN: llvm-mc -filetype=obj -triple=c166 %s -o %t.o

## Placed where the linker script here puts __bitaddr data: FD00H is word 0,
## and each word after it is one more.
# RUN: ld.lld -T %S/../../../llvm/lib/Target/C166/startup/c166.ld %t.o -o %t.exe
# RUN: llvm-objdump -d --triple=c166 %t.exe | FileCheck %s
# RUN: llvm-nm %t.exe | FileCheck %s --check-prefix=SYM

# SYM-DAG: 0000fd00 {{[bB]}} first
# SYM-DAG: 0000fd02 {{[bB]}} second

# CHECK: bset 0.3
# CHECK: bclr 1.0
# CHECK: jnb  1.15

## An address in neither window is not a truncation to report as one - there is
## no bit instruction that reaches it at all.
# RUN: not ld.lld -T %S/Inputs/c166-bitoff-elsewhere.ld %t.o -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --check-prefix=AWAY
# AWAY: bit variable first is at c000, which is not in the bit-addressable space

## And an odd one names no word either, however well placed the section is.
# RUN: not ld.lld -T %S/Inputs/c166-bitoff-odd.ld %t.o -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --check-prefix=ODD
# ODD: bit variable first is at fd01, which is not in the bit-addressable space

        .globl __reset_vector
        .globl _start
        .text
__reset_vector:
_start:
        bset first.3
        bclr second.0
        jnb  second.15, _start
        ret

        .section .bitbss,"aw",@nobits
        .globl first
first:  .zero 2
        .globl second
second: .zero 2
