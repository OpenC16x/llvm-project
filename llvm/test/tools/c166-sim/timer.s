# REQUIRES: c166-registered-target
# RUN: rm -rf %t && split-file %s %t

## The GPT1 timers, which are the first peripherals this simulator has.
##
## What interrupt.s tests is what the core does with a request; a request there
## is injected from the command line at a state count.  What is tested here is
## the other half: a peripheral a program configures the way it would on the
## part, which raises the request itself, through an interrupt control register
## with its own enable and priority.  Nothing on the command line takes part.
##
## Everything asserted below follows from two sentences of the C167CR
## Derivatives User's Manual V3.1 chapter 10.  The rate is
## "fT3 = fCPU / (8 x 2^<T3I>)", which is a count every 8 << T3I states because
## a state is a CPU clock period.  The reload is "upon a trigger signal T3 is
## loaded with the contents of the respective timer register (T2 or T4) and the
## interrupt request flag (T2IR or T4IR) is set".
##
## A flat image has no linker, so every address here is written out; the vector
## table is four bytes a slot, and T3's is 23H, which is 8CH into it.

# RUN: llvm-mc -filetype=obj -triple=c166 %t/rate.s -o %t/rate.o
# RUN: llvm-objcopy -O binary %t/rate.o %t/rate.bin

## The prescaler, with no interrupt in the way at all: T3 counts from zero
## while a fixed loop runs, and R8 is what it reached.  The three runs are the
## same program with T3I 0, 1 and 3, so the same 1396 states of running are
## divided by 8, by 16 and by 64 - and 175, 87 and 21 are those divisions.
# RUN: %c166_sim --binary --dump-state --count-states %t/rate.bin 2>&1 \
# RUN:   | FileCheck %s --check-prefix=RATE0
# RATE0: R8=00af
# RATE0: instructions 408  states 1422
# RUN: llvm-mc -filetype=obj -triple=c166 %t/rate1.s -o %t/rate1.o
# RUN: llvm-objcopy -O binary %t/rate1.o %t/rate1.bin
# RUN: %c166_sim --binary --dump-state --count-states %t/rate1.bin 2>&1 \
# RUN:   | FileCheck %s --check-prefix=RATE1
# RATE1: R8=0057
# RATE1: instructions 408  states 1422
# RUN: llvm-mc -filetype=obj -triple=c166 %t/rate3.s -o %t/rate3.o
# RUN: llvm-objcopy -O binary %t/rate3.o %t/rate3.bin
# RUN: %c166_sim --binary --dump-state --count-states %t/rate3.bin 2>&1 \
# RUN:   | FileCheck %s --check-prefix=RATE3
# RATE3: R8=0015
# RATE3: instructions 408  states 1422

## The whole chain, with the handler reloading the timer itself: the request
## flag goes up in T3IC, the enable and the priority beside it let it through,
## the core takes vector 23H, and the handler counts.  R2 is the loop's own
## counter, which reaches 200 either way, so the handler returned where it
## interrupted every time.
##
## The count of 74 is recorded rather than derived.  A request raised while the
## handler is still running loses the priority comparison against its own level
## and waits, so how many arrive depends on how long the handler holds the next
## one off; the rate above is where the arithmetic is checked.
# RUN: llvm-mc -filetype=obj -triple=c166 %t/tick.s -o %t/tick.o
# RUN: llvm-objcopy -O binary %t/tick.o %t/tick.bin
# RUN: %c166_sim --binary --dump-state --count-states %t/tick.bin 2>&1 \
# RUN:   | FileCheck %s --check-prefix=TICK
# TICK: R2=00c8 R3=004a
# TICK: interrupts 74

## With the enable bit clear the request is still raised and still recorded -
## R7 is T3IC at the end, 0088H, which is xxIR set over the priority that was
## written with xxIE left clear - but nothing is taken, so the loop runs
## undisturbed and the handler never does.
# RUN: llvm-mc -filetype=obj -triple=c166 %t/nogate.s -o %t/nogate.o
# RUN: llvm-objcopy -O binary %t/nogate.o %t/nogate.bin
# RUN: %c166_sim --binary --dump-state --count-states %t/nogate.bin 2>&1 \
# RUN:   | FileCheck %s --check-prefix=NOGATE
## Nothing was taken, so there is no interrupt count line at all; R3 is the
## handler's own counter and says the same thing.
# NOGATE: R2=00c8 R3=0000
# NOGATE: R7=0088
# NOGATE-NOT: interrupts

