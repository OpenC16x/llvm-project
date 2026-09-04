## A program that never loads R0 and takes an interrupt.  The handler is what a
## compiler emits: it subtracts its frame from R0 and adds it back.  R0 is zero,
## so the subtraction wraps to just under 64K - which is above the limit and is
## not a stack pointer - and the addition brings it back to zero.  Neither is an
## overflow, and the check must not arm on the wrapped value.

	.macro	emit ch
	movb	rl2, #\ch
	exts	#0xFF, #1
	movb	[r4], rl2
	.endm

	.section .vectors.008,"ax",@progbits
	jmps	#seg(handler), sof(handler)

	.text
	.globl handler
handler:
	sub	r0, #2
	mov	[r0], r3
	mov	r3, #1
	mov	r5, r3
	mov	r3, [r0]
	add	r0, #2
	reti

	.globl _start
_start:
	bset	psw.11
	mov	r4, #0x0000
	mov	r6, #40
.Lwait:
	sub	r6, #1
	jmpr	cc_NZ, .Lwait
	emit	'w'
	emit	'r'
	emit	'a'
	emit	'p'
	emit	10
	mov	r2, #0
	jmpa	cc_UC, __c166_exit
