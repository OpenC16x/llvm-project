# REQUIRES: c166-registered-target
# RUN: rm -rf %t && split-file %s %t

## Interrupt delivery.  A source here is not a peripheral - there are none -
## but a request injected from the command line at a state count, which is
## deterministic and is enough to exercise everything the core does with one:
## the arbitration, PSW.IEN and the priority comparison, the entry through the
## vector table, PSW.ILVL on the way in and RETI on the way out, and the
## lockout an ATOMIC sequence gets.
##
## A flat image has no linker, so every address here is written out rather than
## named; .org puts the jumps in the vector slots they have to be in, four
## bytes apart, which is the spacing CPUCON1 comes up with.

# RUN: llvm-mc -filetype=obj -triple=c166 %t/count.s -o %t/count.o
# RUN: llvm-objcopy -O binary %t/count.o %t/count.bin

## Nothing injected: the loop runs 20 times and the handler never does.
# RUN: %c166_sim --binary --dump-state --count-states %t/count.bin 2>&1 \
# RUN:   | FileCheck %s --check-prefix=NONE
# NONE: R0=0000 R1=0000 R2=0014 R3=0000
# NONE: instructions 70  states 206

## One request, raised in the middle of the loop.  R3 says the handler ran
## once and R2 says the loop still finished its 20 iterations, so the handler
## returned to where it interrupted.
# RUN: %c166_sim --binary --dump-state --count-states --interrupt-at=50:2:8 \
# RUN:   %t/count.bin 2>&1 | FileCheck %s --check-prefix=ONCE
# ONCE: R0=0000 R1=0000 R2=0014 R3=0001
## R6 is PSW as the handler saw it: 8800H is IEN set and ILVL 8, the priority
## of the source that was accepted, which is what stops a handler being
## interrupted by its own level or below.  R7 is PSW after the RETI, back to
## ILVL 0 with IEN still set, because RETI puts back the whole word.
# ONCE: R6=8800 R7=0808
## Entering the handler costs states but is not an instruction, so the counts
## move by the four states of the entry plus the three instructions of the
## handler and their own time.
# ONCE: instructions 74  states 221

## Raised before the program has enabled interrupts.  A request that cannot be
## accepted is not lost - "it remains pending" - so it is taken as soon as
## PSW.IEN goes up, and the count is still one.
# RUN: %c166_sim --binary --dump-state --count-states --interrupt-at=2:2:8 \
# RUN:   %t/count.bin 2>&1 | FileCheck %s --check-prefix=EARLY
# EARLY: R2=0014 R3=0001
# EARLY: interrupts 1

## A periodic source, which is what stands in for a timer.
# RUN: %c166_sim --binary --dump-state --count-states --interrupt-every=30:2:8 \
# RUN:   %t/count.bin 2>&1 | FileCheck %s --check-prefix=EVERY
# EVERY: R2=0014 R3=000d
# EVERY: interrupts 13

## PSW.IEN never set: the request stays pending for the whole program and the
## handler never runs.
# RUN: llvm-mc -filetype=obj -triple=c166 %t/disabled.s -o %t/disabled.o
# RUN: llvm-objcopy -O binary %t/disabled.o %t/disabled.bin
# RUN: %c166_sim --binary --dump-state --count-states --interrupt-at=50:2:8 \
# RUN:   %t/disabled.bin 2>&1 | FileCheck %s --check-prefix=DISABLED
# DISABLED: R2=0014 R3=0000
# DISABLED: interrupts 0

## The comparison is strict: with the CPU running at level 8, a level 8 source
## does not get in and a level 9 one does.
# RUN: llvm-mc -filetype=obj -triple=c166 %t/level8.s -o %t/level8.o
# RUN: llvm-objcopy -O binary %t/level8.o %t/level8.bin
# RUN: %c166_sim --binary --dump-state --count-states --interrupt-at=50:2:8 \
# RUN:   %t/level8.bin 2>&1 | FileCheck %s --check-prefix=EQUAL
# EQUAL: R2=0014 R3=0000
# EQUAL: interrupts 0
# RUN: %c166_sim --binary --dump-state --count-states --interrupt-at=50:2:9 \
# RUN:   %t/level8.bin 2>&1 | FileCheck %s --check-prefix=HIGHER
# HIGHER: R2=0014 R3=0001
## R6 is 9800H - ILVL 9, the accepted source's - and R7 is 8808H, back to the
## level 8 the interrupted code was running at rather than to zero.
# HIGHER: R6=9800 R7=8808
# HIGHER: interrupts 1

