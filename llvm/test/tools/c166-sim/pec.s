# REQUIRES: c166-registered-target
# RUN: rm -rf %t && split-file %s %t

## The Peripheral Event Controller, which is this part's answer to DMA: eight
## channels, each of which moves one byte or word per request instead of
## entering a handler, counts down, and only lets the request through when it
## runs out.
##
## Everything asserted below is chapter 5 of the C167CR Derivatives User's
## Manual V3.1.  A request "programmed to priority levels 15 or 14 will be
## serviced by the PEC, unless the COUNT field of the associated PECC register
## contains zero"; "the associated PEC channel number is derived from the
## respective ILVL (LSB) and GLVL", so level 15 selects channels 7 to 4 and
## level 14 channels 3 to 0; and Table 5-5 says what COUNT does at each value.
##
## The requests come from GPT1 timer 3, configured the way timer.s beside this
## configures it, because a channel with nothing to trigger it has nothing to
## demonstrate.  A flat image has no linker, so every address is written out;
## T3's vector is 23H, which is 8CH into the table.

# RUN: llvm-mc -filetype=obj -triple=c166 %t/fill.s -o %t/fill.o
# RUN: llvm-objcopy -O binary %t/fill.o %t/fill.bin

## A channel with an incrementing destination fills a buffer without the CPU
## being interrupted at all until the block is done.  R8 to R11 are the first
## four words of the buffer, which are the four the timer's requests moved, and
## R12 is PECC0 at the end with its COUNT down from 4 to 0.
##
## R3 is the handler's counter and is one rather than zero, which is the point
## rather than a leak: four transfers happen with no handler, and the fifth
## request finds COUNT already at zero and takes the vector.  That is what
## "leave request flag set, which triggers another request" is for - it is how
## a program is told the block is finished.
# RUN: %c166_sim --binary --dump-state --count-states %t/fill.bin 2>&1 \
# RUN:   | FileCheck %s --check-prefix=FILL
# FILL: R3=0001
# FILL: R8=1234 R9=1234 R10=1234 R11=1234 R12=0200
# FILL: interrupts 1  pec 4

## The same channel counting from 1: one transfer, then COUNT is zero and the
## request flag is left set, so the very next round takes the vector.  R3 is 1
## because the handler ran, R8 is the one word that moved.
# RUN: llvm-mc -filetype=obj -triple=c166 %t/last.s -o %t/last.o
# RUN: llvm-objcopy -O binary %t/last.o %t/last.bin
# RUN: %c166_sim --binary --dump-state --count-states %t/last.bin 2>&1 \
# RUN:   | FileCheck %s --check-prefix=LAST
# LAST: R3=0001
# LAST: R8=1234 R9=0000
# LAST: interrupts 1  pec 1

## COUNT already zero is not a PEC request at all: "the respective PEC channel
## remains idle and the associated interrupt service routine is activated
## instead".  Nothing is moved and the handler runs.
# RUN: llvm-mc -filetype=obj -triple=c166 %t/idle.s -o %t/idle.o
# RUN: llvm-objcopy -O binary %t/idle.o %t/idle.bin
# RUN: %c166_sim --binary --dump-state --count-states %t/idle.bin 2>&1 \
# RUN:   | FileCheck %s --check-prefix=IDLE
# IDLE: R3=0001
# IDLE: R8=0000
# IDLE-NOT: pec

## Continuous mode, COUNT = FFH: "COUNT is not modified and the respective PEC
## channel services any request until it is disabled again", so the count of
## transfers is however many requests arrived and the handler never runs.
# RUN: llvm-mc -filetype=obj -triple=c166 %t/cont.s -o %t/cont.o
# RUN: llvm-objcopy -O binary %t/cont.o %t/cont.bin
# RUN: %c166_sim --binary --dump-state --count-states %t/cont.bin 2>&1 \
# RUN:   | FileCheck %s --check-prefix=CONT
# CONT: R3=0000 R4=0000
# CONT: R12=00ff
# CONT-NOT: interrupts
# CONT: pec 16

