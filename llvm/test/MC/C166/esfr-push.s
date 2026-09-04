# RUN: llvm-mc -triple=c166 -mcpu=xc16x -mattr=+mac,+sfr-xc164 -show-encoding %s \
# RUN:   | FileCheck %s
# RUN: llvm-mc -triple=c166 -mcpu=xc16x -mattr=+mac,+sfr-xc164 -filetype=obj %s \
# RUN:   -o %t.o
# RUN: llvm-objdump -d --mcpu=xc16x --mattr=+mac,+sfr-xc164 %t.o \
# RUN:   | FileCheck %s --check-prefix=DIS

## An extended special function register is reachable by address like any
## other, but there is no "push mem" - so pushing one means naming it through a
## "reg" field, and a "reg" field means the ordinary space unless an EXTR says
## otherwise.  That is what these two are: one instruction here, EXTR and then
## PUSH on the part, written as one so that nothing can fall between them.

	push	qx0
	pop	qr1
	push	syscon1
# CHECK: push qx0                     ; encoding: [0xd1,0x80,0xec,0x00]
# CHECK: pop qr1                      ; encoding: [0xd1,0x80,0xfc,0x03]
# CHECK: push syscon1                 ; encoding: [0xd1,0x80,0xec,0xee]

## An ordinary one is unchanged: two bytes and no prefix.
	push	r5
	push	dpp0
# CHECK: push r5                      ; encoding: [0xec,0xf5]
# CHECK: push dpp0                    ; encoding: [0xec,0x00]

## What a disassembly makes of those four bytes is the two instructions they
## are.  The two spaces are indistinguishable once encoded - the short address
## is the same number either way - so a push of QX0 reads back as an EXTR and a
## push of the ordinary register at the same short address, which is what the
## bytes say and reassembles to the same bytes.
# DIS:      extr #1
# DIS-NEXT: push dpp0
# DIS-NEXT: extr #1
# DIS-NEXT: pop dpp3