## Nesting, which is the same rule applied to a handler rather than to main.
## The outer handler is entered at level 8 and takes a while; the inner source
## fires while it is still running.  R10 is what the inner handler found the
## outer one had reached: 1 means it got in half way through, 2 means it had
## to wait for the outer to finish.  R9 is 1 either way, because a request
## that loses the comparison is held rather than dropped.
# RUN: llvm-mc -filetype=obj -triple=c166 %t/nest.s -o %t/nest.o
# RUN: llvm-objcopy -O binary %t/nest.o %t/nest.bin
# RUN: %c166_sim --binary --dump-state --interrupt-at=50:2:8 \
# RUN:   --interrupt-at=64:4:9 %t/nest.bin 2>&1 | FileCheck %s --check-prefix=NEST
# NEST: R8=0002 R9=0001 R10=0001
# RUN: %c166_sim --binary --dump-state --interrupt-at=50:2:8 \
# RUN:   --interrupt-at=64:4:8 %t/nest.bin 2>&1 | FileCheck %s --check-prefix=NONEST
# NONEST: R8=0002 R9=0001 R10=0002

## ATOMIC locks interrupts out for the instructions it covers, which is the
## whole reason the instruction exists.  R6 is the loop counter as the handler
## found it.  Raised just before the ATOMIC the request goes straight in and
## the handler sees nothing incremented; raised inside the sequence it is held
## until all four covered instructions have run, and the handler sees all four
## increments.
##
## The NOP between BSET PSW.11 and ATOMIC is what leaves a window at all.
## Enabling interrupts takes effect one instruction late - "no interrupt
## requests are acknowledged until the instruction following the enabling
## instruction" - so with the ATOMIC directly after the BSET there is no round
## between them where a request could be accepted, and both cases below would
## be the second one.  That is the manual's own advice about where a critical
## sequence may begin, seen from the other side.
# RUN: llvm-mc -filetype=obj -triple=c166 %t/atomic.s -o %t/atomic.o
# RUN: llvm-objcopy -O binary %t/atomic.o %t/atomic.bin
# RUN: %c166_sim --binary --dump-state --interrupt-at=20:2:8 %t/atomic.bin 2>&1 \
# RUN:   | FileCheck %s --check-prefix=BEFORE
# BEFORE: R3=0001 R4=0000 R5=0000 R6=0000
# RUN: %c166_sim --binary --dump-state --interrupt-at=22:2:8 %t/atomic.bin 2>&1 \
# RUN:   | FileCheck %s --check-prefix=INSIDE
# INSIDE: R3=0001 R4=0000 R5=0000 R6=0004

## Transparency, which is the property everything above is for: a handler that
## puts back what it used leaves the interrupted computation alone, however
## often it runs.  The loop is 100 rounds of arithmetic ending in a
## conditional branch, so the interrupt lands between a SUB and the JMPR that
## reads its flags about as often as anywhere else - and the flags are in PSW,
## which only the push on entry and the RETI on the way out put back.  Four
## unrelated rhythms all have to give the same answer as no interrupts at all.
# RUN: llvm-mc -filetype=obj -triple=c166 %t/transparent.s -o %t/transparent.o
# RUN: llvm-objcopy -O binary %t/transparent.o %t/transparent.bin
# RUN: %c166_sim --binary --dump-state %t/transparent.bin 2>&1 \
# RUN:   | FileCheck %s --check-prefix=CLEAN
# CLEAN: R2=29e3 R3=006a
# RUN: %c166_sim --binary --dump-state --interrupt-every=41:2:8 \
# RUN:   %t/transparent.bin 2>&1 | FileCheck %s --check-prefix=CLEAN
# RUN: %c166_sim --binary --dump-state --interrupt-every=53:2:8 \
# RUN:   %t/transparent.bin 2>&1 | FileCheck %s --check-prefix=CLEAN
# RUN: %c166_sim --binary --dump-state --interrupt-every=97:2:8 \
# RUN:   %t/transparent.bin 2>&1 | FileCheck %s --check-prefix=CLEAN

## What a malformed spec says.
# RUN: not %c166_sim --binary --interrupt-at=50 %t/count.bin 2>&1 \
# RUN:   | FileCheck %s --check-prefix=E-SHAPE
# E-SHAPE: error: --interrupt-at='50': expected <states>:<vector>[:<level>]
# RUN: not %c166_sim --binary --interrupt-at=x:2 %t/count.bin 2>&1 \
# RUN:   | FileCheck %s --check-prefix=E-STATES
# E-STATES: error: --interrupt-at='x:2': 'x' is not a state count
# RUN: not %c166_sim --binary --interrupt-at=50:200 %t/count.bin 2>&1 \
# RUN:   | FileCheck %s --check-prefix=E-VECTOR
# E-VECTOR: error: --interrupt-at='50:200': the vector table has 128 entries
## Level 0 is the level the CPU runs at out of reset, so a source at it could
## never win: saying so beats declaring one that silently never fires.
# RUN: not %c166_sim --binary --interrupt-at=50:2:0 %t/count.bin 2>&1 \
# RUN:   | FileCheck %s --check-prefix=E-LEVEL
# E-LEVEL: error: --interrupt-at='50:2:0': the priority level must be 1..15
# RUN: not %c166_sim --binary --interrupt-every=0:2 %t/count.bin 2>&1 \
# RUN:   | FileCheck %s --check-prefix=E-PERIOD
# E-PERIOD: error: --interrupt-every='0:2': a period of zero states would never come round

