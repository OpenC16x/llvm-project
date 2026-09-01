# RUN: llvm-mc -triple=c166 -show-encoding %s | FileCheck %s
# RUN: llvm-mc -filetype=obj -triple=c166 %s -o %t.o
# RUN: llvm-readobj -r %t.o | FileCheck %s --check-prefix=RELOC

## A bit address whose word is a symbol rather than a register or a number.
## Which word of the bit-addressable space the symbol is is not known until it
## has an address, so the byte the instruction carries is a relocation - and it
## cannot be an ABS8 of the address, because the mapping from address to bitoff
## is two windows counted from two bases.

# CHECK: bset flags.3 ; encoding: [0x3f,A]
# CHECK-NEXT: fixup A - offset: 1, value: flags, kind: fixup_c166_bitoff
        bset    flags.3

# CHECK: bclr flags.0 ; encoding: [0x0e,A]
        bclr    flags.0

## The bit test branches carry two fixups: the word, and the displacement.
# CHECK: jnb flags.15, .Ltarget ; encoding: [0x9a,A,B,0xf0]
# CHECK-NEXT: fixup A - offset: 1, value: flags, kind: fixup_c166_bitoff
# CHECK-NEXT: fixup B - offset: 2, value: .Ltarget, kind: fixup_c166_rel8w
        jnb     flags.15, .Ltarget
.Ltarget:
        nop

## A two operand form can have one on either side.
# CHECK: bmov flags.1, psw.3
        bmov    flags.1, psw.3
# CHECK: bmov psw.3, flags.1
        bmov    psw.3, flags.1

## And the word-only form BFLDL takes.
# CHECK: bfldl flags, #15, #5
        bfldl   flags, #15, #5

# RELOC: R_C166_BITOFF8 flags
        .globl flags
        .section .bitbss,"aw",@nobits
flags:  .zero 2
