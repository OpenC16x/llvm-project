# REQUIRES: c166-registered-target
# RUN: rm -rf %t && mkdir -p %t
# RUN: llvm-mc -filetype=obj -triple=c166 -mcpu=xc16x -mattr=+mac \
# RUN:     %S/Inputs/esfr.s -o %t/prog.o
# RUN: ld.lld -Ttext=0xc00100 -e _start --defsym=__c166_exit=0xc00000 \
# RUN:     %t/prog.o -o %t/prog.elf
# RUN: %c166_sim --mattr=+mac %t/prog.elf | FileCheck %s

## EXTR switches what a "reg" field names over to the extended register space
## for the next few instructions: the same short address means F000H instead of
## FE00H.  Nothing else distinguishes the two, so a simulator that did not
## model it would push the wrong register and say nothing.
##
## QX0 is at F000H and QX1 at F002H, and DPP0 and DPP1 share their short
## addresses and hold 0 and 1 out of reset.  So a push that read the ordinary
## space would print "01" here.
# CHECK: 79
