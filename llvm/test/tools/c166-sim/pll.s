# REQUIRES: c166-registered-target
# RUN: llvm-mc -filetype=obj -triple=c166 -mcpu=xc16x %s -o %t.o
# RUN: llvm-objcopy -O binary %t.o %t.bin
# RUN: %c166_sim --binary --dump-state %t.bin 2>&1 | FileCheck %s

## The PLL is the XC164CM's, and so are PLLCON and SYSSTAT, so this is
## assembled for that part.  A register name from one derivative's map is
## refused for another; saying which part a file is for is how it gets past
## that, the same way crt0.S does.
##
## SYSSTAT's PLLLOCK is what startup code waits on before it moves the CPU onto
## the PLL, so it has to come up clear and become set later.  A simulator that
## reported it set from the first instruction would let a wait loop that could
## never work pass anyway, which is the one thing modelling this register is
## for.
##
## R3 is the word that ended the wait and R5 is what the same read gives when
## nothing will ever end it; R8 is PLLCON read back and R9 says the wait went
## round more than once.
# CHECK: R3=4000 R4=5343 R5=0000
# CHECK: R8=7343 R9=0001

        .text
## R2 counts the turns the wait takes and R3 is the word that ended it.
        mov     r2, #0
## PLLCTRL = 11B below PLLWRI: the PLL drives the CPU, so the VCO has the
## oscillator to lock to and lock is on its way.
        mov     r4, #0x7343
        mov     pllcon, r4
.Lwait:
        add     r2, #1
        mov     r3, sysstat
        and     r3, #0x4000
        jmpr    cc_z, .Lwait

## Lock arrived, and it was not there on the first look.  How many turns that
## took is this simulator's own number rather than the part's, so what is
## checked is that there was more than one.
        mov     r9, #0
        cmp     r2, #1
        jmpr    cc_ULE, .Lchecked
        mov     r9, #1
.Lchecked:

## PLLCON reads back what was written to it.
        mov     r8, pllcon

## PLLCTRL = 10B is the VCO free running with the oscillator input switched
## off, which is the one setting with nothing to lock to.  Lock is not merely
## late there, it never comes, so R5 stays zero however long anything waits.
        mov     r4, #0x5343
        mov     pllcon, r4
        mov     r6, #500
.Lspin:
        mov     r5, sysstat
        and     r5, #0x4000
        sub     r6, #1
        jmpr    cc_nz, .Lspin

        mov     r7, #0
        exts    #0xFF, #1
        mov     0x0002, r7