## Reload mode, which is how a periodic interrupt is actually written: T2 holds
## the reload value and watches T3's output toggle latch, so nothing in the
## handler has to put the timer back.  R8 is T3 part way through a period,
## R10 is T2IC - 0080H, the reload's own request flag raised and never enabled
## - and the count is one interrupt per 16 counts of 8 states, which is 128
## states, which over 3164 of them is the 24 seen.
# RUN: llvm-mc -filetype=obj -triple=c166 %t/reload.s -o %t/reload.o
# RUN: llvm-objcopy -O binary %t/reload.o %t/reload.bin
# RUN: %c166_sim --binary --dump-state --count-states %t/reload.bin 2>&1 \
# RUN:   | FileCheck %s --check-prefix=RELOAD
# RELOAD: R3=0018
# RELOAD: R8=fff7 R9=0040 R10=0080
# RELOAD: interrupts 24

## What is not modelled says so, at the write that asked for it rather than by
## never counting.  Counter, gated and incremental interface mode all take
## their clock from a pin, and so does the external up/down control; this
## simulator has no pins.
# RUN: llvm-mc -filetype=obj -triple=c166 %t/counter.s -o %t/counter.o
# RUN: llvm-objcopy -O binary %t/counter.o %t/counter.bin
# RUN: not %c166_sim --binary %t/counter.bin 2>&1 \
# RUN:   | FileCheck %s --check-prefix=COUNTER
# COUNTER: error: T3 was configured for counter mode, which is driven by a pin this simulator does not have
# RUN: llvm-mc -filetype=obj -triple=c166 %t/updown.s -o %t/updown.o
# RUN: llvm-objcopy -O binary %t/updown.o %t/updown.bin
# RUN: not %c166_sim --binary %t/updown.bin 2>&1 \
# RUN:   | FileCheck %s --check-prefix=UPDOWN
# UPDOWN: error: T3 was configured for external up/down control, which is driven by a pin this simulator does not have

## Reload and capture are the auxiliary timers' modes; the manual marks both
## reserved on the core timer, so this is a program's mistake rather than a gap
## here, and it is reported as one.
# RUN: llvm-mc -filetype=obj -triple=c166 %t/reserved.s -o %t/reserved.o
# RUN: llvm-objcopy -O binary %t/reserved.o %t/reserved.bin
# RUN: not %c166_sim --binary %t/reserved.bin 2>&1 \
# RUN:   | FileCheck %s --check-prefix=RESERVED
# RESERVED: error: T3CON asks for a mode the manual reserves on the core timer

## A timer configured but not running is not a configuration this has to model,
## so it is left alone.  R8 is T3, still zero because nothing counted.
# RUN: llvm-mc -filetype=obj -triple=c166 %t/stopped.s -o %t/stopped.o
# RUN: llvm-objcopy -O binary %t/stopped.o %t/stopped.bin
# RUN: %c166_sim --binary --dump-state %t/stopped.bin 2>&1 \
# RUN:   | FileCheck %s --check-prefix=STOPPED
# STOPPED: R8=0000

#--- rate.s
        .text
        jmps    #0, 0x100
        .org    0x100
        mov     t3, #0
## T3R set, timer mode, T3I = 0: a count every 8 states.
        mov     t3con, #0x0040
        mov     r4, #200
.Lloop:
        sub     r4, #1
        jmpr    cc_NZ, .Lloop
        mov     r8, t3
        mov     r5, #0
        exts    #0xFF, #1
        mov     0x0002, r5

#--- rate1.s
## The same with T3I = 1, so half as many counts.
        .text
        jmps    #0, 0x100
        .org    0x100
        mov     t3, #0
        mov     t3con, #0x0041
        mov     r4, #200