#--- count.s
        .text
## Trap 0 is reset, which is where a flat image starts.
        jmps    #0, 0x20
## Vector 2.
        .org    8
        jmps    #0, 0x40

        .org    0x20
        mov     r3, #0
        mov     r6, #0
        mov     r2, #0
        bset    psw.11
        mov     r4, #20
.Lloop:
        add     r2, #1
        sub     r4, #1
        jmpr    cc_NZ, .Lloop
        mov     r7, psw
        mov     r5, #0
        exts    #0xFF, #1
        mov     0x0002, r5

        .org    0x40
        add     r3, #1
        mov     r6, psw
        reti

#--- disabled.s
## The same program with the one instruction that enables interrupts taken out.
        .text
        jmps    #0, 0x20
        .org    8
        jmps    #0, 0x40

        .org    0x20
        mov     r3, #0
        mov     r6, #0
        mov     r2, #0
        nop
        mov     r4, #20
.Lloop:
        add     r2, #1
        sub     r4, #1
        jmpr    cc_NZ, .Lloop
        mov     r7, psw
        mov     r5, #0
        exts    #0xFF, #1
        mov     0x0002, r5

        .org    0x40
        add     r3, #1
        mov     r6, psw
        reti

#--- level8.s
## The same program again, running at CPU priority 8 rather than 0.  Writing
## PSW outright takes a word more than setting the one bit did, so the handler
## goes further up to leave room for it.
        .text
        jmps    #0, 0x20
        .org    8
        jmps    #0, 0x60

        .org    0x20
        mov     r3, #0
        mov     r6, #0
        mov     r2, #0
## IEN set and ILVL 8, in one write rather than two so the timings above hold.
        mov     psw, #0x8800
        mov     r4, #20
.Lloop:
        add     r2, #1
        sub     r4, #1
        jmpr    cc_NZ, .Lloop
        mov     r7, psw
        mov     r5, #0
        exts    #0xFF, #1
        mov     0x0002, r5

        .org    0x60
        add     r3, #1
        mov     r6, psw
        reti

#--- nest.s
        .text
        jmps    #0, 0x20
        .org    8
        jmps    #0, 0x40
## Vector 4.
        .org    0x10
        jmps    #0, 0x60

        .org    0x20
        mov     r8, #0
        mov     r9, #0
        mov     r10, #0
        mov     r2, #0
        bset    psw.11
        mov     r4, #40
.Lloop:
        add     r2, #1
        sub     r4, #1
        jmpr    cc_NZ, .Lloop
        mov     r5, #0
        exts    #0xFF, #1
        mov     0x0002, r5

## The outer handler: R8 says how far through it is.
        .org    0x40
        mov     r8, #1
        mov     r11, #6
.Lspin:
        sub     r11, #1
        jmpr    cc_NZ, .Lspin
        mov     r8, #2
        reti

## The inner handler, which records what the outer one had reached.
        .org    0x60
        add     r9, #1
        mov     r10, r8
        reti

#--- atomic.s
        .text
        jmps    #0, 0x20
        .org    8
        jmps    #0, 0x40

        .org    0x20
        mov     r3, #0
        mov     r6, #0
        mov     r2, #0
        bset    psw.11
## Enabling takes effect one instruction late, so this is what gives a request
## anywhere to be accepted before the ATOMIC.
        nop
## Four instructions covered, and a fifth after them that is not.
        atomic  #4
        add     r2, #1
        add     r2, #1
        add     r2, #1
        add     r2, #1
        add     r2, #1
        mov     r5, #0
        exts    #0xFF, #1
        mov     0x0002, r5

        .org    0x40
        add     r3, #1
        mov     r6, r2
        reti

#--- transparent.s
        .text
        jmps    #0, 0x20
        .org    8
        jmps    #0, 0x40

        .org    0x20
        mov     r2, #0
        mov     r3, #1
        mov     r4, #100
        bset    psw.11
.Lloop:
        add     r2, r3
        add     r3, #3
        rol     r3, #1
        xor     r2, r3
        sub     r4, #1
        jmpr    cc_NZ, .Lloop
        mov     r5, #0
        exts    #0xFF, #1
        mov     0x0002, r5

## A handler written the way one has to be: it saves the registers it uses and
## puts them back, and leaves the flags to RETI.  The CMP is there to make sure
## it does leave with the flags wrong, so that the loop's JMPR would take the
## other branch if PSW were not restored.
        .org    0x40
        push    r6
        push    r7
        mov     r6, #0x1234
        mov     r7, #0x5678
        add     r6, r7
        cmp     r6, #0
        pop     r7
        pop     r6
        reti
