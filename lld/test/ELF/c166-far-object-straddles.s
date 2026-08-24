# REQUIRES: c166
## A far access names one segment for the whole object it reaches, because the
## compiler folds the EXTend of two accesses to the same object into one.  An
## object placed across a segment boundary cannot be reached that way, and by
## the time the fold has happened there is no second segment left to disagree
## with the first: the access would land at the right offset in the wrong
## segment.  So the placement is rejected here, where it is finally known.

# RUN: llvm-mc -filetype=obj -triple=c166 %s -o %t.o

## Placed so that the object runs from FF00H into the next segment.
# RUN: echo "SECTIONS { \
# RUN:   .text : { *(.text) } \
# RUN:   .fardata 0xFF00 : { *(.fardata) } }" > %t.bad.script
# RUN: not ld.lld -T %t.bad.script %t.o -o /dev/null 2>&1 | FileCheck %s

# CHECK: error:
# CHECK-SAME: far object 'big' crosses a segment boundary
# CHECK-SAME: placed at 0xFF00
# CHECK-SAME: 512 bytes
# CHECK-SAME: ends in segment 0x1 rather than 0x0

## The same object inside one segment links.
# RUN: echo "SECTIONS { \
# RUN:   .text : { *(.text) } \
# RUN:   .fardata 0xC000 : { *(.fardata) } }" > %t.good.script
# RUN: ld.lld -T %t.good.script %t.o -o /dev/null

  .text
  .globl _start
_start:
  exts #seg(big), #1
  mov r2, sof(big)

  .section .fardata,"aw",@progbits
  .globl big
  .type big,@object
  .size big, 512
big:
  .zero 512