## A byte channel with an incrementing source, which is the shape a serial
## transmitter uses: four bytes are read out of a buffer and written to the
## same place.  R8 is that place, holding the last byte written - 44H, the
## fourth of "ABCD" - and R10 is SRCP0 at the end, four bytes past where it
## started, since a byte transfer steps the pointer by one rather than two.
# RUN: llvm-mc -filetype=obj -triple=c166 %t/bytes.s -o %t/bytes.o
# RUN: llvm-objcopy -O binary %t/bytes.o %t/bytes.bin
# RUN: %c166_sim --binary --dump-state --count-states %t/bytes.bin 2>&1 \
# RUN:   | FileCheck %s --check-prefix=BYTES
# BYTES: R8=0044
# BYTES: R10=f60c
## And one interrupt at the end, for the same reason the first case has one.
# BYTES: interrupts 1  pec 4

#--- fill.s
        .text
        jmps    #0, 0x100
        .org    0x8c
        jmps    #0, 0x160

        .org    0x100
        mov     r3, #0
## The word to be moved, and where the channel is to put it.
        mov     r4, #0xf600
        mov     r5, #0x1234
        mov     [r4], r5
## SRCP0 at FCE0H and DSTP0 at FCE2H.  The source does not move; the
## destination walks the buffer at F700H.
        mov     r6, #0xfce0
        mov     [r6], r4
        mov     r4, #0xf700
        mov     0xfce2, r4
## PECC0: COUNT = 4, BWT = 0 (a word), INC = 01 (step the destination).
        mov     r4, #0x0204
        mov     0xfec0, r4
## T3IC on level 15 group 0, which is PEC channel 4... except that this is
## channel 0, so level 14 group 0 it is: ILVL = 1110B, GLVL = 00B, enabled.
        mov     t3ic, #0x004e
## Reload mode, as timer.s beside this uses it: T2 holds the reload value and
## watches T3's output toggle latch, so T3 reloads itself.  Nothing else can -
## a channel that services the request instead of a handler leaves nobody to
## put the timer back, which is the whole point of the arrangement.
        mov     t2, #0xfff0
        mov     t2con, #0x0027
        mov     t3, #0xfff0
        bset    psw.11
        mov     t3con, #0x0040
        mov     r4, #300
.Lloop:
        sub     r4, #1
        jmpr    cc_NZ, .Lloop
        mov     r8, 0xf700
        mov     r9, 0xf702
        mov     r10, 0xf704
        mov     r11, 0xf706
        mov     r12, 0xfec0
        mov     r5, #0
        exts    #0xFF, #1
        mov     0x0002, r5

        .org    0x160
        add     r3, #1
## Stop the timer, so the run ends where the count ran out rather than going
## on to raise more.
        mov     t3con, #0
        reti

#--- last.s
        .text
        jmps    #0, 0x100
        .org    0x8c
        jmps    #0, 0x160

        .org    0x100
        mov     r3, #0
        mov     r4, #0xf600
        mov     r5, #0x1234
        mov     [r4], r5
        mov     r6, #0xfce0
        mov     [r6], r4
        mov     r4, #0xf700
        mov     0xfce2, r4
## COUNT = 1: one transfer, then the flag is left set and the vector is taken.
        mov     r4, #0x0201
        mov     0xfec0, r4
        mov     t3ic, #0x004e
## Reload mode, as timer.s beside this uses it: T2 holds the reload value and
## watches T3's output toggle latch, so T3 reloads itself.  Nothing else can -
## a channel that services the request instead of a handler leaves nobody to
## put the timer back, which is the whole point of the arrangement.
        mov     t2, #0xfff0
        mov     t2con, #0x0027
        mov     t3, #0xfff0
        bset    psw.11
        mov     t3con, #0x0040
        mov     r4, #300
.Lloop:
        sub     r4, #1
        jmpr    cc_NZ, .Lloop
        mov     r8, 0xf700
        mov     r9, 0xf702
        mov     r5, #0
        exts    #0xFF, #1
        mov     0x0002, r5

        .org    0x160
        add     r3, #1
## Stop the timer, so the one interrupt this is checking for is the only one.
        mov     t3con, #0
        reti

