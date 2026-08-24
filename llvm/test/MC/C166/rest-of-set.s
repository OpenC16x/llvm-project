; RUN: llvm-mc -triple=c166 -show-encoding < %s | FileCheck %s
; RUN: llvm-mc -triple=c166 -filetype=obj < %s | llvm-objdump -d - \
; RUN:   | FileCheck %s --check-prefix=DISASM

; The instruction forms nothing generates.  Their encodings are derived from
; the structure of the opcode map rather than read off the page: each group's
; columns are fixed by the forms that were already here - x0 register, x2
; memory source, x4 memory destination, x6 immediate, x8 short immediate - and
; every opcode below is the one its column left free.  The derivation agreed
; with what the disassembler could not decode before, opcode for opcode, with
; none left over on either side.
;
; Anyone holding V2.0 of the manual should check this file first.  What it
; pins down is that the bytes and the text agree in both directions, which is
; what stops a later change moving one without the other.
;
; tools/c166-sim/rest-of-set.s runs them; utils/C166Sim/tools/coverage.sh
; reports anything still missing.


; CHECK: add 4660, r2 ; encoding: [0x04,0xf2,0x34,0x12]
; DISASM: add 4660, r2
	add	0x1234, r2

; CHECK: addb 4660, rl2 ; encoding: [0x05,0xf4,0x34,0x12]
; DISASM: addb 4660, rl2
	addb	0x1234, rl2

; CHECK: subc 4660, r5 ; encoding: [0x34,0xf5,0x34,0x12]
; DISASM: subc 4660, r5
	subc	0x1234, r5

; CHECK: cmp 4660, r2 ; encoding: [0x44,0xf2,0x34,0x12]
; DISASM: cmp 4660, r2
	cmp	0x1234, r2

; CHECK: cmpb 4660, rl2 ; encoding: [0x45,0xf4,0x34,0x12]
; DISASM: cmpb 4660, rl2
	cmpb	0x1234, rl2

; CHECK: addcb rl2, rl3 ; encoding: [0x11,0x46]
; DISASM: addcb rl2, rl3
	addcb	rl2, rl3

; CHECK: addcb rl2, #3 ; encoding: [0x19,0x43]
; DISASM: addcb rl2, #3
	addcb	rl2, #3

; CHECK: addcb rl2, #18 ; encoding: [0x17,0xf4,0x12,0x00]
; DISASM: addcb rl2, #18
	addcb	rl2, #0x12

; CHECK: addcb rl2, 4660 ; encoding: [0x13,0xf4,0x34,0x12]
; DISASM: addcb rl2, 4660
	addcb	rl2, 0x1234

; CHECK: subcb rh4, rl5 ; encoding: [0x31,0x9a]
; DISASM: subcb rh4, rl5
	subcb	rh4, rl5

; CHECK: add r5, [r2] ; encoding: [0x08,0x5a]
; DISASM: add r5, [r2]
	add	r5, [r2]

; CHECK: add r5, [r2+] ; encoding: [0x08,0x5e]
; DISASM: add r5, [r2+]
	add	r5, [r2+]

; CHECK: subc r7, [r3] ; encoding: [0x38,0x7b]
; DISASM: subc r7, [r3]
	subc	r7, [r3]

; CHECK: cmp r5, [r1] ; encoding: [0x48,0x59]
; DISASM: cmp r5, [r1]
	cmp	r5, [r1]

; CHECK: cmpb rl5, [r0+] ; encoding: [0x49,0xac]
; DISASM: cmpb rl5, [r0+]
	cmpb	rl5, [r0+]

; CHECK: mov [-r2], r3 ; encoding: [0x88,0x32]
; DISASM: mov [-r2], r3
	mov	[-r2], r3

; CHECK: movb [-r2], rl3 ; encoding: [0x89,0x62]
; DISASM: movb [-r2], rl3
	movb	[-r2], rl3

; CHECK: cmp r2, 4660 ; encoding: [0x42,0xf2,0x34,0x12]
; DISASM: cmp r2, 4660
	cmp	r2, 0x1234

; CHECK: cmpb rl2, 4660 ; encoding: [0x43,0xf4,0x34,0x12]
; DISASM: cmpb rl2, 4660
	cmpb	rl2, 0x1234

; CHECK: divl r3 ; encoding: [0x6b,0x33]
; DISASM: divl r3
	divl	r3

; CHECK: divlu r3 ; encoding: [0x7b,0x33]
; DISASM: divlu r3
	divlu	r3

; CHECK: prior r2, r3 ; encoding: [0x2b,0x23]
; DISASM: prior r2, r3
	prior	r2, r3

; CHECK: cmpi1 r2, #3 ; encoding: [0x80,0x23]
; DISASM: cmpi1 r2, #3
	cmpi1	r2, #3

; CHECK: cmpi2 r4, #4660 ; encoding: [0x96,0xf4,0x34,0x12]
; DISASM: cmpi2 r4, #4660
	cmpi2	r4, #0x1234

; CHECK: cmpd1 r6, 4660 ; encoding: [0xa2,0xf6,0x34,0x12]
; DISASM: cmpd1 r6, 4660
	cmpd1	r6, 0x1234

; CHECK: cmpd2 r2, #5 ; encoding: [0xb0,0x25]
; DISASM: cmpd2 r2, #5
	cmpd2	r2, #5

; CHECK: scxt r2, #4660 ; encoding: [0xc6,0xf2,0x34,0x12]
; DISASM: scxt r2, #4660
	scxt	r2, #0x1234

; CHECK: scxt r2, 4660 ; encoding: [0xd6,0xf2,0x34,0x12]
; DISASM: scxt r2, 4660
	scxt	r2, 0x1234

; CHECK: callr 16 ; encoding: [0xbb,0x10]
; A relative target comes back as the address it reaches rather than the
; distance to it, which is what the printer is for.
; DISASM: callr 0x{{[0-9a-f]+}}
	callr	0x10

; CHECK: pcall r2, 4660 ; encoding: [0xe2,0xf2,0x34,0x12]
; DISASM: pcall r2, 4660
	pcall	r2, 0x1234

; CHECK: retp r2 ; encoding: [0xeb,0xf2]
; DISASM: retp r2
	retp	r2
