# REQUIRES: c166-registered-target
# RUN: llvm-mc -filetype=obj -triple=c166 %s -o %t.o
# RUN: llvm-objcopy -O binary %t.o %t.bin

## A program that never finishes is stopped rather than run forever.
# RUN: not %c166_sim --binary --max-steps=1000 %t.bin 2>&1 | FileCheck %s
# CHECK: error: ran for 1000 instructions without finishing

## An ELF that is not for this machine is refused.
# RUN: not %c166_sim %t.bin 2>&1 | FileCheck %s --check-prefix=NOTELF
# NOTELF: error: not an ELF object

        nop
        jmpa    cc_UC, 0
