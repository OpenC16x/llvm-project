# REQUIRES: c166-registered-target
# RUN: llvm-mc -filetype=obj -triple=c166 -mattr=+mac %s -o %t.o
# RUN: llvm-objcopy -O binary %t.o %t.bin
# RUN: not %c166_sim --binary %t.bin 2>&1 | FileCheck %s

## IDX0 and IDX1 reach the internal dual-port RAM and nothing else - PM0036
## section 2.1, "the GPR pointer gives access to the entire memory space,
## whereas IDXi are limited to the internal Dual-Port RAM, except for the CoMOV
## instruction".  Nothing about the encoding says so, so an IDX pointed at
## ordinary static data assembles and links; on the part it reads whatever the
## unit sees there instead.  __dpram is what puts an array where it can be
## reached, and this is what says so when it has not been.
##
## C000H is where a program's static data goes on a part with an extension RAM,
## which is exactly the mistake worth catching.
# CHECK: error: an IDX pointer outside the dual-port RAM

        .text
        jmps    #0, 0x20

        .org    0x20
        mov     r0, #0
        coload  r0, r0

        mov     r1, #0xC000
        mov     idx0, r1
        mov     r2, #0xF600
        comac   [idx0+], [r2+]

        mov     r9, #0
        exts    #0xFF, #1
        mov     0x0002, r9
