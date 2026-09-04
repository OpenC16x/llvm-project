# REQUIRES: c166-registered-target
# RUN: rm -rf %t && mkdir -p %t

## The ABI stack is the one nothing on the part watches.  SP, which holds
## return addresses, is compared against STKOV and STKUN by the hardware on
## every push and pop; R0, which holds frames, spills, locals and arrays, is an
## ordinary general purpose register and has nothing looking at it.  A program
## that walks R0 off the bottom of its stack writes into whatever is under it -
## static data, another object's memory - and carries on with a wrong answer.
##
## So the simulator watches it, from the __user_stack_limit the linker scripts
## define.  That costs the program nothing and catches every crossing, which is
## what a simulator is for; -mstack-check is the other half, and is code in the
## program that works on a part.

# RUN: llvm-mc -filetype=obj -triple=c166 %S/Inputs/user-stack.s -o %t/prog.o
# RUN: ld.lld -Ttext=0xc00100 -e _start --defsym=__user_stack_limit=0xf600 \
# RUN:     --defsym=__c166_exit=0xc00000 %t/prog.o -o %t/prog.elf

## Walking R0 down past the limit stops the program and says where it was.
# RUN: not %c166_sim %t/prog.elf 2>&1 | FileCheck %s
# CHECK: error: ABI stack overflow: R0 is 0x00f5fe, below __user_stack_limit at 0x00f600
# CHECK-SAME: -mstack-check

## And --check-user-stack=false turns it off, which is what a program with its
## own idea of where the stack lives needs.  Then nothing stops it and it runs
## to the end.
# RUN: %c166_sim --check-user-stack=false %t/prog.elf | FileCheck %s --check-prefix=OFF
# OFF: past

## R0 comes out of reset at zero and stays there until the startup code loads
## it, which is below the limit and is not an overflow.  The check arms itself
## the first time R0 is seen above the limit, so a program that never sets it
## at all is left alone rather than stopped on its first instruction.
# RUN: llvm-mc -filetype=obj -triple=c166 %S/Inputs/user-stack-unset.s -o %t/unset.o
# RUN: ld.lld -Ttext=0xc00100 -e _start --defsym=__user_stack_limit=0xf600 \
# RUN:     --defsym=__c166_exit=0xc00000 %t/unset.o -o %t/unset.elf
# RUN: %c166_sim %t/unset.elf | FileCheck %s --check-prefix=UNSET
# UNSET: unset

## And "above the limit" is not enough to arm on, which is the way that went
## wrong.  A program that never sets R0 and takes an interrupt has a handler
## whose prologue subtracts its frame from zero: that wraps to just under 64K,
## which is above the limit and is not a stack pointer at all, and the matching
## addition in the epilogue brings it back to zero.  Arming on the wrapped value
## reported that return as an overflow and stopped a program that had done
## nothing wrong - which is what lld/test/ELF/c166-vectors.s found.  The value
## has to be inside the stack, so __user_stack_top is defined here as the stock
## linker scripts define it.
# RUN: llvm-mc -filetype=obj -triple=c166 %S/Inputs/user-stack-wrap.s -o %t/wrap.o
# RUN: ld.lld -Ttext=0xc00100 -e _start --defsym=__user_stack_limit=0xf600 \
# RUN:     --defsym=__user_stack_top=0xfa00 --defsym=__c166_exit=0xc00000 \
# RUN:     --section-start=.vectors.008=0xc00020 %t/wrap.o -o %t/wrap.elf
# RUN: %c166_sim --interrupt-at=20:8:8 %t/wrap.elf | FileCheck %s --check-prefix=WRAP
# WRAP: wrap
