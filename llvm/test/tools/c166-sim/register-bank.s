# REQUIRES: c166-registered-target
# RUN: rm -rf %t && split-file %s %t

## A register bank is a window into the internal RAM: R0 to R15 are the sixteen
## words at the address CP holds.  Moving CP moves the window, which is what
## __attribute__((c166_bank)) does to give a handler registers of its own, and
## what this checks.

# RUN: llvm-mc -filetype=obj -triple=c166 %t/switch.s -o %t/switch.o
# RUN: llvm-objcopy -O binary %t/switch.o %t/switch.bin
# RUN: %c166_sim --binary --dump-state %t/switch.bin 2>&1 | FileCheck %s --check-prefix=SWITCH

## R2 is written in each of the two banks and read back in the first, so 1111H
## says the second bank's write went somewhere else - which is the whole point
## of switching.  R3 is read after POP CP and is the first bank's again.
# SWITCH: R2=1111 R3=3333

## A context pointer that does not name internal RAM does not name registers.
## On the part the window would read whatever that memory is; here it would
## read the simulator's own array and quietly work, so it is refused instead -
## a bank placed outside the internal RAM by a linker script with no banks
## region is the mistake nothing else catches.
# RUN: llvm-mc -filetype=obj -triple=c166 %t/astray.s -o %t/astray.o
# RUN: llvm-objcopy -O binary %t/astray.o %t/astray.bin
# RUN: not %c166_sim --binary %t/astray.bin 2>&1 | FileCheck %s --check-prefix=ASTRAY
# ASTRAY: error: a context pointer outside the internal RAM

## The last bank that fits ends at FDFFH, and one word further does not.
# RUN: llvm-mc -filetype=obj -triple=c166 %t/edge.s -o %t/edge.o
# RUN: llvm-objcopy -O binary %t/edge.o %t/edge.bin
# RUN: not %c166_sim --binary %t/edge.bin 2>&1 | FileCheck %s --check-prefix=ASTRAY

#--- switch.s
        .text
        jmps    #0, 0x20
        .org    0x20
        mov     r2, #0x1111
        mov     r3, #0x3333
## The banks region the linker scripts define starts here, just above the
## sixteen words the reset register bank occupies.
        scxt    cp, #0xFC20
        nop
        mov     r2, #0x2222
        pop     cp
        nop
        mov     r5, #0
        exts    #0xFF, #1
        mov     0x0002, r5

#--- astray.s
        .text
        jmps    #0, 0x20
        .org    0x20
## The extension RAM, which is memory but not the internal RAM.
        mov     cp, #0xC000
        nop
        mov     r2, #1

#--- edge.s
        .text
        jmps    #0, 0x20
        .org    0x20
## Sixteen words from FDE2H run to FE01H, which is past the end.
        mov     cp, #0xFDE2
        nop
        mov     r2, #1
