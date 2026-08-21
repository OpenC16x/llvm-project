# REQUIRES: c166-registered-target
# RUN: llvm-mc -filetype=obj -triple=c166 %s -o %t.o
# RUN: llvm-objcopy -O binary %t.o %t.bin
# RUN: %c166_sim --binary --dump-state %t.bin 2>&1 | FileCheck %s
## R2 is the set of checks that failed, so it must be zero.
# CHECK: R2=0000

## The addressing overrides.  A near address goes through the DPP window, an
## EXTS turns it into an offset into a named segment, and an EXTP replaces the
## page.  Each check sets a bit in R3 on failure, and R3 is the result.

        mov     r3, #0

## EXTS covers one instruction: the store goes to segment 0x12, the load after
## it is near again.
        mov     r2, #0x1234
        mov     r4, #0x0100
        exts    #0x12, #1
        mov     [r4], r2                ; -> 12'0100H
        mov     r5, #0                  ; near, and does not disturb the above
        exts    #0x12, #1
        mov     r5, [r4]                ; <- 12'0100H
        cmp     r5, r2
        jb      psw.3, .Lexts_ok
        or      r3, #1
.Lexts_ok:

## A near store to the same 16 bit address lands somewhere else entirely,
## since DPP0 maps 0000H-3FFFH to page 0.
        mov     r2, #0x4321
        mov     [r4], r2                ; -> 00'0100H
        exts    #0x12, #1
        mov     r5, [r4]                ; still the far one, 1234H
        mov     r6, #0x1234
        cmp     r5, r6
        jb      psw.3, .Lnear_ok
        or      r3, #2
.Lnear_ok:

## EXTS covering four instructions stays active across all of them.
        mov     r4, #0x0200
        exts    #0x34, #4
        mov     [r4], r2                ; 34'0200H
        add     r4, #2
        mov     [r4], r2                ; 34'0202H
        nop
        mov     r5, #0                  ; the fifth is near again
        exts    #0x34, #1
        mov     r5, [r4]
        cmp     r5, r2
        jb      psw.3, .Lrange_ok
        or      r3, #4
.Lrange_ok:

## EXTP names a 10 bit page instead: page 0x48 is 12'0000H, so the same
## offset reaches the word the first EXTS wrote: 0x48 << 14 is 12'0000H.
        mov     r4, #0x0100
        extp    #0x48, #1
        mov     r5, [r4]
        mov     r6, #0x1234
        cmp     r5, r6
        jb      psw.3, .Lextp_ok
        or      r3, #8
.Lextp_ok:

        mov     r2, r3
        exts    #0xFF, #1
        mov     0x0002, r2
