# REQUIRES: c166
# RUN: llvm-mc -filetype=obj -triple=c166 %s -o %t.o
# RUN: not ld.lld %t.o -o /dev/null --image-base=0x1000 -Ttext=0x8000 \
# RUN:     --defsym=toofar=0x8300 --defsym=odd=0x8009 --defsym=big=0x1ffff \
# RUN:     --defsym=huge=0x1000000 2>&1 | FileCheck %s

## Every C166 relocation field is narrower than the address it carries, so the
## linker has to say so rather than truncate.

  .text
  .globl _start
_start:

## A bit test branch reaches 127 words either way.
  jb r5.3, toofar
# CHECK: error: {{.*}} relocation R_C166_PCREL8W out of range: 382 is not in [-128, 127]; references 'toofar'

## It counts words, so an odd target is not expressible at all.
  jb r5.3, odd
# CHECK: error: {{.*}} improper alignment for relocation R_C166_PCREL8W: 0x3 is not aligned to 2 bytes

## A near address is one word.
  mov r2, big
# CHECK: error: {{.*}} relocation R_C166_ABS16 out of range: 131071 is not in [-32768, 65535]; references 'big'

## The bus is 24 bits, so there is no segment above 0xff and no page above
## 0x3ff.
  exts #seg(huge), #1
# CHECK: error: {{.*}} relocation R_C166_SEG8 out of range: 16777216 is not in [0, 16777215]; references 'huge'
  extp #pag(huge), #1
# CHECK: error: {{.*}} relocation R_C166_PAG10 out of range: 16777216 is not in [0, 16777215]; references 'huge'