.Lloop:
        sub     r4, #1
        jmpr    cc_NZ, .Lloop
        mov     r8, t3
        mov     r5, #0
        exts    #0xFF, #1
        mov     0x0002, r5

#--- rate3.s
## And T3I = 3, an eighth of the first.
        .text
        jmps    #0, 0x100
        .org    0x100
        mov     t3, #0
        mov     t3con, #0x0043
        mov     r4, #200
.Lloop:
        sub     r4, #1
        jmpr    cc_NZ, .Lloop
        mov     r8, t3
        mov     r5, #0
        exts    #0xFF, #1
        mov     0x0002, r5

#--- tick.s
        .text
        jmps    #0, 0x100
## Vector 23H, GPT1 timer 3.
        .org    0x8c
        jmps    #0, 0x160

        .org    0x100
        mov     r3, #0
        mov     r2, #0
## Four counts short of the top, so the first overflow is 32 states away.
        mov     t3, #0xfffc
## T3IC: xxIE at bit 6, and ILVL 8 in the bottom nibble.
        mov     t3ic, #0x0048
        bset    psw.11
        mov     t3con, #0x0040
        mov     r4, #200
.Lloop:
        add     r2, #1
        sub     r4, #1
        jmpr    cc_NZ, .Lloop
        mov     r5, #0
        exts    #0xFF, #1
        mov     0x0002, r5

        .org    0x160
        add     r3, #1
        mov     t3, #0xfffc
        reti

#--- nogate.s
## The same program with the enable bit left clear.
        .text
        jmps    #0, 0x100
        .org    0x8c
        jmps    #0, 0x160

        .org    0x100
        mov     r3, #0
        mov     r2, #0
        mov     t3, #0xfffc
        mov     t3ic, #0x0008
        bset    psw.11
        mov     t3con, #0x0040
        mov     r4, #200
.Lloop:
        add     r2, #1
        sub     r4, #1
        jmpr    cc_NZ, .Lloop
        mov     r7, t3ic
        mov     r5, #0
        exts    #0xFF, #1
        mov     0x0002, r5

        .org    0x160
        add     r3, #1
        mov     t3, #0xfffc
        reti

#--- reload.s
        .text
        jmps    #0, 0x100
        .org    0x8c
        jmps    #0, 0x160

        .org    0x100
        mov     r3, #0
## T2 holds the reload value and is in reload mode, T2I = 111B: any transition
## of T3OTL triggers it.  The auxiliary timer stops itself in this mode, so its
## own run bit is not set.
        mov     t2, #0xfff0
        mov     t2con, #0x0027
        mov     t3, #0xfff0
        mov     t3ic, #0x0048
        bset    psw.11
        mov     t3con, #0x0040
        mov     r4, #400
.Lloop:
        sub     r4, #1
        jmpr    cc_NZ, .Lloop
        mov     r8, t3
        mov     r9, t3con
        mov     r10, t2ic
        mov     r5, #0
        exts    #0xFF, #1
        mov     0x0002, r5

        .org    0x160
        add     r3, #1
        reti

#--- counter.s
        .text
        jmps    #0, 0x100
        .org    0x100
        mov     t3con, #0x0048
        mov     r5, #0
        exts    #0xFF, #1
        mov     0x0002, r5

#--- updown.s
        .text
        jmps    #0, 0x100
        .org    0x100
        mov     t3con, #0x0140
        mov     r5, #0
        exts    #0xFF, #1
        mov     0x0002, r5

#--- reserved.s
        .text
        jmps    #0, 0x100
        .org    0x100
        mov     t3con, #0x0060
        mov     r5, #0
        exts    #0xFF, #1
        mov     0x0002, r5

#--- stopped.s
## Counter mode selected and the run bit clear: nothing counts on the part
## either, so nothing here has to model it.
        .text
        jmps    #0, 0x100
        .org    0x100
        mov     t3, #0
        mov     t3con, #0x0008
        mov     r4, #50
.Lloop:
        sub     r4, #1
        jmpr    cc_NZ, .Lloop
        mov     r8, t3
        mov     r5, #0
        exts    #0xFF, #1
        mov     0x0002, r5