#--- idle.s
        .text
        jmps    #0, 0x100
        .org    0x8c
        jmps    #0, 0x160

        .org    0x100
        mov     r3, #0
        mov     r4, #0xf600
        mov     r5, #0x1234
        mov     [r4], r5
        mov     r6, #0xfce0
        mov     [r6], r4
        mov     r4, #0xf700
        mov     0xfce2, r4
## COUNT = 0, which is "no action" - the handler runs instead.
        mov     r4, #0x0200
        mov     0xfec0, r4
        mov     t3ic, #0x004e
## Reload mode, as timer.s beside this uses it: T2 holds the reload value and
## watches T3's output toggle latch, so T3 reloads itself.  Nothing else can -
## a channel that services the request instead of a handler leaves nobody to
## put the timer back, which is the whole point of the arrangement.
        mov     t2, #0xfff0
        mov     t2con, #0x0027
        mov     t3, #0xfff0
        bset    psw.11
        mov     t3con, #0x0040
        mov     r4, #300
.Lloop:
        sub     r4, #1
        jmpr    cc_NZ, .Lloop
        mov     r8, 0xf700
        mov     r5, #0
        exts    #0xFF, #1
        mov     0x0002, r5

        .org    0x160
        add     r3, #1
        mov     t3con, #0
        reti

#--- cont.s
        .text
        jmps    #0, 0x100
        .org    0x8c
        jmps    #0, 0x160

        .org    0x100
        mov     r3, #0
        mov     r4, #0xf600
        mov     r5, #0x1234
        mov     [r4], r5
        mov     r6, #0xfce0
        mov     [r6], r4
        mov     r4, #0xf700
        mov     0xfce2, r4
## COUNT = FFH with the pointers left alone, which is the same word moved to
## the same place for as long as the timer keeps asking.
        mov     r4, #0x00ff
        mov     0xfec0, r4
        mov     t3ic, #0x004e
## Reload mode, as timer.s beside this uses it: T2 holds the reload value and
## watches T3's output toggle latch, so T3 reloads itself.  Nothing else can -
## a channel that services the request instead of a handler leaves nobody to
## put the timer back, which is the whole point of the arrangement.
        mov     t2, #0xfff0
        mov     t2con, #0x0027
        mov     t3, #0xfff0
        bset    psw.11
        mov     t3con, #0x0040
        mov     r4, #300
.Lloop:
        sub     r4, #1
        jmpr    cc_NZ, .Lloop
        mov     r12, 0xfec0
        mov     r5, #0
        exts    #0xFF, #1
        mov     0x0002, r5

        .org    0x160
        add     r3, #1
        add     r4, #1
        reti

#--- bytes.s
        .text
        jmps    #0, 0x100
        .org    0x8c
        jmps    #0, 0x160

        .org    0x100
        mov     r3, #0
## Four bytes at F608H, walked one at a time into one fixed word.
        mov     r4, #0x4241
        mov     0xf608, r4
        mov     r4, #0x4443
        mov     0xf60a, r4
        mov     r4, #0xf608
        mov     0xfce0, r4
        mov     r4, #0xf700
        mov     0xfce2, r4
## COUNT = 4, BWT = 1 (a byte), INC = 10 (step the source).
        mov     r4, #0x0504
        mov     0xfec0, r4
        mov     t3ic, #0x004e
## Reload mode, as timer.s beside this uses it: T2 holds the reload value and
## watches T3's output toggle latch, so T3 reloads itself.  Nothing else can -
## a channel that services the request instead of a handler leaves nobody to
## put the timer back, which is the whole point of the arrangement.
        mov     t2, #0xfff0
        mov     t2con, #0x0027
        mov     t3, #0xfff0
        bset    psw.11
        mov     t3con, #0x0040
        mov     r4, #300
.Lloop:
        sub     r4, #1
        jmpr    cc_NZ, .Lloop
        mov     r8, 0xf700
        mov     r10, 0xfce0
        mov     r5, #0
        exts    #0xFF, #1
        mov     0x0002, r5

        .org    0x160
        add     r3, #1
        mov     t3con, #0
        reti
